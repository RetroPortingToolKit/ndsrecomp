// Execute real ARM/Thumb stores through the SDK and compare the architectural
// result with the independent portable interpreter, including publication.
#include "arm_decode.h"
#include "thumb_decode.h"
#include "interpreter.h"
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/frontend/A32/a32_ir_emitter.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
constexpr uint32_t pc=0x1000;
void require(bool value,const char* text) {
    if(!value){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}
}
struct Ram final : armv4t::Bus {
    std::array<uint8_t,65536> data{},written{};
    std::array<uint32_t,16> generation{};
    uint64_t epoch=21;
    unsigned alias=4;
    uint32_t physical(uint32_t a) const {
        if(a>=0x8000&&a<0x9000)a=alias*4096+(a&4095);
        require(a<data.size(),"synthetic memory range");return a;
    }
    uint8_t read8(uint32_t a) override {return data[physical(a)];}
    uint16_t read16(uint32_t a) override {return uint16_t(read8(a))|uint16_t(read8(a+1))<<8;}
    uint32_t read32(uint32_t a) override {return uint32_t(read16(a))|uint32_t(read16(a+2))<<16;}
    void publish(uint32_t a,unsigned n) {
        const uint32_t first=physical(a),last=physical(a+n-1);
        for(uint32_t p=first>>12;p<=last>>12;++p)if(++generation[p]==0)generation[p]=1;
        ++epoch;for(unsigned i=0;i<n;++i)written[physical(a+i)]=1;
    }
    void write8(uint32_t a,uint8_t v) override {data[physical(a)]=v;publish(a,1);}
    void write16(uint32_t a,uint16_t v) override {
        a&=~1u;for(unsigned i=0;i<2;++i)data[physical(a+i)]=uint8_t(v>>(i*8));publish(a,2);
    }
    void write32(uint32_t a,uint32_t v) override {
        a&=~3u;for(unsigned i=0;i<4;++i)data[physical(a+i)]=uint8_t(v>>(i*8));publish(a,4);
    }
};
uint32_t flags(const armv4t::CPUState& s) {
    return uint32_t(s.cpsr.n)<<31|uint32_t(s.cpsr.z)<<30|uint32_t(s.cpsr.c)<<29|
        uint32_t(s.cpsr.v)<<28|uint32_t(s.cpsr.q)<<27|uint32_t(s.cpsr.i)<<7|
        uint32_t(s.cpsr.f)<<6|uint32_t(s.cpsr.t)<<5|s.cpsr.mode;
}
struct Environment final : Dynarmic::A32::UserCallbacks {
    Ram ram;
    using Page=Dynarmic::PublishedMemoryWrites::Page;
    std::array<Page,16> descriptors{};
    std::unique_ptr<std::array<Page*,1u<<20>> pages=std::make_unique<std::array<Page*,1u<<20>>();
    uint8_t enabled=1;
    uint64_t transactions=0,callbacks=0,remaining=1;
    bool fault=false;
    Dynarmic::A32::Jit* jit=nullptr;
    Dynarmic::PublishedMemoryWrites publication{pages->data(),&enabled,&ram.epoch,&transactions};
    void refresh(bool no_pages=false) {
        pages->fill(nullptr);
        for(unsigned i=0;i<16;++i) {
            descriptors[i]={ram.data.data()+i*4096,ram.written.data()+i*4096,&ram.generation[i]};
            if(!no_pages)(*pages)[i]=&descriptors[i];
        }
        if(!no_pages)(*pages)[8]=&descriptors[ram.alias];
    }
    std::optional<uint32_t> MemoryReadCode(uint32_t a) override {
        if(a+3>=ram.data.size())return std::nullopt;
        const auto* generation=&ram.generation[ram.physical(a)>>12];
        for(unsigned i=0;i<16;++i)
            if((*pages)[i]&&(*pages)[i]->generation==generation)(*pages)[i]=nullptr;
        return ram.read32(a);
    }
    uint8_t MemoryRead8(uint32_t a) override {return ram.read8(a);}
    uint16_t MemoryRead16(uint32_t a) override {return ram.read16(a&~1u);}
    uint32_t MemoryRead32(uint32_t a) override {return std::rotr(ram.read32(a&~3u),int((a&3)*8));}
    uint64_t MemoryRead64(uint32_t a) override {return uint64_t(MemoryRead32(a))|uint64_t(MemoryRead32(a+4))<<32;}
    void code_written(uint32_t a) {if((ram.physical(a)>>12)==(pc>>12))jit->ClearCache();}
    void MemoryWrite8(uint32_t a,uint8_t v) override {++callbacks;ram.write8(a,v);code_written(a);}
    void MemoryWrite16(uint32_t a,uint16_t v) override {++callbacks;ram.write16(a,v);code_written(a&~1u);}
    void MemoryWrite32(uint32_t a,uint32_t v) override {++callbacks;ram.write32(a,v);code_written(a&~3u);}
    void MemoryWrite64(uint32_t a,uint64_t v) override {MemoryWrite32(a,uint32_t(v));MemoryWrite32(a+4,uint32_t(v>>32));}
    void InterpreterFallback(uint32_t,size_t) override {fault=true;jit->HaltExecution();}
    void CallSVC(uint32_t) override {fault=true;jit->HaltExecution();}
    void ExceptionRaised(uint32_t,Dynarmic::A32::Exception) override {fault=true;jit->HaltExecution();}
    void AddTicks(uint64_t n) override {remaining=n<remaining?remaining-n:0;}
    uint64_t GetTicksRemaining() override {return remaining;}
};
struct Instruction {uint32_t bits;bool thumb;bool stack;};
}

int main() {
    unsigned checks=0;
    for(auto architecture:{Dynarmic::A32::ArchVersion::v4T,Dynarmic::A32::ArchVersion::v5TE}) {
        Environment env;
        Dynarmic::A32::UserConfig conf;conf.callbacks=&env;conf.arch_version=architecture;
        conf.code_cache_size=8*1024*1024;conf.published_memory_writes=&env.publication;
        conf.check_halt_on_memory_access=true;conf.optimizations=Dynarmic::all_safe_optimizations;
        Dynarmic::A32::Jit jit(conf);env.jit=&jit;
        for(const auto instruction:{Instruction{0xe5801000u,false,false}, // STR
                 {0xe5c01000u,false,false},{0xe1c010b0u,false,false}, // STRB/STRH
                 {0xe8a0001eu,false,false}, // STMIA r0!,{r1-r4}
                 {0x6001u,true,false},{0x7001u,true,false},{0x8001u,true,false},
                 {0xc01eu,true,false},{0xb41eu,true,true}}) {
            for(uint32_t target:{0x4000u,0x4001u,0x4ffeu,0x8000u,0x8ffcu,pc}) {
                for(unsigned mode=0;mode<5;++mode) {
                    Ram reference;
                    for(unsigned i=0;i<reference.data.size();++i) {
                        reference.data[i]=uint8_t(i*37+5);
                        constexpr uint8_t provenance[]={0,1,2,127,128,255};
                        reference.written[i]=provenance[i%6];
                    }
                    reference.generation.fill(mode==3?UINT32_MAX:9);
                    for(unsigned i=0;i<(instruction.thumb?2u:4u);++i)
                        reference.data[pc+i]=uint8_t(instruction.bits>>(i*8));
                    env.ram=reference;env.transactions=env.callbacks=0;env.remaining=1;env.fault=false;
                    env.enabled=mode!=1;env.refresh(mode==2);
                    armv4t::CPUState state{};state.R[15]=pc;state.R[0]=target;
                    for(unsigned i=1;i<15;++i)state.R[i]=0xa37b190du*i;
                    state.R[13]=target+16;state.cpsr.mode=0x1f;state.cpsr.t=instruction.thumb;
                    state.cpsr.n=true;state.cpsr.c=true;state.cpsr.q=true;
                    jit.ClearCache();jit.ClearHalt(Dynarmic::HaltReason::UserDefined1|Dynarmic::HaltReason::MemoryAbort);
                    std::copy(std::begin(state.R),std::end(state.R),jit.Regs().begin());jit.SetCpsr(flags(state));
                    if(mode==4) {
                        const auto location=Dynarmic::A32::LocationDescriptor{pc,Dynarmic::A32::PSR{flags(state)},Dynarmic::A32::FPSCR{0}}.UniqueHash();
                        require(jit.PrepareBlock(location),"prepare mapping test");
                        env.ram.alias=reference.alias=5;(*env.pages)[8]=&env.descriptors[5];
                    }
                    auto expected=state;
                    const auto decoded=instruction.thumb?armv4t::ThumbDecoder::decode(uint16_t(instruction.bits),pc):armv4t::ArmDecoder::decode(instruction.bits,pc);
                    const auto result=armv4t::Interpreter::step(expected,reference,decoded);
                    require(result==armv4t::Interpreter::Result::Normal,"independent interpreter store");
                    jit.Step();
                    if(env.fault||!std::equal(std::begin(expected.R),std::end(expected.R),jit.Regs().begin())||
                       flags(expected)!=jit.Cpsr()||reference.data!=env.ram.data||
                       reference.written!=env.ram.written||reference.generation!=env.ram.generation||
                       reference.epoch!=env.ram.epoch) {
                        std::fprintf(stderr,"case arch=%u op=%08x thumb=%u address=%08x mode=%u fast=%llu callbacks=%llu\n",
                            unsigned(architecture),instruction.bits,unsigned(instruction.thumb),target,mode,
                            (unsigned long long)env.transactions,(unsigned long long)env.callbacks);
                        require(false,"architectural state/data/provenance/generation/epoch differ");
                    }
                    if(mode==1||mode==2)require(env.transactions==0,"disabled/protected path used inline store");
                    if(mode==0&&target==0x4000)require(env.transactions>0&&env.callbacks==0,"eligible ARM/Thumb store did not use generated transaction");
                    if(target==pc&&!instruction.stack)require(env.callbacks>0,"executable code write bypassed callback");
                    ++checks;
                }
            }
        }
    }
    std::printf("published_store_probe: %u ARM/Thumb cases, data/registers/flags/provenance/generations/epochs, wrap/disable/protection/remap/self-modification passed\n",checks);
}
