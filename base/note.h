#ifndef SAMPEDIT_BASE_NOTE_H
#define SAMPEDIT_BASE_NOTE_H

#include <string>

// https://en.wikipedia.org/wiki/Piano_key_frequencies
// A-4 (440 Hz) is the 49th key on an idealized keyboard
constexpr int notes_per_octave = 12;

enum class piano_key : unsigned char {
    C_0 = 0,
    A_4 = 69,
    C_5 = 60,
    C_9 = 108,
    OFF = 255,
};

constexpr piano_key operator+(piano_key lhs, int rhs) {
    return static_cast<piano_key>(static_cast<int>(lhs) + rhs);
}

constexpr piano_key operator+(int lhs, piano_key rhs) {
    return rhs + lhs;
}

constexpr piano_key operator-(piano_key lhs, int rhs) {
    return static_cast<piano_key>(static_cast<int>(lhs) - rhs);
}

constexpr piano_key operator-(int lhs, piano_key rhs) {
    return rhs - lhs;
}

float note_difference_to_scale(float note_diff);

float piano_key_to_freq(piano_key n, piano_key base_key = piano_key::A_4, float base_freq = 440.0f);

std::string piano_key_to_string(piano_key n);

piano_key key_to_note(int vk);

#endif
