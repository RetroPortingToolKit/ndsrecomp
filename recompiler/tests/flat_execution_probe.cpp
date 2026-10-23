#include "execution_region.h"
#include "flat_fixture.h"
#include "interpreter.h"
#include "arm_decode.h"
#include "thumb_decode.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "globals.inc"
extern "C" const NdsDispatchEntry g_dispatch_flat_fixture[];
extern "C" const unsigned g_dispatch_flat_fixture_len;

static const uint8_t image[]={
#include "fixture_image.inc"
};
static const uint32_t kernels[]={
#include "kernels.inc"
};
static uint8_t ram[4096], written[4096];
static uint32_t generations[1];
static uint32_t fallback_calls, io_calls, code_writes;
static bool invalidate;
static unsigned current_test;
static void check_at(bool v,unsigned line) {
    if(!v){std::fprintf(stderr,"check failed line=%u test=%u native_pc=%08X\n",line,current_test,g_cpu.R[15]);std::abort();}
}
#define check(v) check_at((v),__LINE__)

struct TestBus:armv4t::Bus {
    uint8_t* data;
    explicit TestBus(uint8_t* p):data(p){}
    uint8_t read8(uint32_t a)override {
        if(a>=0x02010000 && a<0x02010000+sizeof(image))return image[a-0x02010000];
        return data[a&4095];
    }
    uint16_t read16(uint32_t a)override{return read8(a)|(uint16_t(read8(a+1))<<8);}
    uint32_t read32(uint32_t a)override{return read16(a)|(uint32_t(read16(a+2))<<16);}
    void write8(uint32_t a,uint8_t v)override{data[a&4095]=v;}
    void write16(uint32_t a,uint16_t v)override{write8(a,v);write8(a+1,v>>8);}
    void write32(uint32_t a,uint32_t v)override{write16(a,v);write16(a+2,v>>16);}
};
static void import_cpu(armv4t::CPUState& c) {
    memcpy(c.R,g_cpu.R,sizeof(c.R));
    uint32_t f=g_cpu.cpsr;
    c.cpsr.n=f>>31;c.cpsr.z=(f>>30)&1;c.cpsr.c=(f>>29)&1;c.cpsr.v=(f>>28)&1;
    c.cpsr.q=(f>>27)&1;c.cpsr.i=(f>>7)&1;c.cpsr.f=(f>>6)&1;c.cpsr.t=(f>>5)&1;
    c.cpsr.mode=f&31;c.thumb=c.cpsr.t;
}
static uint32_t pack(const armv4t::CPUState& c) {
    return (uint32_t(c.cpsr.n)<<31)|(uint32_t(c.cpsr.z)<<30)|
        (uint32_t(c.cpsr.c)<<29)|(uint32_t(c.cpsr.v)<<28)|(uint32_t(c.cpsr.q)<<27)|
        (uint32_t(c.cpsr.i)<<7)|(uint32_t(c.cpsr.f)<<6)|(uint32_t(c.cpsr.t)<<5)|c.cpsr.mode;
}
static void step(armv4t::CPUState& c,TestBus& b) {
    uint32_t cycles=0;
    auto in=c.cpsr.t?armv4t::ThumbDecoder::decode(b.read16(c.R[15]),c.R[15]):
        armv4t::ArmDecoder::decode(b.read32(c.R[15]),c.R[15]);
    auto result=armv4t::Interpreter::step(c,b,in,&cycles);
    if(result!=armv4t::Interpreter::Result::Normal && result!=armv4t::Interpreter::Result::Branched)
        std::fprintf(stderr,"reference unsupported pc=%08X raw=%08X result=%u\n",in.pc,in.raw,unsigned(result));
    check(result==armv4t::Interpreter::Result::Normal || result==armv4t::Interpreter::Result::Branched);
}
extern "C" bool nds_region_prepare(void) { return true; }
extern "C" void runtime_region_single_step() {
    ++fallback_calls;armv4t::CPUState c{};import_cpu(c);TestBus b(ram);step(c,b);
    memcpy(g_cpu.R,c.R,sizeof(c.R));g_cpu.cpsr=pack(c);++g_insn_count[0];++g_runtime_cycles;
}
extern "C" void nds_region_finish(uint32_t single_step,uint32_t) {
    if(single_step)runtime_region_single_step();
}
extern "C" void runtime_note_code_write() {
    ++code_writes;if(invalidate)g_nds_fast_limit=0;
}
extern "C" void runtime_note_live_write(uint32_t,uint32_t,uint32_t,uint32_t) {}
extern "C" uint32_t runtime_code_cycles(uint32_t pc) {g_last_code_pc[0]=pc;return 1;}
extern "C" uint32_t runtime_mem_cycles_slow(uint32_t a,uint32_t,uint32_t) {g_last_data_addr[0]=a;return 1;}
extern "C" uint32_t arm9_refill_cycles(uint32_t) {return 2;}
extern "C" uint32_t runtime_mul_cycles(uint32_t,uint32_t,uint32_t) {return 1;}
extern "C" uint32_t runtime_clz(uint32_t v) {uint32_t n=0;while(n<32 && !(v&0x80000000u)){v<<=1;++n;}return n;}
extern "C" { uint32_t g_nds_region_hle_vectors=0; }
extern "C" uint32_t bus_read_u32_slow(uint32_t a) {++io_calls;return TestBus(ram).read32(a);}
extern "C" uint16_t bus_read_u16_slow(uint32_t a) {++io_calls;return TestBus(ram).read16(a);}
extern "C" uint8_t bus_read_u8_slow(uint32_t a) {++io_calls;return TestBus(ram).read8(a);}
extern "C" void bus_write_u32_slow(uint32_t a,uint32_t v) {++io_calls;TestBus(ram).write32(a,v);}
extern "C" void bus_write_u16_slow(uint32_t a,uint16_t v) {++io_calls;TestBus(ram).write16(a,v);}
extern "C" void bus_write_u8_slow(uint32_t a,uint8_t v) {++io_calls;TestBus(ram).write8(a,v);}

int main() {
    g_busf_main={ram,written,generations,4095};
    uint32_t random=0x7CF23019;
    static const uint32_t edges[]={0,1,31,32,33,255,256,0x7FFFFFFF,
        0x80000000,0xFFFFFFFF,0xFFFFFFFE,0x40000000,0x80000001};
    uint64_t checks=0;
    for(unsigned test=0;test<4096;++test) {
        current_test=test;
        memset(ram,0,sizeof(ram));memset(&g_cpu,0,sizeof(g_cpu));
        random=random*1664525u+1013904223u;TestBus(ram).write32(0x404,random);
        random=random*1664525u+1013904223u;TestBus(ram).write32(0x408,random);
        if(test>=2048) {
            TestBus(ram).write32(0x404,edges[(test/4)%13]);
            TestBus(ram).write32(0x408,edges[(test/(4*13))%13]);
        }
        g_cpu.cpsr=0x1Fu|(random&0xF8000000u);
        g_cpu.R[4]=((test&1)?0x04000400:0x02000400)+((test>>2)&3);
        g_cpu.R[6]=kernels[(test/4)%(sizeof(kernels)/sizeof(kernels[0]))];
        g_cpu.R[13]=0x02000F00;g_cpu.R[14]=0x02020000;g_cpu.R[15]=0x02010000;
        g_runtime_cycles=0;g_insn_count[0]=0;fallback_calls=io_calls=0;
        std::array<uint8_t,4096> reference;memcpy(reference.data(),ram,sizeof(ram));
        armv4t::CPUState c{};import_cpu(c);TestBus b(reference.data());
        unsigned reference_count=0;
        while(c.R[15]!=0x02020000 && reference_count++<100)step(c,b);
        check(c.R[15]==0x02020000);
        for(unsigned guard=0;g_cpu.R[15]!=0x02020000 && guard<100;++guard) {
            g_nds_fast_limit=g_runtime_cycles+((test&2)?1:100000);
            bool found=false;
            for(unsigned i=0;i<g_dispatch_flat_fixture_len;++i) {
                const auto& e=g_dispatch_flat_fixture[i];
                if(e.addr==g_cpu.R[15] && bool(e.thumb)==bool(g_cpu.cpsr&CPSR_T_BIT)){
                    e.fn();found=true;break;
                }
            }
            check(found);
        }
        if(g_cpu.R[15]!=0x02020000 || memcmp(c.R,g_cpu.R,sizeof(c.R)) ||
           pack(c)!=g_cpu.cpsr || memcmp(reference.data(),ram,sizeof(ram)) ||
           g_insn_count[0]!=reference_count || fallback_calls<1 || fallback_calls>2) {
            std::fprintf(stderr,"flat mismatch test=%u pc=%08X expected=%08X insns=%llu/%u fallback=%u cpsr=%08X/%08X\n",
                test,g_cpu.R[15],c.R[15],g_insn_count[0],reference_count,fallback_calls,g_cpu.cpsr,pack(c));
            return 1;
        }
        if(test&1)check(io_calls>0);
        ++checks;
    }
    // Invalidation must stop after the completed store, before later code runs.
    memset(&g_cpu,0,sizeof(g_cpu));g_cpu.cpsr=0x1F;g_cpu.R[13]=0x02000F00;
    g_cpu.R[14]=0x02020000;g_cpu.R[15]=0x02010000;g_runtime_cycles=0;
    g_nds_fast_limit=100000;g_insn_count[0]=0;invalidate=true;
    g_dispatch_flat_fixture[0].fn();
    check(g_cpu.R[15]==0x02010004 && g_cpu.R[13]==0x02000EFC && g_insn_count[0]==1);
    std::printf("flat execution/interpreter: %llu cases plus code-invalidation boundary passed\n",checks);
}
