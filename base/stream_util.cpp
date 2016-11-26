#include "stream_util.h"

void sanitize(std::string& str)
{
    for (auto& c: str) {
        if (c < 32 || c >= 128) {
            c = ' ';
        }
    }
}

std::string read_string(std::istream& in, int size)
{
    std::string str(size, '\0');
    in.read(&str[0], size);
    sanitize(str);
    return str;
}