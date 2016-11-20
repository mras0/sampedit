#include <stdio.h>
#include <cassert>
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

#include <win32/base.h>
#include <win32/gdi.h>
#include <win32/sample_window.h>
#include <win32/main_window.h>

#include <base/job_queue.h>
#include <base/sample_voice.h>
#include "module.h"
#include "win32/wavedev.h"

std::vector<float> create_sample(int len, float freq, int rate=44100)
{
    std::vector<float> data(len);
    constexpr float pi = 3.14159265359f;
    for (int i = 0; i < len; ++i) {
        data[i] = std::cosf(2*pi*freq*i/rate);
    }
    return data;
}

class mixer {
public:
    explicit mixer()
        : wavedev_(sample_rate_, 4096, [this](short* s, size_t num_stereo_samples) { render(s, static_cast<int>(num_stereo_samples)); }) {
    }

    int sample_rate() const {
        return sample_rate_;
    }

    void at_next_tick(job_queue::job_type job) {
        at_next_tick_.post(job);
    }

    void add_voice(voice& v) {
        at_next_tick_.assert_in_queue_thread();
        voices_.push_back(&v);
    }

    void ticks_per_second(int tps) {
        at_next_tick_.assert_in_queue_thread();
        ticks_per_second_ = tps;
    }

    void global_volume(float vol) {
        at_next_tick_.assert_in_queue_thread();
        global_volume_ = vol;
    }

private:
    static constexpr int sample_rate_ = 44100;

    std::vector<voice*>  voices_;
    std::vector<float>   mix_buffer_;
    int                  next_tick_ = 0;
    int                  ticks_per_second_ = 50; // 125 BPM = 125 * 2 / 5 = 50 Hz
    float                global_volume_ = 1.0f;
    job_queue            at_next_tick_;
    // must be last
    wavedev              wavedev_;

    void tick() {
        at_next_tick_.perform_all();
    }

    void render(short* s, int num_stereo_samples) {
        mix_buffer_.resize(num_stereo_samples * 2);
        float* buffer = &mix_buffer_[0];
        memset(buffer, 0, num_stereo_samples * 2 * sizeof(float));
        while (num_stereo_samples) {
            if (!next_tick_) {
                tick();
                next_tick_ = sample_rate_ / ticks_per_second_;
            }

            const auto now = std::min(next_tick_, num_stereo_samples);

            for (auto v : voices_) {
                v->mix(buffer, now);
            }

            buffer             += now * 2;
            num_stereo_samples -= now;
            next_tick_         -= now;
        }

        for (size_t i = 0; i < mix_buffer_.size(); ++i) {
            s[i] = sample_to_s16(mix_buffer_[i]*global_volume_);
        }
    }
};

class mod_player {
public:
    explicit mod_player(const module& mod, mixer& m) : mod_(mod), mixer_(m) {
        for (int i = 0; i < mod_.num_channels; ++i) {
            channels_.emplace_back(*this);
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

    static constexpr int num_rows     = 64;
    static constexpr int max_volume   = 64;

private:
    class channel {
    public:
        explicit channel(mod_player& player) : player_(player), mix_chan_(player.mixer_.sample_rate()) {
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
        mod_player&     player_;
        sample_voice    mix_chan_;
        int             sample_ = 0;
        int             volume_ = 0;
        int             period_ = 0;
        int             porta_target_period_ = 0;
        int             porta_speed_ = 0;
        int             vib_depth_ = 0;
        int             vib_speed_ = 0;
        int             vib_pos_   = 0;
        int             last_vol_slide_ = 0; // s3m only
        int             last_retrig_ = 0;    // s3m only
        int             sample_offset_ = 0;

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
                        assert(pattern_loop_row_ >= 0 && pattern_loop_row_ < num_rows);
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

                if (++row_ >= num_rows) {
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
        assert(row >= 0 && row < num_rows);
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

class mod_like_grid : public virtual_grid {
public:
    virtual void do_order_change(int order) = 0;
};

#include <iomanip>
#include <sstream>

class test_grid : public mod_like_grid {
public:
    virtual void do_order_change(int) override {
    }

private:
    virtual int do_rows() const override {
        return 64;
    }
    virtual std::vector<int> do_column_widths() const override {
        return { 9, 9, 9, 9 };
    }
    virtual std::string do_cell_value(int row, int column) const override {
        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        ss << '(' << std::setw(2) << column << ", " << std::setw(2) << row << ')';
        return ss.str();
    }
};

class mod_grid : public mod_like_grid {
public:
    explicit mod_grid(const module& mod) : mod_(mod) {
    }

    virtual void do_order_change(int order) override {
        if (order_ != order) {
            wprintf(L"Order %d, Pattern %d\n", order, mod_.order[order]);
            order_ = order;
        }
    }

private:
    const module& mod_;
    int order_ = 0;

    virtual int do_rows() const override {
        return 64;
    }

    virtual std::vector<int> do_column_widths() const override {
        return std::vector<int>(mod_.num_channels, mod_.type == module_type::mod ? 10 : 14);
    }

    virtual std::string do_cell_value(int row, int column) const override {
        assert(row >= 0 && row < mod_player::num_rows);
        assert(column >= 0 && column < mod_.num_channels);
        std::ostringstream ss;
        ss << std::hex << std::setfill('0') << std::uppercase;
        const auto& note = mod_.at(order_, row)[column];
        if (note.note != piano_key::NONE) {
            ss << piano_key_to_string(note.note) << ' ';
        } else {
            ss << "... ";
        }
        if (note.instrument) {
            ss << std::setw(2) << (int)note.instrument << ' ';
        } else {
            ss << ".. ";
        }
        if (mod_.type != module_type::mod) {
            assert(mod_.type == module_type::s3m);
            if (note.volume != no_volume_byte) {
                assert(note.volume < 100);
                ss << 'v' << std::setw(2) << std::dec << (int)note.volume << std::hex << ' ';
            } else {
                ss << "... ";
            }
        }
        if (note.effect) {
            if (mod_.type == module_type::mod) {
                ss << std::setw(3) << (int)note.effect;
            } else {
                assert(mod_.type == module_type::s3m);
                ss << (char)((note.effect >> 8) + 'A' - 1);
                ss << std::setw(2) << (int)(note.effect & 0xff);
            }
        } else {
            ss << "...";
        }
        assert(ss.str().length() == column_widths()[column]);
        return ss.str();
    }
};

int main(int argc, char* argv[])
{
    try {
        mixer m;

        const module* mod_ = nullptr;
        std::unique_ptr<mod_player> mod_player_;
        std::unique_ptr<mod_like_grid> grid;
        if (argc > 1) {
            mod_player_.reset(new mod_player(load_module(argv[1]), m));
            auto& mod = mod_player_->mod();
            wprintf(L"Loaded '%S' - '%-20.20S' %d channels\n", argv[1], mod.name.c_str(), mod.num_channels);
            for (size_t i = 0; i < mod.samples.size(); ++i) {
                const auto& s = mod.samples[i];
                wprintf(L"%2.2d: %-22.22S c5 rate: %d\n", (int)(i+1), s.name().c_str(), (int)(s.c5_rate()+0.5f));
            }
            mod_ = &mod;
            grid.reset(new mod_grid{mod});
        } else {
            static module mod;
            mod.samples.emplace_back(create_sample(44100/4, piano_key_to_freq(piano_key::C_5)), 44100.0f, "Test sample");
            mod.order.push_back(0);
            grid.reset(new test_grid{});
            mod_ = &mod;
        }
        auto main_wnd = main_window::create(*grid);
        assert(mod_);
        main_wnd.set_module(*mod_);

        sample_voice keyboard_voice(m.sample_rate());
        keyboard_voice.volume(0.5f);
        m.at_next_tick([&] { m.add_voice(keyboard_voice); });
        main_wnd.on_piano_key_pressed([&](piano_key key) {
            assert(key != piano_key::NONE);
            if (key == piano_key::OFF) {
                m.at_next_tick([&] { keyboard_voice.key_off(); } );
                return;
            }
            const int idx = main_wnd.current_sample_index();
            if (idx < 0 || idx >= mod_->samples.size()) return;
            const auto freq = piano_key_to_freq(key, piano_key::C_5, mod_->samples[idx].c5_rate());
            wprintf(L"Playing %S at %f Hz\n", piano_key_to_string(key).c_str(), freq);
            m.at_next_tick([&, idx, freq] {
                keyboard_voice.freq(freq);
                keyboard_voice.play(mod_->samples[idx], 0); 
            });
        });
        main_wnd.on_start_stop([&]() {
            if (mod_player_) {
                mod_player_->toggle_playing();
            }
        });

        ShowWindow(main_wnd.hwnd(), SW_SHOW);
        UpdateWindow(main_wnd.hwnd());

        job_queue gui_jobs;
        const DWORD gui_thread_id = GetCurrentThreadId();
        auto add_gui_job = [&gui_jobs, gui_thread_id] (const job_queue::job_type& job) {
            gui_jobs.post(job);
            PostThreadMessage(gui_thread_id, WM_NULL, 0, 0);
        };

        bool exiting = false;
        if (mod_player_) {
            mod_player_->on_position_changed([&](const module_position& pos) {
                add_gui_job([&main_wnd, &grid, pos] {
                    grid->do_order_change(pos.order);
                    main_wnd.position_changed(pos);
                });
            });
            main_wnd.on_exiting([&]() {
                wprintf(L"Exiting\n");
                exiting = true;
                mod_player_->stop();
            });
            main_wnd.on_order_selected([&](int order) {
               mod_player_->skip_to_order(order);
            });
            const int skip_to = argc > 2 ? std::stoi(argv[2]) : 0;
            if (skip_to > 0) {
                mod_player_->skip_to_order(skip_to);
            }
            mod_player_->toggle_playing();
        }

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            if (msg.hwnd == nullptr && msg.message == WM_NULL) {
                if (!exiting) {
                    gui_jobs.perform_all();
                }
            } else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        return static_cast<int>(msg.wParam);
    } catch (const std::runtime_error& e) {
        wprintf(L"%S\n", e.what());
    }
    return 1;
}