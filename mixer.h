#ifndef SAMPEDIT_MIXER_H
#define SAMPEDIT_MIXER_H

#include <memory>

#include <base/job_queue.h>
#include <base/voice.h>

class mixer {
public:
    explicit mixer();
    ~mixer();

    int sample_rate() const {
        return sample_rate_;
    }

    job_queue& tick_queue();

    void add_voice(voice& v);
    void remove_voice(voice& v);
    void ticks_per_second(int tps);
    void global_volume(float vol);

private:
    static constexpr int sample_rate_ = 44100;

    class impl;
    std::unique_ptr<impl> impl_;
};

#endif