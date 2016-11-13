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

std::vector<float> convert_sample_data(const std::vector<signed char>& d) {
    std::vector<float> data(d.size());
    for (size_t i = 0, len = d.size(); i < len; ++i) {
        data[i] = d[i]/128.0f;
    }
    return data;
}

#include <queue>
#include <mutex>
class job_queue {
public:
    using job_type = std::function<void(void)>;

    explicit job_queue() {
    }

    void push(job_type job) {
        std::lock_guard<std::mutex> lock{mutex_};
        jobs_.push(job);
    }

    void perform_all() {
        std::queue<job_type> jobs;
        {
            std::lock_guard<std::mutex> lock{mutex_};
            jobs = std::move(jobs_);
        }
        while (!jobs.empty()) {
            jobs.front()();
            jobs.pop();
        }
    }

private:
    std::mutex           mutex_;
    std::queue<job_type> jobs_;
};

class mixer {
public:
    class channel {
    public:
        explicit channel() {
        }

        void key_off() {
            state_ = state::not_playing;
        }

        void play(const ::sample& s, int pos) {
            assert(pos >= 0 && pos <= s.length());
            sample_ = &s;
            pos_    = static_cast<float>(pos);
            state_  = state::playing_first;
        }

        void freq(float f) {
            incr_ = f / sample_rate;
        }

        void volume(float volume) {
            volume_ = volume;
        }

        void mix(float* stero_buffer, int num_stereo_samples) {
            if (state_ == state::not_playing) return;
            assert(sample_);

            while (num_stereo_samples) {
                const int end = state_ == state::looping ? sample_->loop_start() + sample_->loop_length() : sample_->length();
                assert(pos_ <= end);
                const int samples_till_end = static_cast<int>((end - pos_) / incr_);
                if (!samples_till_end) {
                    if (sample_->loop_length()) {
                        assert(state_ == state::playing_first || state_ == state::looping);
                        pos_   = static_cast<float>(sample_->loop_start());
                        state_ = state::looping;
                        continue;
                    } else {
                        //wprintf(L"End of sample at %f\n", pos_);
                        state_ = state::not_playing;
                        break;
                    }
                }

                const int now = std::min(samples_till_end, num_stereo_samples);
                assert(now > 0);
                do_mix(stero_buffer, now, *sample_, pos_, incr_, volume_, volume_);
                num_stereo_samples -= now;
                stero_buffer       += 2* now;
                pos_               += incr_ * now;
            }
        }

    private:
        const ::sample* sample_ = nullptr;
        float           pos_;
        float           incr_;
        float           volume_;
        enum class state {
            not_playing,
            playing_first,
            looping,
        } state_ = state::not_playing;

        static void do_mix(float* stero_buffer, int num_stereo_samples, const sample& samp, float pos, float incr, float lvol, float rvol) {
            for (int i = 0; i < num_stereo_samples; ++i) {
                const auto s = samp.get_linear(pos + i * incr);
                stero_buffer[i*2+0] += s * lvol;
                stero_buffer[i*2+1] += s * rvol;
            }
        }
    };

    explicit mixer(int channels)
        : channels_(channels)
        , mix_buffer_(buffer_size)
        , wavedev_(sample_rate, buffer_size, [this](short* s, size_t count) { assert(count*2 == buffer_size); render(s); }) {
    }

    void at_next_tick(job_queue::job_type job) {
        at_next_tick_.push(job);
    }

    channel& get_channel(int index) {
        return channels_[index];
    }

    void ticks_per_second(int tps) {
        ticks_per_second_ = tps;
    }

private:
    static constexpr int sample_rate = 44100;
    static constexpr int buffer_size = 4096;

    std::vector<channel> channels_;
    std::vector<float>   mix_buffer_;
    int                  next_tick_ = 0;
    int                  ticks_per_second_ = 50; // 125 BPM = 125 * 2 / 5 = 50 Hz
    job_queue            at_next_tick_;
    // must be last
    wavedev              wavedev_;

    void tick() {
        at_next_tick_.perform_all();
    }

    void render(short* s) {
        float* buffer = &mix_buffer_[0];
        memset(buffer, 0, buffer_size * sizeof(float));
        for (int num_stereo_samples = buffer_size / 2; num_stereo_samples;) {
            if (!next_tick_) {
                tick();
                next_tick_ = sample_rate / ticks_per_second_;
            }

            const auto now = std::min(next_tick_, num_stereo_samples);

            for (auto& ch : channels_) {
                ch.mix(buffer, now);
            }

            buffer             += now * 2;
            num_stereo_samples -= now;
            next_tick_         -= now;
        }

        for (size_t i = 0; i < buffer_size; ++i) {
            s[i] = sample_to_s16(mix_buffer_[i]/(channels_.size()/2));
        }
    }
};

class mod_player {
public:
    explicit mod_player(const module& mod, mixer& m) : mod_(mod), mixer_(m) {
        for (int i = 0; i < num_channels; ++i) {
            channels_.emplace_back(*this, m.get_channel(i));
        }
    }

    void set_samples(std::vector<sample>* samples) {
        samples_ = samples;
    }

    const module& mod() const { return mod_; }

    struct position {
        int order, pattern, row;
    };

    void play() {
        schedule();
    }

    void on_position_changed(const callback_function_type<position>& cb) {
        on_position_changed_.subscribe(cb);
    }

    static constexpr int num_channels = 4;
    static constexpr int num_rows     = 64;
    static constexpr int max_volume   = 64;

    static constexpr float clock_rate = 7159090.5f;

    static float period_to_freq(int period, int finetune) {
        assert(finetune >= -8 && finetune < 7);
        return note_difference_to_scale(finetune / 8.0f) * clock_rate / (period * 2);
    }

    static int freq_to_period(float freq) {
        return static_cast<int>(clock_rate / (2 * freq));
    }

    static piano_key period_to_piano_key(int period) {
        /*
        Finetune 0
                   C    C#   D    D#   E    F    F#   G    G#   A    A#   B
        Octave 0:1712,1616,1525,1440,1357,1281,1209,1141,1077,1017, 961, 907
        Octave 1: 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453
        Octave 2: 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226
        Octave 3: 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
        Octave 4: 107, 101,  95,  90,  85,  80,  76,  71,  67,  64,  60,  57
        */

        constexpr int note_periods[12 * 5] = {
            1712,1616,1525,1440,1357,1281,1209,1141,1077,1017, 961, 907,
            856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
            428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
            214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
            107, 101,  95,  90,  85,  80,  76,  71,  67,  64,  60,  57,
        };

        for (int i = 0; i < sizeof(note_periods) / sizeof(*note_periods); ++i) {
            if (period == note_periods[i]) {
                return piano_key::C_0 + i + 2*12; // Octave offset: PT=0, FT2=2, MPT=3
            }
        }
        assert(false);
        return piano_key::OFF;
    }

private:
    class channel {
    public:
        explicit channel(mod_player& player, mixer::channel& mix_chan) : player_(player), mix_chan_(mix_chan) {}

        void process_note(const module_note& note) {
            if (note.sample) { 
                assert(note.sample >= 1 && note.sample <= 32);
                sample_ = note.sample;
                volume(mod_sample().volume);
            }
            if (note.period) {
                const auto effect = note.effect>>8;
                if (effect == 3 || effect == 5) {
                    porta_target_period_ = note.period;
                } else {
                    set_period(note.period);
                    if (effect != 9 && note.effect>>4 != 0xED) {
                        trig(0);
                    }
                }
            }
        }

        void process_effect(int tick, int effect) {
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
                if (!tick) {
                    if (xy) sample_offset_ = xy << 8;
                    trig(sample_offset_);
                }
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
                }
                break;
            case 0xF: // Fxy Set speed
                if (tick) return;
                if (xy < 0x20) {
                    player_.speed_ = xy;
                } else {
                    // BPM, 2*bpm/5 ticks per second
                    player_.mixer_.ticks_per_second(xy*2/5);
                }
                return;
            }
            if (!tick) wprintf(L"Unhandled effect %03X\n", effect);
        }

    private:
        mod_player&     player_;
        mixer::channel& mix_chan_;
        int             sample_ = 0;
        int             volume_ = 0;
        int             period_ = 0;
        int             porta_target_period_ = 0;
        int             porta_speed_ = 0;
        int             vib_depth_ = 0;
        int             vib_speed_ = 0;
        int             vib_pos_   = 0;
        int             sample_offset_ = 0;

        int volume() const { return volume_; }

        void volume(int vol) {
            assert(vol >= 0 && vol <= max_volume);
            volume_ = vol;
            mix_chan_.volume(static_cast<float>(vol) / max_volume);
        }

        void set_voice_period(int period) {
            mix_chan_.freq(period_to_freq(period, mod_sample().finetune));
        }

        void set_period(int period) {
            period_ = period;
            set_voice_period(period_);
        }

        void do_arpeggio(int amount) {
            const int res_per = freq_to_period(period_to_freq(period_, 0) * note_difference_to_scale(static_cast<float>(amount)));
            //wprintf(L"Arpeggio base period = %d, amount = %d, resulting period = %d\n", period_, amount, res_per);
            set_voice_period(res_per);
        }

        void do_volume_slide(int amount) {
            volume(std::max(0, std::min(max_volume, volume() + amount)));
        }

        void do_volume_slide_xy(int x, int y) {
            if (x && y) {
            } else if (x > 0) {
                do_volume_slide(+x);
            } else if (y > 0) {
                do_volume_slide(-y);
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


        const module_sample& mod_sample() const {
            assert(sample_ >= 1 && sample_ <= 32);
            return player_.mod_.samples[sample_ - 1];
        }

        void trig(int offset) {
            vib_pos_ = 0;
            if (player_.samples_) {
                assert(sample_);
                auto& s = (*player_.samples_)[sample_-1];
                if (s.length()) {
                    mix_chan_.play(s, std::min(s.length(), offset));
                }
            }
        }
    };

    module mod_;
    mixer& mixer_;
    int      speed_ = 6;     // Number of ticks per row 1..127
    int      order_ = 0;     // Position in order table 0..song length-1
    int      row_ = -1;      // Current row in pattern
    int      tick_ = 6;      // Current tick 0..speed
    int      pattern_jump_ = -1;
    int      pattern_break_row_ = -1;
    std::vector<channel> channels_;

    std::vector<sample>* samples_ = nullptr;
    event<position> on_position_changed_;

    void schedule() {
        mixer_.at_next_tick([this] { tick(); });
    }

    void process_row() {
        pattern_break_row_ = -1;
        pattern_jump_      = -1;
        for (int ch = 0; ch < num_channels; ++ch) {
            auto& channel = channels_[ch];
            const auto& note = mod_.at(order_, row_)[ch];
            channel.process_note(note);
        }
    }

    void next_order() {
        if (++order_ >= mod_.num_order) {
            order_ = 0;
            assert(false);
        }
    }

    void tick() {
        if (++tick_ >= speed_) {
            tick_ = 0;
            if (pattern_jump_ != -1) {
                order_ = pattern_jump_;
                row_   = pattern_break_row_ == -1 ? -1 : pattern_break_row_ - 1;
                wprintf(L"Pattern jump %d, %d\n", order_, row_ + 1);
            } else if (pattern_break_row_ != -1) {
                ++order_;
                row_   = pattern_break_row_ - 1;
                wprintf(L"Pattern break %d, %d\n", order_, row_ + 1);
            }

            if (++row_ >= num_rows) {
                row_ = 0;
                next_order();
            }
            process_row();
            on_position_changed_(position{order_, mod_.order[order_], row_});
        } else {
            // Tick
        }
        for (int ch = 0; ch < num_channels; ++ch) {
            auto& channel = channels_[ch];
            const auto& note = mod_.at(order_, row_)[ch];
            if (note.effect) {
                channel.process_effect(tick_, note.effect);
            }
        }
        schedule();
    }

    void pattern_break(int row) {
        assert(row >= 0 && row < num_rows);
        assert(pattern_break_row_ == -1);
        pattern_break_row_ = row;
    }

    void pattern_jump(int order) {
        assert(order_ >= 0 && order_ < mod_.num_order);
        assert(pattern_jump_ == -1);
        pattern_jump_      = order;
        pattern_break_row_ = -1; // A pattern jump after a pattern break makes the break have no effect
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
        return { 3, 9, 9, 9, 9 };
    }
    virtual std::string do_cell_value(int row, int column) const override {
        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        if (column == 0) {
            ss << std::setw(2) << row;
        } else {
            ss << std::setw(2) << column;
        }
        return ss.str();
    }
};

class mod_grid : public mod_like_grid {
public:
    explicit mod_grid(const module& mod) : mod_(mod) {
    }

    virtual void do_order_change(int order) override {
        if (order_ != order) {
            wprintf(L"\n\nNew Order %d\n\n", order);
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
        constexpr int w = 10;
        return { 2, w, w, w, w };
    }

    virtual std::string do_cell_value(int row, int column) const override {
        assert(row >= 0 && row < mod_player::num_rows);
        assert(row >= 0 && column < mod_player::num_channels+1);
        std::ostringstream ss;
        ss << std::hex << std::setfill('0') << std::uppercase;
        if (column == 0) {
            ss << std::setw(2) << row;
        } else {
            const auto& note = mod_.at(order_, row)[column-1];
            if (note.period) {
                ss << piano_key_to_string(mod_player::period_to_piano_key(note.period));
            } else {
                ss << "...";
            }
            ss << " ";
            if (note.sample) {
                ss << std::setw(2) << (int)note.sample;
            } else {
                ss << "..";
            }
            ss << " ";
            if (note.effect) {
                ss << std::setw(3) << (int)note.effect;
            } else {
                ss << "...";
            }
        }
        return ss.str();
    }
};

int main(int argc, char* argv[])
{
    try {
        mixer m{32};

        std::vector<sample> samples;
        std::unique_ptr<mod_player> mod_player_;
        std::unique_ptr<mod_like_grid> grid;
        if (argc > 1) {
            mod_player_.reset(new mod_player(load_module(argv[1]), m));
            auto& mod = mod_player_->mod();
            wprintf(L"Loaded '%S' - '%22.22S'\n", argv[1], mod.name);
            for (int i = 0; i < 31; ++i) {
                const auto& s = mod.samples[i];
                wprintf(L"%22.22S FineTune: %d\n", s.name, s.finetune);
                samples.emplace_back(convert_sample_data(s.data), 8363.0f*note_difference_to_scale(s.finetune/8.0f), std::string(s.name, s.name+sizeof(s.name)));
                if (s.loop_length > 2) {
                    samples.back().loop_start(s.loop_start);
                    samples.back().loop_length(s.loop_length);
                }
            }
            mod_player_->set_samples(&samples);
            grid.reset(new mod_grid{mod});
        } else {
            samples.emplace_back(create_sample(44100/4, piano_key_to_freq(piano_key::C_5)), 44100.0f, "Test sample");
            grid.reset(new test_grid{});
        }
        auto main_wnd = main_window::create(*grid);
        main_wnd.set_samples(samples);

        main_wnd.on_piano_key_pressed([&](piano_key key) {
            if (key == piano_key::OFF) {
                m.at_next_tick([&] { m.get_channel(0).key_off(); } );
                return;
            }
            const int idx = main_wnd.current_sample_index();
            if (idx < 0 || idx >= samples.size()) return;
            const auto freq = piano_key_to_freq(key, piano_key::C_5, samples[idx].c5_rate());
            wprintf(L"Playing %S at %f Hz\n", piano_key_to_string(key).c_str(), freq);
            m.at_next_tick([&m, &samples, idx, freq] {
                auto& ch = m.get_channel(0);
                ch.freq(freq);
                ch.play(samples[idx], 0); 
            });
        });

        ShowWindow(main_wnd.hwnd(), SW_SHOW);
        UpdateWindow(main_wnd.hwnd());

        job_queue gui_jobs;
        const DWORD gui_thread_id = GetCurrentThreadId();
        auto add_gui_job = [&gui_jobs, gui_thread_id] (const job_queue::job_type& job) {
            gui_jobs.push(job);
            PostThreadMessage(gui_thread_id, WM_NULL, 0, 0);
        };

        if (mod_player_) {
            mod_player_->on_position_changed([&](const mod_player::position& pos) {
                add_gui_job([&main_wnd, &grid, pos] {
                    grid->do_order_change(pos.order);
                    main_wnd.update_grid(pos.row);
                });
            });
            mod_player_->play();
        }

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            if (msg.hwnd == nullptr && msg.message == WM_NULL) {
                gui_jobs.perform_all();
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