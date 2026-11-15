#pragma once

#include "runtime_arm.h"
#include "asset_services.h"
#include <cstdint>
#include <vector>

// Complete AMHE0 file/archive requests. The selected profile supplies the
// immutable NitroFS and physical publication implementation at boot.
namespace mph_assets {
using FileView=nds_assets::FileView;
using Services=nds_assets::Services;
struct Replacement {
    void (*function)() = nullptr;
    const NdsStaticValidation* validation = nullptr;
    uint32_t guard_start = 0, guard_size = 0;
};
struct Statistics {
    bool enabled = false;
    uint64_t files = 0, archives = 0, bytes = 0, declines = 0;
    uint32_t last_offset=0,last_compressed=0,last_destination=0,last_decoded=0;
};
bool initialize(const char* rom_sha1, const Services& services);
bool is_entry(NdsCpu cpu, uint32_t pc, bool thumb);
Replacement resolve(NdsCpu cpu, uint32_t pc, bool thumb);
Statistics statistics();
void run();
} // namespace mph_assets
