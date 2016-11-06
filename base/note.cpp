#include "note.h"
#include <assert.h>
#include <math.h>

float note_difference_to_scale(float note_diff)
{
    // To go up a semi-tone multiply the frequency by pow(2,1./12) ~1.06
    return pow(2.0f, note_diff / notes_per_octave);
}

float piano_key_to_freq(piano_key n, piano_key base_key /*= piano_key::A_4*/, float base_freq/* = 440.0f*/)
{
    assert(n != piano_key::OFF);
    return base_freq * note_difference_to_scale(static_cast<float>(static_cast<int>(n) - static_cast<int>(base_key)));
}

std::string piano_key_to_string(piano_key n)
{
    assert(n != piano_key::OFF);
    const int val    = static_cast<int>(n);
    const int octave = val/12;
    const int note   = val%12;
    static const char* const note_names[12] ={"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    return note_names[note] + std::to_string(octave);
}

piano_key key_to_note(int vk) {
    constexpr piano_key offset = piano_key::C_5;
    switch (vk) {
    case 'Q': return offset + 12 + 0;  // C
    case '2': return offset + 12 + 1;  // C#
    case 'W': return offset + 12 + 2;  // D
    case '3': return offset + 12 + 3;  // D#
    case 'E': return offset + 12 + 4;  // E
    case 'R': return offset + 12 + 5;  // F
    case '5': return offset + 12 + 6;  // F#
    case 'T': return offset + 12 + 7;  // G
    case '6': return offset + 12 + 8;  // G#
    case 'Y': return offset + 12 + 9;  // A
    case '7': return offset + 12 + 10; // A#
    case 'U': return offset + 12 + 11; // B

    case 'Z': return offset + 0;  // C
    case 'S': return offset + 1;  // C#
    case 'X': return offset + 2;  // D
    case 'D': return offset + 3;  // D#
    case 'C': return offset + 4;  // E
    case 'V': return offset + 5;  // F
    case 'G': return offset + 6;  // F#
    case 'B': return offset + 7;  // G
    case 'H': return offset + 8;  // G#
    case 'N': return offset + 9;  // A
    case 'J': return offset + 10; // A#
    case 'M': return offset + 11; // B
    }
    return piano_key::OFF;
}
