// Bounded architectural probe, not linked into the game runner. Synthetic code
// and the independent portable interpreter check candidate ARM9 translation.
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <memory>
#include <vector>
#include "arm_decode.h"
#include "thumb_decode.h"
#include "interpreter.h"
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/frontend/A32/a32_ir_emitter.h"
#include "dynarmic/ir/terminal.h"
#include "../src/arm9_native_ir.h"
#include "../src/arm9_native_watch.h"
#include "../src/arm9_native_options.h"

namespace {
#if defined(NDS_ARM7_NATIVE)
constexpr auto architecture=Dynarmic::A32::ArchVersion::v4T;
#else
constexpr auto architecture=Dynarmic::A32::ArchVersion::v5TE;
#endif
constexpr uint32_t code_pc=0x100;
void require(bool ok,const char* what) { if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);} }
struct Ram final : armv4t::Bus {
    std::array<uint8_t,65536> data{};
    uint8_t read8(uint32_t a) override {require(a<data.size(),"read8 range");return data[a];}
    uint16_t read16(uint32_t a) override {return uint16_t(read8(a))|uint16_t(read8(a+1))<<8;}
    uint32_t read32(uint32_t a) override {return uint32_t(read16(a))|uint32_t(read16(a+2))<<16;}
    void write8(uint32_t a,uint8_t v) override {require(a<data.size(),"write8 range");data[a]=v;}
    void write16(uint32_t a,uint16_t v) override {write8(a,uint8_t(v));write8(a+1,uint8_t(v>>8));}
    void write32(uint32_t a,uint32_t v) override {write16(a,uint16_t(v));write16(a+2,uint16_t(v>>16));}
};
uint32_t flags(const armv4t::CPUState& s) {
    return uint32_t(s.cpsr.n)<<31|uint32_t(s.cpsr.z)<<30|uint32_t(s.cpsr.c)<<29|
        uint32_t(s.cpsr.v)<<28|uint32_t(s.cpsr.q)<<27|uint32_t(s.cpsr.i)<<7|
        uint32_t(s.cpsr.f)<<6|uint32_t(s.cpsr.t)<<5|s.cpsr.mode;
}
struct Environment final : Dynarmic::A32::UserCallbacks {
    Ram ram;
    Dynarmic::A32::Jit* jit=nullptr;
    uint64_t remaining=0,ticks=0;
    uint32_t forced_floor=0,floor_pc=0;
    size_t floors=0;
    uint64_t rejected_reads=0,rejected_writes=0;
    bool complete_io=false;
    bool bounded_memory=false;
    bool longer_blocks=false;
    unsigned ram_reads=0,ram_writes=0;
    bool isa_guard=false;
    bool watch_code=false,code_alias=false,watch_io_dma=false,watch_immediate=false;
    std::array<uint32_t,16> generations{};
    NdsArm9GenerationWatch watches;
    uint32_t device_value=0;
    unsigned device_reads=0,device_writes=0;
    unsigned device_read8=0,device_read16=0;
    std::optional<uint32_t> MemoryReadCode(uint32_t a) override {
        if(code_alias&&a>=0x2000&&a<0x3000)a-=0x2000;
        if(watch_code)watches.add(&generations[a>>12]);
        return ram.read32(a);
    }
    bool PreCodeReadHook(bool thumb,uint32_t pc,Dynarmic::A32::IREmitter& ir) override {
        if(forced_floor==pc){ir.SetTerm(Dynarmic::IR::Term::Interpret{ir.current_location});return false;}
        if(isa_guard&&thumb&&!nds_arm9_thumb_native_encoding(ram.read16(pc),ram.read16(pc+2),true)) {
            ir.SetTerm(Dynarmic::IR::Term::Interpret{ir.current_location});return false;
        }
        const auto start=Dynarmic::A32::LocationDescriptor{ir.block.Location()}.PC();
        bool end=complete_io&&pc==code_pc+4&&pc!=start;
        if(bounded_memory&&pc!=start) {
            const auto width=thumb?2u:4u;
            const auto prev=thumb?armv4t::ThumbDecoder::decode(ram.read16(pc-width),pc-width):armv4t::ArmDecoder::decode(ram.read32(pc-width),pc-width);
            end=pc-start>=nds_native_block_limit(longer_blocks)*width||(prev.op>=armv4t::IrOp::LDR&&prev.op<=armv4t::IrOp::STM);
        }
        if(end) {
            // The controlled fixture's first instruction accesses memory.
            // Commit its next PC, then stop if the device requested a handoff.
            nds_arm9_finish_memory(ir);ir.BranchWritePC(ir.Imm32(pc));
            ir.SetTerm(Dynarmic::IR::Term::CheckHalt{Dynarmic::IR::Term::LinkBlock{ir.current_location}});
            return false;
        }
        return true;
    }
    bool permitted(uint32_t a,unsigned n,bool write) {
        if(uint64_t(a)+n<=ram.data.size())return true;
        if(write)++rejected_writes;else ++rejected_reads;
        // No modeled device action has happened. Probe whether this provides
        // a restartable boundary; do not assume a generic abort is sufficient.
        jit->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
        return false;
    }
    uint8_t MemoryRead8(uint32_t a) override {
        ++ram_reads;
        if(complete_io&&a>=0x04000000u){require(jit->Regs()[15]==code_pc,"byte callback PC");++device_reads;++device_read8;jit->HaltExecution();return uint8_t(device_value>>((a&1u)*8));}
        return permitted(a,1,false)?ram.read8(a):0;
    }
    uint16_t MemoryRead16(uint32_t a) override {
        ++ram_reads;
        if(complete_io&&a>=0x04000000u){require(jit->Regs()[15]==code_pc,"half callback PC");++device_reads;++device_read16;jit->HaltExecution();return uint16_t(device_value);}
        return permitted(a,2,false)?ram.read16(a&~1u):0;
    }
    uint32_t MemoryRead32(uint32_t a) override {
        ++ram_reads;
        const auto kind=NdsArm9ReadKind(jit->ExtRegs()[0]);
        if(kind==NdsArm9ReadKind::HalfUnsigned||kind==NdsArm9ReadKind::HalfSigned)
            return nds_arm9_read_halfword(a,kind==NdsArm9ReadKind::HalfSigned,
                [this](uint32_t x){return MemoryRead8(x);},[this](uint32_t x){return MemoryRead16(x);});
        if(complete_io&&a>=0x04000000u){require(jit->Regs()[15]==code_pc,"read callback PC survives optimization");++device_reads;jit->HaltExecution();return device_value;}
        return permitted(a,4,false)?std::rotr(ram.read32(a&~3u),int((a&3)*8)):0;
    }
    uint64_t MemoryRead64(uint32_t a) override {return uint64_t(MemoryRead32(a))|uint64_t(MemoryRead32(a+4))<<32;}
    void MemoryWrite8(uint32_t a,uint8_t v) override {if(permitted(a,1,true))ram.write8(a,v);}
    void MemoryWrite16(uint32_t a,uint16_t v) override {if(permitted(a,2,true))ram.write16(a&~1u,v);}
    void MemoryWrite32(uint32_t a,uint32_t v) override {
        ++ram_writes;
        if(complete_io&&a>=0x04000000u){
            require(jit->Regs()[15]==code_pc,"write callback PC survives optimization");
            ++device_writes;device_value=v;jit->HaltExecution();
            if(watch_io_dma) {
                require(nds_arm9_can_defer_generation_check(true,true),"device halt permits coalescing");
                ram.write32(code_pc+4,0xe3a00007u);++generations[(code_pc+4)>>12];watches.defer();
            }
            return;
        }
        if(permitted(a,4,true)) {
            ram.write32(a&~3u,v);
            if(watch_code&&watch_immediate) {
                ++generations[a>>12];
                if(watches.changed()){jit->ClearCache();watches.clear();}
            }
        }
    }
    void MemoryWrite64(uint32_t a,uint64_t v) override {MemoryWrite32(a,uint32_t(v));MemoryWrite32(a+4,uint32_t(v>>32));}
    void InterpreterFallback(uint32_t pc,size_t n) override {
        floor_pc=pc;floors+=n;jit->HaltExecution();
    }
    void CallSVC(uint32_t) override {require(false,"unexpected SVC in ordinary translation");}
    void ExceptionRaised(uint32_t pc,Dynarmic::A32::Exception e) override {
        std::fprintf(stderr,"unexpected exception pc=%08x word=%08x kind=%u\n",pc,ram.read32(pc),unsigned(e));std::exit(1);
    }
    void AddTicks(uint64_t n) override {ticks+=n;remaining=n<remaining?remaining-n:0;}
    uint64_t GetTicksRemaining() override {return remaining;}
};
void set_state(Dynarmic::A32::Jit& jit,const armv4t::CPUState& state) {
    jit.ClearHalt(Dynarmic::HaltReason::UserDefined1|Dynarmic::HaltReason::MemoryAbort);
    std::copy(std::begin(state.R),std::end(state.R),jit.Regs().begin());jit.SetCpsr(flags(state));
    jit.ExtRegs()[0]=uint32_t(NdsArm9ReadKind::Word);
}
bool equal(const armv4t::CPUState& expected,const Dynarmic::A32::Jit& jit) {
    return std::equal(std::begin(expected.R),std::end(expected.R),jit.Regs().begin())&&flags(expected)==jit.Cpsr();
}
}
int main() {
    Environment env;
    Dynarmic::A32::UserConfig conf;
    conf.callbacks=&env;conf.arch_version=architecture;
    conf.code_cache_size=16*1024*1024;conf.check_halt_on_memory_access=true;
    conf.optimizations=Dynarmic::all_safe_optimizations;
    Dynarmic::A32::Jit jit(conf);env.jit=&jit;
    std::mt19937 random(0x4e445339);
    std::vector<uint32_t> arm;
    for(unsigned op=0;op<16;++op)for(unsigned sh=0;sh<4;++sh) {
        // Test opcodes require S=1; all others also use S to exercise flag paths.
        const unsigned dest=(op>=8&&op<=11)?0u:2u;
        const unsigned first=(op==13||op==15)?0u:1u;
        arm.push_back(0xe0100000u|(op<<21)|(first<<16)|(dest<<12)|(sh<<5)|3u);
        arm.push_back(0xe2100000u|(op<<21)|(first<<16)|(dest<<12)|0xf01u);
    }
    for(uint32_t word:{0xe0020391u,0xe0224391u,0xe16f2f13u,0xe1022053u,
                       0xe1222053u,0xe1422053u,0xe1622053u,0xe5912000u,
                       0xe5812000u,0xe5d12000u,0xe5c12000u,0xe1d120b0u,
                       0xe1c120b0u,0xe8a1000cu,0xe8b1000cu,0xe12fff13u})arm.push_back(word);
    uint64_t cases=0;
    const uint32_t edges[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu,0xfffffffeu,31,32};
    for(uint32_t word:arm)for(unsigned sample=0;sample<24;++sample) {
#if defined(NDS_ARM7_NATIVE)
        if(word==0xe16f2f13u||word==0xe1022053u||word==0xe1222053u||word==0xe1422053u||word==0xe1622053u)continue;
#endif
        armv4t::CPUState original{};
        for(auto& r:original.R)r=random();
        original.R[1]=0x4000+(sample%8)*4;original.R[3]=0x600+(sample&1);
        const auto decoded=armv4t::ArmDecoder::decode(word,code_pc);
        if(!(decoded.op>=armv4t::IrOp::LDR&&decoded.op<=armv4t::IrOp::STM)&&decoded.op!=armv4t::IrOp::BX) {
            original.R[1]=sample<8?edges[sample]:random();
            original.R[3]=sample<8?edges[(sample+3)%8]:random();
        }
        original.R[15]=code_pc;original.cpsr.mode=0x1f;original.cpsr.i=1;
        original.cpsr.n=sample&1;original.cpsr.z=(sample>>1)&1;
        original.cpsr.c=(sample>>2)&1;original.cpsr.v=(sample>>3)&1;
        original.cpsr.q=(sample>>4)&1;
        for(auto& b:env.ram.data)b=uint8_t(random());
        env.ram.write32(code_pc,word);
        Ram reference=env.ram;auto expected=original;
        const auto result=armv4t::Interpreter::step(expected,reference,decoded);
        require(result==armv4t::Interpreter::Result::Normal||result==armv4t::Interpreter::Result::Branched,"reference unsupported ARM case");
        jit.InvalidateCacheRange(code_pc,4);set_state(jit,original);env.remaining=1;jit.Step();
        if(!equal(expected,jit)||reference.data!=env.ram.data){
            std::fprintf(stderr,"ARM mismatch word=%08x sample=%u cpsr expected=%08x actual=%08x\n",word,sample,flags(expected),jit.Cpsr());
            for(unsigned r=0;r<16;++r)if(expected.R[r]!=jit.Regs()[r])std::fprintf(stderr,"r%u %08x != %08x\n",r,expected.R[r],jit.Regs()[r]);
            return 1;
        }
        ++cases;
    }
    for(uint16_t hw:{0x00cau,0x08cau,0x10cau,0x184au,0x1a4au,0x2255u,
                     0x3255u,0x3a55u,0x428au,0x434au,0x43cau,0x400au,
                     0x680au,0x600au,0x880au,0x800au,0x780au,0x700au,0x4718u}) {
        for(unsigned sample=0;sample<24;++sample) {
            armv4t::CPUState original{};
            for(auto& r:original.R)r=random();
            original.R[1]=0x4000;original.R[3]=0x600+(sample&1);original.R[15]=code_pc;
            original.cpsr.mode=0x1f;original.cpsr.i=1;original.cpsr.t=true;original.thumb=true;
            original.cpsr.c=(sample>>2)&1;original.cpsr.v=(sample>>3)&1;
            env.ram.write32(code_pc,hw);Ram reference=env.ram;auto expected=original;
            auto result=armv4t::Interpreter::step(expected,reference,armv4t::ThumbDecoder::decode(hw,code_pc));
            require(result==armv4t::Interpreter::Result::Normal||result==armv4t::Interpreter::Result::Branched,"reference unsupported Thumb case");
            jit.InvalidateCacheRange(code_pc,4);set_state(jit,original);env.remaining=1;jit.Step();
            if(!equal(expected,jit)||reference.data!=env.ram.data){std::fprintf(stderr,"Thumb mismatch %04x sample=%u\n",hw,sample);return 1;}
            ++cases;
        }
    }
    require(!env.floors&&!env.rejected_reads&&!env.rejected_writes,"ordinary cases unexpectedly fell back");
    std::printf("ARM9 native translation differential cases passed: %llu\n",static_cast<unsigned long long>(cases));
    env.bounded_memory=true;
    const uint32_t half_arm[]={0xe1d120b0u,0xe1d120f0u,0xe19120b3u,0xe19120f3u,
        0xe1f120b2u,0xe1f120f2u,0xe0d120b2u,0xe0d120f2u,0xe15120b2u,0xe15120f2u,
        0x11d120b0u,0x11d120f0u,0xe1df20b0u,0xe1df20f0u};
    uint64_t half_cases=0;
    for(bool thumb:{false,true}) {
        const unsigned count=thumb?3:unsigned(std::size(half_arm));
        for(unsigned instruction=0;instruction<count;++instruction)for(unsigned sample=0;sample<32;++sample) {
            const uint32_t word=thumb?uint32_t(std::array<uint16_t,3>{0x880au,0x5a0au,0x5e0au}[instruction]):half_arm[instruction];
            armv4t::CPUState original{};
            for(auto& r:original.R)r=random();
            original.R[0]=0;original.R[1]=0x4000+(sample&3);original.R[3]=0;original.R[15]=code_pc;
            original.cpsr.mode=0x1f;original.cpsr.t=thumb;original.thumb=thumb;
            original.cpsr.n=sample&1;original.cpsr.z=(sample>>1)&1;original.cpsr.c=(sample>>2)&1;original.cpsr.v=(sample>>3)&1;
            for(unsigned at=0x3ff0;at<0x4010;++at)env.ram.data[at]=uint8_t(random());
            env.ram.write32(code_pc,word);env.ram.write32(code_pc+4,0xeafffffeu);
            Ram reference=env.ram;auto expected=original;
            const auto decoded=thumb?armv4t::ThumbDecoder::decode(uint16_t(word),code_pc):armv4t::ArmDecoder::decode(word,code_pc);
            require(armv4t::Interpreter::step(expected,reference,decoded)==armv4t::Interpreter::Result::Normal,"halfword reference case");
            jit.InvalidateCacheRange(code_pc,8);set_state(jit,original);env.remaining=1;jit.Run();
            if(!equal(expected,jit)||reference.data!=env.ram.data) {
                std::fprintf(stderr,"Halfword mismatch %08x T=%u sample=%u result %08x != %08x\n",word,unsigned(thumb),sample,expected.R[2],jit.Regs()[2]);return 1;
            }
            ++half_cases;
        }
    }
    env.bounded_memory=false;
    std::printf("ARM9 halfword native addressing/alignment/predication cases passed: %llu\n",static_cast<unsigned long long>(half_cases));
    // Does an I/O abort really leave a restartable instruction? This is a
    // research result, not a passing assertion about an unimplemented adapter.
    for(uint32_t word:{0xe5912000u,0xe4912004u,0xe5b12004u,0xe4812004u}) {
        armv4t::CPUState original{};original.R[1]=0x04000000u;original.R[2]=0x12345678;
        original.R[15]=code_pc;original.cpsr.mode=0x1f;
        env.ram.write32(code_pc,word);jit.InvalidateCacheRange(code_pc,4);
        set_state(jit,original);env.remaining=1;jit.Step();
        std::printf("IO_BOUNDARY word=%08x pc=%08x base=%08x dest=%08x restartable=%s\n",
            word,jit.Regs()[15],jit.Regs()[1],jit.Regs()[2],equal(original,jit)?"yes":"NO");
    }
    // A privileged-instruction stop must return before executing it. Host
    // banking/CP15 handling can then run outside the non-recursive JIT call.
    env.forced_floor=code_pc;env.ram.write32(code_pc,0xe121f000u);
    jit.InvalidateCacheRange(code_pc,4);
    armv4t::CPUState original{};original.R[15]=code_pc;original.cpsr.mode=0x1f;
    set_state(jit,original);env.remaining=1;jit.Run();
    require(env.floor_pc==code_pc&&env.floors==1&&equal(original,jit),"privileged stop contract");
    env.forced_floor=0;env.complete_io=true;jit.ClearCache();
    for(uint32_t word:{0xe5912000u,0xe4912004u,0xe5b12004u,0xe4812004u}) {
        armv4t::CPUState initial{};initial.R[1]=0x04000000u;initial.R[2]=0x12345678u;
        initial.R[3]=37;initial.R[15]=code_pc;initial.cpsr.mode=0x1f;
        auto expected=initial;expected.R[15]+=4;
        const bool write=word==0xe4812004u;
        if(word!=0xe5912000u)expected.R[1]+=4;
        if(!write)expected.R[2]=0x89abcdefu;
        env.device_value=0x89abcdefu;env.device_reads=env.device_writes=0;
        env.ram.write32(code_pc,word);env.ram.write32(code_pc+4,0xe2833001u);
        env.ram.write32(code_pc+8,0xeafffffeu);
        jit.InvalidateCacheRange(code_pc,12);set_state(jit,initial);env.remaining=100;
        jit.Run();
        require(equal(expected,jit),"completed I/O handoff register state/PC");
        require(env.device_reads==unsigned(!write)&&env.device_writes==unsigned(write),"device operation exactly once");
        require(env.device_value==(write?0x12345678u:0x89abcdefu),"device outcome");
        const unsigned operations=env.device_reads+env.device_writes;
        jit.ClearHalt();env.remaining=1;jit.Step();
        require(jit.Regs()[3]==38&&jit.Regs()[15]==code_pc+8,"resumed after completed I/O");
        require(operations==env.device_reads+env.device_writes,"device operation was replayed");
    }
    std::puts("COMPLETED_IO_HANDOFF passed: four addressing forms, device once, following instruction waits");
    for(bool sign:{false,true})for(unsigned odd=0;odd<2;++odd) {
        armv4t::CPUState state{};state.R[1]=0x04000000u+odd;state.R[2]=0x12345678u;state.R[15]=code_pc;state.cpsr.mode=0x1f;
        env.ram.write32(code_pc,sign?0xe1d120f0u:0xe1d120b0u);env.ram.write32(code_pc+4,0xeafffffeu);
        env.device_value=0xa1b2;env.device_reads=env.device_writes=env.device_read8=env.device_read16=0;
        jit.InvalidateCacheRange(code_pc,8);set_state(jit,state);env.remaining=100;jit.Run();
        const uint32_t expected=sign?(odd?0xffffffa1u:0xffffa1b2u):(odd?0xb20000a1u:0xa1b2u);
        require(jit.Regs()[2]==expected&&jit.Regs()[15]==code_pc+4,"halfword I/O result and completed PC");
        require(env.device_reads==1&&env.device_writes==0,"one halfword device operation");
        require(env.device_read8==unsigned(sign&&odd)&&env.device_read16==unsigned(!(sign&&odd)),"halfword device read width");
    }
    std::puts("HALFWORD_IO_WIDTH passed: signed odd byte access, other halfword access, operation once");
    env.complete_io=false;env.forced_floor=0;jit.ClearCache();
    env.ram.write32(0x200,0xe3a00000u); // mov r0,#0
    env.ram.write32(0x204,0xe2800001u); // add r0,r0,#1
    env.ram.write32(0x208,0xeafffffdu); // b 0x204
    armv4t::CPUState loop{};loop.R[15]=0x200;loop.cpsr.mode=0x1f;
    set_state(jit,loop);env.remaining=32;const auto ticks_before=env.ticks;
    jit.Run();
    require(env.ticks-ticks_before>=32&&env.ticks-ticks_before<=34,"native loop obeys work budget");
    require(jit.Regs()[0]>=15&&jit.Regs()[0]<=17,"native loop completed real instructions");
    std::printf("NATIVE_LOOP_BUDGET passed: ticks=%llu additions=%u\n",static_cast<unsigned long long>(env.ticks-ticks_before),jit.Regs()[0]);
    // Real boot failed at a leading conditional instruction when context IR was
    // inserted before predication. Exercise a conditional store/back edge with
    // the production post-translation stamping helper and a bounded work budget.
    env.bounded_memory=true;jit.ClearCache();
    env.ram.write32(0x300,0xe3a00004u); // mov r0,#4
    env.ram.write32(0x304,0xe2500001u); // subs r0,r0,#1
    env.ram.write32(0x308,0x15810000u); // strne r0,[r1]
    env.ram.write32(0x30c,0x1afffffcu); // bne 0x304
    env.ram.write32(0x310,0xeafffffeu); // b .
    loop={};loop.R[15]=0x300;loop.R[1]=0x4000;loop.cpsr.mode=0x1f;
    set_state(jit,loop);env.remaining=32;const auto conditional_before=env.ticks;
    jit.Run();
    require(jit.Regs()[0]==0&&env.ram.read32(0x4000)==1,"conditional memory/flags result");
    require(env.ticks-conditional_before>=32&&env.ticks-conditional_before<=35,"conditional block cannot become zero-work self loop");
    std::puts("CONDITIONAL_MEMORY_BUDGET passed: no zero-work self link, last conditional store preserved");
    require(nds_arm9_thumb_native_encoding(0xf000,0xf800,true)&&
        nds_arm9_thumb_native_encoding(0xf7ff,0xeffe,true)&&
        !nds_arm9_thumb_native_encoding(0xf800,0xffff,true)&&
        !nds_arm9_thumb_native_encoding(0xe800,0xf800,true)&&
        !nds_arm9_thumb_native_encoding(0xf000,0xf800,false),"ARMv5 Thumb pair encoding gate");
    env.bounded_memory=false;env.isa_guard=true;jit.ClearCache();
    env.ram.write32(code_pc,0xfffff800u);
    armv4t::CPUState suffix{};suffix.R[14]=0x800;suffix.R[15]=code_pc;
    suffix.cpsr.mode=0x1f;suffix.cpsr.t=true;suffix.thumb=true;
    const auto old_floors=env.floors;set_state(jit,suffix);env.remaining=10;jit.Run();
    require(env.floors==old_floors+1&&env.floor_pc==code_pc&&equal(suffix,jit),"standalone Thumb suffix stops before newer decoding");
    std::puts("ARMV5_THUMB_RESUME passed: paired calls accepted, standalone suffix returns to faithful step");
    // Exercise the production physical-generation set with real cached code,
    // including cross-CPU writes, aliases, and nested device/DMA modifications.
    env.isa_guard=false;env.bounded_memory=false;env.complete_io=false;
    env.watch_code=true;env.generations.fill(1);env.watches.clear();jit.ClearCache();
    env.ram.write32(code_pc,0xe3a00001u);env.ram.write32(code_pc+4,0xeafffffeu);
    armv4t::CPUState watched{};watched.R[15]=code_pc;watched.cpsr.mode=0x1f;
    set_state(jit,watched);env.remaining=16;jit.Run();
    require(jit.Regs()[0]==1&&env.watches.size()==1,"cached initial code watched physically");
    const auto checks_before=env.watches.checks;
    env.ram.write32(code_pc,0xe3a00002u);
    for(unsigned n=0;n<8;++n){++env.generations[0];env.watches.defer();}
    require(env.watches.checks==checks_before,"ARM7 writes do not repeatedly scan");
    require(env.watches.take_pending()&&env.watches.changed(),"batch detects code modification before execution");
    require(env.watches.checks==checks_before+1,"eight writes validated in one physical-page comparison");
    jit.ClearCache();env.watches.clear();
    set_state(jit,watched);env.remaining=16;jit.Run();
    require(jit.Regs()[0]==2,"modified ARM7 code replaces cached instruction");
    ++env.generations[3];env.watches.defer();
    require(env.watches.take_pending()&&!env.watches.changed(),"unwatched data write preserves code cache");
    require(!env.watches.take_pending(),"repeated flush without writes is empty");
    env.code_alias=true;watched.R[15]=0x2000+code_pc;
    set_state(jit,watched);env.remaining=16;jit.Run();
    require(jit.Regs()[0]==2&&env.watches.size()==1,"execution alias shares physical watch");
    env.ram.write32(code_pc,0xe3a00003u);++env.generations[0];env.watches.defer();
    require(env.watches.take_pending()&&env.watches.changed(),"physical write invalidates alias code");
    jit.ClearCache();env.watches.clear();
    set_state(jit,watched);env.remaining=16;jit.Run();
    require(jit.Regs()[0]==3,"alias executes changed bytes");
    env.watches.defer();env.watches.clear();
    require(!env.watches.take_pending()&&env.watches.size()==0,"restore/reset discards deferred products");
    env.code_alias=false;jit.ClearCache();
    env.ram.write32(code_pc,0xe5812000u);env.ram.write32(code_pc+4,0xe3a00001u);
    env.ram.write32(code_pc+8,0xeafffffeu);
    watched={};watched.R[15]=code_pc+4;watched.cpsr.mode=0x1f;
    set_state(jit,watched);env.remaining=16;jit.Run();
    require(jit.Regs()[0]==1,"DMA fixture has stale target cached");
    watched.R[15]=code_pc;watched.R[1]=0x04000000;watched.R[2]=37;
    env.complete_io=true;env.watch_io_dma=true;env.bounded_memory=true;
    set_state(jit,watched);env.remaining=100;jit.Run();
    require(jit.Regs()[15]==code_pc+4&&jit.Regs()[0]==0,"device halt prevents executing modified cached target");
    require(env.watches.take_pending()&&env.watches.changed(),"nested DMA code write validated after completed handoff");
    jit.ClearCache();env.watches.clear();env.complete_io=false;env.watch_io_dma=false;
    jit.ClearHalt();env.remaining=16;jit.Run();
    require(jit.Regs()[0]==7,"resume observes DMA-modified instruction");
    env.ram.write32(code_pc+4,0xe3a00001u);jit.ClearCache();env.watches.clear();
    watched.R[15]=code_pc+4;set_state(jit,watched);env.remaining=16;jit.Run();
    watched.R[15]=code_pc;watched.R[1]=code_pc+4;watched.R[2]=0xe3a00009u;
    env.watch_immediate=true;set_state(jit,watched);env.remaining=100;jit.Run();
    require(jit.Regs()[15]==code_pc+4,"self-modifying native store exits at completed instruction");
    jit.ClearHalt();env.remaining=16;jit.Run();
    require(jit.Regs()[0]==9,"self-modified code executes after immediate invalidation");
    require(!nds_arm9_can_defer_generation_check(true,false)&&
        nds_arm9_can_defer_generation_check(false,false),"unhalted native execution retains immediate validation");
    std::puts("GENERATION_BATCH passed: coalesced ARM7 stores, aliases, data-only writes, restore reset, nested DMA and immediate native self modification");
    // Read-only page-table lowering exercises the actual patched x64 emitter,
    // not a mock memory wrapper. Stores and special ARM9 reads retain callbacks.
    {
        Environment direct;direct.bounded_memory=true;
        using Pages=std::array<uint8_t*,Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>;
        auto pages=std::make_unique<Pages>();pages->fill(nullptr);
        (*pages)[4]=direct.ram.data.data()+0x4000;
        (*pages)[2]=direct.ram.data.data()+0x4000; // physical alias
        Dynarmic::A32::UserConfig config;
        config.callbacks=&direct;config.arch_version=architecture;
        config.page_table=pages.get();config.page_table_read_only=true;
        config.detect_misaligned_access_via_page_table=16|32|64;
        config.page_table_read_filter=[&](uint64_t d) {
            const Dynarmic::A32::LocationDescriptor loc{Dynarmic::IR::LocationDescriptor{d}};
            const auto i=armv4t::ArmDecoder::decode(direct.ram.read32(loc.PC()),loc.PC());
            return i.op==armv4t::IrOp::LDR||i.op==armv4t::IrOp::LDRB||i.op==armv4t::IrOp::LDM;
        };
        Dynarmic::A32::Jit fast(config);direct.jit=&fast;
        direct.ram.write32(0x4000,0x89abcdefu);direct.ram.write32(code_pc+4,0xeafffffeu);
        const auto run=[&](uint32_t instruction,uint32_t address) {
            direct.ram.write32(code_pc,instruction);fast.ClearCache();
            armv4t::CPUState state{};state.R[15]=code_pc;state.R[1]=address;
            state.R[2]=0x11223344;state.cpsr.mode=0x1f;
            set_state(fast,state);direct.remaining=1;direct.ram_reads=direct.ram_writes=0;fast.Run();
        };
        run(0xe5912000u,0x4000);require(fast.Regs()[2]==0x89abcdefu&&!direct.ram_reads,"aligned RAM read avoids callback");
        run(0xe5912000u,0x4001);require(fast.Regs()[2]==0xef89abcdu&&direct.ram_reads==1,"unaligned word rotates through callback");
        run(0xe5912000u,0x2000);require(fast.Regs()[2]==0x89abcdefu&&!direct.ram_reads,"physical read alias");
        std::array<uint8_t,4096> replacement{};uint32_t replaced=0x76543210;
        std::memcpy(replacement.data(),&replaced,4);(*pages)[4]=replacement.data();
        fast.Regs()[15]=code_pc;direct.remaining=1;fast.ClearHalt();fast.Run();
        // The alias used page 2 above; move back to page 4 without retranslating.
        fast.Regs()[1]=0x4000;fast.Regs()[15]=code_pc;direct.remaining=1;fast.ClearHalt();fast.Run();
        require(fast.Regs()[2]==replaced,"remapped read page replaces backing without stale pointer");
        (*pages)[4]=direct.ram.data.data()+0x4000;
        run(0xe5812000u,0x4000);require(direct.ram_writes==1&&direct.ram.read32(0x4000)==0x11223344,"read-only table never bypasses write callback");
        direct.ram.write32(0x4000,0x89abcdefu);
        run(0xe1d120b0u,0x4001);require(fast.Regs()[2]==0xef0000cdu&&direct.ram_reads,"odd unsigned halfword stays on special callback");
        run(0xe1d120f0u,0x4001);require(fast.Regs()[2]==0xffffffcdu&&direct.ram_reads,"odd signed halfword keeps byte semantics");
        (*pages)[4]=nullptr;
        run(0xe5912000u,0x4000);require(fast.Regs()[2]==0x89abcdefu&&direct.ram_reads==1,"unmapped table entry falls back");
        direct.longer_blocks=true;
        for(unsigned n=0;n<64;++n)direct.ram.write32(0x800+n*4,0xe2800001u);
        direct.ram.write32(0x900,0xeafffffeu);fast.ClearCache();
        armv4t::CPUState arithmetic{};arithmetic.R[15]=0x800;arithmetic.cpsr.mode=0x1f;
        set_state(fast,arithmetic);direct.remaining=64;fast.Run();
        require(fast.Regs()[0]==64&&fast.Regs()[15]==0x900,"64-instruction arithmetic block commits real work and stops at budget");
        std::puts("CONSOLIDATION_DIRECT_READS passed: actual emitter, aliases, mapping replacement, write fallback, ARM9 alignment and halfwords");
    }
    require(nds_native_plain_video(0x05000000u,4)&&nds_native_plain_video(0x07fffffcu,4)&&
        !nds_native_plain_video(0x04000240u,1)&&!nds_native_plain_video(0x08000000u,1),"data coalescing excludes mapping/status/MMIO and slot devices");
    require(nds_native_gx_command(0x04000400u,4)&&nds_native_gx_command(0x040005c8u,4)&&
        !nds_native_gx_command(0x04000600u,4)&&!nds_native_gx_command(0x04000400u,2),"GX batching excludes readbacks/control and partial writes");
    require(nds_native_event_quantum(true,true,false,false,false)==256&&
        nds_native_event_quantum(true,false,false,false,false)==64&&
        nds_native_event_quantum(true,true,true,false,false)==64&&
        nds_native_event_quantum(true,true,false,true,false)==64&&
        nds_native_event_quantum(true,true,false,false,true)==64&&
        nds_native_event_quantum(false,true,false,false,false)==64,"event windows require sleeping peer without wake, DMA or geometry stall");
    std::puts("PRIVILEGED_STOP and CONSOLIDATION_FENCES passed; no MKDS timing or performance claim from this probe");
}
