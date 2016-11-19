#ifndef SAMPEDIT_MODULE_H
#define SAMPEDIT_MODULE_H

#include <stdint.h>
#include <vector>
#include <string>
#include <base/sample.h>
#include <base/note.h>

struct module_instrument {
    int volume;
};

struct module_note {
    piano_key  note        = piano_key::NONE;
    uint16_t   period      = 0;
    uint8_t    instrument  = 0;
    uint8_t    volume      = 0;
    uint16_t   effect      = 0;
};

enum class module_type { mod, s3m };

struct module {
    module_type                            type;
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
