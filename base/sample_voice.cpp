#include "sample_voice.h"

class sample_voice::impl {
public:
    explicit impl(int sample_rate) : sample_rate_(sample_rate) {
        pan(0.5f);
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
        assert(f > 0);
        incr_ = f / sample_rate_;
    }

    void volume(float volume) {
        volume_ = volume;
    }

    void pan(float pan) {
        assert(pan >= 0);
        assert(pan <= 1);
        constexpr float pi = 3.14159265359f;
        const auto ang=pan*pi*0.5f;
        panl_ = cos(pan);
        panr_ = sin(pan);
    }

    void paused(bool pause) {
        paused_ = pause;
    }

    void mix(float* stero_buffer, int num_stereo_samples) {
        if (paused_ || state_ == state::not_playing) return;
        assert(sample_);

        while (num_stereo_samples) {
            const int end = current_end();
            assert(state_ == state::looping_backward && pos_ >= end || pos_ <= end);
            const float real_incr = state_ == state::looping_backward ? -incr_ : incr_;
            const int samples_till_end = static_cast<int>((end - pos_) / real_incr);
            assert(samples_till_end >= 0);
            if (!samples_till_end) {
                if (sample_->loop_type() != loop_type::none) {
                    assert(state_ == state::playing_first || state_ == state::looping_forward || state_ == state::looping_backward);
                    if (sample_->loop_type() == loop_type::pingpong) {
                        if (state_ == state::looping_backward) {
                            assert(pos_-sample_->loop_start() < incr_);
                            state_ = state::looping_forward;
                        } else {
                            assert(end-pos_ < incr_);
                            state_ = state::looping_backward;
                        }
                    } else {
                        pos_   = static_cast<float>(sample_->loop_start());
                        state_ = state::looping_forward;
                    }
                    continue;
                } else {
                    //wprintf(L"End of instrument at %f\n", pos_);
                    state_ = state::not_playing;
                    break;
                }
            }

            const int now = std::min(samples_till_end, num_stereo_samples);
            assert(now > 0);
            do_mix_sample(stero_buffer, now, *sample_, pos_, real_incr, volume_ * panl_, volume_ * panr_);
            num_stereo_samples -= now;
            stero_buffer       += 2* now;
            pos_               += real_incr * now;
        }
    }

private:
    const int       sample_rate_;
    const ::sample* sample_ = nullptr;
    float           pos_;
    float           incr_;
    float           volume_;
    float           panl_;
    float           panr_;
    bool            paused_ = false;
    enum class state {
        not_playing,
        playing_first,
        looping_forward,
        looping_backward,
    } state_ = state::not_playing;

    int current_end() const {
        if (state_ == state::looping_forward) return sample_->loop_start() + sample_->loop_length();
        if (state_ == state::looping_backward) return sample_->loop_start();
        return sample_->length();
    }

    static void do_mix_sample(float* stero_buffer, int num_stereo_samples, const sample& samp, float pos, float incr, float lvol, float rvol) {
        for (int i = 0; i < num_stereo_samples; ++i) {
            const auto s = samp.get_linear(pos + i * incr);
            stero_buffer[i*2+0] += s * lvol;
            stero_buffer[i*2+1] += s * rvol;
        }
    }
};

sample_voice::sample_voice(int sample_rate) : impl_(std::make_unique<impl>(sample_rate)) {
}

sample_voice::sample_voice(sample_voice&&) = default;

sample_voice::~sample_voice() = default;

void sample_voice::key_off() {
    impl_->key_off();
}

void sample_voice::play(const ::sample& s, int pos) {
    impl_->play(s, pos);
}

void sample_voice::freq(float f) {
    impl_->freq(f);
}

void sample_voice::volume(float volume) {
    impl_->volume(volume);
}

void sample_voice::pan(float volume) {
    impl_->pan(volume);
}

void sample_voice::paused(bool pause) {
    impl_->paused(pause);
}

void sample_voice::do_mix(float* stero_buffer, int num_stereo_samples) {
    impl_->mix(stero_buffer, num_stereo_samples);
}