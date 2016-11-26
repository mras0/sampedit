#ifndef SAMPEDIT_BASE_STREAM_UTIL_H
#define SAMPEDIT_BASE_STREAM_UTIL_H

#include <istream>
#include <string>
#include <cassert>

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

std::string read_string(std::istream& in, int size);

inline uint8_t read_be_u8(std::istream& in)
{
    return static_cast<uint8_t>(in.get());
}

inline uint16_t read_be_u16(std::istream& in)
{
    const uint8_t hi = read_be_u8(in);
    const uint8_t lo = read_be_u8(in);
    return (static_cast<uint16_t>(hi)<<8) + lo;
}

inline uint8_t read_le_u8(std::istream& in)
{
    return static_cast<uint8_t>(in.get());
}

inline uint16_t read_le_u16(std::istream& in)
{
    const uint8_t lo = read_le_u8(in);
    const uint8_t hi = read_le_u8(in);
    return (static_cast<uint16_t>(hi)<<8) + lo;
}

inline uint32_t read_le_u32(std::istream& in)
{
    const uint16_t lo = read_le_u16(in);
    const uint16_t hi = read_le_u16(in);
    return (static_cast<uint32_t>(hi)<<16) + lo;
}

#endif