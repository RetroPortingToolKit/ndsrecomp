// Real Tier3, runtime, bus, CP15 and scheduler. Devices are controlled stubs.
// Run this same binary with NDS_TIER3_CANONICAL_STATE=0 and =1; compare its
// complete retired-state, RAM, timing and access-control transcripts.
#include "runtime_arm.h"
#include "state.h"
#include "scheduler.h"
#include "tier3.h"
#include "io.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" { uint32_t g_insn_hook_armed=0; void runtime_request_yield_poll(void); }
namespace {
constexpr uint32_t code=0x02004000,data=0x02028000,io=0x04000004;
FILE* output;
unsigned checks;
int event;
bool halted[2],dma[2],event_break;
uint64_t stop_at;
void require(bool ok,const char* message){++checks;if(!ok){
    std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
template<class T>void record(const T& value){require(std::fwrite(&value,sizeof(value),1,output)==1,"transcript write");}
void access(uint32_t kind,uint32_t address,uint32_t value){
    record(kind);record(address);record(value);record(g_cpu.R[15]);record(g_cpu.cpsr);record(g_runtime_cycles);
}
void snapshot(){
    record(g_cpu);record(g_runtime_cycles);record(g_insn_count[0]);record(g_insn_count[1]);
    auto debt=runtime_deferred_cycles();record(debt);
    auto depth=runtime_call_stack_depth();record(depth);
    for(unsigned n=0;n<depth;++n)record(runtime_call_stack_data()[n]);
    record(g_nds_unwinding);
    std::array<Tier3TraceEvent,4096> trace{};
    auto count=tier3_debug_trace_copy(trace.data(),uint32_t(trace.size()));record(count);
    for(unsigned n=0;n<count;++n){const auto& t=trace[n];
        record(t.seq);record(t.cpu);record(t.thumb);record(t.phase);record(t.result);
        record(t.pc);record(t.raw);record(t.next_pc);record(t.cpsr);
        record(t.r0);record(t.r1);record(t.r2);record(t.r3);record(t.r12);
        record(t.sp);record(t.lr);record(t.cycles);
    }
    std::array<uint8_t,256> bytes{};for(unsigned i=0;i<bytes.size();++i)bytes[i]=bus_read_u8(data+i);record(bytes);
}
void initialize(NdsCpu cpu,bool thumb=false){
    event=0;event_break=false;stop_at=0;
    std::memset(halted,0,sizeof(halted));std::memset(dma,0,sizeof(dma));
    g_nds_active=cpu;g_cpu={};g_cpu.cpsr=0x6000031f|(thumb?32:0);
    for(unsigned r=0;r<15;++r)g_cpu.R[r]=0x12340000+23*r;
    for(unsigned b=0;b<6;++b){g_cpu.banked_sp[b]=data+128+4*b;
        g_cpu.banked_lr[b]=code+128+4*b;g_cpu.banked_spsr[b]=0xa000003f;}
    for(unsigned r=0;r<5;++r){g_cpu.r8_12_user[r]=0xa000+r;g_cpu.r8_12_fiq[r]=0xb000+r;}
    g_cpu.R[0]=3;g_cpu.R[1]=data;g_cpu.R[2]=5;g_cpu.R[3]=io;g_cpu.R[15]=code;
    g_insn_count[0]=g_insn_count[1]=0;g_nds_insn_stop=false;
    g_nds_irq_pending_cache[0]=g_nds_irq_pending_cache[1]=0;
    g_runtime_cycles=0;runtime_deferred_cycles_set(0);runtime_call_stack_restore(nullptr,0);
    nds_clear_unwinding();nds_slice_begin(10000000);runtime_set_deep_trace(1);tier3_reset();
    for(unsigned i=0;i<256;++i)bus_write_u8(data+i,uint8_t(i*19+7));
}
void step(uint32_t raw,bool thumb=false){
    if(thumb)bus_write_u16(g_cpu.R[15],uint16_t(raw));else bus_write_u32(g_cpu.R[15],raw);
    nds_clear_unwinding();tier3_step_one(g_cpu.R[15]);snapshot();
    require(!g_nds_terminal,"instruction implemented");
}
void static_parent(){
    g_cpu.R[15]=code;tier3_run(code);
    // A real static frame may restore an obsolete continuation after return.
    // The runtime must restore the preserved faithful state on unwind.
    if(runtime_unwinding()) {g_cpu.R[15]=0xdead0000;nds_restore_unwind_state();return;}
    g_cpu.R[15]=0xbad00000;
}
const NdsDispatchEntry parent[]={{0x02090000,0,static_parent,nullptr}};
void loop(){bus_write_u32(code,0xe2900001);bus_write_u32(code+4,0xeafffffd);}
}
uint32_t nds_io_read(uint32_t address,uint32_t){access(1,address,0x13579bdf);return 0x13579bdf;}
void nds_io_write(uint32_t address,uint32_t value,uint32_t){
    access(2,address,value);
    if(event==1)halted[g_nds_active]=true;
    if(event==2)dma[g_nds_active]=true;
    if(event==3)g_nds_irq_pending_cache[g_nds_active]=1;
    if(event==4)event_break=true;
    runtime_request_yield_poll();
}
bool nds_cpu_halted(int cpu){return halted[cpu];}
bool nds_dma_cpu_stalled(int cpu){return dma[cpu];}
bool nds_event_break_hit(){return event_break;}
uint32_t nds_irq_pending(int cpu){return g_nds_irq_pending_cache[cpu];}
void nds_note_insn_retired(int cpu){
    if(++g_insn_count[cpu]==stop_at){g_nds_insn_stop=true;runtime_request_yield_poll();}
}
void nds_note_irq_accept(int cpu,uint32_t pc){access(3,uint32_t(cpu),pc);}
int main(int argc,char** argv){
    require(argc==2,"transcript path");output=std::fopen(argv[1],"wb");require(output,"open transcript");
    bus_init();cp15_reset();runtime_init(nullptr);
    for(auto cpu:{NDS_ARM9,NDS_ARM7}){
        // Both cores, failed conditions, writeback and ordinary RAM traffic.
        initialize(cpu);step(0x12900001);require(g_cpu.R[0]==3,"failed condition did not change R0");
        step(0xe2900001);require(g_cpu.R[0]==4,"ADD retired");
        step(0xe4810004);require(g_cpu.R[1]==data+4,"STR writeback");
        step(0xe5912000);step(0xe5932000);step(0xe8a10005);step(0xe8b10005);
        // PC-containing and empty block lists; aliasing base/destination.
        initialize(cpu);step(0xe8a10000);initialize(cpu);step(0xe8b10000);
        initialize(cpu);step(0xe8b10002);initialize(cpu);step(0xe1c1f0d0);
        // SWI/IRQ transitions publish all banks and control.
        initialize(cpu);step(0xef000003);require((g_cpu.cpsr&31)==0x13&&g_cpu.R[15]==8,"SWI entry");
        initialize(cpu);g_cpu.cpsr&=~CPSR_I_BIT;g_nds_irq_pending_cache[cpu]=1;
        step(0xe2900001);require(g_cpu.R[15]==24&&(g_cpu.cpsr&31)==0x12,"IRQ entry");
        // NDS-specific Thumb MUL carry policy must remain outside common core.
        initialize(cpu,true);step(0x4350,true);require(g_cpu.R[0]==15,"Thumb MUL value");
        require(bool(g_cpu.cpsr&CPSR_C_BIT)==(cpu==NDS_ARM9),"Thumb MUL per-core carry");
        // Interworking return balances the real runtime call stack.
        initialize(cpu);g_cpu.cpsr|=CPSR_T_BIT;runtime_call_push_return(code+64);
        g_cpu.cpsr&=~CPSR_T_BIT;g_cpu.R[14]=code+65;
        step(0xe12fff1e);require(g_cpu.R[15]==code+64&&(g_cpu.cpsr&CPSR_T_BIT),"BX interworking");
        require(runtime_call_stack_depth()==0,"cross-mode return balanced");
        // Device-triggered HALT/DMA/event/IRQ exits settle pending debt once.
        for(int kind=1;kind<=4;++kind){
            initialize(cpu);event=kind;runtime_deferred_cycles_set(17);
            bus_write_u32(code,0xe5830000);bus_write_u32(code+4,0xe2900001);
            if(kind==3){g_cpu.cpsr&=~CPSR_I_BIT;stop_at=1;}
            tier3_run(code);snapshot();
            require(g_insn_count[cpu]==1,"device event retires exactly one instruction");
            require(runtime_deferred_cycles()==0,"debt consumed once");
            require(runtime_unwinding(),"device/observer preserves unwind");
            require(g_cpu.R[15]==(kind==3?24:code+4),"event PC");
        }
        // Stop before execution, then an exact retired-index nested static unwind.
        initialize(cpu);loop();g_nds_insn_stop=true;tier3_run(code);snapshot();
        require(g_insn_count[cpu]==0&&g_cpu.R[15]==code,"pre-exec observer");
        initialize(cpu);loop();stop_at=7;
        nds_register_dispatch(cpu,parent,1,0);runtime_dispatch(0x02090000);snapshot();
        require(g_insn_count[cpu]==7&&g_cpu.R[0]==7,"exact stop under static frame");
        require(g_cpu.R[15]==code+4,"preserved PC after nested static frame");
        nds_unregister_dispatch(cpu,parent,1);
        // Actual scheduler slots execute the same copied RAM stream and slice exit.
        initialize(cpu);loop();runtime_set_deep_trace(0);scheduler_init();
        scheduler_reset_cpu(0,code,0x1f);scheduler_reset_cpu(1,code,0x1f);
        auto result=scheduler_run(512);record(result.cycles[0]);record(result.cycles[1]);record(result.rounds);
        record(scheduler_cpu_state(0));record(scheduler_cpu_state(1));snapshot();
        auto stats=tier3_stats();
        require((stats.canonical_entries[0]+stats.canonical_entries[1]>0)==tier3_canonical_state_enabled(),"selected path really executed");
    }
    std::fclose(output);
    std::printf("PASS %u real Tier3/runtime/bus/CP15/scheduler checks; canonical=%d\n",checks,tier3_canonical_state_enabled());
}
