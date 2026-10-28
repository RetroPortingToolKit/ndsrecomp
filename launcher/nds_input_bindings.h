#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <string>

// Launcher NDS button order: Up, Down, Left, Right, A, B, X, Y, L, R,
// Start, Select. SDL2 and SDL3 share these scancode/gamepad numeric values.
using NdsButtonBindings = std::array<int, 12>;
inline constexpr std::array<const char*, 12> kNdsButtonNames = {
    "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "L", "R", "Start", "Select"};
inline constexpr NdsButtonBindings kNdsDefaultKeys = {
    82, 81, 80, 79, 29, 27, 22, 4, 20, 26, 40, 42};
inline constexpr NdsButtonBindings kNdsDefaultPad = {
    12, 13, 14, 15, 1, 2, 3, 4, 10, 11, 7, 5};
inline constexpr std::array<uint16_t, 12> kNdsButtonMasks = {
    1u << 6, 1u << 7, 1u << 5, 1u << 4, 1u << 0, 1u << 1,
    1u << 10, 1u << 11, 1u << 9, 1u << 8, 1u << 3, 1u << 2};

inline bool nds_valid_binding(int value, bool gamepad) {
    // Standard gamepad buttons (encoded as button+1) and six signed axes.
    return gamepad ? (value >= 0 && (value <= 21 ||
                                      (value >= 100 && value <= 111)))
                   : value >= 0 && value < 512;
}

inline bool nds_parse_bindings(const std::string& text, bool gamepad,
                               NdsButtonBindings* output) {
    NdsButtonBindings parsed{};
    const char* cursor = text.data();
    const char* end = cursor + text.size();
    for (size_t i = 0; i < parsed.size(); ++i) {
        const auto result = std::from_chars(cursor, end, parsed[i]);
        if (result.ec != std::errc{} || !nds_valid_binding(parsed[i], gamepad))
            return false;
        cursor = result.ptr;
        if (i + 1 == parsed.size()) {
            if (cursor != end) return false;
        } else if (cursor == end || *cursor++ != ',') {
            return false;
        }
    }
    *output = parsed;
    return true;
}

inline std::string nds_format_bindings(const NdsButtonBindings& bindings) {
    std::string text;
    for (int value : bindings) {
        if (!text.empty()) text += ',';
        text += std::to_string(value);
    }
    return text;
}

inline uint16_t nds_binding_mask(const NdsButtonBindings& bindings, int value) {
    if (value <= 0) return 0;  // zero is an explicit unbind
    uint16_t mask = 0;
    for (size_t i = 0; i < bindings.size(); ++i)
        if (bindings[i] == value) mask |= kNdsButtonMasks[i];
    return mask;
}
