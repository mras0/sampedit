#include "module.h"
#include "xm.h"
#include <base/stream_util.h>
#include <base/note.h>
#include <fstream>
#include <stdint.h>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <assert.h>

constexpr uint8_t default_pan_value = 0x30;
constexpr float s3m_clock_rate      = 14317056.0f;

constexpr float amiga_period_to_freq(int period) {
    return amiga_clock_rate / (period * 2);
}

constexpr int freq_to_amiga_period(float freq) {
    return static_cast<int>(0.5 + amiga_clock_rate / (2 * freq));
}

const module_note* module::at(int ord, int row) const
{
    assert(ord < order.size());
    assert(order[ord] < patterns.size());
    assert(row < 64);
    const auto& pattern = patterns[order[ord]];
    assert((row+1) * num_channels - 1 < pattern.size());
    return &pattern[row * num_channels];
}

int module::note_to_period(piano_key note) const
{
    const int amiga_period = freq_to_amiga_period(piano_key_to_freq(note, piano_key::C_5, amiga_c5_rate));
    if (type == module_type::mod) {
        return amiga_period;
    } else {
        assert(type == module_type::s3m);
        return 4 * amiga_period;
    }
}

int module::freq_to_period(float freq) const {
    if (type == module_type::mod) {
        return freq_to_amiga_period(freq);
    } else {
        assert(type == module_type::s3m);
        return static_cast<int>(0.5+s3m_clock_rate / freq);
    }
}

float module::period_to_freq(int period) const {
    if (type == module_type::mod) {
        return amiga_period_to_freq(period);
    } else {
        assert(type == module_type::s3m);
        return s3m_clock_rate / period;
    }
}

int module::channel_default_pan(int channel) const
{
    assert(channel >= 0 && channel < num_channels);
    if (type == module_type::mod) {
        const bool left = channel % 4 == 0 || channel % 4 == 3;
        return left ? default_pan_value : 0xFF - default_pan_value;
    } else {
        assert(type == module_type::s3m);
        return s3m.channel_panning[channel];
    }
}

std::vector<float> convert_sample_data(const std::vector<signed char>& d) {
    std::vector<float> data(d.size());
    for (size_t i = 0, len = d.size(); i < len; ++i) {
        data[i] = d[i]/128.0f;
    }
    return data;
}

std::vector<float> convert_sample_data(const std::vector<unsigned char>& d) {
    std::vector<float> data(d.size());
    for (size_t i = 0, len = d.size(); i < len; ++i) {
        data[i] = (d[i]/255.0f)*2-1;
    }
    return data;
}

bool is_s3m(std::istream& in)
{
    stream_pos_saver sps{in};
    char buf[4];
    return in.seekg(44) && in.read(buf, sizeof(buf)) && buf[0] == 'S' && buf[1] == 'C' && buf[2] == 'R' && buf[3] == 'M';
}

void s3m_seek(std::istream& in, int para_pointer)
{
    in.seekg(para_pointer * 16);
    assert(in);
}

void skip(std::istream& in, int count) {
    assert(count > 0);
    in.seekg(count, std::ios_base::cur);
    assert(in);
}

std::vector<uint16_t> read_le_u16(std::istream& in, int count) {
    assert(count > 0);
    std::vector<uint16_t> res(count);
    for (auto& x : res) {
        x = read_le_u16(in);
    }
    return res;
}

constexpr uint32_t make_sig(char a, char b, char c, char d)
{
    return a|(b<<8)|(c<<16)|(d<<24);
}

constexpr uint32_t make_sig(const char (&arr)[5]) {
    return make_sig(arr[0], arr[1], arr[2], arr[3]);
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
    const int song_length     = read_le_u16(in);
    const int num_instruments = read_le_u16(in);
    const int num_patterns    = read_le_u16(in);
    const int flags           = read_le_u16(in);
    const int tracker_version = read_le_u16(in);
    const int sample_type     = read_le_u16(in); // 1 = signed, 2 = unsigned
    const auto signature      = read_le_u32(in);
    const int global_volume   = read_le_u8(in);
    mod.initial_speed         = read_be_u8(in);
    mod.initial_tempo         = read_le_u8(in); // BPM
    const int master_volume   = read_le_u8(in); // 7-bit volume MSB set = stereo
    const int ultraclick_rem  = read_le_u8(in);
    const int default_pan     = read_le_u8(in); // 252 = default
    skip(in, 10); // Skip expansion (8 bytes) and special (2 bytes)
    assert((int)in.tellg() == 0x40);
    wprintf(L"S3M tracker version: %4.4X\n", tracker_version);
    assert(tracker_version == 0x1300 || tracker_version == 0x1301 || tracker_version == 0x1310 || tracker_version == 0x1320 || tracker_version == 0x3212); // 0x1301=ST3.01, 0x1310=ST3.10, 0x1320=ST3.20
    if (tracker_version == 0x1300) {
        wprintf(L"Ignoring compatiblity (vol slides process on tick 0 etc..) for tracker_version=%X\n", tracker_version);
    }
    assert(sample_type == 2);
    assert(signature == make_sig("SCRM"));

    // Channel settings
    constexpr int max_channels = 32;
    int num_channels = 0;
    int channel_remap[max_channels];
    int channel_pan[max_channels];
    for (int i = 0; i < max_channels; ++i) {
        const auto setting = read_le_u8(in);
        if (setting < 16) {
            channel_remap[i] = num_channels++;
            channel_pan[channel_remap[i]] = setting < 8 ? default_pan_value : 0xFF - default_pan_value;
        } else {
            channel_remap[i] = -1;
            channel_pan[i] = -1;
        }
    }
    assert(num_channels > 0);
    mod.num_channels = num_channels;

    for (int i = 0; i < song_length; ++i) {
        const auto ord = read_le_u8(in);
        if (ord < 254) { // 254 = marker pattern, 255 = end of song
            mod.order.push_back(ord);
            assert(ord < num_patterns);
        }
    }
    assert(num_patterns > 0);
    const auto instrument_pointers = read_le_u16(in, num_instruments);
    const auto pattern_pointers = read_le_u16(in, num_patterns);
    
    wprintf(L"Song name: '%S', %d channel(s), len %d, %d instrument(s), %d pattern(s)\n", mod.name.c_str(), num_channels, song_length, num_instruments, num_patterns);

    if (default_pan == 252) {
        for (int i = 0; i < max_channels; ++i) {
            const uint8_t val = read_le_u8(in) & 0xf;
            const int chidx = channel_remap[i];
            if (chidx >= 0) {
                channel_pan[chidx] = val << 4;
            }
        }
    }

    if (!(master_volume & 0x80)) {
        wprintf(L"Making song mono!\n");
        for (auto& p : channel_pan) p = 0x80;
    }

    for (int i = 0; i < num_channels; ++i) {
        assert(channel_pan[i] >= 0 && channel_pan[i] <= 0xFF);
        mod.s3m.channel_panning.push_back(static_cast<uint8_t>(channel_pan[i]));
    }


    for (int i = 0; i < num_instruments; ++i) {
        s3m_seek(in, instrument_pointers[i]);
        const uint8_t type = read_le_u8(in);
        if (type == 0) {
            mod.instruments.push_back(module_instrument{0, sample{std::vector<float>{}, 1.0f, ""}});
            continue;
        }
        assert(type == 1); // 1 = instrument
        char dos_filename[12];
        in.read(dos_filename, sizeof(dos_filename));
        // Read oddball 24-bit memseg value
        const uint32_t upper        = read_le_u8(in);
        const uint32_t lower        = read_le_u16(in);
        const uint32_t memseg       = (upper << 16) | lower;
        const uint32_t length       = read_le_u32(in);
        const uint32_t loop_start   = read_le_u32(in);
        const uint32_t loop_end     = read_le_u32(in);
        const uint8_t  volume       = read_le_u8(in);
                                      read_le_u8(in); // unused
        const uint8_t  packing      = read_le_u8(in);
        const uint8_t  sample_flags = read_le_u8(in); // 1 = loop
        const uint32_t c2spd        = read_le_u32(in);
        skip(in, 12);
        const auto name            = read_string(in, 28);
        const uint32_t samplesig   = read_le_u32(in);
        wprintf(L"%2d: %-28.28S Len=%6d Loop=(%6d, %6d) c2speed=%d\n", i, name.c_str(), length, loop_start, loop_end, c2spd);
        assert(packing == 0);
        assert(length < 0x10000);
        assert(loop_start < 0x10000);
        assert(loop_end   < 0x10000);
        assert(!(sample_flags & ~1));
        assert(samplesig == make_sig("SCRS"));
        assert(in && (int)in.tellg() == instrument_pointers[i]*16 + 0x50);

        std::vector<unsigned char> data(length);
        if (length) {
            s3m_seek(in, memseg);
            in.read(reinterpret_cast<char*>(&data[0]), length);
        }

        mod.instruments.push_back(module_instrument{volume, sample{convert_sample_data(data), static_cast<float>(c2spd), name}});
        if (sample_flags & 1) {
            assert(loop_start <= loop_end);
            mod.instruments.back().samp.loop(loop_start, loop_end - loop_start);
        }
    }

    constexpr int rows_per_pattern = 64;

    for (int i = 0; i < num_patterns; ++i) {
        if (!pattern_pointers[i]) {
            // Push an empty pattern if there is no pattern data
            mod.patterns.push_back(std::vector<module_note>());
            continue;
        }

        s3m_seek(in, pattern_pointers[i]);
        const uint16_t packed_length = read_le_u16(in);
        module_note row_data[32];
        auto clear_row_data = [&row_data] { for (auto& rd : row_data) rd = module_note{}; };
        clear_row_data();
        std::vector<module_note> this_pattern;

        for (int row = 0; row < rows_per_pattern;) {
            const uint8_t b = read_le_u8(in);
            if (!b) {
#if 0
                wprintf(L"%2.2d ", row);
                for (int ch = 0; ch < num_channels; ++ch) {
                    auto& rd = row_data[ch];
                    if (rd.note != piano_key::NONE) {
                        wprintf(L"%2.2X %2.2X ", rd.note, rd.instrument);
                    } else {
                        wprintf(L".. .. ");
                    }
                    if (rd.volume != no_volume_byte) {
                        wprintf(L"v%2.2d ", rd.volume);
                        assert(rd.volume);
                    } else {
                        wprintf(L"... ");
                    }
                    if (rd.effect) {
                        const int effect = rd.effect >> 8;
                        const int effect_param = rd.effect & 0xff;
                        wprintf(L"%c%2.2X  ", effect + 'A' - 1, effect_param);
                    } else {
                        wprintf(L"...  ");
                    }
                }
                wprintf(L"\n");
#endif

                this_pattern.insert(this_pattern.end(), row_data, row_data + num_channels);
                row++;
                clear_row_data();
            }
            const int channel = channel_remap[b & 0x1f];
            module_note ignored;
            auto& rd = channel >= 0 && channel < num_channels ? row_data[channel] : ignored;
            if (b & 0x20) {
                const uint8_t note = read_le_u8(in);
                rd.note       = note == 255 ? piano_key::NONE : note == 254 ? piano_key::OFF : piano_key::C_0 + 12 * (1 + note / 16) + note % 16;
                rd.instrument = read_le_u8(in);
                assert(rd.instrument <= num_instruments);
            }
            if (b & 0x40) {
                rd.volume = read_le_u8(in);
                assert(rd.volume != no_volume_byte);
            }
            if (b & 0x80) {
                const uint8_t effect       = read_le_u8(in);
                const uint8_t effect_param = read_le_u8(in);
                if (effect) {
                    assert(effect <= 26);
                    rd.effect = (effect << 8) | effect_param;
                } else {
                    // 2nd_pm.s3m has 0 effect in order 22 (pattern 13) row 19
                    rd.effect = 0;
                }
            }
        }

        assert(in && (int)in.tellg() == pattern_pointers[i]*16 + packed_length);

        mod.patterns.emplace_back(std::move(this_pattern));
    }
}

piano_key period_to_piano_key(int period) {
    if (period == 0) {
        return piano_key::NONE;
    }

    /*
    Finetune 0
    C    C#   D    D#   E    F    F#   G    G#   A    A#   B
    Octave 0:1712,1616,1525,1440,1357,1281,1209,1141,1077,1017, 961, 907
    Octave 1: 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453
    Octave 2: 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226
    Octave 3: 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
    Octave 4: 107, 101,  95,  90,  85,  80,  76,  71,  67,  64,  60,  57
    */

    constexpr int note_periods[12 * 5 + 1] = {
        1712,1616,1525,1440,1357,1281,1209,1141,1077,1017, 961, 907,
        856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
        428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
        214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
        107, 101,  95,  90,  85,  80,  76,  71,  67,  64,  60,  57,

        // DOPE.MOD has a note played at period 56
        -1
    };
    const int count = sizeof(note_periods) / sizeof(*note_periods);

    for (int i = 0; i < count - 1; ++i) {
        if (period == note_periods[i] || (i+1 < count && period > note_periods[i+1])) {
            if (period != note_periods[i]) {
                // DOPE.MOD uses non-protracker periods..
                //wprintf(L"Period %d isn't exact! Using %d\n", period, note_periods[i]);
            }
            return piano_key::C_0 + i + 3*12; // Octave offset: PT=0, FT2=2, MPT=3 (3 also matches a C5 speed of 8363 (period 428)
        }
    }
    assert(false);
    return piano_key::NONE;
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
                n.instrument = (b[0]&0xf0) | (b[2]>>4);
                const int period = ((b[0]&0x0f) << 8) | b[1];
                n.effect = ((b[2] & 0x0f) << 8) | b[3];
                n.volume = no_volume_byte;
                n.note   = period_to_piano_key(period);
                //if (period) {
                //    const float freq = piano_key_to_freq(n.note, piano_key::C_5, 8363);
                //    wprintf(L"period = %d, freq = %f, key = %S (%f)\n", period, amiga_period_to_freq(period), piano_key_to_string(n.note).c_str(), freq);
                //}

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

        mod.instruments.push_back(module_instrument{s.volume, sample{convert_sample_data(data), amiga_c5_rate * note_difference_to_scale(s.finetune/8.0f), s.name}});
        if (s.loop_length > 2) {
            mod.instruments.back().samp.loop(s.loop_start, s.loop_length);
        }

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
    if (is_xm(in)) {
        mod.type = module_type::xm;
        load_xm(in, filename, mod);
    } else if (is_s3m(in)) {
        mod.type = module_type::s3m;
        load_s3m(in, filename, mod);
    } else if (is_mod(in)) {
        mod.type = module_type::mod;
        load_mod(in, filename, mod);
    } else {
        throw std::runtime_error("Unsupported format " + std::string(filename));
    }
    return mod;
}