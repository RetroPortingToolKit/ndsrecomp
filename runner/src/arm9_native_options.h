#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Process-fixed investigation switches. Default builds and the faithful model
// retain their original behavior. The optional translator is Windows/x64 gated.
struct NdsNativeOptions {
    bool reads=false, blocks=false, data=false, gx=false, events=false;
};
inline const NdsNativeOptions& nds_native_options() {
    static const NdsNativeOptions options=[] {
        NdsNativeOptions o;
#if defined(NDS_ARM9_DYNARMIC)
        const auto on=[](const char* name) {const char* v=std::getenv(name);return v&&std::strcmp(v,"1")==0;};
        o.reads=on("NDS_ARM9_DIRECT_READS");o.blocks=on("NDS_ARM9_LONG_BLOCKS");
        o.data=on("NDS_ARM9_DATA_HANDOFFS");o.gx=on("NDS_ARM9_GX_BATCH");
        o.events=on("NDS_ARM9_EVENT_WINDOWS");
#endif
        return o;
    }();
    return options;
}
// This subset contains video DATA, never its mapping/control/status registers.
// Actual accesses still go through the original bus/video implementation.
inline bool nds_native_plain_video(uint32_t addr,unsigned width) {
    return addr>=0x05000000u&&uint64_t(addr)+width<=0x08000000u;
}
inline bool nds_native_gx_command(uint32_t addr,unsigned width) {
    // Complete aligned command writes only. GXSTAT/results and all rendering
    // control/table writes remain ordinary synchronization boundaries.
    return width==4&&(addr&3u)==0&&addr>=0x04000400u&&addr<0x040005ccu;
}
inline unsigned nds_native_block_limit(bool longer) {return longer?64u:16u;}
inline uint64_t nds_native_event_quantum(bool enabled,bool peer_sleeping,
                                        bool peer_wake,bool dma,bool gx_stall) {
    return enabled&&peer_sleeping&&!peer_wake&&!dma&&!gx_stall?256u:64u;
}
