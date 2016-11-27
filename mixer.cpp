#include "mixer.h"
#include <win32/wavedev.h>
#include <base/sample.h>

#include <vector>
#include <cassert>
#include <algorithm>

class mixer::impl {
public:
    explicit impl()
        : wavedev_(sample_rate_, 4096, [this](short* s, size_t num_stereo_samples) { render(s, static_cast<int>(num_stereo_samples)); }) {
    }

    int sample_rate() const {
        return sample_rate_;
    }

    job_queue& tick_queue() {
        return at_next_tick_;
    }

    void add_voice(voice& v) {
        at_next_tick_.assert_in_queue_thread();
        voices_.push_back(&v);
    }

    void remove_voice(voice& v) {
        at_next_tick_.assert_in_queue_thread();
        auto it = std::find(voices_.begin(), voices_.end(), &v);
        assert(it != voices_.end());
        voices_.erase(it);
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

mixer::mixer() : impl_(std::make_unique<impl>()) {
}

mixer::~mixer() = default;

job_queue& mixer::tick_queue() {
    return impl_->tick_queue();
}

void mixer::add_voice(voice& v) {
    impl_->add_voice(v);
}

void mixer::remove_voice(voice& v) {
    impl_->remove_voice(v);
}

void mixer::ticks_per_second(int tps) {
    impl_->ticks_per_second(tps);
}

void mixer::global_volume(float vol) {
    impl_->global_volume(vol);
}