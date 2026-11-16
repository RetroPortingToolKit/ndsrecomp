#pragma once
#include "hle_mkds.h"
#include <string>

namespace nds_assets {
bool configure(const char* rom_sha1,bool requested);
bool selected();
bool is_entry(NdsCpu cpu,uint32_t pc,bool thumb);
mkds_hle::Replacement resolve(NdsCpu cpu,uint32_t pc,bool thumb);
std::string json();
}
