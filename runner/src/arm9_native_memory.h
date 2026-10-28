#pragma once
#include <cstdint>

enum class NdsArm9ReadKind : uint32_t { Word, HalfUnsigned, HalfSigned, BlockWord };

inline bool nds_arm9_thumb_native_encoding(uint16_t first,uint16_t second,bool has_second) {
    if(first<0xe800u)return true;
    return (first&0xf800u)==0xf000u&&has_second&&
        ((second&0xf800u)==0xf800u||(second&0xf800u)==0xe800u);
}
