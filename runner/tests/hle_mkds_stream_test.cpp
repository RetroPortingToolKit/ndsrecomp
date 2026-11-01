#include "hle_mkds_stream.h"
#include "hle_mkds_card_pio.h"
#include "streaming_lz.h"
#include "arm_decode.h"
#include "interpreter.h"
#include "sha1.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

// Isolated extension of the existing stream original-ARM fixture. --pio runs
// only the owner-approved card family; the accepted LZ cases are not repeated.
namespace pio_fixture {
bool active=false;
bool allowed_pc(uint32_t pc);
void before_step(armv4t::Bus& memory,uint32_t pc,const armv4t::CPUState& cpu);
void step_actual();
void published(const uint32_t* first,uint32_t count);
void tick(uint32_t cycles);
uint64_t event_time();
void differential(const char* path);
}

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
    auto pc=c.R[15];require(pio_fixture::active ? pio_fixture::allowed_pc(pc):pc>=entry&&pc<entry+256,"reference escaped routine");
    if(pio_fixture::active)pio_fixture::before_step(m,pc,c);
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
void tier3_step_one(uint32_t){if(pio_fixture::active){pio_fixture::step_actual();return;}auto c=from_host();step(c,*actual);std::memcpy(g_cpu.R,c.R,sizeof(c.R));g_cpu.cpsr=packed(c.cpsr);g_runtime_cycles+=2;}
bool nds_arm9_native_supported(){return true;}
uint64_t nds_next_system_event_time(){return pio_fixture::event_time();}
extern "C" {
ArmCpuState g_cpu{};NdsCpu g_nds_active=NDS_ARM9;
unsigned char g_nds_unwinding=0;unsigned long long g_runtime_cycles=0,g_nds_fast_limit=0;
uint32_t g_runtime_deep_trace=0,g_insn_hook_armed=0;
NdsBusFastWin g_busf_main{},g_busf_wram_lo[2]{},g_busf_wram_hi[2]{},g_busf_itcm{},g_busf_dtcm{};
uint32_t g_busf_itcm_limit=0,g_busf_dtcm_base=0,g_busf_dtcm_limit=0;
void runtime_note_code_write(){}
void runtime_note_code_write_pages(const uint32_t* first,uint32_t count){++publications;if(pio_fixture::active)pio_fixture::published(first,count);}
void runtime_note_live_write(uint32_t,uint32_t,uint32_t,uint32_t){}
uint8_t bus_read_u8_slow(uint32_t a){return actual->read8(a);}
uint16_t bus_read_u16_slow(uint32_t a){return actual->read16(a);}
uint32_t bus_read_u32_slow(uint32_t a){return actual->read32(a);}
void bus_write_u8_slow(uint32_t a,uint8_t v){actual->write8(a,v);}
void bus_write_u16_slow(uint32_t a,uint16_t v){actual->write16(a,v);}
void bus_write_u32_slow(uint32_t a,uint32_t v){actual->write32(a,v);}
void runtime_tick(uint32_t cycles){if(pio_fixture::active){pio_fixture::tick(cycles);return;}require(cycles&&g_runtime_cycles+cycles<=g_nds_fast_limit,"job crossed rendezvous");g_runtime_cycles+=cycles;}
bool runtime_should_yield(){return g_runtime_cycles>=g_nds_fast_limit;}
bool runtime_unwinding(){return g_nds_unwinding!=0;}
int runtime_call_should_return(uint32_t pc){if(!pio_fixture::active)require(pc==target,"return target");return 1;}
void runtime_dispatch(uint32_t){}
void runtime_dispatch_with_exchange(uint32_t){}
}
int main(int argc,char** argv){try{if(argc==3&&std::strcmp(argv[1],"--pio")==0){pio_fixture::differential(argv[2]);return 0;}smoke();if(argc==2)differential(argv[1]);else require(argc==1,"usage: hle_mkds_stream_test [private-mainram.bin] | --pio private-arm9.bin");std::puts("[hle_mkds_stream_test] PASS");return 0;}catch(const std::exception& e){std::fprintf(stderr,"[hle_mkds_stream_test] FAIL: %s\n",e.what());return 1;}}

namespace pio_fixture {
namespace {
constexpr uint32_t Request=0x0217fa20,Cache=0x02180060,Pio=0x021463a4;
constexpr uint32_t Control=0x040001a4,Data=0x04100010;
constexpr uint32_t Busy=0x80000000,Ready=0x00800000;
constexpr uint32_t Archive=0x0222c000,Record=0x0222d000,Signal=0x0222e000;
constexpr uint32_t Main=0x02000000,RomSize=0x20000;
constexpr uint64_t Never=std::numeric_limits<uint64_t>::max();
using Access=std::tuple<uint64_t,uint32_t,unsigned,uint32_t,bool>;
using Event=std::tuple<uint64_t,unsigned,uint32_t>;
bool in_native=false,in_floor=false;
unsigned native_ticks=0,floor_steps=0;

// Independent deterministic B7 producer. This follows the existing io.cpp
// start/ready/consume protocol, not the PIO implementation's branch/PC graph.
// All timestamps are ARM9 cycles; public event_time converts them to system
// cycles. The ROM pattern is immutable synthetic input, not owner save data.
struct Card {
    uint32_t control=0,position=0,length=0,address=0,last_word=0;
    uint16_t aux=0;
    std::array<uint8_t,8> command{};
    uint64_t deadline=Never,last_word_time=Never;
    bool irq_pending=false;
    unsigned starts=0,consumes=0,irqs=0,ready_events=0;
    std::vector<Access> accesses;
    std::vector<Event> events;
    std::vector<std::pair<uint64_t,uint64_t>> stable_intervals;
    static uint8_t rom_byte(uint32_t at) {
        require(at<RomSize,"card ROM bounds");
        return uint8_t((at*17u+(at>>8u)*29u+(at>>16u)*11u)^0xa5u);
    }
    static uint32_t rom_word(uint32_t at) {
        return uint32_t(rom_byte(at))|(uint32_t(rom_byte(at+1))<<8)|
            (uint32_t(rom_byte(at+2))<<16)|(uint32_t(rom_byte(at+3))<<24);
    }
    void schedule(uint64_t now,uint64_t delay) {
        require(delay&&now<=Never-delay,"card event overflow");
        deadline=now+delay;stable_intervals.emplace_back(now,deadline);
    }
    void advance(uint64_t now) {
        if(deadline<=now) {
            require((control&Busy)&&!(control&Ready),"invalid card Ready event");
            events.emplace_back(deadline,1u,position);deadline=Never;
            control|=Ready;++ready_events;
        }
    }
    uint32_t read(uint64_t now,uint32_t a,unsigned width) {
        uint32_t value=0;
        if(a==Control&&width==4)value=control;
        else if(a==Data&&width==4) {
            value=0xffffffffu;
            if(!(control&0x40000000u)&&(control&Ready)) {
                require(position+4<=length,"card consumed beyond response");
                value=rom_word(address+position);control&=~Ready;position+=4;++consumes;
                if(position<length) {
                    const uint32_t xfer=(control&0x08000000u)?8u:5u;
                    uint32_t delay=4u;
                    if(!(position&0x1ffu))delay+=(control>>16u)&63u;
                    schedule(now,uint64_t(xfer)*delay*2u);
                } else {
                    control&=~Busy;deadline=Never;
                    last_word=value;last_word_time=now;
                    if(aux&0x4000u){irq_pending=true;++irqs;events.emplace_back(now,2u,position);}
                }
            }
        } else throw std::runtime_error("unexpected original card read/width");
        accesses.emplace_back(now,a,width,value,false);return value;
    }
    void write(uint64_t now,uint32_t a,unsigned width,uint32_t value) {
        accesses.emplace_back(now,a,width,value,true);
        if(a==0x040001a1u&&width==1) {aux=uint16_t((aux&255u)|((value&255u)<<8));return;}
        if(a>=0x040001a8u&&a<0x040001b0u&&width==1) {command[a-0x040001a8u]=uint8_t(value);return;}
        require(a==Control&&width==4,"unexpected original card write/width");
        const bool start=(value&~control&Busy)!=0;
        control=(value&0xff7f7fffu)|(control&0x20800000u);
        if(!start||!(aux&0x8000u)||(aux&0x2000u))return;
        require(command[0]==0xb7u,"fixture supports the selected B7 operation only");
        require(((control>>24u)&7u)==1u&&!(control&0x40000000u),"fixture requires512B read");
        address=((uint32_t(command[1])<<24)|(uint32_t(command[2])<<16)|
            (uint32_t(command[3])<<8)|uint32_t(command[4]))&(RomSize-1u);
        if(address<0x8000u)address=0x8000u+(address&0x1ffu);
        length=512;position=0;control&=~Ready;++starts;
        require(address+length<=RomSize,"selected card response bounds");
        const uint32_t xfer=(control&0x08000000u)?8u:5u;
        const uint32_t delay=8u+(control&0x1fffu)+((control>>16u)&63u)+4u;
        schedule(now,uint64_t(xfer)*delay*2u);
        events.emplace_back(now,0u,address);
    }
};

struct PioMemory final : Memory {
    Card card;
    uint64_t cycles=0;
    std::vector<uint8_t> written=std::vector<uint8_t>(mask+1);
    std::array<uint32_t,1024> generations{};
    unsigned callbacks=0,notifications=0;
    uint32_t notification_arg=0,notification_status=~0u;
    bool observe_irq=false,read_fence=false,read_fired=false;
    uint32_t watch_store=0,expected_next=0,expected_cost=0,stop_before=0;
    bool store_fired=false,phase_stopped=false;
    uint64_t fence_time=Never;
    uint32_t fence_next=0,fence_cost=0;
    unsigned fence_boundaries=0;
    PioMemory(){generations.fill(1);}
    static bool ram(uint32_t a,unsigned width) {
        return a>=Main&&uint64_t(a)+width<=0x03000000ull;
    }
    uint32_t ram_read(uint32_t a,unsigned width) {
        require(ram(a,width)&&uint64_t(a&mask)+width<=mask+1u,"PIO RAM read bounds");
        uint32_t value=0;std::memcpy(&value,bytes.data()+(a&mask),width);return value;
    }
    void ram_write(uint32_t a,unsigned width,uint32_t value) {
        require(ram(a,width)&&uint64_t(a&mask)+width<=mask+1u,"PIO RAM write bounds");
        const uint32_t off=a&mask;
        std::memcpy(bytes.data()+off,&value,width);
        for(unsigned i=0;i<width;++i)written[off+i]=1;
        const uint32_t first=off>>12,last=(off+width-1u)>>12;
        for(uint32_t page=first;page<=last;++page){uint32_t v=generations[page]+1;generations[page]=v?v:1;}
        if(active&&this==actual)runtime_note_code_write_pages(generations.data()+first,last-first+1);
    }
    uint32_t io_read(uint32_t a,unsigned width) {
        const auto value=card.read(cycles,a,width);
        if(active&&this==actual) {
            if(read_fence&&!read_fired&&in_native&&!in_floor&&a==Control&&g_cpu.R[15]==0x02146418u) {
                read_fired=true;g_nds_fast_limit=0;fence_time=cycles;fence_next=0x0214641c;fence_cost=2;
            }
            if(observe_irq&&card.irq_pending&&!(g_cpu.cpsr&CPSR_I_BIT))g_nds_fast_limit=0;
        }
        return value;
    }
    uint8_t read8(uint32_t a) override{return uint8_t(ram(a,1)?ram_read(a,1):io_read(a,1));}
    uint16_t read16(uint32_t a) override{return uint16_t(ram(a,2)?ram_read(a,2):io_read(a,2));}
    uint32_t read32(uint32_t a) override{return ram(a,4)?ram_read(a,4):io_read(a,4);}
    void write8(uint32_t a,uint8_t v) override{if(ram(a,1))ram_write(a,1,v);else card.write(cycles,a,1,v);}
    void write16(uint32_t a,uint16_t v) override{if(ram(a,2))ram_write(a,2,v);else card.write(cycles,a,2,v);}
    void write32(uint32_t a,uint32_t v) override{if(ram(a,4))ram_write(a,4,v);else card.write(cycles,a,4,v);}
    void advance(uint32_t n){cycles+=n;card.advance(cycles);}
};

PioMemory& live(){return *static_cast<PioMemory*>(actual);}
void bind(PioMemory& memory) {
    actual=&memory;g_busf_main={memory.bytes.data(),memory.written.data(),memory.generations.data(),mask};
}
void compare(const char* name,const PioMemory& memory,const PioMemory& reference,const armv4t::CPUState& cpu) {
    require(g_runtime_cycles==memory.cycles,"PIO host/device clock diverged");
    if(!same(cpu)||memory.cycles!=reference.cycles) {
        std::fprintf(stderr,"PIO %s cycles=%llu/%llu PC=%08x/%08x CPSR=%08x/%08x\n",name,
            (unsigned long long)memory.cycles,(unsigned long long)reference.cycles,g_cpu.R[15],cpu.R[15],g_cpu.cpsr,packed(cpu.cpsr));
        for(unsigned r=0;r<16;++r)if(g_cpu.R[r]!=cpu.R[r])std::fprintf(stderr," R%u=%08x/%08x\n",r,g_cpu.R[r],cpu.R[r]);
        throw std::runtime_error("PIO original-ARM register/flag/time boundary mismatch");
    }
    if(memory.bytes!=reference.bytes) {
        const auto at=std::mismatch(memory.bytes.begin(),memory.bytes.end(),reference.bytes.begin()).first-memory.bytes.begin();
        std::fprintf(stderr,"PIO %s RAM firstdiff=%08x %02x/%02x PC=%08x\n",name,unsigned(Main+at),memory.bytes[at],reference.bytes[at],g_cpu.R[15]);
        throw std::runtime_error("PIO original-ARM memory boundary mismatch");
    }
    require(memory.generations==reference.generations&&memory.written==reference.written,"PIO typed publication differs at boundary");
    require(memory.card.control==reference.card.control&&memory.card.position==reference.card.position&&
        memory.card.deadline==reference.card.deadline&&memory.card.irq_pending==reference.card.irq_pending&&
        memory.card.command==reference.card.command&&memory.card.events==reference.card.events,
        "PIO independent producer state differs at boundary");
}
bool control_read(const Access& a){return std::get<1>(a)==Control&&!std::get<4>(a);}
unsigned compare_accesses(const PioMemory& memory,const PioMemory& reference) {
    std::vector<Access> a,b;
    for(const auto& v:memory.card.accesses)if(!control_read(v))a.push_back(v);
    for(const auto& v:reference.card.accesses)if(!control_read(v))b.push_back(v);
    require(a==b,"PIO command/consuming-device order/value/width/time differs");
    size_t index=0;
    for(const auto& v:memory.card.accesses) {
        while(index<reference.card.accesses.size()&&reference.card.accesses[index]!=v)++index;
        require(index<reference.card.accesses.size(),"PIO access not present at original guest timestamp");++index;
    }
    unsigned omitted=0;
    for(const auto& v:reference.card.accesses)if(control_read(v)&&
        std::find(memory.card.accesses.begin(),memory.card.accesses.end(),v)==memory.card.accesses.end()) {
        ++omitted;
        const auto time=std::get<0>(v);const auto value=std::get<3>(v);
        require((value&Busy)&&!(value&Ready),"PIO lost a Ready/final transition read");
        bool justified=false;
        for(const auto& interval:reference.card.stable_intervals)if(interval.first<time&&time<interval.second) {
            for(const auto& anchor:memory.card.accesses)if(control_read(anchor)&&std::get<3>(anchor)==value&&
                interval.first<std::get<0>(anchor)&&std::get<0>(anchor)<time){justified=true;break;}
            if(justified)break;
        }
        require(justified,"PIO omitted read without stable interior/event bound and observed anchor");
    }
    return omitted;
}
} // namespace

bool allowed_pc(uint32_t pc) {
    return (pc>=0x02146300u&&pc<0x02146498u)||
        (pc>=0x02146730u&&pc<0x021467a8u)||(pc>=0x021467d0u&&pc<0x02146864u)||
        (pc>=0x0214d1fcu&&pc<0x0214d340u)||
        (pc>=0x0200fca0u&&pc<0x0200fcccu)||(pc>=0x0200df9cu&&pc<0x0200e01cu)||
        (pc>=0x020050b0u&&pc<0x020050bcu)||(pc>=0x02003be0u&&pc<0x02003c60u);
}
void before_step(armv4t::Bus& bus,uint32_t pc,const armv4t::CPUState& cpu) {
    auto& memory=dynamic_cast<PioMemory&>(bus);
    if(pc==0x020050b0u)++memory.callbacks;
    if(pc==0x02003be0u){++memory.notifications;memory.notification_arg=cpu.R[0];memory.notification_status=cpu.R[1];}
}
void step_actual() {
    const bool was_floor=in_floor;in_floor=true;++floor_steps;
    auto c=from_host();step(c,*actual);std::memcpy(g_cpu.R,c.R,sizeof(c.R));g_cpu.cpsr=packed(c.cpsr);
    live().advance(2);g_runtime_cycles+=2;
    if(g_runtime_cycles>=g_nds_fast_limit)g_nds_unwinding=1;
    in_floor=was_floor;
}
void published(const uint32_t* first,uint32_t count) {
    auto& memory=live();
    require(first>=memory.generations.data()&&first+count<=memory.generations.data()+memory.generations.size(),"PIO publication backing/range");
    if(in_native&&!in_floor&&memory.watch_store&&!memory.store_fired&&g_cpu.R[15]==memory.watch_store) {
        memory.store_fired=true;g_nds_fast_limit=0;memory.fence_time=memory.cycles;
        memory.fence_next=memory.expected_next;memory.fence_cost=memory.expected_cost;
    }
}
void tick(uint32_t cycles) {
    require(cycles&&!(cycles&1u),"PIO timing2 charge");++native_ticks;
    live().advance(cycles);g_runtime_cycles+=cycles;
    if(g_runtime_cycles>=g_nds_fast_limit)g_nds_unwinding=1;
    auto& memory=live();
    if(memory.stop_before&&!memory.phase_stopped&&g_cpu.R[15]==memory.stop_before) {
        memory.phase_stopped=true;g_nds_fast_limit=g_runtime_cycles;g_nds_unwinding=1;
    }
}
uint64_t event_time(){return active&&actual&&live().card.deadline!=Never?live().card.deadline/2u:Never;}

void differential(const char* path) {
    std::ifstream input(path,std::ios::binary);require(bool(input),"private ARM9 image missing");
    const std::vector<uint8_t> image{std::istreambuf_iterator<char>(input),{}};
    require(image.size()==1536952u,"exact MKDS ARM9 image size");
    require(gba::sha1(image.data(),image.size()).hex()=="34408d363d44b84afba82f09ed09733167daebe0","exact private ARM9 image identity");
    require(!mkds_card_pio::initialize("unrelated")&&!mkds_card_pio::is_entry(Pio),"PIO unrelated ROM admitted");
    require(mkds_card_pio::initialize("691e00d9a5dd80b04f80cc7559503e8b06848785"),"PIO ROM scope");
    // Two admission-decline checks reuse the same one-instruction original
    // ARM oracle. Neither is a new card scenario or an execution matrix.
    for(const bool trace:{true,false}) {
        active=false;actual=nullptr;PioMemory memory;
        std::fill(memory.bytes.begin(),memory.bytes.end(),0xcc);
        std::copy(image.begin(),image.end(),memory.bytes.begin());
        std::fill(memory.written.begin(),memory.written.begin()+image.size(),1);
        memory.write32(Cache+4,0xa1416657);memory.write32(Request+0x24,512);
        PioMemory reference=memory;bind(memory);active=true;
        g_cpu={};for(unsigned r=0;r<16;++r)g_cpu.R[r]=0x24680000u+r*0x111u;
        g_cpu.R[0]=Cache;g_cpu.R[13]=trace?0x02230000u:Request+40u;
        g_cpu.R[14]=target;g_cpu.R[15]=Pio;g_cpu.cpsr=0xb000009fu;
        g_runtime_cycles=0;g_nds_fast_limit=512;g_nds_unwinding=0;
        g_runtime_deep_trace=trace?1u:0u;g_insn_hook_armed=0;
        provenance=true;performance=true;
        require(mkds_card_pio::resolve(Pio).function,"decline original proof");
        auto cpu=from_host();const uint32_t old_sp=g_cpu.R[13];
        const unsigned tick_before=native_ticks,floor_before=floor_steps;
        in_native=true;mkds_card_pio::run();in_native=false;
        step(cpu,reference);reference.advance(2);
        compare(trace?"decline-trace":"decline-stack-alias",memory,reference,cpu);
        require(g_cpu.R[15]==0x021463a8u&&g_cpu.R[13]==old_sp-36u&&g_runtime_cycles==2&&
            native_ticks==tick_before&&floor_steps==floor_before+1u&&memory.card.accesses.empty(),
            "PIO decline half-applied grouped prologue or touched MMIO");
        std::printf("[hle_mkds_stream_test] PIO %s PASS: one original PUSH, PC63A8, SPold-36,2cycles, no native tick/MMIO\n",trace?"decline-trace":"decline-stack-alias");
        active=false;actual=nullptr;
    }
    g_runtime_deep_trace=0;
    struct Case {const char* name;uint32_t source,destination,length,budget;bool irq,read_fence;uint32_t store_pc,next_pc,cost,stop_before;};
    const Case cases[]={
        {"aligned",0x9000,0x02208000,512,128,false,false,0,0,0,0},
        {"two-block",0x9000,0x02208000,1024,512,false,false,0,0,0,0},
        {"staged-odd",0x9000,0x02208001,512,128,false,false,0,0,0,0},
        {"head-tail",0x9003,0x02208001,700,512,false,false,0,0,0,0},
        {"short-budget",0x9001,0x02208003,17,2,false,false,0,0,0,0},
        {"final-word-irq-restore",0x9000,0x02208000,512,128,true,false,0,0,0,0},
        {"read-fence",0x9000,0x02208000,512,512,false,true,0,0,0,0},
        {"data-store-fence",0x9000,0x02208000,512,512,false,false,0x0214642c,0x02146430,4,0x02146428},
        {"metadata-store-fence",0x9000,0x02208000,512,512,false,false,0x02146460,0x02146464,20,0x0214643c},
        {"push-store-fence",0x9000,0x02208000,512,512,false,false,0x021463a4,0x021463a8,2,0},
        {"cache-header-fence",0x9001,0x02208001,17,512,false,false,0x021463f8,0x021463fc,12,0x021463cc},
    };
    unsigned total_boundaries=0,total_irq=0,total_omitted=0;
    for(const auto& test:cases) {
        active=false;actual=nullptr;
        PioMemory memory;std::fill(memory.bytes.begin(),memory.bytes.end(),0xcc);
        std::copy(image.begin(),image.end(),memory.bytes.begin());std::fill(memory.written.begin(),memory.written.begin()+image.size(),1);
        const auto put=[&](uint32_t at,uint32_t v){memory.write32(at,v);};
        put(Request,Signal);put(Signal,1);put(Request+0x1c,test.source);put(Request+0x20,test.destination);
        put(Request+0x24,test.length);put(Request+0x28,~0u);put(Request+0x38,0x020050b0);put(Request+0x3c,Archive);
        put(Request+0x114,7);put(Request+0x10c,0);put(Request+0x110,0);
        put(Cache,Pio);put(Cache+4,0xa1416657);put(Cache+8,~0u);
        put(Archive+0xc,0);put(Archive+0x10,0);put(Archive+0x1c,0x200);put(Archive+0x24,Record);put(Record+0x14,0xbadc0de);
        memory.observe_irq=test.irq;memory.read_fence=test.read_fence;memory.watch_store=test.store_pc;
        memory.expected_next=test.next_pc;memory.expected_cost=test.cost;memory.stop_before=test.stop_before;
        PioMemory reference=memory;bind(memory);active=true;
        g_cpu={};for(unsigned r=0;r<16;++r)g_cpu.R[r]=0x13570000u+r*0x111u;
        g_cpu.R[13]=0x02230000;g_cpu.R[14]=target;g_cpu.R[15]=0x02146300;
        g_cpu.cpsr=0xb000001fu|(test.irq?0u:CPSR_I_BIT);g_runtime_cycles=0;g_nds_unwinding=0;
        g_runtime_deep_trace=0;g_insn_hook_armed=0;provenance=true;performance=true;
        require(mkds_card_pio::resolve(Pio).function,"PIO independent whole-code proof");
        if(&test==cases) {
            provenance=false;require(!mkds_card_pio::resolve(Pio).function,"PIO unwritten body admitted");provenance=true;
            memory.bytes[(Pio+252u)&mask]^=1u;
            require(!mkds_card_pio::resolve(Pio).function,"PIO changed proof tail admitted");
            memory.bytes[(Pio+252u)&mask]^=1u;
            require(mkds_card_pio::resolve(Pio).function,"PIO original proof not restored");
        }
        auto cpu=from_host();unsigned rounds=0,boundaries=0,irq_observed=0;
        const unsigned native_before=native_ticks;
        while(g_cpu.R[15]!=target) {
            require(++rounds<200000&&memory.cycles<1000000,"PIO comparison failed to finish");
            g_nds_unwinding=0;g_nds_fast_limit=memory.cycles+test.budget;
            if(memory.card.deadline!=Never)g_nds_fast_limit=std::min<uint64_t>(g_nds_fast_limit,memory.card.deadline);
            const uint64_t invocation_end=g_nds_fast_limit;
            if(mkds_card_pio::is_entry(g_cpu.R[15])) {
                in_native=true;mkds_card_pio::run();in_native=false;
            } else step_actual();
            require(memory.cycles<=invocation_end,"PIO crossed saved scheduler/event invocation bound");
            unsigned steps=0;
            while(reference.cycles<memory.cycles) {
                require(cpu.R[15]!=target&&++steps<10000,"PIO reference escaped elapsed boundary");
                step(cpu,reference);reference.advance(2);
            }
            compare(test.name,memory,reference,cpu);++boundaries;
            if(memory.fence_time!=Never) {
                require(g_cpu.R[15]==memory.fence_next&&memory.cycles==memory.fence_time+memory.fence_cost,"PIO store/read fence did not preserve exact next instruction");
                ++memory.fence_boundaries;memory.fence_time=Never;
            }
            if(test.irq&&memory.card.irq_pending) {
                require(reference.card.irq_pending&&g_cpu.R[15]==0x02146428u&&g_cpu.R[0]==127u&&
                    g_cpu.R[1]==memory.card.last_word,"PIO final-word IRQ lost pre-STR canonical state");
                require(memory.read32(g_cpu.R[8]+g_cpu.R[0]*4u)!=g_cpu.R[1],"PIO IRQ observer already sees pending final STR");
                // Focused canonical snapshot/restore, not an ISR, serializer or
                // claimed engine save/load check. The external observer is an
                // identity handler and acknowledges the pending IRQ before resume.
                const ArmCpuState saved_cpu=g_cpu;const PioMemory saved=memory;
                const uint64_t saved_clock=g_runtime_cycles,saved_limit=g_nds_fast_limit;
                g_cpu={};g_runtime_cycles=0;g_nds_fast_limit=0;
                memory.bytes[0]^=0xff;memory.written[0]^=1;memory.generations[0]^=1;
                memory.cycles=0;memory.card=Card{};
                memory=saved;g_cpu=saved_cpu;g_runtime_cycles=saved_clock;g_nds_fast_limit=saved_limit;bind(memory);
                require(mkds_card_pio::initialize("691e00d9a5dd80b04f80cc7559503e8b06848785")&&mkds_card_pio::resolve(g_cpu.R[15]).function,"PIO restored continuation proof");
                compare(test.name,memory,reference,cpu);
                memory.card.irq_pending=false;reference.card.irq_pending=false;++irq_observed;
            }
        }
        const unsigned omitted=compare_accesses(memory,reference);
        const uint32_t blocks=(test.source%512u+test.length+511u)/512u;
        require(memory.card.starts==blocks&&memory.card.consumes==blocks*128u&&memory.card.ready_events==blocks*128u&&memory.card.irqs==blocks,"PIO producer/request counts");
        require(memory.read32(Request+0x1c)==test.source+test.length&&memory.read32(Request+0x20)==test.destination+test.length&&memory.read32(Request+0x24)==0,"PIO completed-prefix fields");
        require(memory.callbacks==1&&memory.notifications==1&&memory.notification_arg==Archive&&memory.notification_status==0,"original callback/notification not executed exactly once");
        require(memory.read32(Signal)==0&&memory.read32(Record+0x14)==0&&!(memory.read32(Archive+0x1c)&0x200u)&&memory.read32(Request+0x114)==3,"original completion publication absent");
        require(!test.irq||irq_observed==1,"final-word IRQ observer not exercised");
        require(!test.read_fence||(memory.read_fired&&memory.fence_boundaries==1),"read fence path not exercised");
        require(!test.store_pc||(memory.store_fired&&memory.fence_boundaries==1),"native grouped-store fence path not exercised");
        if(test.budget>=32)require(native_ticks>native_before,"PIO native service not exercised");
        std::printf("[hle_mkds_stream_test] PIO %s PASS: originalARM boundaries=%u cycles=%llu blocks=%u words=%u omitted_stable_polls=%u irq_observer=%u fence=%u\n",test.name,boundaries,(unsigned long long)memory.cycles,blocks,memory.card.consumes,omitted,irq_observed,memory.fence_boundaries);
        total_boundaries+=boundaries;total_irq+=irq_observed;total_omitted+=omitted;active=false;actual=nullptr;
    }
    require(total_irq==1,"PIO IRQ case vacuous");
    require(total_omitted>0,"PIO coalesced polling path not exercised");
    std::printf("[hle_mkds_stream_test] PIO PASS: %zu approved-family cases, %u exact original-ARM boundaries; real helper/wrapper/callback bytes; MMIO timing/order; typed publication; snapshot continuation only (no ISR/engine NSS/performance claim)\n",std::size(cases),total_boundaries);
}
} // namespace pio_fixture
