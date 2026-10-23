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
#include <vector>
#include "arm_decode.h"
#include "thumb_decode.h"
#include "interpreter.h"
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/frontend/A32/a32_ir_emitter.h"
#include "dynarmic/ir/terminal.h"

namespace {
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
    uint32_t device_value=0;
    unsigned device_reads=0,device_writes=0;
    std::optional<uint32_t> MemoryReadCode(uint32_t a) override {return ram.read32(a);}
    void PreCodeTranslationHook(bool,uint32_t pc,Dynarmic::A32::IREmitter& ir) override {ir.BranchWritePC(ir.Imm32(pc));}
    bool PreCodeReadHook(bool,uint32_t pc,Dynarmic::A32::IREmitter& ir) override {
        if(forced_floor==pc){ir.SetTerm(Dynarmic::IR::Term::Interpret{ir.current_location});return false;}
        if(complete_io&&pc==code_pc+4&&pc!=Dynarmic::A32::LocationDescriptor{ir.block.Location()}.PC()) {
            // The controlled fixture's first instruction accesses memory.
            // Commit its next PC, then stop if the device requested a handoff.
            ir.BranchWritePC(ir.Imm32(pc));
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
    uint8_t MemoryRead8(uint32_t a) override {return permitted(a,1,false)?ram.read8(a):0;}
    uint16_t MemoryRead16(uint32_t a) override {return permitted(a,2,false)?ram.read16(a&~1u):0;}
    uint32_t MemoryRead32(uint32_t a) override {
        if(complete_io&&a>=0x04000000u){++device_reads;jit->HaltExecution();return device_value;}
        return permitted(a,4,false)?std::rotr(ram.read32(a&~3u),int((a&3)*8)):0;
    }
    uint64_t MemoryRead64(uint32_t a) override {return uint64_t(MemoryRead32(a))|uint64_t(MemoryRead32(a+4))<<32;}
    void MemoryWrite8(uint32_t a,uint8_t v) override {if(permitted(a,1,true))ram.write8(a,v);}
    void MemoryWrite16(uint32_t a,uint16_t v) override {if(permitted(a,2,true))ram.write16(a&~1u,v);}
    void MemoryWrite32(uint32_t a,uint32_t v) override {
        if(complete_io&&a>=0x04000000u){++device_writes;device_value=v;jit->HaltExecution();return;}
        if(permitted(a,4,true))ram.write32(a&~3u,v);
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
}
bool equal(const armv4t::CPUState& expected,const Dynarmic::A32::Jit& jit) {
    return std::equal(std::begin(expected.R),std::end(expected.R),jit.Regs().begin())&&flags(expected)==jit.Cpsr();
}
}
int main() {
    Environment env;
    Dynarmic::A32::UserConfig conf;
    conf.callbacks=&env;conf.arch_version=Dynarmic::A32::ArchVersion::v5TE;
    conf.code_cache_size=16*1024*1024;conf.check_halt_on_memory_access=true;
    conf.optimizations=Dynarmic::all_safe_optimizations&~Dynarmic::OptimizationFlag::GetSetElimination;
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
    std::puts("PRIVILEGED_STOP passed; no MKDS timing or performance claim from this probe");
}
