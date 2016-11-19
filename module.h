#ifndef SAMPEDIT_MODULE_H
#define SAMPEDIT_MODULE_H

#include <stdint.h>
#include <vector>
#include <string>
#include <base/sample.h>

struct module_instrument {
    int volume;
};

struct module_note {
    uint8_t  sample;
    uint16_t period;
    uint16_t effect;
};

struct module {
    std::string                            name;
    std::vector<module_instrument>         instruments;
    std::vector<uint8_t>                   order;
    int                                    num_channels;
    std::vector<std::vector<module_note>>  patterns;
    std::vector<sample>                    samples;

    static constexpr int rows_per_pattern = 64;

    const module_note* at(int order, int row) const;
};

module load_module(const char* filename);

#endif
