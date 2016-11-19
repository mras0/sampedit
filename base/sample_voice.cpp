#include "sample_voice.h"

class sample_voice::impl {
public:
    explicit impl(int sample_rate) : sample_rate_(sample_rate) {
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
        incr_ = f / sample_rate_;
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
            do_mix_sample(stero_buffer, now, *sample_, pos_, incr_, volume_, volume_);
            num_stereo_samples -= now;
            stero_buffer       += 2* now;
            pos_               += incr_ * now;
        }
    }

private:
    const int       sample_rate_;
    const ::sample* sample_ = nullptr;
    float           pos_;
    float           incr_;
    float           volume_;
    enum class state {
        not_playing,
        playing_first,
        looping,
    } state_ = state::not_playing;

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

void sample_voice::do_mix(float* stero_buffer, int num_stereo_samples) {
    impl_->mix(stero_buffer, num_stereo_samples);
}