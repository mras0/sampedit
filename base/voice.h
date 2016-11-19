#ifndef SAMPEDIT_BASE_VOICE_H
#define SAMPEDIT_BASE_VOICE_H

class voice {
public:
    virtual ~voice() {};

    void mix(float* stero_buffer, int num_stereo_samples) {
        do_mix(stero_buffer, num_stereo_samples);
    }

private:
    virtual void do_mix(float* stero_buffer, int num_stereo_samples) = 0;
};

#endif