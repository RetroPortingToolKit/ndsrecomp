#include "hle_mkds_stream.h"
#include "streaming_lz.h"
#include "arm_decode.h"
#include "interpreter.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t entry=0x0214d464, target=0x020f0000, mask=0x3fffff;
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
struct Memory : armv4t::Bus {
    std::vector<uint8_t> bytes=std::vector<uint8_t>(mask+1);
    uint8_t read8(uint32_t a) override { require(a>=0x02000000&&a<0x03000000,"RAM read bounds");return bytes[a&mask]; }
    uint16_t read16(uint32_t a) override { return read8(a)|(uint16_t(read8(a+1))<<8); }
    uint32_t read32(uint32_t a) override { return read16(a)|(uint32_t(read16(a+2))<<16); }
    void write8(uint32_t a,uint8_t v) override { require(a>=0x02000000&&a<0x03000000,"RAM write bounds");bytes[a&mask]=v; }
    void write16(uint32_t a,uint16_t v) override { write8(a,v);write8(a+1,v>>8); }
    void write32(uint32_t a,uint32_t v) override { write16(a,v);write16(a+2,v>>16); }
};
Memory* actual;
bool provenance=true, performance=true;
unsigned publications=0;
uint32_t packed(const armv4t::CPSR& c) {
    return uint32_t(c.mode)|(uint32_t(c.t)<<5)|(uint32_t(c.f)<<6)|(uint32_t(c.i)<<7)|
        (uint32_t(c.q)<<27)|(uint32_t(c.v)<<28)|(uint32_t(c.c)<<29)|(uint32_t(c.z)<<30)|(uint32_t(c.n)<<31);
}
armv4t::CPUState from_host() {
    armv4t::CPUState c{};std::memcpy(c.R,g_cpu.R,sizeof(c.R));auto f=g_cpu.cpsr;
    c.cpsr={bool(f&CPSR_N_BIT),bool(f&CPSR_Z_BIT),bool(f&CPSR_C_BIT),bool(f&CPSR_V_BIT),bool(f&(1u<<27)),bool(f&128),bool(f&64),bool(f&32),uint8_t(f&31)};
    c.thumb=c.cpsr.t;return c;
}
void step(armv4t::CPUState& c,Memory& m) {
    auto pc=c.R[15];require(pc>=entry&&pc<entry+256,"reference escaped routine");
    auto result=armv4t::Interpreter::step(c,m,armv4t::ArmDecoder::decode(m.read32(pc),pc));
    require(result==armv4t::Interpreter::Result::Normal||result==armv4t::Interpreter::Result::Branched,"reference opcode");
}
bool same(const armv4t::CPUState& c) {
    return !std::memcmp(c.R,g_cpu.R,sizeof(c.R))&&packed(c.cpsr)==g_cpu.cpsr;
}
void smoke() {
    struct Bytes {
        std::array<uint8_t,32> src{0x40,'A',0xf0,0x00};
        std::array<uint8_t,8192> dst{};
        uint8_t input(uint32_t p) { return src.at(p); }
        uint8_t output(uint32_t p) { return dst.at(p); }
        void write(uint32_t p,uint8_t v) { dst.at(p)=v; }
    } m;
    nds_lz::State s{0,4,4096,19,0,0,0,0,0,0,31,nds_lz::Phase::Output};
    for(unsigned n=0;s.phase!=nds_lz::Phase::Finish&&n<100;++n)nds_lz::run(s,m,64);
    require(s.phase==nds_lz::Phase::Finish&&!s.remaining&&s.output==4115,"literal/overlap job");
    for(unsigned p=4096;p<4115;++p)require(m.dst[p]=='A',"distance-one overlapping backreference");
    m={};
    s={0,3,4096,19,0,0,0,0,0,0,31,nds_lz::Phase::Output};
    for(unsigned n=0;s.phase!=nds_lz::Phase::Finish&&n<100;++n)nds_lz::run(s,m,64);
    require(s.pending==1&&s.token==0xf0&&s.input==3&&s.remaining==18,"split compressed token saved");
    s.available=1;s.phase=nds_lz::Phase::Output;
    for(unsigned n=0;s.phase!=nds_lz::Phase::Finish&&n<100;++n)nds_lz::run(s,m,64);
    require(s.phase==nds_lz::Phase::Finish&&!s.remaining&&!s.pending&&s.output==4115,"split token resumes on next input chunk");
    for(unsigned p=4096;p<4115;++p)require(m.dst[p]=='A',"split overlapping output");
    require(mkds_stream::is_entry(entry)&&mkds_stream::is_entry(0x0214d4fc)&&!mkds_stream::is_entry(entry+4),"continuation barriers");
}
void differential(const char* path) {
    std::ifstream file(path,std::ios::binary);require(bool(file),"private fixture missing");
    Memory initial;initial.bytes={std::istreambuf_iterator<char>(file),{}};
    require(initial.bytes.size()==mask+1,"expected captured 4MiB RAM image");
    actual=&initial;
    require(mkds_stream::resolve(entry).function!=nullptr,"whole-code proof");
    initial.bytes[(entry+252)&mask]^=1;
    require(!mkds_stream::resolve(entry).function,"modified return instruction accepted");
    initial.bytes[(entry+252)&mask]^=1;provenance=false;
    require(!mkds_stream::resolve(entry).function,"unwritten bytes accepted");provenance=true;
    std::vector<uint8_t> written(mask+1);std::array<uint32_t,1024> generations{};
    unsigned comparisons=0;
    for(unsigned test=0;test<60;++test) {
        Memory live=initial;actual=&live;
        std::fill(live.bytes.begin()+0x200000,live.bytes.begin()+0x230000,uint8_t(0x31+test));
        std::fill(written.begin(),written.end(),uint8_t(0));generations.fill(1);
        g_busf_main={live.bytes.data(),written.data(),generations.data(),mask};
        uint32_t output=0x02208000,source=0x02201000,context=0x02222000,sp=0x02221000;
        if(test%7==1)source=output-8; // source aliases already produced/history bytes
        if(test%7==2)source=output+8; // writes can modify future compressed input
        if(test%7==3)sp=output+28; // real saved-register frame is observable
        if(test%7==4)context=output-16;
        if(test%7==5){source+=0x400000;context+=0x400000;sp+=0x400000;output+=0x400000;}
        if(test%7==6)context=sp-28; // PUSH may overwrite the context before LDR
        uint32_t size=test<56?std::array<uint32_t,8>{0,1,2,3,7,16,31,64}[test/7]:1024u;
        for(uint32_t i=0;i<size;++i)live.write8(source+i,uint8_t((i*37+test*19)^0xa5));
        if(test>=56)for(uint32_t i=0;i<size;++i)live.write8(source+i,i%17==0?0xff:(i%2?0xf0:0));
        live.write32(context,output);
        live.write32(context+4,std::array<uint32_t,7>{0,1,17,129,1024,0xffffffffu,32}[test%7]);
        live.write8(context+11,test&1?0x80:0);
        live.write8(context+12,test%3?8:0);
        live.write8(context+13,0x20);
        live.write8(context+14,test%4==0?1:0);
        if(test>=56){live.write32(context+4,16384);live.write8(context+12,0);live.write8(context+14,0);}
        g_cpu={};for(unsigned r=0;r<16;++r)g_cpu.R[r]=0x12340000+r;
        g_cpu.R[0]=context;g_cpu.R[1]=source;g_cpu.R[2]=size;g_cpu.R[13]=sp;
        g_cpu.R[14]=target|(test&1);g_cpu.R[15]=entry;g_cpu.cpsr=((test&15)<<28)|31;
        // For the intentional stack/context alias, keep overwritten fields
        // valid but different so the original post-PUSH reads are exercised.
        if(test%7==6) {
            g_cpu.R[4]=output;g_cpu.R[5]=32;g_cpu.R[6]=0;g_cpu.R[7]=0;
        }
        Memory reference=live;auto cpu=from_host();
        unsigned rounds=0,steps=0;
        while(g_cpu.R[15]!=target) {
            require(++rounds<100000,"job failed to finish");
            g_nds_unwinding=0;g_nds_fast_limit=g_runtime_cycles+std::array<uint32_t,4>{8,32,128,512}[(rounds+test)%4];
            const auto cycle_begin=g_runtime_cycles;
            if(mkds_stream::is_entry(g_cpu.R[15]))mkds_stream::decode();
            else {auto one=from_host();step(one,live);std::memcpy(g_cpu.R,one.R,sizeof(one.R));g_cpu.cpsr=packed(one.cpsr);g_runtime_cycles+=2;}
            // Compare every materialized scheduler/dispatch boundary against
            // the actual ARM byte stream, including all scratch registers,
            // CPSR, the dead stack frame, and all caller-visible memory.
            unsigned local=0;
            while(!same(cpu)) {
                require(++local<10000&&++steps<1000000,"reference failed to reach native continuation");
                if(cpu.R[15]==target) {
                    std::fprintf(stderr,"case=%u native=%08x reference return flags=%08x/%08x\n",test,g_cpu.R[15],g_cpu.cpsr,packed(cpu.cpsr));
                    throw std::runtime_error("native return mismatch");
                }
                step(cpu,reference);
            }
            require(g_runtime_cycles-cycle_begin==local*2u,"native-service instruction timing profile mismatch");
            require(live.bytes==reference.bytes,"memory differs at continuation");++comparisons;
        }
        require(!std::memcmp(cpu.R,g_cpu.R,sizeof(cpu.R))&&packed(cpu.cpsr)==g_cpu.cpsr,"complete caller contract");
    }
    const auto stats=mkds_stream::statistics();
    require(stats.calls&&stats.bytes&&stats.batches&&stats.declines,"native and fallback paths exercised");
    require(publications<stats.bytes,"output still published per byte");
    std::printf("[hle_mkds_stream_test] 60 original-ARM cases, %u exact boundaries, bytes=%llu batches=%llu declines=%llu\n",comparisons,(unsigned long long)stats.bytes,(unsigned long long)stats.batches,(unsigned long long)stats.declines);
}
}
bool bus_range_has_write_provenance(uint32_t,uint32_t){return provenance;}
bool bus_debug_copy(int,uint32_t a,uint8_t* out,uint32_t size){for(uint32_t i=0;i<size;++i)out[i]=actual->read8(a+i);return true;}
bool nds_performance_execution(){return performance;}
void tier3_step_one(uint32_t){auto c=from_host();step(c,*actual);std::memcpy(g_cpu.R,c.R,sizeof(c.R));g_cpu.cpsr=packed(c.cpsr);g_runtime_cycles+=2;}
extern "C" {
ArmCpuState g_cpu{};NdsCpu g_nds_active=NDS_ARM9;
unsigned char g_nds_unwinding=0;unsigned long long g_runtime_cycles=0,g_nds_fast_limit=0;
uint32_t g_runtime_deep_trace=0;
NdsBusFastWin g_busf_main{},g_busf_wram_lo[2]{},g_busf_wram_hi[2]{},g_busf_itcm{},g_busf_dtcm{};
uint32_t g_busf_itcm_limit=0,g_busf_dtcm_base=0,g_busf_dtcm_limit=0;
void runtime_note_code_write(){}
void runtime_note_code_write_pages(const uint32_t*,uint32_t){++publications;}
void runtime_note_live_write(uint32_t,uint32_t,uint32_t,uint32_t){}
uint8_t bus_read_u8_slow(uint32_t a){return actual->read8(a);}
uint32_t bus_read_u32_slow(uint32_t a){return actual->read32(a);}
void bus_write_u8_slow(uint32_t a,uint8_t v){actual->write8(a,v);}
void bus_write_u32_slow(uint32_t a,uint32_t v){actual->write32(a,v);}
void runtime_tick(uint32_t cycles){require(cycles&&g_runtime_cycles+cycles<=g_nds_fast_limit,"job crossed rendezvous");g_runtime_cycles+=cycles;}
bool runtime_should_yield(){return g_runtime_cycles>=g_nds_fast_limit;}
bool runtime_unwinding(){return g_nds_unwinding!=0;}
int runtime_call_should_return(uint32_t pc){require(pc==target,"return target");return 1;}
void runtime_dispatch(uint32_t){}
void runtime_dispatch_with_exchange(uint32_t){}
}
int main(int argc,char** argv){try{smoke();if(argc==2)differential(argv[1]);else require(argc==1,"usage: hle_mkds_stream_test [private-mainram.bin]");std::puts("[hle_mkds_stream_test] PASS");return 0;}catch(const std::exception& e){std::fprintf(stderr,"[hle_mkds_stream_test] FAIL: %s\n",e.what());return 1;}}
