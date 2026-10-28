#pragma once
#include "runtime_arm.h"
#include <cstdint>
#include <cstring>

// One instruction-local view of ordinary physical RAM. Mapping, provenance,
// code watches and store publication refer to the SAME window/offset. Never
// retain a view across a callback, mapping change or CPU switch. Unaligned,
// unmapped and device accesses keep their existing bus path. Naturally aligned
// widths <=4 cannot straddle the model's TCM or shared-WRAM mapping boundaries.
// This adds no memory-timing model and grants no executable provenance.
struct NdsNativeRam {
    const NdsBusFastWin* window = nullptr;
    uint32_t offset = 0;
    explicit operator bool() const { return window != nullptr; }

    template<class T> static NdsNativeRam map(uint32_t address) {
        static_assert(sizeof(T)==1 || sizeof(T)==2 || sizeof(T)==4);
        if (address & (sizeof(T)-1u)) return {};
        uint32_t off = 0;
        const auto* w = nds_busf_classify(address, &off);
        if (!w || !w->data || !w->written || !w->gen ||
            uint64_t{off}+sizeof(T)>uint64_t{w->mask}+1u) return {};
        return {w,off};
    }
    template<class T> bool has_provenance() const {
        for (unsigned i=0;i<sizeof(T);++i)
            if (!window->written[offset+i]) return false;
        return true;
    }
    template<class T> T read() const {
        T value;
        std::memcpy(&value,window->data+offset,sizeof(T));
        return value;
    }
    const uint32_t* generation() const { return window->gen+(offset>>12); }
    template<class T> void write(uint32_t address,T value) const {
        // Preserve the existing live firmware/context observer as well as the
        // shared physical publication authority used by slow/AOT/HLE stores.
        if (address+sizeof(T)>0x027e0000u && address<0x027e0040u)
            runtime_note_live_write(address,sizeof(T),read<T>(),value);
        std::memcpy(window->data+offset,&value,sizeof(T));
        nds_busf_note_write(window,offset,sizeof(T));
    }
};
