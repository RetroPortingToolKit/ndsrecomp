#pragma once

#include "runtime_arm.h"
#include <cstdint>
#include <vector>

// Complete AMHE0 file/archive requests. The selected profile supplies the
// immutable NitroFS and physical publication implementation at boot.
namespace mph_assets {
struct FileView {
    uint32_t offset = 0;
    uint32_t size = 0;
    const uint8_t* data = nullptr;
};
struct Services {
    bool (*lookup)(const char* path, FileView* out) = nullptr;
    bool (*view)(uint32_t offset, uint32_t size, FileView* out) = nullptr;
    // inspect validates the complete stream without modifying guest memory.
    bool (*inspect)(const FileView& file, uint32_t* decoded_size) = nullptr;
    bool (*decode)(const FileView& file, std::vector<uint8_t>* decoded) = nullptr;
    // One all-or-nothing physical write, including the pre-mutation audio
    // fence and the ordinary executable/provenance publication.
    bool (*publish)(uint32_t address, const uint8_t* bytes, uint32_t size,
                    uint64_t arm9_cycles) = nullptr;
};
struct Replacement {
    void (*function)() = nullptr;
    const NdsStaticValidation* validation = nullptr;
    uint32_t guard_start = 0, guard_size = 0;
};
struct Statistics {
    bool enabled = false;
    uint64_t files = 0, archives = 0, bytes = 0, declines = 0;
};
bool initialize(const char* rom_sha1, const Services& services);
bool is_entry(NdsCpu cpu, uint32_t pc, bool thumb);
Replacement resolve(NdsCpu cpu, uint32_t pc, bool thumb);
Statistics statistics();
void run();
} // namespace mph_assets
