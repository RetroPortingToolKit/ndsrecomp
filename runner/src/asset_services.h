#pragma once
#include <cstdint>
#include <vector>

namespace nds_assets {
struct FileView { uint32_t offset=0,size=0;const uint8_t* data=nullptr; };
struct Services {
    bool (*lookup)(const char*,FileView*)=nullptr;
    bool (*view)(uint32_t,uint32_t,FileView*)=nullptr;
    bool (*inspect)(const FileView&,uint32_t*)=nullptr;
    bool (*decode)(const FileView&,std::vector<uint8_t>*)=nullptr;
    bool (*publish)(uint32_t,const uint8_t*,uint32_t,uint64_t)=nullptr;
};
}
