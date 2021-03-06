#include "xm.h"
#include "module.h"
#include <base/stream_util.h>

constexpr int  xm_signature_length = 17;
constexpr char xm_signature[xm_signature_length+1] = "Extended Module: ";

template<size_t size>
void get(std::istream& in, char (&buffer)[size])
{
    in.read(&buffer[0], size);
}

template<size_t size>
void get(std::istream& in, uint8_t (&buffer)[size])
{
    in.read(reinterpret_cast<char*>(&buffer[0]), size);
}

void get(std::istream& in, uint8_t& i) {
    i = read_le_u8(in);
}

void get(std::istream& in, int8_t& i) {
    i = static_cast<int8_t>(read_le_u8(in));
}

void get(std::istream& in, uint16_t& i) {
    i = read_le_u16(in);
}

void get(std::istream& in, uint32_t& i) {
    i = read_le_u32(in);
}

struct xm_header {
    char     id[17];
    char     name[20];
    uint8_t  escape;
    char     tracker[20];
    uint16_t version;
    uint32_t header_size;
    uint16_t song_length;
    uint16_t restart_position;
    uint16_t num_channels;
    uint16_t num_patterns;
    uint16_t num_instruments;
    uint16_t flags;
    uint16_t default_tempo;
    uint16_t default_bpm;
    uint8_t  order[256];
};
constexpr uint8_t xm_header_flags_linear_frequency_mask     = 0x03;

void get(std::istream& in, xm_header& xm) {
    get(in, xm.id);
    get(in, xm.name);
    get(in, xm.escape);
    get(in, xm.tracker);
    get(in, xm.version);
    get(in, xm.header_size);
    get(in, xm.song_length);
    get(in, xm.restart_position);
    get(in, xm.num_channels);
    get(in, xm.num_patterns);
    get(in, xm.num_instruments);
    get(in, xm.flags);
    get(in, xm.default_tempo);
    get(in, xm.default_bpm);
    get(in, xm.order);
}

constexpr int xm_header_size = 276;

bool check(const xm_header& xm) {
    return strncmp(xm.id, xm_signature, xm_signature_length) == 0
        && xm.escape == '\x1a'
        && xm.version == 0x0104
        && (xm.header_size == xm_header_size || xm.header_size == 275) // 275 seen in Lamb - In your arms.xm
        && xm.song_length <= 256
        && xm.num_channels <= 32
        && xm.num_patterns <= 256
        && xm.num_instruments <= 128;
}

struct xm_pattern_header {
    uint32_t header_length;
    uint8_t  packing_type;
    uint16_t num_rows;
    uint16_t data_size;
};

void get(std::istream& in, xm_pattern_header& pat_hdr) {
    get(in, pat_hdr.header_length);
    get(in, pat_hdr.packing_type);
    get(in, pat_hdr.num_rows);
    get(in, pat_hdr.data_size);
}

bool check(const xm_pattern_header& pat_hdr) {
    return pat_hdr.header_length == 9 && pat_hdr.packing_type == 0 && pat_hdr.num_rows >= 1 && pat_hdr.num_rows <= 256;
}

struct xm_note {
    uint8_t note;
    uint8_t instrument;
    uint8_t volume;
    uint8_t effect_type;
    uint8_t effect_param;
};

void get(std::istream& in, xm_note& note) {
    const uint8_t first = read_le_u8(in);
    if (first & 0x80) {
        // Packed
        note = xm_note{};
        if (first & 0x01) get(in, note.note);
        if (first & 0x02) get(in, note.instrument);
        if (first & 0x04) get(in, note.volume);
        if (first & 0x08) get(in, note.effect_type);
        if (first & 0x10) get(in, note.effect_param);
    } else {
        note.note = first;
        get(in, note.instrument);
        get(in, note.volume);
        get(in, note.effect_type);
        get(in, note.effect_param);
    }
}

piano_key convert_xm_note_key(uint8_t n) {
    if (!n) return piano_key::NONE;
    if (n == 97) return piano_key::OFF;
    assert(n >= 1 && n < 97);
    return piano_key::C_0 + 12 * (xm_octave_offset + (n-1) / 12) + (n-1) % 12;
}

module_note convert_note(const xm_note& n) {
    module_note res;
    res.note       = convert_xm_note_key(n.note);
    res.instrument = n.instrument;
    res.volume     = static_cast<volume_command>(n.volume);
    res.effect     = (n.effect_type<<8) | n.effect_param;
    return res;
}

constexpr uint8_t null_note    = 0;
constexpr uint8_t key_off_note = 97;

struct xm_instrument_header {
    uint32_t header_length;
    char     name[22];
    uint8_t  type;
    uint16_t num_samples;

    // Only if num_samples > 0
    uint32_t sample_header_length;
    uint8_t  sample_number[96]; // For each note
    uint8_t  volume_points[48];
    uint8_t  panning_points[48];
    uint8_t  num_volume_points;
    uint8_t  num_panning_points;
    uint8_t  volume_sustain_point;
    uint8_t  volume_loop_start;
    uint8_t  volume_loop_end;
    uint8_t  panning_sustain_point;
    uint8_t  panning_loop_start;
    uint8_t  panning_loop_end;
    uint8_t  volume_type;  // bit 0: On; 1: Sustain; 2: Loop
    uint8_t  panning_type; // bit 0: On; 1: Sustain; 2: Loop
    uint8_t  vibrato_type;
    uint8_t  vibrato_sweep;
    uint8_t  vibrato_depth;
    uint8_t  vibrato_rate;
    uint16_t volume_fadeout;
    uint16_t reserved;
};

void get(std::istream& in, xm_instrument_header& ins_hdr) {
    memset(&ins_hdr, 0, sizeof(ins_hdr));
    const auto ins_start = in.tellg();
    get(in, ins_hdr.header_length);
    get(in, ins_hdr.name);
    get(in, ins_hdr.type);
    get(in, ins_hdr.num_samples);

    if (ins_hdr.num_samples > 0) {
        get(in, ins_hdr.sample_header_length);
        get(in, ins_hdr.sample_number);
        get(in, ins_hdr.volume_points);
        get(in, ins_hdr.panning_points);
        get(in, ins_hdr.num_volume_points);
        get(in, ins_hdr.num_panning_points);
        get(in, ins_hdr.volume_sustain_point);
        get(in, ins_hdr.volume_loop_start);
        get(in, ins_hdr.volume_loop_end);
        get(in, ins_hdr.panning_sustain_point);
        get(in, ins_hdr.panning_loop_start);
        get(in, ins_hdr.panning_loop_end);
        get(in, ins_hdr.volume_type);
        get(in, ins_hdr.panning_type);
        get(in, ins_hdr.vibrato_type);
        get(in, ins_hdr.vibrato_sweep);
        get(in, ins_hdr.vibrato_depth);
        get(in, ins_hdr.vibrato_rate);
        get(in, ins_hdr.volume_fadeout);
        get(in, ins_hdr.reserved);
    }
    const auto ins_hdr_end = ins_start + std::streamoff(ins_hdr.header_length);
    assert(in.tellg() <= ins_hdr_end);
    in.seekg(ins_hdr_end, std::ios::beg);
}

bool check(xm_instrument_header& ins_hdr) {
    // Instrument type is checked elsewhere
    if (ins_hdr.num_samples) {
        if (ins_hdr.header_length != 263) {
            assert(false);
            return false;
        }
        if (ins_hdr.sample_header_length != 40) {
            assert(false);
            return false;
        }
    } else {
        if (ins_hdr.header_length != 33) {
            assert(false);
            return false;
        }
        if (ins_hdr.sample_header_length) {
            assert(false);
            return false;
        }
    }
    return true;
}

struct xm_sample_header {
    uint32_t length;
    uint32_t loop_start;
    uint32_t loop_length;
    uint8_t  volume;
    int8_t   finetune;
    uint8_t  type;   // Type: Bit 0-1: 0 = No loop, 1 = Forward loop, 2 = Ping-pong loop 4: 16-bit sampledata
    uint8_t  panning;
    int8_t   relative_note;
    uint8_t  reserved;
    char     name[22];
};

void get(std::istream& in, xm_sample_header& samp_hdr)
{
    get(in, samp_hdr.length);
    get(in, samp_hdr.loop_start);
    get(in, samp_hdr.loop_length);
    get(in, samp_hdr.volume);
    get(in, samp_hdr.finetune);
    get(in, samp_hdr.type);
    get(in, samp_hdr.panning);
    get(in, samp_hdr.relative_note);
    get(in, samp_hdr.reserved);
    get(in, samp_hdr.name);
}

constexpr uint8_t xm_sample_type_loop_mask     = 0x03;
constexpr uint8_t xm_sample_loop_type_none     = 0x00;
constexpr uint8_t xm_sample_loop_type_forward  = 0x01;
constexpr uint8_t xm_sample_loop_type_pingpong = 0x02;
constexpr uint8_t xm_sample_type_16bit_mask    = 0x10;

bool is_xm(std::istream& in)
{
    stream_pos_saver sps{in};
    in.seekg(0);
    return read_string(in, xm_signature_length) == xm_signature;
}

void load_xm(std::istream& in, const char* filename, module& mod)
{
    assert(mod.type == module_type::xm);
    assert(is_xm(in));

    xm_header xm;
    get(in, xm);
    if (!check(xm)) {
        assert(false);
        throw std::runtime_error("Invalid/Unsupported XM: " + std::string(filename));
    }
    assert((int)in.tellg() == 60+xm_header_size);
    if (xm.header_size != xm_header_size) {
        in.seekg(60+xm.header_size, std::ios_base::beg);
    }

    wprintf(L"Song name:    %20.20S\n", xm.name);
    wprintf(L"Tracker:      %20.20S\n", xm.tracker);
    wprintf(L"#Channels:    %d\n", xm.num_channels);
    wprintf(L"#Patterns:    %d\n", xm.num_patterns);
    wprintf(L"#Instruments: %d\n", xm.num_instruments);

    mod.name            = std::string(xm.name, xm.name+sizeof(xm.name));
    sanitize(mod.name);
    mod.num_channels    = xm.num_channels;
    mod.initial_speed   = xm.default_tempo;
    mod.initial_tempo   = xm.default_bpm;
    mod.order           = std::vector<uint8_t>(xm.order, xm.order + xm.song_length);
    mod.xm.use_linear_frequency = (xm.flags & xm_header_flags_linear_frequency_mask) != 0;

    for (unsigned pat = 0; pat < xm.num_patterns; ++pat) {
        xm_pattern_header pat_hdr;
        get(in, pat_hdr);
        const auto pat_start = in.tellg();
        //wprintf(L"Pattern %2.2d: header_length=%d, packing=%d, rows=%d, size=%d\n", pat, pat_hdr.header_length, pat_hdr.packing_type, pat_hdr.num_rows, pat_hdr.data_size);
        if (!check(pat_hdr)) {
            assert(false);
            throw std::runtime_error("Invalid/Unsupported XM: " + std::string(filename) + " Pattern " + std::to_string(pat) + " is invalid");
        }
        std::vector<module_note> this_pattern;
        const int num_notes = pat_hdr.num_rows * xm.num_channels;
        this_pattern.reserve(num_notes);
        if (pat_hdr.data_size) {
            for (unsigned row = 0; row < pat_hdr.num_rows; ++row) {
                for (unsigned ch = 0; ch < xm.num_channels; ++ch) {
                    xm_note note;
                    get(in, note);
                    this_pattern.push_back(convert_note(note));
                }
            }
            assert(this_pattern.size() == num_notes);
        } else {
            this_pattern.resize(num_notes);
        }
        mod.patterns.push_back(std::move(this_pattern));
        assert(in.tellg() - pat_start == pat_hdr.data_size);
    }

    int instrument_type = -1;
    for (unsigned ins = 0; ins < xm.num_instruments; ++ins) {
        xm_instrument_header ins_hdr;
        get(in, ins_hdr);
        if (!ins) instrument_type = ins_hdr.type; // The instrument type should be the same for all instruments (it should really just be zero, but isn't always)
        if (!check(ins_hdr) || ins_hdr.type != instrument_type) {
            assert(false);
            throw std::runtime_error("Invalid/Unsupported XM: " + std::string(filename) + " Instrument " + std::to_string(ins) + " is invalid");
        }

        wprintf(L"%2.2d: %22.22S\n", ins, ins_hdr.name);

        if (!ins_hdr.num_samples) {
            mod.instruments.push_back(module_instrument{});
            continue;
        }

#define EXPECT(elem, val) if (ins_hdr.elem != (val)) wprintf(L"%d != %d -- ins_hdr.%S != %S\n", ins_hdr.elem, val, #elem, #val);
        //EXPECT(num_volume_points, 0);
        //EXPECT(num_panning_points, 0);
        //EXPECT(volume_sustain_point, 0);
        //EXPECT(volume_loop_start, 0);
        //EXPECT(volume_loop_end, 0);
        //EXPECT(panning_sustain_point, 0);
        //EXPECT(panning_loop_start, 0);
        //EXPECT(panning_loop_end, 0);
        EXPECT(volume_type, 0);  // bit 0: On; 1: Sustain; 2: Loop
        EXPECT(panning_type, 0); // bit 0: On; 1: Sustain; 2: Loop
        EXPECT(vibrato_type, 0);
        EXPECT(vibrato_sweep, 0);
        EXPECT(vibrato_depth, 0);
        EXPECT(vibrato_rate, 0);

        module_instrument inst{ins_hdr.volume_fadeout};
        std::vector<xm_sample_header> sample_headers;
        for (unsigned samp_num = 0; samp_num < ins_hdr.num_samples; ++samp_num) {
            xm_sample_header samp_hdr;
            get(in, samp_hdr);
            sample_headers.push_back(samp_hdr);
        }

        for (unsigned samp_num = 0; samp_num < ins_hdr.num_samples; ++samp_num) {
            auto& samp_hdr = sample_headers[samp_num];
            const int loop_type = samp_hdr.type & xm_sample_type_loop_mask;
            const bool is_16bit = (samp_hdr.type & xm_sample_type_16bit_mask) != 0;

            wprintf(L"  %2.2d: %22.22S len %6d type %02X ", samp_num, samp_hdr.name, samp_hdr.length, samp_hdr.type);
            if (loop_type) wprintf(L"Loop %6d %6d ", samp_hdr.loop_start, samp_hdr.loop_length);
            wprintf(L"\n");

            short last = 0;
            std::vector<short> sample_data(samp_hdr.length);
            const int len = is_16bit ? samp_hdr.length/2 : samp_hdr.length;
            for (int i = 0; i < len; ++i) {
                short cur = 0;
                if (is_16bit) {
                    cur = static_cast<short>(read_le_u16(in));
                } else {
                    cur = static_cast<short>(static_cast<signed char>(read_le_u8(in)) << 8);
                }
                cur += last;
                sample_data[i] = cur;
                last = cur;
            }

            std::string name = std::string(samp_hdr.name, samp_hdr.name + sizeof(samp_hdr.name));
            sanitize(name);

            module_sample samp{sample{sample_data, amiga_c5_rate * note_difference_to_scale(samp_hdr.finetune/128.0f), name}, samp_hdr.volume, samp_hdr.relative_note};
            if (loop_type) {
                samp.data().loop(samp_hdr.loop_start, samp_hdr.loop_length, loop_type == xm_sample_loop_type_forward ? ::loop_type::forward : ::loop_type::pingpong);
            }
            inst.add_sample(std::move(samp));
        }
        inst.sample_mapping(ins_hdr.sample_number);
        mod.instruments.push_back(std::move(inst));
    }

    wprintf(L"Using %S frequency table\n", mod.xm.use_linear_frequency ? "linear" : "amiga");
}