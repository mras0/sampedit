#ifndef SAMPEDIT_MODULE_H
#define SAMPEDIT_MODULE_H

#include <stdint.h>
#include <vector>
#include <string>
#include <base/sample.h>
#include <base/note.h>

constexpr float amiga_clock_rate = 7159090.5f;
constexpr float amiga_c5_rate    = amiga_clock_rate / (2 * 428);

constexpr float amiga_period_to_freq(int period) {
    return amiga_clock_rate / (period * 2);
}

constexpr float s3m_period_to_freq(int period) {
    return 14317056.0f / period;
}

constexpr int freq_to_amiga_period(float freq) {
    return static_cast<int>(amiga_clock_rate / (2 * freq));
}

struct module_instrument {
    int volume;
};

constexpr uint8_t no_volume_byte = 0xFF;

struct module_note {
    piano_key  note        = piano_key::NONE;
    uint8_t    instrument  = 0;
    uint8_t    volume      = no_volume_byte;
    uint16_t   effect      = 0;
};

enum class module_type { mod, s3m };
constexpr const char* const module_type_name[] = { "MOD", "S3M" };

struct module_position {
    int order, pattern, row;
};

struct module {
    module_type                            type;
    int                                    initial_speed = 6;
    int                                    initial_tempo = 125;
    std::string                            name;
    std::vector<module_instrument>         instruments;
    std::vector<uint8_t>                   order;
    int                                    num_channels;
    std::vector<std::vector<module_note>>  patterns;
    std::vector<sample>                    samples;

    struct {
        std::vector<uint8_t> channel_panning;
    } s3m;

    static constexpr int rows_per_pattern = 64;

    int note_to_period(piano_key note) const;
    const module_note* at(int order, int row) const;
    int channel_default_pan(int channel) const;
};

module load_module(const char* filename);

#endif
