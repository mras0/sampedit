#ifndef SAMPEDIT_BASE_SAMPLE_VOICE_H
#define SAMPEDIT_BASE_SAMPLE_VOICE_H

#include <base/voice.h>
#include <base/sample.h>
#include <memory>

class sample_voice : public voice {
public:
    explicit sample_voice(int sample_rate);
    sample_voice(sample_voice&&);
    ~sample_voice();

    void key_off();

    void play(const ::sample& s, int pos);
    void freq(float f);
    void volume(float volume);
    void pan(float pan);

    void paused(bool pause);

private:
    class impl;
    std::unique_ptr<impl> impl_;

    virtual void do_mix(float* stero_buffer, int num_stereo_samples) override;
};

#endif