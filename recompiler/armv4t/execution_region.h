#pragma once
#include "runtime_arm.h"

// Flat AOT extension. Only active registers/flags are local. Privileged bank
// changes use the reference single-step path after publishing this state.
typedef struct NdsRegionRegisters { uint32_t R[16], cpsr; } NdsRegionRegisters;
typedef struct NdsExecRegion {
    NdsRegionRegisters cpu;
    uint64_t cycles;
    uint64_t insn_counts[2];
    uint32_t io, single_step;
} NdsExecRegion;

#ifdef __cplusplus
extern "C" {
#endif
bool nds_region_prepare(void);
void nds_region_end(NdsExecRegion* r);
void runtime_region_single_step(void);
extern uint32_t g_nds_region_hle_vectors;
extern uint64_t g_nds_region_entries, g_nds_region_reference_steps;
#ifdef __cplusplus
}
#endif

// Keep this register block's address inside the generated translation unit
// until the exit. An external begin(r) could retain its address, requiring
// register reloads around every subsequent external timing/bus helper.
NDS_MACHINERY_INLINE bool nds_region_begin(NdsExecRegion* r) {
    if (!nds_region_prepare()) return false;
    memcpy(r->cpu.R, g_cpu.R, sizeof(r->cpu.R));
    r->cpu.cpsr = g_cpu.cpsr;
    r->cycles = g_runtime_cycles;
    r->insn_counts[0] = r->insn_counts[1] = 0;
    r->io = r->single_step = 0;
    return true;
}

NDS_MACHINERY_INLINE void nds_region_publish(NdsExecRegion* r) {
    memcpy(g_cpu.R, r->cpu.R, sizeof(r->cpu.R));
    g_cpu.cpsr = r->cpu.cpsr;
    g_runtime_cycles = r->cycles;
    g_insn_count[g_nds_active] += r->insn_counts[g_nds_active];
    r->insn_counts[g_nds_active] = 0;
}
NDS_MACHINERY_INLINE void nds_region_charge(NdsExecRegion* r, uint32_t n) {
    r->cycles += n;
}
NDS_MACHINERY_INLINE bool nds_region_needs_exit(const NdsExecRegion* r) {
    return r->io || r->cycles >= g_nds_fast_limit;
}
NDS_MACHINERY_INLINE void nds_region_exchange(NdsExecRegion* r, uint32_t pc) {
    r->cpu.cpsr = (r->cpu.cpsr & ~CPSR_T_BIT) | ((pc & 1) ? CPSR_T_BIT : 0);
    r->cpu.R[15] = pc & ((pc & 1) ? ~1u : ~3u);
}

NDS_MACHINERY_INLINE uint32_t nds_region_read(NdsExecRegion* r, uint32_t addr,
                                             uint32_t width) {
    uint32_t off;
    const NdsBusFastWin* w = nds_busf_classify(addr, &off);
    if (!g_runtime_deep_trace && w && w->data &&
        (uint64_t)off + width <= (uint64_t)w->mask + 1) {
        const uint8_t* p=w->data+off;
        if(width==1)return p[0];
        if(width==2)return (uint32_t)p[0]|((uint32_t)p[1]<<8);
        return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    }
    nds_region_publish(r);
    r->io = 1;
    if (width == 4) return bus_read_u32_slow(addr);
    if (width == 2) return bus_read_u16_slow(addr);
    return bus_read_u8_slow(addr);
}
NDS_MACHINERY_INLINE void nds_region_write(NdsExecRegion* r, uint32_t addr,
                                           uint32_t value, uint32_t width) {
    uint32_t off;
    const NdsBusFastWin* w = nds_busf_classify(addr, &off);
    if (!g_runtime_deep_trace && w && w->data &&
        (uint64_t)off + width <= (uint64_t)w->mask + 1) {
        // Keep the original write-provenance, code-invalidation and shared
        // live-write effects. A cleared fast limit exits before the next op.
        if (width == 4) bus_write_u32(addr, value);
        else if (width == 2) bus_write_u16(addr, (uint16_t)value);
        else bus_write_u8(addr, (uint8_t)value);
        return;
    }
    nds_region_publish(r);
    r->io = 1;
    if (width == 4) bus_write_u32_slow(addr, value);
    else if (width == 2) bus_write_u16_slow(addr, (uint16_t)value);
    else bus_write_u8_slow(addr, (uint8_t)value);
}

// Portable unsigned arithmetic, preserving all unaffected CPSR bits.
NDS_MACHINERY_INLINE int nds_region_cond(uint32_t f, unsigned cond) {
    const uint32_t n=f>>31, z=(f>>30)&1, c=(f>>29)&1, v=(f>>28)&1;
    switch (cond & 15) {
        case 0:return z; case 1:return !z; case 2:return c; case 3:return !c;
        case 4:return n; case 5:return !n; case 6:return v; case 7:return !v;
        case 8:return c&&!z; case 9:return !c||z; case 10:return n==v;
        case 11:return n!=v; case 12:return !z&&n==v; case 13:return z||n!=v;
        case 14:return 1; default:return 0;
    }
}
NDS_MACHINERY_INLINE void nds_region_nz(uint32_t* f, uint32_t v) {
    *f=(*f&~(CPSR_N_BIT|CPSR_Z_BIT))|(v&CPSR_N_BIT)|(v==0?CPSR_Z_BIT:0);
}
NDS_MACHINERY_INLINE void nds_region_nzc(uint32_t* f, uint32_t v, uint32_t c) {
    *f=(*f&~CPSR_C_BIT)|(c?CPSR_C_BIT:0); nds_region_nz(f,v);
}
NDS_MACHINERY_INLINE void nds_region_arith(uint32_t* f,uint32_t a,uint32_t b,
                                          uint32_t v,uint32_t carry,int sub) {
    uint32_t ov=(sub?(a^b):~(a^b))&(a^v);
    *f=(*f&~(CPSR_C_BIT|CPSR_V_BIT))|(carry?CPSR_C_BIT:0)|
        ((ov>>31)?CPSR_V_BIT:0); nds_region_nz(f,v);
}
NDS_MACHINERY_INLINE void nds_region_add(uint32_t*f,uint32_t a,uint32_t b,uint32_t v) {
    nds_region_arith(f,a,b,v,v<a,0);
}
NDS_MACHINERY_INLINE void nds_region_adc(uint32_t*f,uint32_t a,uint32_t b,uint32_t c,uint32_t v) {
    nds_region_arith(f,a,b,v,(uint32_t)(((uint64_t)a+b+c)>>32),0);
}
NDS_MACHINERY_INLINE void nds_region_sub(uint32_t*f,uint32_t a,uint32_t b,uint32_t v) {
    nds_region_arith(f,a,b,v,a>=b,1);
}
NDS_MACHINERY_INLINE void nds_region_sbc(uint32_t*f,uint32_t a,uint32_t b,uint32_t c,uint32_t v) {
    nds_region_arith(f,a,b,v,(uint32_t)(((uint64_t)a+(uint32_t)~b+c)>>32),1);
}
NDS_MACHINERY_INLINE uint32_t nds_region_shift(uint32_t*f,uint32_t v,uint32_t n,int sc,unsigned kind) {
    if (!n) return v;
    uint32_t carry=0, result=0;
    if (kind==0) {
        carry=n<=32?((v>>(32-n))&1u):0;
        result=n<32?v<<n:0;
    } else if (kind==1) {
        carry=n<=32?((v>>(n-1))&1u):0;
        result=n<32?v>>n:0;
    } else if (kind==2) {
        carry=n<32?((v>>(n-1))&1u):(v>>31);
        result=n>=32?(0u-(v>>31)):
            ((v>>n)|((v>>31)?(~0u<<(32-n)):0));
    } else {
        n &= 31;
        result=n?((v>>n)|(v<<(32-n))):v;
        carry=result>>31;
    }
    if(sc)*f=(*f&~CPSR_C_BIT)|(carry?CPSR_C_BIT:0);
    return result;
}

// Generated bodies opt into these spellings only AFTER this header's helpers
// have been parsed. The ordinary runtime and faithful banks are unaffected.
#ifdef NDS_FLAT_GENERATED_BODY
#define g_cpu _region.cpu
#define g_insn_count _region.insn_counts
#define g_insn_hook_armed 0
#define runtime_should_yield() false
#define runtime_unwinding() false
#define runtime_tick(n) nds_region_charge(&_region,(n))
#define runtime_slice_yield() nds_region_needs_exit(&_region)
#define runtime_trace_event(...) ((void)0)
#define runtime_live_transfer(...) ((void)0)
#define runtime_call_push_return(...) ((void)0)
#define runtime_call_cancel_return(...) ((void)0)
#define runtime_call_should_return(...) 0
#define arm_cond_passes(c) nds_region_cond(_region.cpu.cpsr,(c))
#define arm_cond_passes_i(c) nds_region_cond(_region.cpu.cpsr,(c))
#define cpsr_n() ((_region.cpu.cpsr>>31)&1u)
#define cpsr_z() ((_region.cpu.cpsr>>30)&1u)
#define cpsr_c() ((_region.cpu.cpsr>>29)&1u)
#define cpsr_v() ((_region.cpu.cpsr>>28)&1u)
#define arm_set_nz(v) nds_region_nz(&_region.cpu.cpsr,(v))
#define arm_set_nzc_logic(v,c) nds_region_nzc(&_region.cpu.cpsr,(v),(c))
#define arm_set_nzcv_add(a,b,v) nds_region_add(&_region.cpu.cpsr,(a),(b),(v))
#define arm_set_nzcv_adc(a,b,c,v) nds_region_adc(&_region.cpu.cpsr,(a),(b),(c),(v))
#define arm_set_nzcv_sub(a,b,v) nds_region_sub(&_region.cpu.cpsr,(a),(b),(v))
#define arm_set_nzcv_sbc(a,b,c,v) nds_region_sbc(&_region.cpu.cpsr,(a),(b),(c),(v))
#define arm_shift_lsl(v,n,c) nds_region_shift(&_region.cpu.cpsr,(v),(n),(c),0)
#define arm_shift_lsr(v,n,c) nds_region_shift(&_region.cpu.cpsr,(v),(n),(c),1)
#define arm_shift_asr(v,n,c) nds_region_shift(&_region.cpu.cpsr,(v),(n),(c),2)
#define arm_shift_ror(v,n,c) nds_region_shift(&_region.cpu.cpsr,(v),(n),(c),3)
#define bus_read_u32(a) nds_region_read(&_region,(a),4)
#define bus_read_u16(a) ((uint16_t)nds_region_read(&_region,(a),2))
#define bus_read_u8(a) ((uint8_t)nds_region_read(&_region,(a),1))
#define bus_write_u32(a,v) nds_region_write(&_region,(a),(v),4)
#define bus_write_u16(a,v) nds_region_write(&_region,(a),(v),2)
#define bus_write_u8(a,v) nds_region_write(&_region,(a),(v),1)
#define runtime_dispatch_with_exchange(t) do { nds_region_exchange(&_region,(t)); goto NDS_REGION_TRANSFER; } while(0)
#define runtime_dispatch_literal_call(t) runtime_dispatch_with_exchange(t)
#define runtime_dispatch_literal_branch(t) runtime_dispatch_with_exchange(t)
#define runtime_dispatch_literal_fallthrough(t) runtime_dispatch_with_exchange(t)
#define runtime_dispatch(t) do { _region.cpu.R[15]=(t)&~1u; goto NDS_REGION_TRANSFER; } while(0)
#define runtime_link_call(s) runtime_dispatch_with_exchange((s)->target_pc)
#define runtime_link_branch(s) runtime_dispatch_with_exchange((s)->target_pc)
#define runtime_link_fallthrough(s) runtime_dispatch_with_exchange((s)->target_pc)
#endif
