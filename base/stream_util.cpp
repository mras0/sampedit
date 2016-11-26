#include "stream_util.h"

std::string read_string(std::istream& in, int size)
{
    std::string str(size, '\0');
    in.read(&str[0], size);
    for (auto& c: str) {
        if (c < 32 || c >= 128) {
            c = ' ';
        }
    }
    return str;
}