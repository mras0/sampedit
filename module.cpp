#include "module.h"
#include <fstream>
#include <stdint.h>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <assert.h>

const module_note* module::at(int ord, int row) const
{
    assert(ord < num_order);
    assert(ord < sizeof(order));
    assert(order[ord] < num_patterns());
    assert(row < 64);

    return &pattern_data[(order[ord] * rows_per_pattern + row) * num_channels];
}


uint8_t read_be_u8(std::istream& in)
{
    return static_cast<uint8_t>(in.get());
}

uint16_t read_be_u16(std::istream& in)
{
    const uint8_t hi = read_be_u8(in);
    const uint8_t lo = read_be_u8(in);
    return (static_cast<uint16_t>(hi)<<8) + lo;
}

module load_module(const char* filename)
{
    std::ifstream in(filename, std::ifstream::binary);
    if (!in || !in.is_open()) {
        throw std::runtime_error("Could not open " + std::string(filename));
    }

    module mod;

    in.read(mod.name, sizeof(mod.name));
    //printf("Songname: %-*s\n", (int)sizeof(mod.name), mod.name);
    for (auto& s : mod.samples) {
        in.read(s.name, sizeof(s.name));
        s.length      = read_be_u16(in) * 2;
        s.finetune    = read_be_u8(in);
        if (s.finetune >= 8) s.finetune = s.finetune-16;
        s.volume      = read_be_u8(in);
        s.loop_start  = read_be_u16(in) * 2;
        s.loop_length = read_be_u16(in) * 2;
        if (s.loop_length <= 2) s.loop_length = 0;

        //printf("%-*s len=%6d finetune=%2d volume=%2d loop=%d, %d\n", (int)sizeof(s.name), s.name, s.length, s.finetune, s.volume, s.loop_start, s.loop_length);
    }
    assert((int)in.tellg() == 950);
    mod.num_order = read_be_u8(in);
    mod.song_end    = read_be_u8(in);
    in.read(reinterpret_cast<char*>(mod.order), sizeof(mod.order));
    in.read(mod.format, sizeof(mod.format));
    if (mod.num_order == 0 || mod.format[0] != 'M' || mod.format[1] != '.' || mod.format[2] != 'K' || mod.format[3] != '.') {
        throw std::runtime_error("Not a supported module format " + std::string(filename));
    }
    mod.num_channels = 4;

    const int num_patterns = *std::max_element(mod.order, mod.order + mod.num_order) + 1;

    printf("Num patterns: %d\n", num_patterns);

    // 4 bytes for each channel for each of the 64 rows in each pattern
    int total_rows = mod.num_channels * 64 * num_patterns;
    std::vector<uint8_t> pd(4 * total_rows);
    in.read((char*)pd.data(), pd.size());
    mod.pattern_data.resize(total_rows);

    for (int i = 0; i < total_rows; ++i) {
        auto& n = mod.pattern_data[i];
        const auto* b = &pd[i*4];

        n.sample = (b[0]&0xf0) | (b[2]>>4);
        n.period = ((b[0]&0x0f) << 8) | b[1];
        n.effect = ((b[2] & 0x0f) << 8) | b[3];
    }

    for (auto& s : mod.samples) {
        if (!s.length) continue;
        s.data.resize(s.length);
        in.read(reinterpret_cast<char*>(&s.data[0]), s.length);
        assert(in);
    }

    const auto p = in.tellg();
    in.seekg(0, std::ios::end);
    if (!in || in.tellg() != p) {
        throw std::runtime_error("Couldn't read " + std::string(filename));
    }

    return mod;
}
