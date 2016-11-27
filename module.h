#ifndef SAMPEDIT_MODULE_H
#define SAMPEDIT_MODULE_H

#include <stdint.h>
#include <vector>
#include <string>
#include <base/sample.h>
#include <base/note.h>

// Matches XM format
enum class volume_command : uint8_t {
    none   = 0x00,
    set_00 = 0x10,
    set_40 = 0x50,
    pan_0  = 0xc0,
    pan_f  = 0xcf,
};

inline int operator-(volume_command l, volume_command r) {
    assert(l >= r);
    return static_cast<int>(l) - static_cast<int>(r);
}

inline volume_command operator+(volume_command l, int r) {
    assert(static_cast<int>(l) + r <= 255);
    return static_cast<volume_command>(static_cast<int>(l) + r);
}

constexpr float amiga_clock_rate     = 7159090.5f;
constexpr float amiga_c5_rate        = amiga_clock_rate / (2 * 428);

struct module_instrument {
    int    volume;
    sample samp;
};

struct module_note {
    piano_key         note        = piano_key::NONE;
    uint8_t           instrument  = 0;
    volume_command    volume      = volume_command::none;
    uint16_t          effect      = 0;
};

enum class module_type { mod, s3m, xm };
constexpr const char* const module_type_name[] = { "MOD", "S3M", "XM" };

struct module_position {
    int order, pattern, row;
};

struct module {
    explicit module(module_type type) : type(type) {
        switch (type) {
        case module_type::mod:
            break;
        case module_type::s3m:
            new (&s3m) s3m_s();
            break;
        case module_type::xm:
            new (&xm) xm_s();
            break;
        }
    }
    module(module&& mod)
        : type(mod.type)
        , initial_speed(mod.initial_speed)
        , initial_tempo(mod.initial_tempo)
        , name(std::move(mod.name))
        , instruments(std::move(mod.instruments))
        , order(std::move(mod.order))
        , num_channels(mod.num_channels)
        , patterns(std::move(mod.patterns)) {
        switch (type) {
        case module_type::mod:
            break;
        case module_type::s3m:
            new (&s3m) s3m_s(std::move(mod.s3m));
            break;
        case module_type::xm:
            new (&xm) xm_s(std::move(mod.xm));
            break;
        }
    }

    ~module() {
        switch (type) {
        case module_type::mod:
            break;
        case module_type::s3m:
            s3m.~s3m_s();
            break;
        case module_type::xm:
            xm.~xm_s();
            break;
        }
    }

    const module_type                      type;
    int                                    initial_speed = 6;
    int                                    initial_tempo = 125;
    std::string                            name;
    std::vector<module_instrument>         instruments;
    std::vector<uint8_t>                   order;
    int                                    num_channels;
    std::vector<std::vector<module_note>>  patterns;

    struct s3m_s {
        std::vector<uint8_t> channel_panning;
    };
    struct xm_s {
        bool                           use_linear_frequency;
    };
    union {
        s3m_s s3m;
        xm_s  xm;
    };

    static constexpr int rows_per_pattern = 64;

    int note_to_period(piano_key note) const;
    int freq_to_period(float freq) const;
    float period_to_freq(int period) const;
    const module_note* at(int order, int row) const;
    int channel_default_pan(int channel) const;
};

module load_module(const char* filename);

#endif
