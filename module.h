#ifndef SAMPEDIT_MODULE_H
#define SAMPEDIT_MODULE_H

#include <stdint.h>
#include <vector>

struct module_sample {
    char                     name[22];
    int                      length;
    int                      finetune;
    int                      volume;
    int                      loop_start;
    int                      loop_length;
    std::vector<signed char> data;
};

struct module_note {
    uint8_t  sample;
    uint16_t period;
    uint16_t effect;
};

struct module {
    char                     name[20];
    module_sample            samples[31];
    int                      num_order;
    int                      song_end;
    uint8_t                  order[128];
    char                     format[4];
    int                      num_channels;
    std::vector<module_note> pattern_data;

    static constexpr int rows_per_pattern = 64;

    int num_patterns() const { 
        return static_cast<int>(pattern_data.size() / (num_channels * rows_per_pattern));
    }
    const module_note* at(int order, int row) const;
};

module load_module(const char* filename);

#endif
