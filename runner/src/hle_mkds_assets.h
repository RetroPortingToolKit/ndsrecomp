#pragma once
#include "asset_services.h"
#include "hle_mkds.h"

namespace mkds_assets {
struct Statistics {
    bool enabled=false;uint64_t files=0,bytes=0,declines=0;
    uint32_t last_offset=0,last_compressed=0,last_destination=0,last_decoded=0;
};
bool initialize(const char* rom_sha1,const nds_assets::Services& services);
bool is_entry(NdsCpu cpu,uint32_t pc,bool thumb);
mkds_hle::Replacement resolve(NdsCpu cpu,uint32_t pc,bool thumb);
Statistics statistics();
void run();
}
