#include "mod_player.h"
#include "mixer.h"
#include <base/sample_voice.h>

class channel_base {
public:
    virtual ~channel_base() {}
    virtual void process_note(const module_note& note) = 0;
    virtual void process_effect(int tick, int effect) = 0;

protected:
    explicit channel_base(mod_player::impl& player, sample_voice& voice, uint8_t default_pan) : player_(player), voice_(voice) {
        voice_.pan(default_pan / 255.0f);
    }

    //
    // Global stuff
    //
    const module& mod() const;
    module_position current_position() const;
    void do_set_speed(int speed);
    void do_set_tempo(int bpm);
    void do_pattern_break(int row);
    void do_pattern_jump(int order);
    void do_pattern_delay(int delay_notes);
    void do_pattern_loop(int x);

    //
    // Trig
    //
    void trig(int offset) {
        vib_pos_ = 0;
        if (!instrument_number()) {
            wprintf(L"Warning: No sample. Ignoring trig offset %d\n", offset);
            return;
        }
        auto& s = instrument().samp;
        if (s.length()) {
            voice_.play(s, std::min(s.length(), offset));
        }
    }

    //
    // Volume
    //
    int volume() const { return volume_; }
    void volume(int vol) {
        volume_ = std::max(0, std::min(mod_player::max_volume, vol));
        voice_.volume(static_cast<float>(volume_) / mod_player::max_volume);
    }

    void do_volume_slide(int amount) {
        volume(volume() + amount);
    }

    void do_volume_slide(int x, int y) {
        if (x && y) {
        } else if (x > 0) {
            do_volume_slide(+x);
        } else if (y > 0) {
            do_volume_slide(-y);
        }
    }

    //
    // Panning
    //
    void pan(int amount) {
        assert(amount >= 0 && amount <= 255);
        voice_.pan(amount / 255.0f);
    }

    //
    // Instrument
    //
    void instrument_number(int inst) {
        assert(inst >= 1 && inst <= mod().instruments.size());
        instrument_ = inst;
        volume(instrument().volume);
    }

    int instrument_number() const {
        return instrument_;
    }

    const module_instrument& instrument() const {
        assert(instrument_ >= 1 && instrument_ <= mod().instruments.size());
        return mod().instruments[instrument_ - 1];
    }

    //
    // Period
    //
    void set_period(int period) {
        static constexpr int s3m_min_period = 10;
        static constexpr int s3m_max_period = 27392;
        period_ = std::min(s3m_max_period, std::max(s3m_min_period, period));
        set_voice_period(period_);
    }

    void do_arpeggio(int tick, int x, int y) {
        if (!tick) return;
        const int amount = (tick % 3 == 0) ? 0 : (tick % 3 == 1) ? x : y;
        const int res_per = mod().freq_to_period(mod().period_to_freq(period_) * note_difference_to_scale(static_cast<float>(amount)));
        //wprintf(L"Arpeggio base period = %d, amount = %d, resulting period = %d\n", period_, amount, res_per);
        set_voice_period(res_per);
    }

    void set_porta_target(int target_period) {
        porta_target_period_ = target_period;
    }

    void porta_speed(int speed) {
        assert(speed);
        porta_speed_ = speed;
    }

    int porta_speed() const {
        return porta_speed_;
    }

    void do_porta(int amount) {
        set_period(period_ + amount);
    }

    void do_porta_to_note() {
        if (period_ < porta_target_period_) {
            set_period(std::min(porta_target_period_, period_ + porta_speed_));
        } else {
            set_period(std::max(porta_target_period_, period_ - porta_speed_));
        }
    }

    void do_vibrato(int tick, int speed, int depth) {
        if (!tick) {
            if (speed) vib_speed_ = speed;
            if (depth) vib_depth_ = depth;
            return;
        }

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

private:
    mod_player::impl&   player_;
    sample_voice&       voice_;
    int                 volume_     = 0;
    int                 instrument_ = 0;
    int                 period_ = 0;

    // Effect parameters
    int                 porta_target_period_ = 0;
    int                 porta_speed_ = 0;
    int                 vib_depth_ = 0;
    int                 vib_speed_ = 0;
    int                 vib_pos_   = 0;

    void set_voice_period(int period) {
        assert(period > 0);
        if (!instrument_number()) {
            wprintf(L"Warning: No sample. Ignoring period %d\n", period);
            return;
        }
        auto& s = instrument().samp;
        const int adjusted_period = static_cast<int>(0.5 + period * amiga_c5_rate / s.c5_rate());
        voice_.freq(mod().period_to_freq(adjusted_period));
    }
};

std::unique_ptr<channel_base> make_channel(mod_player::impl& player, sample_voice& voice, uint8_t default_pan);

class mod_player::impl {
public:
    explicit impl(module&& mod, mixer& m) : mod_(std::move(mod)), mixer_(m) {
        for (int i = 0; i < mod_.num_channels; ++i) {
            voices_.emplace_back(mixer_.sample_rate());
        }
        for (int i = 0; i < mod_.num_channels; ++i) {
            channels_.emplace_back(make_channel(*this, voices_[i], static_cast<uint8_t>(mod_.channel_default_pan(i))));
            wprintf(L"%2d: Pan %d\n", i+1, mod_.channel_default_pan(i));
        }
        mixer_.at_next_tick([this] {
            set_speed(mod_.initial_speed);
            set_tempo(mod_.initial_tempo);
            tick_ = mod_.initial_speed+1;
            for (auto& v : voices_) {
                mixer_.add_voice(v);
            }
            mixer_.global_volume(1.0f/mod_.num_channels);
        });
    }

    ~impl() {
        mixer_.at_next_tick([this] {
            for (auto& v : voices_) {
                mixer_.remove_voice(v);
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

private:
    module                                      mod_;
    mixer&                                      mixer_;
    bool                                        playing_ = false;
    int                                         speed_ = 6;     // Number of ticks per row 1..127
    int                                         order_ = 0;     // Position in order table 0..song length-1
    int                                         row_ = -1;      // Current row in pattern
    int                                         tick_ = 6;      // Current tick 0..speed
    int                                         pattern_jump_ = -1;
    int                                         pattern_break_row_ = -1;
    int                                         pattern_delay_ = -1;
    int                                         pattern_loop_row_ = -1;
    int                                         pattern_loop_counter_ = -1;
    bool                                        pattern_loop_ = false;
    event<module_position>                      on_position_changed_;
    std::vector<sample_voice>                   voices_;
    std::vector<std::unique_ptr<channel_base>>  channels_;

    friend channel_base;

    void schedule() {
        mixer_.at_next_tick([this] { tick(); });
    }

    void set_playing(bool playing) {
        playing_ = playing;
        for (auto& v : voices_) {
            v.paused(!playing_);
        }
        if (playing_) {
            schedule();
        }
    }

    module_position current_position() const {
        return module_position{order_, mod_.order[order_], std::max(0, row_)};
    }

    void notify_position_change() {
        on_position_changed_(current_position());
    }

    void process_row() {
        pattern_break_row_ = -1;
        pattern_jump_      = -1;
        for (int ch = 0; ch < mod_.num_channels; ++ch) {
            auto& channel = channels_[ch];
            const auto& note = mod_.at(order_, row_)[ch];
            channel->process_note(note);
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
                channel->process_effect(tick_, note.effect);
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

mod_player::mod_player(module&& mod, mixer& m) : impl_(std::make_unique<impl>(std::move(mod), m)) {
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

//
// channel_base
//
const module& channel_base::mod() const {
    return player_.mod_;
}

module_position channel_base::current_position() const {
    return player_.current_position();
}

void channel_base::do_set_speed(int speed) {
    player_.set_speed(speed);
}

void channel_base::do_set_tempo(int bpm) {
    player_.set_tempo(bpm);
}

void channel_base::do_pattern_break(int row) {
    player_.pattern_break(row);
}

void channel_base::do_pattern_jump(int order) {
    player_.pattern_jump(order);
}

void channel_base::do_pattern_delay(int delay_notes) {
    player_.pattern_delay(delay_notes);
}

void channel_base::do_pattern_loop(int x) {
    player_.pattern_loop(x);
}

//
// mod_channel
//
class mod_channel : public channel_base {
public:
    explicit mod_channel(mod_player::impl& player, sample_voice& voice, uint8_t default_pan) : channel_base(player, voice, default_pan) {
        assert(mod().type == module_type::mod);
    }

    virtual void process_note(const module_note& note) override {
        if (note.instrument) {
            instrument_number(note.instrument);
        }
        if (note.note != piano_key::NONE) {
            assert(note.note != piano_key::OFF);
            const int period = mod().note_to_period(note.note);
            const auto effect = note.effect>>8;
            if (effect == 3 || effect == 5) {
                set_porta_target(period);
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
        assert(note.volume == volume_command::none);
    }

    virtual void process_effect(int tick, int effect) override {
        assert(effect);
        const int xy = effect&0xff;
        const int x  = xy>>4;
        const int y  = xy&0xf;
        switch (effect>>8) {
        case 0x00: // 0xy Arpeggio
            do_arpeggio(tick, x, y);
            return;
        case 0x01: // 1xy Porta down
            if (tick) {
                do_porta(-xy);
            }
            return;
        case 0x02: // 2xy Porta down
            if (tick) {
                do_porta(+xy);
            }
            return;
        case 0x3: // 3xy Porta to note
            if (xy) porta_speed(xy);
            if (tick) {
                do_porta_to_note();
            }
            return;
        case 0x4: // 4xy Vibrato
            do_vibrato(tick, x, y);
            return;
        case 0x5: // 5xy Porta + Voume slide (5xy = 300 + Axy)
            if (tick) {
                do_porta_to_note();
                do_volume_slide(x, y);
            }
            return;
        case 0x6: // 6xy Vibrato + Volume slide (6xy = 400 + Axy)
            if (tick) {
                do_vibrato(tick, 0, 0);
                do_volume_slide(x, y);
            }
            return;
        case 0x8: // 8xy
            return; // ignored
        case 0x9: // 9xy Sample offset
            return;
        case 0xA: // Axy Volume slide
            if (tick) {
                do_volume_slide(x, y);
            }
            return;
        case 0xB: // Bxy Pattern jump
            if (!tick) {
                do_pattern_jump(xy);
            }
            return;
        case 0xC: // Cxy Set volume
            if (tick == 0) {
                volume(xy);
            }
            return;
        case 0xD: // Dxy Pattern break
            if (tick == 0) {
                do_pattern_break(x*10 + y);
            }
            return;
        case 0xE:
            switch (x) {
            case 0x0: // E0y Set fiter
                return;
            case 0x1: // E1y Fine porta down
                if (!tick) {
                    do_porta(-y);
                }
                return;
            case 0x2: // E2y Fine porta down
                if (!tick) {
                    do_porta(+y);
                }
                return;
            case 0x6: // E6y Pattern loop
                if (!tick) {
                    do_pattern_loop(y);
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
                    do_pattern_delay(y);
                }
                return;
            }
            break;
        case 0xF: // Fxy Set speed
            if (tick) return;
            if (xy < 0x20) {
                do_set_speed(xy);
            } else {
                do_set_tempo(xy);
            }
            return;
        }
        if (!tick) wprintf(L"Unhandled effect %03X\n", effect);
    }

private:
    int  sample_offset_ = 0;
};

//
// s3m_channel
//
class s3m_channel : public channel_base {
public:
    explicit s3m_channel(mod_player::impl& player, sample_voice& voice, uint8_t default_pan) : channel_base(player, voice, default_pan) {
        assert(mod().type == module_type::s3m);
    }

    virtual void process_note(const module_note& note) override {
        if (note.instrument) {
            instrument_number(note.instrument);
        }
        if (note.note != piano_key::NONE) {
            if (note.note == piano_key::OFF) {
                volume(0);
                return;
            }
            const int period = mod().note_to_period(note.note);
            const char effchar = static_cast<char>((note.effect>>8)-1+'A');
            if (effchar == 'G') {
                set_porta_target(period);
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
        if (note.volume != volume_command::none) {
            assert(note.volume >= volume_command::set_00 && note.volume <= volume_command::set_40);
            volume(note.volume - volume_command::set_00);
        }
    }

    virtual void process_effect(int tick, int effect) override {
        const char effchar = static_cast<char>((effect>>8)-1+'A');
        const int xy = effect&0xff;
        const int x = xy >> 4;
        const int y = xy & 0xf;
        switch (effchar) {
        case 'A': // Set speed
            do_set_speed(xy);
            break;
        case 'B': // Pattern jump
            if (!tick) {
                wprintf(L"Pattern jump! B%02X\n", xy);
                do_pattern_jump(xy);
            }
        case 'C': // Pattern break
            if (!tick) {
                do_pattern_break(x*10 + y);
            }
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
            do_vibrato(tick, x, y);
            break;
        case 'J': // Arpeggio
            do_arpeggio(tick, x, y);
            break;
        case 'K': // Kxy Vibrato + Volume Slide
            do_vibrato(tick, 0, 0);
            do_s3m_volume_slide(tick, xy);
            break;
        case 'G': // Gxy Porta to note
            if (xy) porta_speed(xy * 4);
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
                volume(new_vol);
                trig(0);
            }
            break;
        case 'S':
            switch(x) {
            case 0x8: // Pan position
                if (!tick) {
                    pan(y << 4);
                }
                break;
            case 0xB: // Pattern Loop
                if (!tick) {
                    do_pattern_loop(y);
                }
                break;
            case 0xC: // Note cut
                if (tick == y) {
                    volume(0);
                }
                break;
            case 0xD: // Note delay
                if (tick == y) {
                    trig(0);
                }
                break;
            default:
                if (!tick) wprintf(L"%2.2d: Ignoring effect %c%02X\n", current_position().row, effchar, xy);
            }
            break;
        case 'T': // Txy Set tempo
            assert(xy >= 0x20);
            do_set_tempo(xy);
            break;
        default:
            if (!tick) wprintf(L"%2.2d: Ignoring effect %c%02X\n", current_position().row, effchar, xy);
        }
    }

private:
    int  sample_offset_ = 0;
    int  last_vol_slide_ = 0;
    int  last_retrig_ = 0;

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
                do_volume_slide(x, y);
            }
        }            
    }

    // direction -1 => Up, 1 => Down
    void do_s3m_porta(int tick, int direction, int xy) {
        assert(direction == -1 || direction == +1);
        if (xy) porta_speed(xy);
        const int x = porta_speed() >> 4;
        const int y = porta_speed() & 0xf;
        const bool is_fine = x == 0xE || x == 0xF;
        if (!tick) {
            if (is_fine) {
                do_porta(direction * (x == 0xF ? 4 : 1) * y);
            }
        } else {
            if (!is_fine) {
                do_porta(direction * 4 * porta_speed());
            }
        }
    }
};

//
// xm_channel
//
class xm_channel : public channel_base {
public:
    explicit xm_channel(mod_player::impl& player, sample_voice& voice, uint8_t default_pan) : channel_base(player, voice, default_pan) {
    }
    virtual void process_note(const module_note& note) override {
        if (note.instrument) {
            instrument_number(note.instrument);
        }
        if (note.note != piano_key::NONE) {
            if (note.note == piano_key::OFF) {
                volume(0);
                return;
            }
            const int effect_type = note.effect >> 8;
            const int period = mod().note_to_period(note.note);
            if (effect_type == 3 || effect_type == 5) {
                set_porta_target(period);
            } else {
                set_period(period);

                if (note.effect>>4 != 0xED) {
                    int offset = 0;
                    if (effect_type == 9) {
                        offset = (note.effect & 0xff) << 8;
                        if (offset) sample_offset_ = offset;
                        else offset = sample_offset_;
                    }
                    trig(offset);
                }
            }
        }
        if (note.volume != volume_command::none) {
            if (note.volume >= volume_command::set_00 && note.volume <= volume_command::set_40) {
                volume(note.volume - volume_command::set_00);
            } else if (note.volume >= volume_command::pan_0 && note.volume <= volume_command::pan_f) {
                pan((note.volume - volume_command::pan_0) << 4);
            } else {
                wprintf(L"%2.2d: Ignoring volume command %02X\n", current_position().row, static_cast<int>(note.volume));
            }
        }
    }
    virtual void process_effect(int tick, int effect) override {
        const int effect_type = effect >> 8;
        const int xy          = effect & 0xff;
        const int x           = xy >> 4;
        const int y           = xy & 0xf;
        assert(effect_type < 38);
        switch (effect_type) {
        case 0x0: // 0xy Arpeggio
            do_arpeggio(tick, x, y);
            return;
        case 0x01: // 1xy Porta down
            if (tick) {
                do_porta(-xy);
            }
            return;
        case 0x02: // 2xy Porta down
            if (tick) {
                do_porta(+xy);
            }
            return;
        case 0x3: // 3xy Porta to note
            if (xy) porta_speed(xy);
            if (tick) {
                do_porta_to_note();
            }
            return;
        case 0x4: // 4xy Vibrato
            do_vibrato(tick, x, y);
            return;
        case 0x5: // 5xy Porta + Voume slide (5xy = 300 + Axy)
            if (tick) {
                do_porta_to_note();
                do_volume_slide(x, y);
            }
            return;
        case 0x6: // 6xy Vibrato + Volume slide (6xy = 400 + Axy)
            if (tick) {
                do_vibrato(tick, 0, 0);
                do_volume_slide(x, y);
            }
            return;
        case 0x8: // 8xy Set pan
            pan(xy);
            return;
        case 0x9: // 9xy Sample offset
            break;
        case 0xA: // Axy Volume slide
            if (tick) {
                do_volume_slide(x, y);
            }
            return;
        case 0xC: // Cxy Set volume
            if (tick == 0) {
                volume(xy);
            }
            return;
        case 0xD: // Dxy Pattern break
            if (tick == 0) {
                do_pattern_break(x*10 + y);
            }
            return;
        case 0xE:
            switch (x) {
            case 0x0: // E0y Set fiter
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
            case 0xD: // EDy Delay note
                if (tick == y) {
                    trig(0);
                }
                return;
            default:
                if (!tick) {
                    wprintf(L"%2.2d: Ignoring effect E%02X\n", current_position().row, xy);
                }
            }
            break;
        case 0xF: // Fxy Set speed
            if (tick) return;
            if (xy < 0x20) {
                do_set_speed(xy);
            } else {
                do_set_tempo(xy);
            }
            return;
        case 'W'-'A'+10: // Wxy Sync?
            break;
        default:
            if (!tick) {
                wprintf(L"%2.2d: Ignoring effect %c%02X\n", current_position().row, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[effect_type], xy);
            }
        }
    }
private:
    int  sample_offset_ = 0;
};

std::unique_ptr<channel_base> make_channel(mod_player::impl& player, sample_voice& voice, uint8_t default_pan) {
    switch (player.mod().type) {
    case module_type::mod: return std::make_unique<mod_channel>(player, voice, default_pan);
    case module_type::s3m: return std::make_unique<s3m_channel>(player, voice, default_pan);
    case module_type::xm: return std::make_unique<xm_channel>(player, voice, default_pan);
    }
    assert(false);
    throw std::runtime_error("Unknown module type");
}