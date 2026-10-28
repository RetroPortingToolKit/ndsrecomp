#include "arm9_native.h"

#if !defined(NDS_ARM9_DYNARMIC)
bool nds_arm9_native_run() { return false; }
bool nds_arm9_native_supported() { return false; }
void nds_arm9_native_note_write() {}
void nds_arm9_native_note_write_pages(const uint32_t*, uint32_t) {}
void nds_arm9_native_flush_writes() {}
void nds_arm9_native_clear() {}
void nds_arm9_native_census(bool, bool) {}
std::string nds_arm9_native_json() { return "{\"available\":false}"; }
std::string nds_arm9_native_compilation_json(bool) { return "{\"available\":false}"; }
void nds_arm9_native_prepare_initialize(const std::string&) {}
bool nds_arm9_native_prepare_idle(uint32_t) { return false; }
std::string nds_arm9_native_preparation_json() { return "{\"available\":false}"; }
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
#if defined(NDS_NATIVE_CONTROL)
#include "native_control.h"
#endif
#include "io.h"
#include "scheduler.h"
#include "sha1.h"
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/frontend/A32/a32_ir_emitter.h"
#include "dynarmic/frontend/A32/a32_types.h"
#include "dynarmic/ir/terminal.h"
#include <algorithm>
#include <chrono>
#include <bit>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

namespace {
// Explicit simulated-timing profiles: two cycles of the executing CPU per
// translated instruction. ARM7 has its own identified optional profile.
// Guest time remains independent of host elapsed time. Device accesses complete
// through the existing bus, then leave JIT execution at the instruction boundary.
constexpr uint64_t cycles_per_instruction=2;
bool diagnostic() {static const bool on=std::getenv("NDS_ARM9_NATIVE_DIAGNOSTIC")!=nullptr;return on;}
bool batched_writes() {
    static const bool enabled=[] {const char* value=std::getenv("NDS_ARM9_BATCH_WRITES");return value&&std::strcmp(value,"1")==0;}();
    return enabled;
}
bool census_enabled=false;
bool compilation_census() {
    static const bool on=[] {const char* v=std::getenv("NDS_NATIVE_COMPILE_CENSUS");return v&&std::strcmp(v,"1")==0;}();
    return on;
}
bool preparation_selected=false;
bool preparation_enabled() {
    static const bool enabled=[] {const char* v=std::getenv("NDS_NATIVE_PREPARE");return !v||std::strcmp(v,"0")!=0;}();
    return preparation_selected&&enabled;
}
#if defined(NDS_MKDS_PERFORMANCE)
struct PreparationPage {uint32_t address,first,end;const char* sha1;};
#include "mkds_native_preparation.inc"
struct PreparationProgress {
    const uint32_t* generation=nullptr;
    uint32_t value=0,next=0;
    bool checked=false,matched=false;
};
#endif
class Native final : public Dynarmic::A32::UserCallbacks {
public:
    const NdsCpu cpu;
    std::unique_ptr<Dynarmic::A32::Jit> jit;
    std::array<uint32_t,16>* registers=nullptr;
    std::array<uint32_t,64>* extensions=nullptr;
    NdsArm9GenerationWatch watches;
    std::unordered_map<uint64_t,uint64_t> floor_census;
    std::unordered_map<uint64_t,uint64_t> entry_census,read_census,write_census;
    uint64_t census_dropped=0;
    std::unordered_set<uint64_t> compiled_locations;
    uint64_t compiled_blocks=0,recompiled_blocks=0,compile_census_dropped=0;
#if defined(NDS_MKDS_PERFORMANCE)
    std::array<PreparationProgress,std::size(preparation_pages)> preparation_progress{};
    size_t preparation_cursor=0;
    bool preparation_full=false,preparation_done=false;
    uint64_t preparation_epoch=~uint64_t(0);
    uint64_t preparation_requests=0,preparation_mismatches=0,preparation_ns=0,preparation_max_ns=0;
#endif
    void census(std::unordered_map<uint64_t,uint64_t>& counts,uint64_t key) {
        if(!census_enabled)return;
        auto at=counts.find(key);
        if(at!=counts.end())++at->second;
        else if(counts.size()<16384)counts.emplace(key,1);
        else ++census_dropped;
    }
    uint64_t access_key(uint32_t address) const {
        return uint64_t((*registers)[15]|((jit->Cpsr()&CPSR_T_BIT)?1u:0u))<<32|address;
    }
    uint64_t checks_by_cpu[2]={};
    uint64_t remaining=0,entries=0,instructions=0,floors=0,devices=0,invalidations=0;
    uint64_t write_notifications=0,deferred_notifications=0,write_batches=0;
    uint64_t precise_notifications=0,precise_code_hits=0;
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
        if(cpu==NDS_ARM9) {
            add(0,g_busf_itcm_limit);add(g_busf_dtcm_base,uint64_t(g_busf_dtcm_base)+g_busf_dtcm_limit);
        }
        read_mapping_generation=g_busf_mapping_generation;
    }
    bool direct_read_instruction(uint64_t descriptor) {
        const Dynarmic::A32::LocationDescriptor location{Dynarmic::IR::LocationDescriptor{descriptor}};
        const auto i=decode(location.TFlag(),location.PC());
        // Width-correct reads share one mapping and alias lifetime. Unsigned
        // halfwords align/rotate in IR; odd signed halves and unaligned words
        // retain callbacks through the SDK's alignment guard.
        const bool ordinary=i.op==armv4t::IrOp::LDR||i.op==armv4t::IrOp::LDRB||i.op==armv4t::IrOp::LDM||
            i.op==armv4t::IrOp::LDRH||i.op==armv4t::IrOp::LDRSH||i.op==armv4t::IrOp::LDRSB;
        if(ordinary)++read_sites;
        return ordinary;
    }
    explicit Native(NdsCpu cpu):cpu(cpu) {
        Dynarmic::A32::UserConfig conf;
        conf.callbacks=this;conf.arch_version=cpu==NDS_ARM9?Dynarmic::A32::ArchVersion::v5TE:Dynarmic::A32::ArchVersion::v4T;
#if defined(NDS_NATIVE_CONTROL)
        if(cpu==NDS_ARM9)conf.coprocessors[15]=std::make_shared<NdsNativeCacheMaintenance>();
#endif
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
        jit->ClearCache();watches.clear();compiled_locations.clear();++invalidations;
#if defined(NDS_MKDS_PERFORMANCE)
        reset_preparation();
#endif
    }
#if defined(NDS_MKDS_PERFORMANCE)
    void reset_preparation() {
        preparation_progress.fill({});preparation_cursor=0;
        preparation_full=false;preparation_done=false;
    }
#endif
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
    bool floor(const armv4t::Instr& i,bool thumb) {
        using Op=armv4t::IrOp;
#if defined(NDS_NATIVE_CONTROL)
        if(!i.is_undefined) {
            if(i.op==Op::MRS&&!i.psr.spsr&&i.rd<15)return false;
            if(i.op==Op::MSR&&!i.psr.spsr&&i.psr.mask==8)return false;
            if(cpu==NDS_ARM7&&i.op==Op::MUL&&!thumb&&!i.set_flags&&i.rd<15)return false;
            if(cpu==NDS_ARM9&&i.op==Op::MCR&&nds_native_stateless_cache_encoding(i.raw))return false;
        }
#else
        (void)thumb;
#endif
        // ARM7's destroyed Thumb MUL carry is modeled by the faithful step.
        // Keep ARMv5-only operations there too, including undefined encodings.
        if(cpu==NDS_ARM7&&(i.op==Op::MUL||i.op==Op::CLZ||i.op==Op::BLX_reg||i.op==Op::BLX_imm))return true;
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
        if(pc==start&&compilation_census()) {
            ++compiled_blocks;
            const uint64_t location=ir.block.Location().Value();
            if(compiled_locations.contains(location))++recompiled_blocks;
            else if(compiled_locations.size()<65536)compiled_locations.insert(location);
            else ++compile_census_dropped;
        }
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
#if defined(NDS_NATIVE_CONTROL)
        if(bytes&&!thumb&&cpu==NDS_ARM9&&nds_native_stateless_cache_encoding(*bytes))native_encoding=true;
#endif
        if(!native_encoding||floor(decode(thumb,pc),thumb)||nds_native_service_entry(cpu,pc,thumb)) {
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
        if(census_enabled)census(read_census,access_key(a));
        ++slow_reads;
        if(nds_native_options().data&&nds_native_plain_video(a,width)) {context();++data_coalesced;}
        else device();
    }
    uint8_t MemoryRead8(uint32_t a) override {++read_callbacks;read_boundary(a,1);return bus_read_u8(a);}
    uint16_t MemoryRead16(uint32_t a) override {
        ++read_callbacks;
        if(NdsArm9ReadKind((*extensions)[0])==NdsArm9ReadKind::HalfSigned&&(a&1u)) {
            read_boundary(a,1);
            const uint32_t value=bus_read_u8(a);
            return uint16_t((value^0x80u)-0x80u);
        }
        read_boundary(a&~1u,2);return bus_read_u16(a&~1u);
    }
    uint32_t MemoryRead32(uint32_t a) override {
        ++read_callbacks;
        const auto kind=NdsArm9ReadKind((*extensions)[0]);
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
            if(census_enabled)census(write_census,access_key(a));
            ++slow_writes;
            if(batch_gx||batch_data)context();else device();
        } else {g_cpu.R[15]=(*registers)[15];g_cpu.R[14]=(*registers)[14];}
        // Only this inline RAM store is covered by the exact generation test
        // below. I/O may invoke another bus master, whose writes must stay visible.
        {
            known_write=ordinary_ram;
            if(width==4)bus_write_u32(a,value);
            else if(width==2)bus_write_u16(a,uint16_t(value));
            else bus_write_u8(a,uint8_t(value));
            known_write=false;
        }
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
        remaining=n<remaining?remaining-n:0;instructions+=n;g_insn_count[cpu]+=n;
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
std::unique_ptr<Native> natives[2];
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

bool nds_arm9_native_supported() { return supported_host(); }
bool nds_arm9_native_run() {
#if !defined(NDS_ARM7_NATIVE)
    if(g_nds_active!=NDS_ARM9)return false;
#endif
    if(!nds_performance_execution()||g_insn_hook_armed||
       g_runtime_deep_trace||(g_cpu.cpsr&0x200u)||runtime_deferred_cycles()!=0||!supported_host())return false;
    // A service whose full proof declined and has no generated entry must
    // retire its first instruction in the existing reference floor. Otherwise
    // native translation would stop at this same barrier and redispatch forever.
    if(nds_native_service_entry(g_nds_active,g_cpu.R[15],(g_cpu.cpsr&CPSR_T_BIT)!=0))return false;
    uint32_t off=0;const auto* w=nds_busf_classify(g_cpu.R[15],&off);
    if(!w||!w->data||!bus_range_has_write_provenance(g_cpu.R[15],4))return false;
    const uint32_t before_poll=g_cpu.R[15];
    runtime_tick(0);
    if(runtime_unwinding()||runtime_should_yield())return true;
    if(g_cpu.R[15]!=before_poll)return true;
    if(g_nds_fast_limit<=g_runtime_cycles)return false;
    auto& native=natives[g_nds_active];
    if(!native)native=std::make_unique<Native>(g_nds_active);
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
    n.census(n.entry_census,(uint64_t(g_cpu.R[15])<<1)|((g_cpu.cpsr&CPSR_T_BIT)?1u:0u));
    if(diagnostic()&&n.entries<=32)std::fprintf(stderr,"[arm9-native] enter %llu pc=%08x cycles=%llu limit=%llu budget=%llu\n",
        static_cast<unsigned long long>(n.entries),g_cpu.R[15],g_runtime_cycles,g_nds_fast_limit,static_cast<unsigned long long>(n.remaining));
    {Native::Running running(n);n.jit->Run();}
    n.publish();n.flush_writes();
    if(diagnostic()&&n.entries<=32)std::fprintf(stderr,"[arm9-native] leave pc=%08x cycles=%llu pending=%u floorpc=%08x\n",
        g_cpu.R[15],g_runtime_cycles,unsigned(n.pending),n.pending_pc);
    if(n.pending&&!runtime_should_yield()) {
        g_cpu.R[15]=n.pending_pc;
        const bool thumb=(g_cpu.cpsr&CPSR_T_BIT)!=0;
        if(nds_native_service_entry(g_nds_active,n.pending_pc,thumb))
            runtime_dispatch(n.pending_pc | unsigned(thumb));
        else tier3_step_one(n.pending_pc);
    }
    runtime_tick(0);
    return true;
}
// Addressless/legacy publications remain conservative. Normal writes from every
// bus master use the physical-page entry below, including AOT/BIOS and DMA.
void nds_arm9_native_note_write() {
    for(auto& n:natives)if(n)n->unknown_write();
}
void nds_arm9_native_note_write_pages(const uint32_t* first,uint32_t count) {
#if defined(NDS_NATIVE_PRECISE_WRITES)
    // A native writer still performs its existing immediate post-store check.
    // Nested I/O/DMA has no known_write proof and publishes to both CPUs.
    const auto& active=natives[g_nds_active];
    const int writer=active&&active->known_write?int(g_nds_active):-1;
    if(nds_arm9_notify_write_pages(first,count,natives,writer))return;
#else
    (void)first;(void)count;
#endif
    nds_arm9_native_note_write();
}
void nds_arm9_native_flush_writes() {for(auto& n:natives)if(n)n->flush_writes();}
void nds_arm9_native_clear() {for(auto& n:natives)if(n)n->clear();}
void nds_arm9_native_prepare_initialize(const std::string& rom_sha1) {
#if defined(NDS_MKDS_PERFORMANCE)
    preparation_selected=rom_sha1=="691e00d9a5dd80b04f80cc7559503e8b06848785";
#else
    (void)rom_sha1;
#endif
}
bool nds_arm9_native_prepare_idle(uint32_t budget_us) {
#if defined(NDS_MKDS_PERFORMANCE)
    if(!budget_us||!preparation_enabled()||!nds_performance_execution()||!natives[NDS_ARM9])return false;
    auto& n=*natives[NDS_ARM9];
    if(n.running||n.code_mapping_generation!=g_busf_mapping_generation||
       n.mapping.control!=g_cp15.control||n.mapping.itcm_size!=g_cp15.itcm_size||
       n.mapping.dtcm_base!=g_cp15.dtcm_base||n.mapping.dtcm_size!=g_cp15.dtcm_size)return false;
    const auto epoch=n.jit->CodeCacheGeneration();
    if(n.preparation_epoch!=epoch) {n.reset_preparation();n.preparation_epoch=epoch;}
    if(n.preparation_full||n.preparation_done)return false;
    using Clock=std::chrono::steady_clock;
    const auto begin=Clock::now();
    const auto deadline=begin+std::chrono::microseconds(std::min(budget_us,500000u));
    // Only code reads/classification run here. Do not switch scheduler slots or
    // copy register files; restore the bus's CPU selector before returning.
    struct CpuView {
        NdsCpu old=g_nds_active;
        CpuView() {g_nds_active=NDS_ARM9;}
        ~CpuView() {g_nds_active=old;}
    } view;
    n.flush_writes();n.refresh_reads();
    bool worked=false;
    std::array<uint8_t,4096> bytes;
    for(size_t visited=0;visited<std::size(preparation_pages)&&Clock::now()<deadline;) {
        const auto& page=preparation_pages[n.preparation_cursor];
        auto& progress=n.preparation_progress[n.preparation_cursor];
        uint32_t offset=0;
        const auto* window=nds_busf_classify(page.address,&offset);
        if(window&&window->data&&window->gen&&uint64_t(offset)+bytes.size()<=uint64_t(window->mask)+1) {
            const uint32_t* generation=window->gen+(offset>>12);
            if(!progress.checked||progress.generation!=generation||progress.value!=*generation) {
                progress={generation,*generation,page.first,true,false};
                std::memcpy(bytes.data(),window->data+offset,bytes.size());
                progress.matched=gba::sha1(bytes.data(),bytes.size()).hex()==page.sha1;
                if(!progress.matched) {
                    ++n.preparation_mismatches;progress.next=page.end;
                }
            }
            for(;progress.next<page.end&&Clock::now()<deadline;++progress.next) {
                const uint64_t location=preparation_locations[progress.next];
                // Hints never confer executable provenance. The ordinary code
                // reader and its physical-page watches qualify the live image.
                if(!n.code(uint32_t(location)&~3u,false)) {progress.matched=false;continue;}
                if(!n.jit->PrepareBlock(location)) {n.preparation_full=true;break;}
                n.preparation_epoch=n.jit->CodeCacheGeneration();
                ++n.preparation_requests;worked=true;
            }
            if(progress.next<page.end)break;
        }
        n.preparation_cursor=(n.preparation_cursor+1)%std::size(preparation_pages);
        ++visited;
    }
    n.preparation_done=true;
    for(size_t i=0;i<std::size(preparation_pages);++i) {
        const auto& p=n.preparation_progress[i];
        if(!p.matched||p.next<preparation_pages[i].end) {n.preparation_done=false;break;}
    }
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();
    n.preparation_ns+=uint64_t(ns);n.preparation_max_ns=std::max(n.preparation_max_ns,uint64_t(ns));
    return worked;
#else
    (void)budget_us;return false;
#endif
}
std::string nds_arm9_native_preparation_json() {
    std::string out="{\"available\":true,\"selected\":"+std::string(preparation_selected?"true":"false")+
        ",\"enabled\":"+(preparation_enabled()?"true":"false");
#if defined(NDS_MKDS_PERFORMANCE)
    out+=",\"seed_pages\":"+std::to_string(std::size(preparation_pages))+
        ",\"seed_locations\":"+std::to_string(std::size(preparation_locations));
    if(const auto& n=natives[NDS_ARM9])out+=",\"requests\":"+std::to_string(n->preparation_requests)+
        ",\"page_mismatches\":"+std::to_string(n->preparation_mismatches)+
        ",\"wall_work_ns\":"+std::to_string(n->preparation_ns)+
        ",\"max_call_ns\":"+std::to_string(n->preparation_max_ns)+
        ",\"complete\":"+(n->preparation_done?"true":"false")+
        ",\"cache_full\":"+(n->preparation_full?"true":"false");
#endif
    return out+"}";
}
void nds_arm9_native_census(bool enabled,bool reset) {
    census_enabled=enabled;
    if(reset)for(auto& n:natives)if(n) {
        n->floor_census.clear();n->entry_census.clear();n->read_census.clear();n->write_census.clear();n->census_dropped=0;
    }
}
std::string nds_arm9_native_compilation_json(bool reset) {
    if(reset)for(auto& native:natives)if(native) {
        // Reset diagnostics without discarding warmed executable code.
        native->compiled_locations.clear();native->compiled_blocks=0;
        native->recompiled_blocks=0;native->compile_census_dropped=0;
    }
    std::string out="{\"enabled\":"+std::string(compilation_census()?"true":"false");
    for(const auto cpu:{NDS_ARM9,NDS_ARM7}) {
        out+=cpu==NDS_ARM9?",\"arm9\":[":",\"arm7\":[";
        if(const auto& native=natives[cpu]) {
            std::vector<uint64_t> locations(native->compiled_locations.begin(),native->compiled_locations.end());
            std::sort(locations.begin(),locations.end());
            bool comma=false;
            for(const auto descriptor:locations) {
                const Dynarmic::A32::LocationDescriptor location{Dynarmic::IR::LocationDescriptor{descriptor}};
                if(comma)out+=",";
                comma=true;
                out+="{\"pc\":"+std::to_string(location.PC())+
                    ",\"thumb\":"+(location.TFlag()?"true":"false")+
                    ",\"descriptor\":\""+std::to_string(descriptor)+"\"}";
            }
        }
        out+="]";
    }
    return out+"}";
}
static std::string native_census_json(const Native* n,int cpu) {
    std::string out;
    const auto pcs=[&](const char* name,const auto& counts,bool instructions) {
        out+=std::string(",\"")+name+"\":[";bool comma=false;
        for(const auto& [key,hits]:counts) {
            const uint32_t pc=uint32_t(key>>1);const bool thumb=(key&1)!=0;
            if(comma)out+=",";
            comma=true;
            out+="{\"pc\":"+std::to_string(pc)+",\"thumb\":"+std::to_string(thumb)+",\"hits\":"+std::to_string(hits);
            if(instructions) {
                uint32_t word=0;const bool valid=bus_debug_copy(cpu,pc&~3u,reinterpret_cast<uint8_t*>(&word),4);
                out+=",\"bytes_valid\":"+std::string(valid?"true":"false");
                if(valid) {
                    const auto i=thumb?armv4t::ThumbDecoder::decode(uint16_t(word>>((pc&2)*8)),pc):armv4t::ArmDecoder::decode(word,pc);
                    out+=",\"word\":"+std::to_string(word)+",\"op\":"+std::to_string(unsigned(i.op));
                }
            }
            out+="}";
        }
        out+="]";
    };
    if(!n)return ",\"reference_census\":[],\"entry_census\":[],\"read_census\":[],\"write_census\":[]";
    pcs("reference_census",n->floor_census,true);pcs("entry_census",n->entry_census,false);
    for(const auto& pair:{std::pair{"read_census",&n->read_census},std::pair{"write_census",&n->write_census}}) {
        out+=std::string(",\"")+pair.first+"\":[";bool comma=false;
        for(const auto& [key,hits]:*pair.second) {
            if(comma)out+=",";
            comma=true;
            out+="{\"pc\":"+std::to_string(uint32_t(key>>32)&~1u)+",\"thumb\":"+std::to_string((key>>32)&1u)+
                ",\"address\":"+std::to_string(uint32_t(key))+",\"hits\":"+std::to_string(hits)+"}";
        }
        out+="]";
    }
    return out+",\"census_dropped\":"+std::to_string(n->census_dropped);
}
std::string nds_arm9_native_json() {
    const auto& native=natives[NDS_ARM9];
    uint64_t windows=0,fences=0;scheduler_performance_window_counts(&windows,&fences);
    std::string out="{\"available\":"+std::string(supported_host()?"true":"false")+
        ",\"diagnostic\":"+std::string(diagnostic()?"true":"false")+
        ",\"census_enabled\":"+std::string(census_enabled?"true":"false")+
        ",\"compile_census_enabled\":"+std::string(compilation_census()?"true":"false")+
        ",\"batched_writes\":"+std::string(batched_writes()?"true":"false")+
        ",\"direct_reads\":"+(nds_native_options().reads?"true":"false")+
        ",\"narrow_reads\":true"+
        ",\"long_blocks\":"+(nds_native_options().blocks?"true":"false")+
        ",\"data_handoffs\":"+(nds_native_options().data?"true":"false")+
        ",\"gx_batch\":"+(nds_native_options().gx?"true":"false")+
        ",\"event_windows\":"+(nds_native_options().events?"true":"false")+
        ",\"event_window_rounds\":"+std::to_string(windows)+
        ",\"event_window_fences\":"+std::to_string(fences)+
        ",\"cycles_per_instruction\":"+std::to_string(cycles_per_instruction);
#if defined(NDS_NATIVE_PRECISE_WRITES)
    out+=",\"precise_ram_writes\":true,\"physical_write_publication\":true";
#else
    out+=",\"precise_ram_writes\":false,\"physical_write_publication\":false";
#endif
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
        ",\"compiled_blocks\":"+std::to_string(native->compiled_blocks)+
        ",\"recompiled_blocks\":"+std::to_string(native->recompiled_blocks)+
        ",\"compile_census_dropped\":"+std::to_string(native->compile_census_dropped)+
        ",\"unknown_write_checks\":"+std::to_string(native->watches.checks)+
        ",\"watched_pages\":"+std::to_string(native->watches.size())+
        ",\"write_notifications\":"+std::to_string(native->write_notifications)+
        ",\"deferred_notifications\":"+std::to_string(native->deferred_notifications)+
        ",\"write_batches\":"+std::to_string(native->write_batches)+
        ",\"precise_notifications\":"+std::to_string(native->precise_notifications)+
        ",\"precise_code_hits\":"+std::to_string(native->precise_code_hits)+
        ",\"write_checks_arm9\":"+std::to_string(native->checks_by_cpu[0])+
        ",\"write_checks_arm7\":"+std::to_string(native->checks_by_cpu[1]);
    out+=native_census_json(native.get(),9);
    const auto& arm7=natives[NDS_ARM7];
    out+=",\"arm7\":{\"available\":";
#if defined(NDS_ARM7_NATIVE)
    out+=supported_host()?"true":"false";
#else
    out+="false";
#endif
    if(arm7)out+=",\"entries\":"+std::to_string(arm7->entries)+
        ",\"invalidations\":"+std::to_string(arm7->invalidations)+
        ",\"compiled_blocks\":"+std::to_string(arm7->compiled_blocks)+
        ",\"recompiled_blocks\":"+std::to_string(arm7->recompiled_blocks)+
        ",\"compile_census_dropped\":"+std::to_string(arm7->compile_census_dropped)+
        ",\"instructions\":"+std::to_string(arm7->instructions)+
        ",\"reference_stops\":"+std::to_string(arm7->floors)+
        ",\"device_handoffs\":"+std::to_string(arm7->devices)+
        ",\"watched_pages\":"+std::to_string(arm7->watches.size())+
        ",\"unknown_write_checks\":"+std::to_string(arm7->watches.checks);
    out+=native_census_json(arm7.get(),7)+"}";
    return out+"}";
}
#endif
