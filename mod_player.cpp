#include "mod_player.h"
#include "mixer.h"
#include <base/sample_voice.h>

class mod_player::impl {
public:
    explicit impl(const module& mod, mixer& m) : mod_(mod), mixer_(m) {
        for (int i = 0; i < mod_.num_channels; ++i) {
            channels_.emplace_back(*this, mod.channel_default_pan(i));
            wprintf(L"%2d: Pan %d\n", i+1,mod.channel_default_pan(i));
        }
        mixer_.at_next_tick([this] {
            set_speed(mod_.initial_speed);
            set_tempo(mod_.initial_tempo);
            tick_ = mod_.initial_speed+1;
            for (auto& ch : channels_) {
                mixer_.add_voice(ch.get_voice());
            }
            mixer_.global_volume(1.0f/mod_.num_channels);
        });
    }

    ~impl() {
        mixer_.at_next_tick([this] {
            for (auto& ch : channels_) {
                mixer_.remove_voice(ch.get_voice());
            }
            mixer_.global_volume(1.0f);
        });
    }

    const module& mod() const { return mod_; }

    void skip_to_order(int order) {
        assert(order >= 0 && order < mod_.order.size());
        mixer_.at_next_tick([order, this] {
            // TODO: Process (some) effects...
            wprintf(L"Skipping to order %d, cur = %d\n", order, order_);
            order_ = order;
            row_   = -1;
            tick_  = speed_;
            notify_position_change();
        });
    }

    void stop() {
        mixer_.at_next_tick([this] {
            set_playing(false);
        });
    };

    void toggle_playing() {
        mixer_.at_next_tick([this] {
            set_playing(!playing_);
        });
    }

    void on_position_changed(const callback_function_type<module_position>& cb) {
        on_position_changed_.subscribe(cb);
    }

    static constexpr int max_rows     = 64;
    static constexpr int max_volume   = 64;

private:
    class channel {
    public:
        explicit channel(mod_player::impl& player, uint8_t default_pan) : player_(player), mix_chan_(player.mixer_.sample_rate()) {
            mix_chan_.pan(default_pan / 255.0f);
        }

        sample_voice& get_voice() {
            return mix_chan_;
        }

        void process_note(const module_note& note) {
            if (note.instrument) { 
                assert(note.instrument >= 1 && note.instrument <= player_.mod_.instruments.size());
                sample_ = note.instrument;
                volume(instrument().volume);
            }
            if (note.note != piano_key::NONE) {
                if (player_.mod_.type == module_type::mod) {
                    assert(note.note != piano_key::OFF);
                    process_mod_note(note);
                } else {
                    assert(player_.mod_.type == module_type::s3m);
                    process_s3m_note(note);
                }
            }
            if (note.volume != no_volume_byte) {
                assert(player_.mod_.type == module_type::s3m);
                assert(note.volume >= 0 && note.volume <= max_volume);
                volume(note.volume);
            }
        }

        void process_effect(int tick, int effect) {
            if (player_.mod_.type == module_type::mod) {
                process_mod_effect(tick, effect);
            } else {
                process_s3m_effect(tick, effect);
            }
        }


    private:
        mod_player::impl&   player_;
        sample_voice        mix_chan_;
        int                 sample_ = 0;
        int                 volume_ = 0;
        int                 period_ = 0;
        int                 porta_target_period_ = 0;
        int                 porta_speed_ = 0;
        int                 vib_depth_ = 0;
        int                 vib_speed_ = 0;
        int                 vib_pos_   = 0;
        int                 last_vol_slide_ = 0; // s3m only
        int                 last_retrig_ = 0;    // s3m only
        int                 sample_offset_ = 0;

        int volume() const { return volume_; }

        void volume(int vol) {
            assert(vol >= 0 && vol <= max_volume);
            volume_ = vol;
            mix_chan_.volume(static_cast<float>(vol) / max_volume);
        }

        void set_voice_period(int period) {
            assert(period > 0);
            if (!sample_) {
                wprintf(L"Warning: No sample. Ignoring period %d\n", period);
                return;
            }
            auto& s = player_.mod_.samples[sample_-1];
            const int adjusted_period = static_cast<int>(0.5 + period * amiga_c5_rate / s.c5_rate());
            if (player_.mod_.type == module_type::mod) {
                mix_chan_.freq(amiga_period_to_freq(adjusted_period));
            } else {
                assert(player_.mod_.type == module_type::s3m);
                mix_chan_.freq(s3m_period_to_freq(adjusted_period));
            }
        }

        void set_period(int period) {
            static constexpr int s3m_min_period = 10;
            static constexpr int s3m_max_period = 27392;
            period_ = std::min(s3m_max_period, std::max(s3m_min_period, period));
            set_voice_period(period_);
        }

        void do_arpeggio(int amount) {
            const int res_per = freq_to_amiga_period(amiga_period_to_freq(period_) * note_difference_to_scale(static_cast<float>(amount)));
            //wprintf(L"Arpeggio base period = %d, amount = %d, resulting period = %d\n", period_, amount, res_per);
            set_voice_period(res_per);
        }

        void do_set_volume_checked(int vol) {
            volume(std::max(0, std::min(max_volume, vol)));
        }

        void do_volume_slide(int amount) {
            do_set_volume_checked(volume() + amount);
        }

        void do_volume_slide_xy(int x, int y) {
            if (x && y) {
            } else if (x > 0) {
                do_volume_slide(+x);
            } else if (y > 0) {
                do_volume_slide(-y);
            }
        }

        void do_s3m_volume_slide(int tick, int xy) {
            if (xy) last_vol_slide_ = xy;
            const int x = last_vol_slide_ >> 4;
            const int y = last_vol_slide_ & 0xf;
            if (x == 0xF) {
                // Fine volume slide down
                if (!tick) do_volume_slide(-y);
            } else if (y == 0xF) {
                // Fine volume slide up
                if (!tick) do_volume_slide(+x);
            } else {
                if (tick) {
                    do_volume_slide_xy(x, y);
                }
            }            
        }

        void do_porta_to_note() {
            if (period_ < porta_target_period_) {
                set_period(std::min(porta_target_period_, period_ + porta_speed_));
            } else {
                set_period(std::max(porta_target_period_, period_ - porta_speed_));
            }
        }

        void do_vibrato() {
            static const uint8_t sinetable[32] ={
                0, 24, 49, 74, 97,120,141,161,
                180,197,212,224,235,244,250,253,
                255,253,250,244,235,224,212,197,
                180,161,141,120, 97, 74, 49, 24
            };

            assert(vib_pos_ >= -32 && vib_pos_ <= 31);

            const int delta = sinetable[vib_pos_ & 31];

            set_voice_period(period_ + (((vib_pos_ < 0 ? -delta : delta) * vib_depth_) / 128));

            vib_pos_ += vib_speed_;
            if (vib_pos_ > 31) vib_pos_ -= 64;
        }


        const module_instrument& instrument() const {
            assert(sample_ >= 1 && sample_ <= player_.mod_.instruments.size());
            return player_.mod_.instruments[sample_ - 1];
        }

        void trig(int offset) {
            vib_pos_ = 0;
            if (!sample_) {
                wprintf(L"Warning: No sample. Ignoring trig offset %d\n", offset);
                return;
            }
            auto& s = player_.mod_.samples[sample_-1];
            if (s.length()) {
                mix_chan_.play(s, std::min(s.length(), offset));
            }
        }

        void process_s3m_note(const module_note& note) {
            if (note.note == piano_key::OFF) {
                volume(0);
                return;
            }
            assert(note.note != piano_key::NONE);
            const int period = player_.mod_.note_to_period(note.note);
            const char effchar = static_cast<char>((note.effect>>8)-1+'A');
            if (effchar == 'G') {
                porta_target_period_ = period;
            } else {
                int offset = 0;
                set_period(period);
                if (effchar != 'S' || ((note.effect>>4)&0xf) != 0xD) { // SDy Note delay
                    if (effchar == 'O') {
                        offset = (note.effect&0xff) << 8;
                        if (offset) sample_offset_ = offset;
                        else offset = sample_offset_;
                    }
                }
                trig(offset);
            }
        }

        // direction -1 => Up, 1 => Down
        void do_s3m_porta(int tick, int direction, int xy) {
            assert(direction == -1 || direction == +1);
            if (xy) porta_speed_ = xy;
            const int x = porta_speed_ >> 4;
            const int y = porta_speed_ & 0xf;
            const bool is_fine = x == 0xE || x == 0xF;
            if (!tick) {
                if (is_fine) {
                    set_period(period_ + direction * (x == 0xF ? 4 : 1) * y);
                }
            } else {
                if (!is_fine) {
                    set_period(period_ + direction * 4 * porta_speed_);
                }
            }
        }

        void process_s3m_effect(int tick, int effect) {
            const char effchar = static_cast<char>((effect>>8)-1+'A');
            const int xy = effect&0xff;
            const int x = xy >> 4;
            const int y = xy & 0xf;
            switch (effchar) {
            case 'A': // Set speed
                player_.set_speed(xy);
                break;
            case 'B': // Pattern jump
                if (!tick) wprintf(L"Pattern jump! B%02X\n", xy);
                process_mod_effect(tick, 0xB00 | xy);
            case 'C': // Pattern break
                process_mod_effect(tick, 0xD00 | xy);
                break;
            case 'D': // Volume slide 
                do_s3m_volume_slide(tick, xy);
                break;
            case 'E': // Portamento down
                do_s3m_porta(tick, +1, xy);
                break;
            case 'F': // Portamento up
                do_s3m_porta(tick, -1, xy);
                break;
            case 'H': // Vibrato
                process_mod_effect(tick, 0x400 | xy);
                break;
            case 'J': // Arpeggio
                process_mod_effect(tick, 0x000 | xy);
                break;
            case 'K': // Kxy Vibrato + Volume Slide
                process_mod_effect(tick, 0x400);
                do_s3m_volume_slide(tick, xy);
                break;
            case 'G': // Gxy Porta to note
                if (xy) porta_speed_ = xy * 4;
                if (tick) {
                    do_porta_to_note();
                }
                break;
            case 'O': // Oxy Sample offset
                break;
            case 'Q': // Qxy (Retrig + Volume Slide)                
                if (xy) {
                    assert(y);
                    last_retrig_ = xy;
                }
                if (tick && tick % (last_retrig_&0xf) == 0) {
                    int new_vol = volume();
                    switch (last_retrig_ >> 4) {
                    case 0x0: break;
                    case 0x1: new_vol -= 1; break;
                    case 0x2: new_vol -= 2; break;
                    case 0x3: new_vol -= 4; break;
                    case 0x4: new_vol -= 8; break;
                    case 0x5: new_vol -= 16; break;
                    case 0x6: new_vol = new_vol*2/3; break;
                    case 0x7: new_vol /= 2; break;
                    case 0x8: break;
                    case 0x9: new_vol += 1; break;
                    case 0xA: new_vol += 2; break;
                    case 0xB: new_vol += 4; break;
                    case 0xC: new_vol += 8; break;
                    case 0xD: new_vol += 16; break;
                    case 0xE: new_vol = new_vol*3/2; break;
                    case 0xF: new_vol *= 2; break;
                    }
                    do_set_volume_checked(new_vol);
                    trig(0);
                }
                break;
            case 'S':
                switch(x) {
                case 0x8: // Pan position
                    if (!tick) {
                        mix_chan_.pan((y<<4) / 255.0f);
                    }
                    break;
                case 0xB: // Pattern Loop
                    process_mod_effect(tick, 0xE60 | y);
                    break;
                case 0xC: // Note cut
                    process_mod_effect(tick, 0xEC0 | y);
                    break;
                case 0xD: // Note delay
                    process_mod_effect(tick, 0xED0 | y);
                    break;
                default:
                    if (!tick) wprintf(L"%2.2d: Ignoring effect %c%02X\n", player_.row_, effchar, xy);
                }
                break;
            case 'T': // Txy Set tempo
                assert(xy >= 0x20);
                player_.set_tempo(xy);
                break;
            default:
                if (!tick) wprintf(L"%2.2d: Ignoring effect %c%02X\n", player_.row_, effchar, xy);
            }
        }

        void process_mod_note(const module_note& note) {
            const int period = player_.mod_.note_to_period(note.note);
            const auto effect = note.effect>>8;
            if (effect == 3 || effect == 5) {
                porta_target_period_ = period;
            } else {
                set_period(period);

                if (note.effect>>4 != 0xED) {
                    int offset = 0;
                    if (effect == 9) {
                        offset = (note.effect & 0xff) << 8;
                        if (offset) sample_offset_ = offset;
                        else offset = sample_offset_;
                    }
                    trig(offset);
                }
            }
        }

        void process_mod_effect(int tick, int effect) {
            assert(effect);
            const int xy = effect&0xff;
            const int x  = xy>>4;
            const int y  = xy&0xf;
            switch (effect>>8) {
            case 0x00: // 0xy Arpeggio
                if (tick) {
                    switch (tick % 3) {
                    case 0: do_arpeggio(0); break;
                    case 1: do_arpeggio(x); break;
                    case 2: do_arpeggio(y); break;
                    }
                }
                return;
            case 0x01: // 1xy Porta down
                if (tick) {
                    set_period(period_ - xy);
                }
                return;
            case 0x02: // 2xy Porta down
                if (tick) {
                    set_period(period_ + xy);
                }
                return;
            case 0x3: // 3xy Porta to note
                if (xy) porta_speed_ = xy;
                if (tick) {
                    do_porta_to_note();
                }
                return;
            case 0x4: // 4xy Vibrato
                if (!tick) {
                    if (x) vib_speed_ = x;
                    if (y) vib_depth_ = y;
                } else {
                    do_vibrato();
                }
                return;
            case 0x5: // 5xy Porta + Voume slide (5xy = 300 + Axy)
                if (tick) {
                    do_porta_to_note();
                    do_volume_slide_xy(x, y);
                }
                return;
            case 0x6: // 6xy Vibrato + Volume slide (6xy = 400 + Axy)
                if (tick) {
                    do_vibrato();
                    do_volume_slide_xy(x, y);
                }
                return;
            case 0x8: // 8xy
                return; // ignored
            case 0x9: // 9xy Sample offset
                return;
            case 0xA: // Axy Volume slide
                if (tick) {
                    do_volume_slide_xy(x, y);
                }
                return;
            case 0xB: // Bxy Pattern jump
                if (!tick) {
                    player_.pattern_jump(xy);
                }
                return;
            case 0xC: // Cxy Set volume
                if (tick == 0) {
                    volume(xy);
                }
                return;
            case 0xD: // Dxy Pattern break
                if (tick == 0) {
                    player_.pattern_break(x*10 + y);
                }
                return;
            case 0xE:
                switch (x) {
                case 0x0: // E0y Set fiter
                    return;
                case 0x1: // E1y Fine porta down
                    if (!tick) {
                        set_period(period_ - y);
                    }
                    return;
                case 0x2: // E2y Fine porta down
                    if (!tick) {
                        set_period(period_ + y);
                    }
                    return;
                case 0x6: // E6y Pattern loop
                    if (!tick) {
                        player_.pattern_loop(y);
                    }
                    return;
                case 0x9: // E9x Retrig note
                    assert(y);
                    if (tick && tick % y == 0) {
                        trig(0);
                    }
                    return;
                case 0xA: // EAy Fine volume slide up
                    if (!tick)  {
                        do_volume_slide(+y);
                    }
                    return;
                case 0xB: // EAy Fine volume slide down
                    if (!tick) {
                        do_volume_slide(-y);
                    }
                    return;
                case 0xC: // ECy Cut note
                    if (tick == y) {
                        volume(0);
                    }
                    return;
                case 0xD: // EDy Delay note
                    if (tick == y) {
                        trig(0);
                    }
                    return;
                case 0xE: // EEy Pattern delay
                    if (!tick) {
                        player_.pattern_delay(y);
                    }
                    return;
                }
                break;
            case 0xF: // Fxy Set speed
                if (tick) return;
                if (xy < 0x20) {
                    player_.set_speed(xy);
                } else {
                    player_.set_tempo(xy);
                }
                return;
            }
            if (!tick) wprintf(L"Unhandled effect %03X\n", effect);
        }

    };

    module                  mod_;
    mixer&                  mixer_;
    bool                    playing_ = false;
    int                     speed_ = 6;     // Number of ticks per row 1..127
    int                     order_ = 0;     // Position in order table 0..song length-1
    int                     row_ = -1;      // Current row in pattern
    int                     tick_ = 6;      // Current tick 0..speed
    int                     pattern_jump_ = -1;
    int                     pattern_break_row_ = -1;
    int                     pattern_delay_ = -1;
    int                     pattern_loop_row_ = -1;
    int                     pattern_loop_counter_ = -1;
    bool                    pattern_loop_ = false;
    event<module_position>  on_position_changed_;
    std::vector<channel>    channels_;

    void schedule() {
        mixer_.at_next_tick([this] { tick(); });
    }

    void set_playing(bool playing) {
        playing_ = playing;
        for (auto& ch : channels_) {
            ch.get_voice().paused(!playing_);
        }
        if (playing_) {
            schedule();
        }
    }

    void notify_position_change() {
        on_position_changed_(module_position{order_, mod_.order[order_], std::max(0, row_)});
    }

    void process_row() {
        pattern_break_row_ = -1;
        pattern_jump_      = -1;
        for (int ch = 0; ch < mod_.num_channels; ++ch) {
            auto& channel = channels_[ch];
            const auto& note = mod_.at(order_, row_)[ch];
            channel.process_note(note);
        }
    }

    void next_order() {
        if (++order_ >= mod_.order.size()) {
            order_ = 0; // TODO: Use restart pos
        }
    }

    void process_effects() {
        for (int ch = 0; ch < mod_.num_channels; ++ch) {
            auto& channel = channels_[ch];
            const auto& note = mod_.at(order_, row_)[ch];
            if (note.effect) {
                channel.process_effect(tick_, note.effect);
            }
        }
    }

    void tick() {
        if (!playing_) {
            return;
        }

        if (++tick_ >= speed_) {
            tick_ = 0;
            if (pattern_delay_ > 0) {
                --pattern_delay_;
            } else {
                pattern_delay_ = -1;
                if (pattern_jump_ != -1) {
                    order_ = pattern_jump_;
                    row_   = pattern_break_row_ == -1 ? -1 : pattern_break_row_ - 1;
                } else if (pattern_break_row_ != -1) {
                    next_order();
                    row_   = pattern_break_row_ - 1;
                }

                if (pattern_loop_) {
                    pattern_loop_ = false;
                    if (pattern_loop_counter_ > 0) {
                        assert(pattern_loop_row_ >= 0 && pattern_loop_row_ < max_rows);
                        //wprintf(L"%2.2d: Looping back to %d (counter %d)\n", row_, pattern_loop_row_, pattern_loop_counter_);
                        row_ = pattern_loop_row_ -1;
                        pattern_loop_row_ = -1;
                        --pattern_loop_counter_;
                    } else if (pattern_loop_counter_ == 0) {
                        //wprintf(L"%2.2d: Looping done\n", row_);
                        pattern_loop_counter_ = -1;
                        pattern_loop_row_     = -1;
                    }
                }

                if (++row_ >= max_rows) {
                    row_ = 0;
                    next_order();
                }
                process_row();
                notify_position_change();
                process_effects();
            }
        } else {
            // Intra-row tick
            process_effects();
        }
        schedule();
    }

    void set_speed(int speed) {
        speed_ = speed;
    }

    void set_tempo(int bpm) {
        // BPM, 2*bpm/5 ticks per second
        mixer_.ticks_per_second(bpm*2/5);
    }

    void pattern_break(int row) {
        assert(row >= 0 && row < max_rows);
        assert(pattern_break_row_ == -1);
        pattern_break_row_ = row;
    }

    void pattern_jump(int order) {
        assert(order_ >= 0 && order_ < mod_.order.size());
        assert(pattern_jump_ == -1);
        pattern_jump_      = order;
        pattern_break_row_ = -1; // A pattern jump after a pattern break makes the break have no effect
    }

    void pattern_delay(int delay_notes) {
        if (pattern_delay_ == -1) {
            pattern_delay_ = delay_notes;
        }
    }

    void pattern_loop(int x) {
        if (x == 0) {
            pattern_loop_row_ = row_;
        } else {
            if (pattern_loop_counter_ == -1 && pattern_loop_row_ != -1) {
                pattern_loop_counter_ = x;
            }
            pattern_loop_ = true;
        }
        //wprintf(L"%2.2d: Pattern loop x = %d (row =%d, counter = %d)\n", row_, x, pattern_loop_row_, pattern_loop_counter_);
    }
};

mod_player::mod_player(const module& mod, mixer& m) : impl_(std::make_unique<impl>(mod, m)) {
}

mod_player::~mod_player() = default;

const module& mod_player::mod() const {
    return impl_->mod();
}

void mod_player::skip_to_order(int order) {
    impl_->skip_to_order(order);
}

void mod_player::stop() {
    impl_->stop();
}

void mod_player::toggle_playing() {
    impl_->toggle_playing();
}

void mod_player::on_position_changed(const callback_function_type<module_position>& cb) {
    impl_->on_position_changed(cb);
}
