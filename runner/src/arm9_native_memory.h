#pragma once
#include <bit>
#include <cstdint>

enum class NdsArm9ReadKind : uint32_t { Word, HalfUnsigned, HalfSigned, BlockWord };

inline bool nds_arm9_thumb_native_encoding(uint16_t first,uint16_t second,bool has_second) {
    if(first<0xe800u)return true;
    return (first&0xf800u)==0xf000u&&has_second&&
        ((second&0xf800u)==0xf800u||(second&0xf800u)==0xe800u);
}

// Same observable reads as the faithful instruction implementation: odd signed
// halfwords read one byte; unsigned halfwords read an aligned halfword and rotate
// in a32-bit result. Keep this result wide all the way into the guest register.
template<class Read8,class Read16>
uint32_t nds_arm9_read_halfword(uint32_t address,bool sign,Read8 read8,Read16 read16) {
    if(sign&&(address&1u)) {
        const uint32_t value=read8(address);
        return (value^0x80u)-0x80u;
    }
    const uint32_t value=read16(address&~1u);
    if(sign)return (value^0x8000u)-0x8000u;
    return std::rotr(value,int((address&1u)*8u));
}
