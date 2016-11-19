#include "module.h"
#include <base/note.h>
#include <fstream>
#include <stdint.h>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <assert.h>

const module_note* module::at(int ord, int row) const
{
    assert(ord < order.size());
    assert(ord < sizeof(order));
    assert(order[ord] < patterns.size());
    assert(row < 64);

    return &patterns[order[ord]][row * num_channels];
}

std::vector<float> convert_sample_data(const std::vector<signed char>& d) {
    std::vector<float> data(d.size());
    for (size_t i = 0, len = d.size(); i < len; ++i) {
        data[i] = d[i]/128.0f;
    }
    return data;
}

std::string read_string(std::istream& in, int size)
{
    std::string str(size, '\0');
    in.read(&str[0], size);
    for (auto& c: str) {
        if (!isprint(c)) {
            c = ' ';
        }
    }
    return str;
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

uint8_t read_le_u8(std::istream& in)
{
    return static_cast<uint8_t>(in.get());
}

uint16_t read_le_u16(std::istream& in)
{
    const uint8_t lo = read_le_u8(in);
    const uint8_t hi = read_le_u8(in);
    return (static_cast<uint16_t>(hi)<<8) + lo;
}

uint32_t read_le_u32(std::istream& in)
{
    const uint16_t lo = read_le_u16(in);
    const uint16_t hi = read_le_u16(in);
    return (static_cast<uint32_t>(hi)<<16) + lo;
}

class stream_pos_saver {
public:
    explicit stream_pos_saver(std::istream& in) : in_(in), pos_(in_.tellg()) {
    }
    ~stream_pos_saver() {
        in_.seekg(pos_);
    }
    stream_pos_saver(const stream_pos_saver&) = delete;
    stream_pos_saver& operator=(const stream_pos_saver&) = delete;

private:
    std::istream&       in_;
    std::ios::streampos pos_;
};

bool is_s3m(std::istream& in)
{
    stream_pos_saver sps{in};
    char buf[4];
    return in.seekg(44) && in.read(buf, sizeof(buf)) && buf[0] == 'S' && buf[1] == 'C' && buf[2] == 'R' && buf[3] == 'M';
}

void load_s3m(std::istream& in, const char* filename, module& mod)
{
    assert(is_s3m(in));
    mod.name = read_string(in, 28);
    if (in.get() != 0x1a /* EOF marker */
        || in.get() != 0x10 /* Filetype (16 = ST3)*/) {
        throw std::runtime_error("Invalid format for S3M " + std::string(filename));
    }
    read_le_u16(in); // Expansion bytes
    const int song_length = read_le_u16(in);
    const int num_instruments = read_le_u16(in); 
    wprintf(L"Song name: %S len=%d num_instruments=%d\n", mod.name.c_str(), song_length, num_instruments);

    throw std::runtime_error("Could load S3M " + std::string(filename));
}

int mod_channels_from_id(char buf[4])
{
    if (buf[0] == 'M' && buf[1] == '.' && buf[2] == 'K' && buf[3] == '.') {
        return 4;
    }
    if (isdigit(buf[0]) && isdigit(buf[1]) && buf[2] == 'C' && buf[3] == 'H') {
        return (buf[0]-'0') * 10 + (buf[1] - '0');
    }
    return 0;
}

bool is_mod(std::istream& in)
{
    stream_pos_saver sps{in};
    char buf[4];
    return in.seekg(1080) && in.read(buf, sizeof(buf)) && mod_channels_from_id(buf) > 0;
}

void load_mod(std::istream& in, const char* filename, module& mod)
{
    assert(is_mod(in));

    mod.name = read_string(in, 20);

    struct mod_sample {
        std::string              name;
        int                      length;
        int                      finetune;
        int                      volume;
        int                      loop_start;
        int                      loop_length;
        std::vector<signed char> data;
    };

    constexpr int num_instruments = 31;
    mod_sample samples[num_instruments];
    for (auto& s : samples) {
        s.name        = read_string(in, 22);
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
    const int num_order = read_be_u8(in);
    const int song_end  = read_be_u8(in);
    uint8_t order[128];
    in.read(reinterpret_cast<char*>(order), sizeof(order));
    char format[4];
    in.read(format, sizeof(format));
    mod.num_channels = mod_channels_from_id(format);
    if (num_order == 0 || num_order > sizeof(order) || mod.num_channels <= 0) {
        throw std::runtime_error("Not a supported module format " + std::string(filename));
    }    
    mod.order = std::vector<uint8_t>(order, order + num_order);

    const int num_patterns = *std::max_element(mod.order.begin(), mod.order.end()) + 1;

    // 4 bytes for each channel for each of the 64 rows in each pattern
    for (int pat = 0; pat < num_patterns; ++pat) {
        std::vector<module_note> this_pattern;
        for (int row = 0; row < 64; ++row) {
            for (int ch = 0; ch < mod.num_channels; ++ch) {
                uint8_t b[4];
                in.read(reinterpret_cast<char*>(b), sizeof(b));

                module_note n;
                n.sample = (b[0]&0xf0) | (b[2]>>4);
                n.period = ((b[0]&0x0f) << 8) | b[1];
                n.effect = ((b[2] & 0x0f) << 8) | b[3];
                this_pattern.push_back(n);
            }
        }
        mod.patterns.emplace_back(std::move(this_pattern));
    }

    for (int i = 0; i < num_instruments; ++i) {
        const auto& s = samples[i];
        std::vector<signed char> data;
        if (s.length) {
            data.resize(s.length);
            in.read(reinterpret_cast<char*>(&data[0]), s.length);
        }

        mod.samples.emplace_back(convert_sample_data(data), 8363.0f * note_difference_to_scale(s.finetune/8.0f), s.name);
        if (s.loop_length > 2) {
            mod.samples.back().loop(s.loop_start, s.loop_length);
        }

        mod.instruments.push_back(module_instrument{s.volume});

        assert(in);
    }

    const auto p = in.tellg();
    in.seekg(0, std::ios::end);
    if (!in || in.tellg() != p) {
        throw std::runtime_error("Couldn't read module " + std::string(filename));
    }
}

module load_module(const char* filename)
{
    std::ifstream in(filename, std::ifstream::binary);
    if (!in || !in.is_open()) {
        throw std::runtime_error("Could not open " + std::string(filename));
    }

    module mod;
    if (is_s3m(in)) {
        load_s3m(in, filename, mod);
    } else if (is_mod(in)) {
        load_mod(in, filename, mod);
    } else {
        throw std::runtime_error("Unsupported format " + std::string(filename));
    }
    return mod;
}