#ifndef SAMPEDIT_BASE_SAMPLE_H
#define SAMPEDIT_BASE_SAMPLE_H

#include <vector>
#include <cassert>
#include <algorithm>
#include <string>

class sample {
public:
    explicit sample(const std::vector<float>& data, float c5_rate, const std::string& name)
        : data_(data)
        , c5_rate_(c5_rate)
        , name_(name)
        , loop_start_(0)
        , loop_length_(0) {}

    float c5_rate() const { return c5_rate_; }

    const std::string& name() const { return name_; }

    int length() const { return static_cast<int>(data_.size()); }

    int loop_start() const { return loop_start_; }
    void loop_start(int start) { loop_start_ = start; }

    int loop_length() const { return loop_length_; }
    void loop_length(int length) { loop_length_ = length; }

    float get(int pos) const {
        return data_[pos];
    }

    float get_linear(float pos) const {
        const int ipos   = static_cast<int>(pos);
        const float frac = pos - static_cast<float>(ipos);
        return data_[ipos]*(1.0f-frac) + data_[std::min(ipos+1, length()-1)]*frac;
    }

private:
    std::vector<float> data_;
    float              c5_rate_;
    std::string        name_;
    int                loop_start_;
    int                loop_length_;
};

inline short sample_to_s16(float s) {
    s *= 32767.0f;
    if (s > 0) {
        s += 0.5f;
        if (s > 32767.0f) s = 32767.0f;
    } else {
        s -= 0.5f;
        if (s < -32768.0f) s = -32768.0f;
    }
    return static_cast<short>(s);
}

struct sample_range {
    int x0, x1;

    sample_range() : x0(0), x1(0) {}
    explicit sample_range(int start, int end) : x0(start), x1(end) {}

    int size() const { return x1 - x0; }
    bool valid() const { return size() > 0; }

    int clamp(int x) const {
        assert(valid());
        return std::max(x0, std::min(x, x1));
    }
};

#endif