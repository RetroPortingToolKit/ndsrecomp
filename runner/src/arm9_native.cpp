#include "arm9_native.h"

#if !defined(NDS_ARM9_DYNARMIC)
bool nds_arm9_native_run() { return false; }
void nds_arm9_native_note_write() {}
void nds_arm9_native_flush_writes() {}
void nds_arm9_native_clear() {}
void nds_arm9_native_census(bool, bool) {}
std::string nds_arm9_native_json() { return "{\"available\":false}"; }
#else
#include "state.h"
#include "tier3.h"
#include "execution_model.h"
#include "arm_decode.h"
#include "thumb_decode.h"
#include "native_service_entries.h"
#include "arm9_native_ir.h"
#include "arm9_native_watch.h"
#include "arm9_native_options.h"
#include "io.h"
#include "scheduler.h"
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/frontend/A32/a32_ir_emitter.h"
#include "dynarmic/frontend/A32/a32_types.h"
#include "dynarmic/ir/terminal.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <vector>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

namespace {
// Explicit simulated-timing profile: two ARM9 cycles per translated instruction.
// Guest time remains independent of host elapsed time. Device accesses complete
// through the existing bus, then leave JIT execution at the instruction boundary.
constexpr uint64_t cycles_per_instruction=2;
bool diagnostic() {static const bool on=std::getenv("NDS_ARM9_NATIVE_DIAGNOSTIC")!=nullptr;return on;}
bool batched_writes() {
    static const bool enabled=[] {const char* value=std::getenv("NDS_ARM9_BATCH_WRITES");return value&&std::strcmp(value,"1")==0;}();
    return enabled;
}
bool census_enabled=false;
class Native final : public Dynarmic::A32::UserCallbacks {
public:
    std::unique_ptr<Dynarmic::A32::Jit> jit;
    std::array<uint32_t,16>* registers=nullptr;
    std::array<uint32_t,64>* extensions=nullptr;
    NdsArm9GenerationWatch watches;
    std::unordered_map<uint64_t,uint64_t> floor_census;
    uint64_t checks_by_cpu[2]={};
    uint64_t remaining=0,entries=0,instructions=0,floors=0,devices=0,invalidations=0;
    uint64_t write_notifications=0,deferred_notifications=0,write_batches=0;
    uint32_t pending_pc=0;
    bool pending=false,known_write=false,running=false,device_halted=false;
    struct Running {
        Native& owner;
        explicit Running(Native& owner):owner(owner) {owner.running=true;owner.device_halted=false;}
        ~Running() {owner.running=false;}
    };
    Cp15State mapping{};
    uint64_t code_mapping_generation=~uint64_t(0);
    using ReadPages=std::array<uint8_t*,Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>;
    std::unique_ptr<ReadPages> read_pages;
    uint64_t read_mapping_generation=~uint64_t(0),read_sites=0,read_callbacks=0;
    uint64_t data_coalesced=0,gx_coalesced=0,slow_reads=0,slow_writes=0;
    uint64_t arithmetic_cap_splits=0;
    void refresh_reads() {
        if(!read_pages||read_mapping_generation==g_busf_mapping_generation)return;
        read_pages->fill(nullptr);
        const auto add=[&](uint64_t first,uint64_t limit) {
            for(uint64_t a=first&~uint64_t(4095);a<limit&&a<0x100000000ull;a+=4096) {
                uint32_t lo=0,hi=0;
                const auto* w=nds_busf_classify(uint32_t(a),&lo);
                const auto* end=nds_busf_classify(uint32_t(a+4095),&hi);
                if(w&&w==end&&w->data&&hi==lo+4095&&uint64_t(hi)<=w->mask)
                    (*read_pages)[a>>12]=w->data+lo;
            }
        };
        add(0x02000000u,0x04000000u);
        add(0,g_busf_itcm_limit);add(g_busf_dtcm_base,uint64_t(g_busf_dtcm_base)+g_busf_dtcm_limit);
        read_mapping_generation=g_busf_mapping_generation;
    }
    bool direct_read_instruction(uint64_t descriptor) {
        const Dynarmic::A32::LocationDescriptor location{Dynarmic::IR::LocationDescriptor{descriptor}};
        const auto i=decode(location.TFlag(),location.PC());
        // The existing halfword lowering intentionally uses a wide callback.
        // Keep it there, including odd signed-byte device semantics. Unaligned
        // word/block reads also fall back, via the SDK's alignment guard.
        const bool ordinary=i.op==armv4t::IrOp::LDR||i.op==armv4t::IrOp::LDRB||i.op==armv4t::IrOp::LDM;
        if(ordinary)++read_sites;
        return ordinary;
    }
    Native() {
        Dynarmic::A32::UserConfig conf;
        conf.callbacks=this;conf.arch_version=Dynarmic::A32::ArchVersion::v5TE;
        conf.code_cache_size=32*1024*1024;
        // Normal bus callbacks use address/value arguments, current PC and
        // control state. General registers/flags are canonical at Run return.
        conf.optimizations=Dynarmic::all_safe_optimizations;
        if(nds_native_options().reads) {
            read_pages=std::make_unique<ReadPages>();read_pages->fill(nullptr);
            conf.page_table=read_pages.get();conf.page_table_read_only=true;
            conf.page_table_read_filter=[this](uint64_t location){return direct_read_instruction(location);};
            conf.detect_misaligned_access_via_page_table=16|32|64;
        }
        jit=std::make_unique<Dynarmic::A32::Jit>(conf);
        registers=&jit->Regs();extensions=&jit->ExtRegs();mapping=g_cp15;
    }
    void clear() {
        jit->ClearCache();watches.clear();++invalidations;
    }
    void publish() {
        std::copy(registers->begin(),registers->end(),g_cpu.R);g_cpu.cpsr=jit->Cpsr();
    }
    std::optional<uint32_t> code(uint32_t addr,bool watch) {
        uint32_t off=0;const auto* win=nds_busf_classify(addr,&off);
        // No speculative execution from MMIO, ROM or unmapped bytes. Native
        // BIOS/C helpers and remaining executable regions retain their backend.
        if(!win||!win->data||!win->gen||uint64_t(off)+4>uint64_t(win->mask)+1||
           !bus_range_has_write_provenance(addr,4))return std::nullopt;
        if(watch) {
            const uint32_t* generation=win->gen+(off>>12);
            watches.add(generation);
        }
        uint32_t value;std::memcpy(&value,win->data+off,4);return value;
    }
    std::optional<uint32_t> MemoryReadCode(uint32_t addr) override {return code(addr,true);}
    armv4t::Instr decode(bool thumb,uint32_t pc) {
        auto word=code(pc&~3u,false);
        if(!word)return {};
        return thumb?armv4t::ThumbDecoder::decode(uint16_t(*word>>((pc&2)*8)),pc):armv4t::ArmDecoder::decode(*word,pc);
    }
    static bool memory(const armv4t::Instr& i) {
        using Op=armv4t::IrOp;
        return (i.op>=Op::LDR&&i.op<=Op::STM)||i.op==Op::SWP||i.op==Op::SWPB;
    }
    bool floor(const armv4t::Instr& i) {
        using Op=armv4t::IrOp;
        // Exception returns/banked accesses remain in the faithful runtime.
        if(i.op==Op::MRS||i.op==Op::MSR||i.op==Op::MCR||i.op==Op::MRC||i.op==Op::CDP||
           i.op==Op::SWI||i.op==Op::SWP||i.op==Op::SWPB||
           i.op==Op::LDRD||i.op==Op::STRD||i.op==Op::BL_suffix||i.is_undefined)return true;
        if((i.op==Op::LDM||i.op==Op::STM)&&(i.block.s_bit||
           (i.block.writeback&&(i.block.reg_list&(1u<<i.block.rn)))))return true;
        if(memory(i)&&i.rd==15)return true;
        if(i.is_pc_writing&&(memory(i)||i.set_flags))return true;
        return false;
    }
    bool PreCodeReadHook(bool thumb,uint32_t pc,Dynarmic::A32::IREmitter& ir) override {
        const uint32_t start=Dynarmic::A32::LocationDescriptor{ir.block.Location()}.PC();
        const unsigned width=thumb?2:4;
        if(pc!=start&&(pc-start>=nds_native_block_limit(nds_native_options().blocks)*width||memory(decode(thumb,pc-width)))) {
            if(pc-start>=nds_native_block_limit(nds_native_options().blocks)*width)++arithmetic_cap_splits;
            const auto previous=decode(thumb,pc-width);
            nds_arm9_finish_memory(ir,previous.op==armv4t::IrOp::LDM?NdsArm9ReadKind::BlockWord:NdsArm9ReadKind::Word);
            ir.BranchWritePC(ir.Imm32(pc));
            ir.SetTerm(Dynarmic::IR::Term::CheckHalt{Dynarmic::IR::Term::LinkBlock{ir.current_location}});
            return false;
        }
        const auto bytes=code(pc&~3u,false);
        bool native_encoding=bool(bytes);
        if(bytes&&thumb) {
            const uint16_t first=uint16_t(*bytes>>((pc&2u)*8u));
            if(first>=0xe800u) {
                // ARMv5 has only BL/BLX pairs here. A suffix can be a real
                // resume PC from faithful execution; newer Thumb32 decoding
                // must not consume it together with the following instruction.
                const auto next=code((pc+2)&~3u,false);
                const uint16_t second=next?uint16_t(*next>>(((pc+2)&2u)*8u)):0;
                native_encoding=nds_arm9_thumb_native_encoding(first,second,bool(next));
            }
        }
        if(bytes&&!thumb&&(*bytes&0x0c000000u)==0x0c000000u)native_encoding=false;
        if(!native_encoding||floor(decode(thumb,pc))||nds_native_service_entry(NDS_ARM9,pc,thumb)) {
            ir.SetTerm(Dynarmic::IR::Term::Interpret{ir.current_location});return false;
        }
        return true;
    }
    bool ram(uint32_t addr,unsigned width) {
        uint32_t off=0;const auto* w=nds_busf_classify(addr,&off);
        return w&&w->data&&uint64_t(off)+width<=uint64_t(w->mask)+1;
    }
    void context() {
        g_cpu.R[15]=(*registers)[15];
        g_cpu.cpsr=(g_cpu.cpsr&~0xffu)|(jit->Cpsr()&0xffu);
    }
    void device() {
        context();++devices;jit->HaltExecution();device_halted=true;
    }
    void read_boundary(uint32_t a,unsigned width) {
        if(ram(a,width))return;
        ++slow_reads;
        if(nds_native_options().data&&nds_native_plain_video(a,width)) {context();++data_coalesced;}
        else device();
    }
    uint8_t MemoryRead8(uint32_t a) override {++read_callbacks;read_boundary(a,1);return bus_read_u8(a);}
    uint16_t MemoryRead16(uint32_t a) override {++read_callbacks;read_boundary(a&~1u,2);return bus_read_u16(a&~1u);}
    uint32_t MemoryRead32(uint32_t a) override {
        ++read_callbacks;
        const auto kind=NdsArm9ReadKind((*extensions)[0]);
        if(kind==NdsArm9ReadKind::HalfUnsigned||kind==NdsArm9ReadKind::HalfSigned) {
            const bool sign=kind==NdsArm9ReadKind::HalfSigned;
            const bool byte=sign&&(a&1u);
            read_boundary(byte?a:(a&~1u),byte?1:2);
            return nds_arm9_read_halfword(a,sign,[](uint32_t x){return bus_read_u8(x);},[](uint32_t x){return bus_read_u16(x);});
        }
        read_boundary(a&~3u,4);
        if(kind==NdsArm9ReadKind::BlockWord)return bus_read_u32(a&~3u);
        return std::rotr(bus_read_u32(a&~3u),int((a&3)*8));
    }
    uint64_t MemoryRead64(uint32_t a) override {return uint64_t(MemoryRead32(a))|uint64_t(MemoryRead32(a+4))<<32;}
    void write(uint32_t a,uint32_t value,unsigned width) {
        a&=~(width-1u);
        uint32_t off=0;const auto* w=nds_busf_classify(a,&off);
        const uint32_t* generation=w&&w->data&&w->gen? w->gen+(off>>12):nullptr;
        const bool ordinary_ram=ram(a,width);
        const bool batch_gx=!ordinary_ram&&nds_native_options().gx&&nds_native_gx_command(a,width);
        const bool batch_data=!ordinary_ram&&nds_native_options().data&&nds_native_plain_video(a,width);
        if(!ordinary_ram) {
            ++slow_writes;
            if(batch_gx||batch_data)context();else device();
        } else {g_cpu.R[15]=(*registers)[15];g_cpu.R[14]=(*registers)[14];}
        // Only this inline RAM store is covered by the exact generation test
        // below. I/O may invoke another bus master, whose writes must stay visible.
        known_write=ordinary_ram;
        if(width==4)bus_write_u32(a,value);
        else if(width==2)bus_write_u16(a,uint16_t(value));
        else bus_write_u8(a,uint8_t(value));
        known_write=false;
        if(batch_data)++data_coalesced;
        if(batch_gx) {
            // The modeled FIFO IS the ordered queue. Submit each command once
            // at the current guest timestamp, retaining decoding/status/IRQ/DMA.
            // Avoid an additional host queue and synchronize on actual effects.
            ++gx_coalesced;
            if(nds_gxfifo_stalled()||nds_dma_cpu_stalled(0)||nds_cpu_halted(0)||
               g_nds_irq_pending_cache[0]||nds_event_break_hit()||nds_slice_over())device();
        }
        if(generation&&watches.contains(generation))clear();
    }
    void MemoryWrite8(uint32_t a,uint8_t v) override {write(a,v,1);}
    void MemoryWrite16(uint32_t a,uint16_t v) override {write(a,v,2);}
    void MemoryWrite32(uint32_t a,uint32_t v) override {write(a,v,4);}
    void MemoryWrite64(uint32_t a,uint64_t v) override {write(a,uint32_t(v),4);write(a+4,uint32_t(v>>32),4);}
    void InterpreterFallback(uint32_t pc,size_t n) override {
        pending=true;pending_pc=pc;floors+=n;
        if(census_enabled)floor_census[(uint64_t(pc)<<1)|((jit->Cpsr()&CPSR_T_BIT)?1u:0u)]+=n;
        jit->HaltExecution();
    }
    void CallSVC(uint32_t) override {pending=true;pending_pc=jit->Regs()[15];jit->HaltExecution();}
    void ExceptionRaised(uint32_t pc,Dynarmic::A32::Exception) override {pending=true;pending_pc=pc;jit->HaltExecution();}
    void AddTicks(uint64_t n) override {
        remaining=n<remaining?remaining-n:0;instructions+=n;g_insn_count[NDS_ARM9]+=n;
        g_runtime_cycles+=n*cycles_per_instruction;
    }
    uint64_t GetTicksRemaining() override {return remaining;}
    void validate_writes() {
        const uint64_t before=watches.checks;
        const bool changed=watches.changed();
        checks_by_cpu[g_nds_active==NDS_ARM7?1:0]+=watches.checks-before;
        if(changed)clear();
    }
    void flush_writes() {
        if(watches.take_pending()) {++write_batches;validate_writes();}
    }
    void unknown_write() {
        if(known_write)return;
        ++write_notifications;
        if(batched_writes()&&nds_arm9_can_defer_generation_check(running,device_halted)) {
            ++deferred_notifications;watches.defer();return;
        }
        validate_writes();
    }
};
std::unique_ptr<Native> native;
bool supported_host() {
    static const bool supported=[] {
#if defined(_MSC_VER)
    int flags[4];__cpuid(flags,1);return (flags[2]&1)!=0;
#else
    unsigned a,b,c,d;return __get_cpuid(1,&a,&b,&c,&d)!=0&&(c&bit_SSE3)!=0;
#endif
    }();
    return supported;
}
}

bool nds_arm9_native_run() {
    if(g_nds_active!=NDS_ARM9||!nds_performance_execution()||g_insn_hook_armed||
       g_runtime_deep_trace||(g_cpu.cpsr&0x200u)||runtime_deferred_cycles()!=0||!supported_host())return false;
    uint32_t off=0;const auto* w=nds_busf_classify(g_cpu.R[15],&off);
    if(!w||!w->data||!bus_range_has_write_provenance(g_cpu.R[15],4))return false;
    const uint32_t before_poll=g_cpu.R[15];
    runtime_tick(0);
    if(runtime_unwinding()||runtime_should_yield())return true;
    if(g_cpu.R[15]!=before_poll)return true;
    if(g_nds_fast_limit<=g_runtime_cycles)return false;
    if(!native)native=std::make_unique<Native>();
    auto& n=*native;
    if(n.code_mapping_generation!=g_busf_mapping_generation) {
        n.clear();n.code_mapping_generation=g_busf_mapping_generation;
    }
    if(n.mapping.control!=g_cp15.control||n.mapping.itcm_size!=g_cp15.itcm_size||
       n.mapping.dtcm_base!=g_cp15.dtcm_base||n.mapping.dtcm_size!=g_cp15.dtcm_size) {
        n.clear();n.mapping=g_cp15;
    }
    n.refresh_reads();
    n.flush_writes(); // No cached native instruction executes before validation.
    runtime_call_stack_restore(nullptr,0);
    std::copy(std::begin(g_cpu.R),std::end(g_cpu.R),n.registers->begin());n.jit->SetCpsr(g_cpu.cpsr);
    n.jit->ClearHalt(Dynarmic::HaltReason::UserDefined1|Dynarmic::HaltReason::MemoryAbort);
    n.remaining=std::max<uint64_t>(1,(g_nds_fast_limit-g_runtime_cycles)/cycles_per_instruction);
    n.pending=false;++n.entries;
    if(diagnostic()&&n.entries<=32)std::fprintf(stderr,"[arm9-native] enter %llu pc=%08x cycles=%llu limit=%llu budget=%llu\n",
        static_cast<unsigned long long>(n.entries),g_cpu.R[15],g_runtime_cycles,g_nds_fast_limit,static_cast<unsigned long long>(n.remaining));
    {Native::Running running(n);n.jit->Run();}
    n.publish();n.flush_writes();
    if(diagnostic()&&n.entries<=32)std::fprintf(stderr,"[arm9-native] leave pc=%08x cycles=%llu pending=%u floorpc=%08x\n",
        g_cpu.R[15],g_runtime_cycles,unsigned(n.pending),n.pending_pc);
    if(n.pending&&!runtime_should_yield()) {
        g_cpu.R[15]=n.pending_pc;
        const bool thumb=(g_cpu.cpsr&CPSR_T_BIT)!=0;
        if(nds_native_service_entry(NDS_ARM9,n.pending_pc,thumb))
            runtime_dispatch(n.pending_pc | unsigned(thumb));
        else tier3_step_one(n.pending_pc);
    }
    runtime_tick(0);
    return true;
}
void nds_arm9_native_note_write() {if(native)native->unknown_write();}
void nds_arm9_native_flush_writes() {if(native)native->flush_writes();}
void nds_arm9_native_clear() {if(native)native->clear();}
void nds_arm9_native_census(bool enabled,bool reset) {
    census_enabled=enabled;
    if(reset&&native)native->floor_census.clear();
}
std::string nds_arm9_native_json() {
    uint64_t windows=0,fences=0;scheduler_performance_window_counts(&windows,&fences);
    std::string out="{\"available\":"+std::string(supported_host()?"true":"false")+
        ",\"diagnostic\":"+std::string(diagnostic()?"true":"false")+
        ",\"census_enabled\":"+std::string(census_enabled?"true":"false")+
        ",\"batched_writes\":"+std::string(batched_writes()?"true":"false")+
        ",\"direct_reads\":"+(nds_native_options().reads?"true":"false")+
        ",\"long_blocks\":"+(nds_native_options().blocks?"true":"false")+
        ",\"data_handoffs\":"+(nds_native_options().data?"true":"false")+
        ",\"gx_batch\":"+(nds_native_options().gx?"true":"false")+
        ",\"event_windows\":"+(nds_native_options().events?"true":"false")+
        ",\"event_window_rounds\":"+std::to_string(windows)+
        ",\"event_window_fences\":"+std::to_string(fences)+
        ",\"cycles_per_instruction\":"+std::to_string(cycles_per_instruction);
    if(native)out+=",\"entries\":"+std::to_string(native->entries)+
        ",\"read_callbacks\":"+std::to_string(native->read_callbacks)+
        ",\"arithmetic_cap_splits\":"+std::to_string(native->arithmetic_cap_splits)+
        ",\"direct_read_sites\":"+std::to_string(native->read_sites)+
        ",\"data_coalesced\":"+std::to_string(native->data_coalesced)+
        ",\"gx_coalesced\":"+std::to_string(native->gx_coalesced)+
        ",\"slow_reads\":"+std::to_string(native->slow_reads)+
        ",\"slow_writes\":"+std::to_string(native->slow_writes)+
        ",\"instructions\":"+std::to_string(native->instructions)+
        ",\"reference_stops\":"+std::to_string(native->floors)+
        ",\"device_handoffs\":"+std::to_string(native->devices)+
        ",\"invalidations\":"+std::to_string(native->invalidations)+
        ",\"unknown_write_checks\":"+std::to_string(native->watches.checks)+
        ",\"watched_pages\":"+std::to_string(native->watches.size())+
        ",\"write_notifications\":"+std::to_string(native->write_notifications)+
        ",\"deferred_notifications\":"+std::to_string(native->deferred_notifications)+
        ",\"write_batches\":"+std::to_string(native->write_batches)+
        ",\"write_checks_arm9\":"+std::to_string(native->checks_by_cpu[0])+
        ",\"write_checks_arm7\":"+std::to_string(native->checks_by_cpu[1]);
    out+=",\"reference_census\":[";
    bool comma=false;
    if(native)for(const auto& [key,hits]:native->floor_census) {
        const auto pc=uint32_t(key>>1);const bool thumb=(key&1)!=0;
        uint32_t word=0;const bool valid=bus_debug_copy(0,pc&~3u,reinterpret_cast<uint8_t*>(&word),4);
        const auto instruction=thumb?armv4t::ThumbDecoder::decode(uint16_t(word>>((pc&2)*8)),pc):armv4t::ArmDecoder::decode(word,pc);
        if(comma)out+=",";
        comma=true;
        out+="{\"pc\":"+std::to_string(pc)+",\"thumb\":"+std::to_string(thumb)+
            ",\"hits\":"+std::to_string(hits)+",\"bytes_valid\":"+(valid?"true":"false")+
            ",\"word\":"+std::to_string(word)+",\"op\":"+std::to_string(unsigned(instruction.op))+"}";
    }
    out+="]";
    return out+"}";
}
#endif
