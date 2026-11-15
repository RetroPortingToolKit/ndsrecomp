#include "native_asset_data.h"
#include <cstring>
#include <new>

namespace nds_asset_data {
namespace {
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 |
        uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
bool within(uint32_t offset, uint32_t count, uint32_t size) {
    return offset <= size && count <= size - offset;
}
}

bool slice(const uint8_t* rom, uint32_t bytes, uint32_t offset,
           uint32_t size, View* out) {
    if (!rom || !out || !within(offset, size, bytes)) return false;
    *out = {rom + offset, offset, size}; return true;
}

bool find_file(const uint8_t* rom, uint32_t bytes, std::string_view path,
               View* out, uint16_t directory) {
    if (!rom || !out || bytes < 0x50 || path.empty()) return false;
    const uint32_t fnt_offset = u32(rom + 0x40), fnt_size = u32(rom + 0x44);
    const uint32_t fat_offset = u32(rom + 0x48), fat_size = u32(rom + 0x4c);
    if (!within(fnt_offset, fnt_size, bytes) || fnt_size < 8 ||
        !within(fat_offset, fat_size, bytes) || (fat_size & 7)) return false;
    const auto* fnt = rom + fnt_offset;
    const uint32_t directories = u16(fnt + 6);
    if (!directories || directories > 0x1000 || directories * 8u > fnt_size)
        return false;
    const auto valid_directory = [&](uint16_t id) {
        return id >= 0xf000 && uint32_t(id - 0xf000) < directories;
    };
    const auto colon = path.find(':');
    if (colon != std::string_view::npos) {
        if (path.substr(0, colon) != "rom") return false;
        path.remove_prefix(colon + 1); directory = 0xf000;
    }
    if (!path.empty() && path.front() == '/') directory = 0xf000;
    if (!valid_directory(directory)) return false;
    size_t cursor = 0;
    while (cursor < path.size()) {
        while (cursor < path.size() && path[cursor] == '/') ++cursor;
        if (cursor == path.size()) return false;
        auto end = path.find('/', cursor);
        if (end == std::string_view::npos) end = path.size();
        const auto name = path.substr(cursor, end - cursor);
        const bool last = end == path.size();
        const auto* dir = fnt + uint32_t(directory - 0xf000) * 8u;
        if (name == "." || name == "..") {
            if (last) return false;
            if (name == ".." && directory != 0xf000) directory = u16(dir + 6);
            if (!valid_directory(directory)) return false;
            cursor = end + 1; continue;
        }
        uint32_t at = u32(dir), file = u16(dir + 4);
        bool found = false;
        while (at < fnt_size) {
            const uint8_t tag = fnt[at++];
            if (!tag) break;
            const uint32_t length = tag & 127u;
            const bool subdirectory = (tag & 128u) != 0;
            if (!length || !within(at, length + (subdirectory ? 2u : 0u), fnt_size))
                return false;
            const bool equal = name.size() == length &&
                std::memcmp(name.data(), fnt + at, length) == 0;
            if (equal) {
                if (subdirectory) {
                    if (last) return false;
                    directory = u16(fnt + at + length);
                    if (!valid_directory(directory)) return false;
                    found = true; break;
                }
                if (!last || file >= fat_size / 8u) return false;
                const auto* entry = rom + fat_offset + file * 8u;
                const uint32_t start = u32(entry), finish = u32(entry + 4);
                return finish >= start && slice(rom, bytes, start, finish - start, out);
            }
            at += length + (subdirectory ? 2u : 0u);
            if (!subdirectory && ++file > 0xffffu) return false;
        }
        if (!found) return false;
        cursor = end + 1;
    }
    return false;
}

bool inspect(const View& input, uint32_t* output_bytes, uint32_t limit) {
    if (!input.data || input.size < 4 || !output_bytes ||
        (input.data[0] != 0x10 && input.data[0] != 0x30)) return false;
    const uint32_t count = u32(input.data) >> 8;
    if (!count || count > limit) return false;
    *output_bytes = count; return true;
}

bool decode(const View& input, std::vector<uint8_t>* out, uint32_t limit) {
    uint32_t count = 0;
    if (!out || !inspect(input, &count, limit)) return false;
    try {
        std::vector<uint8_t> decoded(count);
        uint32_t source = 4, destination = 0;
        while (destination < count) {
            if (source == input.size) return false;
            const uint8_t flags = input.data[source++];
            if (input.data[0] == 0x30) {
                const uint32_t length = (flags & 127u) + ((flags & 128u) ? 3u : 1u);
                if (length > count - destination) return false;
                if (flags & 128u) {
                    if (source == input.size) return false;
                    std::memset(decoded.data() + destination, input.data[source++], length);
                } else {
                    if (!within(source, length, input.size)) return false;
                    std::memcpy(decoded.data() + destination, input.data + source, length);
                    source += length;
                }
                destination += length; continue;
            }
            for (unsigned bit = 0; bit < 8 && destination < count; ++bit) {
                if (flags & (0x80u >> bit)) {
                    if (!within(source, 2, input.size)) return false;
                    const uint8_t first = input.data[source++], second = input.data[source++];
                    const uint32_t length = (first >> 4) + 3u;
                    const uint32_t distance = ((first & 15u) << 8 | second) + 1u;
                    if (distance > destination || length > count - destination) return false;
                    for (uint32_t i = 0; i < length; ++i) {
                        decoded[destination] = decoded[destination - distance]; ++destination;
                    }
                } else {
                    if (source == input.size) return false;
                    decoded[destination++] = input.data[source++];
                }
            }
        }
        out->swap(decoded); return true;
    } catch (const std::bad_alloc&) { return false; }
}
}
