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
        std::lock_guard<std::mutex> lock{mutex_};
        while (!jobs_.empty()) {
            jobs_.front()();
            jobs_.pop();
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

    void play(const ::sample& s, float freq) {
        at_next_tick([&s, freq, this]() { channels_[0].play(s, freq); });
    }

private:
    static constexpr int sample_rate = 44100;
    static constexpr int buffer_size = 4096;

    class channel {
    public:
        explicit channel() {
        }

        void play(const ::sample& s, float freq) {
            sample_ = &s;
            pos_    = 0;
            incr_   = freq / sample_rate;
        }

        void mix(float* stero_buffer, int num_stereo_samples) {
            if (!sample_) return;
            while (pos_ < sample_->length() && num_stereo_samples) {
                const auto s = sample_->get_linear(pos_);
                stero_buffer[0] += s;
                stero_buffer[1] += s;
                pos_          += incr_;
                stero_buffer  += 2;
                num_stereo_samples--;
            }
        }

    private:
        const ::sample* sample_ = nullptr;
        float           pos_;
        float           incr_;
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


int main(int argc, char* argv[])
{
    try {
        std::vector<sample> samples;
        if (argc > 1) {
            auto mod = load_module(argv[1]);
            wprintf(L"Loaded '%S' - '%22.22S'\n", argv[1], mod.name);
            for (int i = 0; i < 31; ++i) {
                const auto& s = mod.samples[i];
                samples.emplace_back(convert_sample_data(s.data), std::string(s.name, s.name+sizeof(s.name)));
                if (s.loop_length > 2) {
                    samples.back().loop_start(s.loop_start);
                    samples.back().loop_length(s.loop_length);
                }
            }
        } else {
            for (int i = 1; i <= 4; ++i) { 
                samples.emplace_back(create_sample(44100/4, 440.0f * i), "Sample " + std::to_string(i));
            }
        }
        auto main_wnd = main_window::create();
        main_wnd.set_samples(samples);

        ShowWindow(main_wnd.hwnd(), SW_SHOW);
        UpdateWindow(main_wnd.hwnd());

        mixer m{32};

        m.play(samples[0], 2*8287.14f);

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        return static_cast<int>(msg.wParam);
    } catch (const std::runtime_error& e) {
        wprintf(L"%S\n", e.what());
    }
    return 1;
}