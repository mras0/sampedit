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
    explicit mixer(int channels)
        : channels_(channels)
        , mix_buffer_(buffer_size)
        , wavedev_(sample_rate, buffer_size, [this](short* s, size_t count) { assert(count*2 == buffer_size); render(s); }) {
    }

    void at_next_tick(job_queue::job_type job) {
        at_next_tick_.push(job);
    }

    void play(int channel, const ::sample& s, float freq) {
        assert(channel >= 0 && channel < channels_.size());
        channels_[channel].play(s, freq);
    }

    void key_off(int channel) {
        assert(channel >= 0 && channel < channels_.size());
        channels_[channel].key_off();
    }

private:
    static constexpr int sample_rate = 44100;
    static constexpr int buffer_size = 4096;

    class channel {
    public:
        explicit channel() {
        }

        void key_off() {
            state_ = state::not_playing;
        }

        void play(const ::sample& s, float freq) {
            sample_ = &s;
            pos_    = 0;
            incr_   = freq / sample_rate;
            state_  = state::playing_first;
        }

        void mix(float* stero_buffer, int num_stereo_samples) {
            if (state_ == state::not_playing) return;
            assert(sample_);

            while (num_stereo_samples) {
                const int end = state_ == state::looping ? sample_->loop_start() + sample_->loop_length() : sample_->length();
                const int samples_till_end = static_cast<int>((end - pos_) / incr_);
                if (!samples_till_end) {
                    if (sample_->loop_length()) {
                        assert(state_ == state::playing_first || state_ == state::looping);
                        pos_   = static_cast<float>(sample_->loop_start());
                        state_ = state::looping;
                        continue;
                    } else {
                        wprintf(L"End of sample at %f\n", pos_);
                        state_ = state::not_playing;
                        break;
                    }
                }

                const int now = std::min(samples_till_end, num_stereo_samples);
                assert(now > 0);
                do_mix(stero_buffer, now, *sample_, pos_, incr_);
                num_stereo_samples -= now;
                stero_buffer       += 2* now;
                pos_               += incr_ * now;
            }

            #if 0
            while (pos_ < sample_->length() && num_stereo_samples) {
                const auto s = sample_->get_linear(pos_);
                stero_buffer[0] += s;
                stero_buffer[1] += s;
                pos_          += incr_;
                stero_buffer  += 2;
                num_stereo_samples--;
            }
            #endif
        }

    private:
        const ::sample* sample_ = nullptr;
        float           pos_;
        float           incr_;
        enum class state {
            not_playing,
            playing_first,
            looping,
        } state_ = state::not_playing;

        static void do_mix(float* stero_buffer, int num_stereo_samples, const sample& samp, float pos, float incr) {
            for (int i = 0; i < num_stereo_samples; ++i) {
                const auto s = samp.get_linear(pos + i * incr);
                stero_buffer[i*2+0] += s;
                stero_buffer[i*2+1] += s;
            }
        }
    };

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
            s[i] = sample_to_s16(mix_buffer_[i]);
        }
    }
};

#include <iomanip>
#include <sstream>
#include <base/virtual_grid.h>
class test_grid : public virtual_grid {
public:
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

class mod_player {
public:
    explicit mod_player(const module& mod, mixer& m) : mod_(mod), mixer_(m) {
    }

    const module& mod() const { return mod_; }

    struct position {
        int order, pattern, row;
    };

    void play_pattern() {
        schedule();
    }

    void on_position_changed(const callback_function_type<position>& cb) {
        on_position_changed_.subscribe(cb);
    }
    
private:
    module mod_;
    mixer& mixer_;
    int      speed_ = 6;     // Number of ticks per row 1..127
    int      bpm_   = 125;   // BPM, 2*bpm/5 ticks per second
    int      order_ = 0;     // Position in order table 0..song length-1
    int      row_ = -1;      // Current row in pattern
    int      tick_ = 6;      // Current tick 0..speed

    event<position> on_position_changed_;

    void schedule() {
        mixer_.at_next_tick([this] { tick(); });
    }

    void process_row() {
        
    }

    void tick() {
        if (++tick_ >= speed_) {
            tick_ = 0;
            if (++row_ >= 64) {
                row_ = 0;
            }
            process_row();
            on_position_changed_(position{order_, mod_.order[order_], row_});
        } else {
            // Tick
        }
        schedule();
    }
};

class mod_grid : public virtual_grid {
public:
    explicit mod_grid(const module& mod) : mod_(mod) {
    }

private:
    const module& mod_;

    virtual int do_rows() const override {
        return 64;
    }

    virtual std::vector<int> do_column_widths() const override {
        constexpr int w = 10;
        return { 2, w, w, w, w };
    }

    static piano_key period_to_piano_key(int period) {
        //const float sample_rate = 7159090.5f / (period * 2);

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

    virtual std::string do_cell_value(int row, int column) const override {
        assert(row >= 0 && row < 64);
        assert(row >= 0 && column < 5);
        std::ostringstream ss;
        ss << std::hex << std::setfill('0') << std::uppercase;
        if (column == 0) {
            ss << std::setw(2) << row;
        } else {
            const auto& note = mod_.at(0, row)[column-1];
            if (note.period) {
                ss << piano_key_to_string(period_to_piano_key(note.period));
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
                ss << std::setw(2) << (int)note.effect;
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
        std::unique_ptr<virtual_grid> grid;
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
            grid.reset(new mod_grid{mod});
        } else {
            samples.emplace_back(create_sample(44100/4, piano_key_to_freq(piano_key::C_5)), 44100.0f, "Test sample");
            grid.reset(new test_grid{});
        }
        auto main_wnd = main_window::create(*grid);
        main_wnd.set_samples(samples);

        main_wnd.on_piano_key_pressed([&](piano_key key) {
            if (key == piano_key::OFF) {
                m.at_next_tick([&] { m.key_off(0); } );
                return;
            }
            const int idx = main_wnd.current_sample_index();
            if (idx < 0 || idx >= samples.size()) return;
            const auto freq = piano_key_to_freq(key, piano_key::C_5, samples[idx].c5_rate());
            wprintf(L"Playing %S at %f Hz\n", piano_key_to_string(key).c_str(), freq);
            m.at_next_tick([&m, &samples, idx, freq] { m.play(0, samples[idx], freq); } );
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
                add_gui_job([&main_wnd, pos] {
                    main_wnd.update_grid(pos.row);
                });
            });
            mod_player_->play_pattern();
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