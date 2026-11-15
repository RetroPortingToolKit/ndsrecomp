#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace nds_asset_data {
struct View {
    const uint8_t* data = nullptr;
    uint32_t offset = 0, size = 0;
};
// Callers verify the live SDK archive identity/ownership before using the
// immutable cartridge filesystem. No guest FSFile or card transaction exists.
bool slice(const uint8_t* rom, uint32_t bytes, uint32_t offset,
           uint32_t size, View* out);
bool find_file(const uint8_t* rom, uint32_t bytes, std::string_view path,
               View* out, uint16_t directory = 0xf000);
bool inspect(const View& input, uint32_t* output_bytes,
             uint32_t limit = 8u * 1024u * 1024u);
// Transactional host decode: malformed/unsupported input leaves out unchanged.
// Guest publication is a separate, single canonical write after validation.
bool decode(const View& input, std::vector<uint8_t>* out,
            uint32_t limit = 8u * 1024u * 1024u);
}
