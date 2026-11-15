// SPDX-License-Identifier: MIT
#include "offline_arm7.h"
#include "offline_arm7_card.h"
#include "io.h"
#include "spu.h"
#include "sha1.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

bool nds_offline_sound_ready();
bool nds_offline_sound_configure(const char*);
bool nds_offline_sound_takeover(uint64_t,uint64_t);
bool nds_offline_sound_preflight(uint32_t,bool);
bool nds_offline_sound_can_receive(uint32_t,bool);
bool nds_offline_sound_receive(uint32_t,bool,uint64_t);
uint64_t nds_offline_sound_next_event(uint64_t);
void nds_offline_sound_advance(uint64_t);
void nds_offline_sound_sleep(bool,uint64_t);
void nds_offline_sound_reset();
bool nds_offline_sound_snapshot(std::vector<uint8_t>*);
bool nds_offline_sound_validate(const std::vector<uint8_t>&,std::string*);
bool nds_offline_sound_import(const std::vector<uint8_t>&,std::string*);

namespace {
constexpr uint64_t Never=UINT64_MAX,Line=2130,Frame=Line*263;
struct Addresses {uint32_t touch,power,mic,firmware,rtc,queue,led,patterns;};
constexpr Addresses address[2]={
    {0x03809DD4,0x03809EB8,0x03809EEC,0x03809F64,0x03809F88,0x03809BDC,0x03809EE8,0x038075D4},
    {0x0380A9F4,0x0380AAD8,0x0380BA7C,0x027F958C,0x027F95B0,0x0380A7FC,0x0380AB08,0x038081F4}};
struct Proof {uint32_t pc,size;const char* hash;};
constexpr Proof proofs[2][5]={
    {{0x0380121C,0x70,"46139cc6e31279ee0b85a4327489ec3e27e5512e"},{0x03801B14,0x1B8,"afb5ac9ed010fc851ea003f9675f52e7bfbde505"},
     {0x03802510,0x1CC,"77ac7a982b0d31e755b141e6e5042b5168a85f5a"},{0x03804338,0x1A0,"6341d6d3bbcd1f2fd357d426544ab848062a7b17"},
     {0x03805584,0x134,"b801e3ec3f4d9d395997b8857447753190674ef7"}},
    {{0x03804660,0x70,"95fe1d3e65a21fc6afab277064b1cca8ae350e15"},{0x03804F58,0x1B8,"afb5ac9ed010fc851ea003f9675f52e7bfbde505"},
     {0x03805954,0x1CC,"77ac7a982b0d31e755b141e6e5042b5168a85f5a"},{0x027F5A70,0x1A0,"17d5837dc373e5ca894326b8873dafeef7d7da79"},
     {0x027F6CBC,0x134,"bd6e84be741f0d368bc271b8945dc330b7bbdecf"}}};
struct State {
    bool active=false,touch_auto=false,mic_auto=false,sleep=false,reset_pending=false;
    std::array<std::array<uint16_t,16>,4> packets{}; // firmware,touch,power,mic
    uint32_t samples=0,phase=0,filter=20,led=1,led_frame=0,led_actual=1;
    uint32_t mic_flags=0,mic_base=0,mic_size=0,mic_pos=0,mic_period=1,mic_half=0;
    uint32_t sleep_flags=0,sleep_pm=0,sleep_rcnt=0;
    uint32_t touch_pending=0,touch_raw=0;
    uint64_t led_next=Never,mic_next=Never;
    std::array<uint64_t,4> touch_next{Never,Never,Never,Never};
    std::array<std::array<uint32_t,3>,12> patterns{};
} state;
unsigned game=0; // 0 unavailable, 1 MKDS, 2 MPH; fixed before boot
uint64_t next_takeover_check=0;
bool fail(std::string* error,const char* message){if(error)*error=message;return false;}
bool memory(uint32_t addr,void* bytes,uint32_t size,bool write,uint64_t now=0){return nds_io_offline_memory(addr,static_cast<uint8_t*>(bytes),size,write,now);}
uint32_t load32(uint32_t addr){uint8_t b[4]{};if(!memory(addr,b,4,false))return UINT32_MAX;return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);}
bool store(uint32_t addr,uint32_t value,unsigned size,uint64_t now){uint8_t b[4]={uint8_t(value),uint8_t(value>>8),uint8_t(value>>16),uint8_t(value>>24)};return memory(addr,b,size,true,now);}
bool ram(uint32_t addr,uint32_t size){return addr>=0x02000000u&&addr<0x02800000u&&size<=0x02800000u-addr;}
uint32_t wire(unsigned tag,uint32_t payload){return (payload<<6)|tag;}
bool reply(unsigned tag,unsigned command,unsigned status,uint64_t now){
    const uint32_t payload=tag==5?0x8000u|((command&0x7Fu)<<8)|(status&0xFFu):
        0x03000000u|((command|0x80u)<<8)|(status&0xFFu);
    return nds_io_offline_reply(wire(tag,payload),now);
}
int slot(unsigned tag){return tag==4?0:tag==6?1:tag==8?2:tag==9?3:-1;}
bool command(unsigned tag,unsigned cmd){
    if(tag==4)return cmd>=0x20&&cmd<=0x2D;
    if(tag==6)return cmd<=3;
    if(tag==8)return cmd==0x60||cmd==0x61||(cmd>=0x63&&cmd<=0x67);
    if(tag==9)return cmd>=0x40&&cmd<=0x43;
    if(tag==5)return cmd<=1||(cmd>=0x10&&cmd<=0x19)||(cmd>=0x20&&cmd<=0x29);
    return false;
}
uint32_t pair(const std::array<uint16_t,16>& cells,unsigned high,unsigned low){return (uint32_t(cells[high])<<16)|cells[low];}
std::array<uint16_t,16> preview(unsigned tag,uint32_t payload){
    auto cells=state.packets[slot(tag)];if(payload&0x02000000u)cells.fill(0);
    cells[(payload>>16)&15]=uint16_t(payload);return cells;
}
uint64_t next_line(uint64_t now,unsigned line){const uint64_t at=(now/Frame)*Frame+(line%263)*Line;return at>now?at:at+Frame;}
void schedule_touch(uint64_t now){state.touch_next.fill(Never);for(unsigned i=0;i<state.samples;++i)state.touch_next[i]=next_line(now,(i*263/state.samples+state.phase)%263);}
bool sample_touch(unsigned cmd,unsigned index,uint64_t now){return store(0x027FFFAA,nds_io_offline_touch(),4,now)&&reply(6,cmd,index,now);}
bool pm_read(uint8_t reg,uint8_t* value){const uint8_t tx[2]={uint8_t(reg|0x80u),0};uint8_t rx[2]{};if(!nds_io_offline_spi(0,tx,rx,2))return false;*value=rx[1];return true;}
bool pm_write(uint8_t reg,uint8_t value){const uint8_t tx[2]={reg,value};return nds_io_offline_spi(0,tx,nullptr,2);}
bool pm_mask(uint8_t set,uint8_t clear){uint8_t value;if(!pm_read(0,&value))return false;return pm_write(0,uint8_t((value|set)&~clear));}
bool led_mode(unsigned mode){state.led_actual=mode;if(mode==1)return pm_mask(0,0x10);if(mode==2)return pm_mask(0x10,0x20);if(mode==3)return pm_mask(0x30,0);return false;}
bool power_switch(unsigned op){
    if(op>=1&&op<=3){static constexpr unsigned modes[3]={1,3,2};state.led=modes[op-1];state.led_frame=0;return led_mode(state.led);}
    static constexpr uint8_t masks[12]={4,4,8,8,12,12,1,1,2,2,0,0x40};
    if(op<4||op>15)return false;
    if(op==14)return pm_mask(0x40,1);
    return (op&1)?pm_mask(0,masks[op-4]):pm_mask(masks[op-4],0);
}
bool touch_request(const std::array<uint16_t,16>& c,uint64_t now){
    const unsigned cmd=c[0]>>8,arg=c[0]&255;
    if(cmd==0)return sample_touch(0,0,now);
    if(cmd==3){if(!arg)return reply(6,cmd,2,now);state.filter=arg;return reply(6,cmd,0,now);}
    if(cmd==1){if(state.touch_auto)return reply(6,cmd,3,now);if(!arg||arg>4||c[1]>=263)return reply(6,cmd,2,now);state.samples=arg;state.phase=c[1];state.touch_auto=true;schedule_touch(now);return reply(6,cmd,0,now);}
    if(!state.touch_auto)return reply(6,cmd,3,now);
    state.touch_auto=false;state.touch_next.fill(Never);state.touch_pending=0;return reply(6,cmd,0,now);
}
bool rtc_request(unsigned cmd,uint64_t now){
    nds_tick_rtc(now);
    uint8_t shared[8]{};if(!memory(0x027FFDE8,shared,8,false))return false;
    uint8_t status[2]{};
    if(cmd==0){uint8_t reset=1;if(!nds_io_offline_rtc(0,true,&reset,1))return false;return reply(5,cmd,0,now);}
    if(cmd==1){
        if((shared[0]&2u)!=0){
            if(!nds_io_offline_rtc(0,false,status,1))return false;
            if(!(status[0]&2u)){
                status[0]|=2;if(!nds_io_offline_rtc(0,true,status,1))return false;
                for(uint8_t reg:{uint8_t(0x10),uint8_t(0x50)}){
                    uint8_t alarm[3]{};if(!nds_io_offline_rtc(reg,false,alarm,3))return false;
                    const unsigned hour=alarm[1]&0x3F;
                    if(hour<=0x11&&(hour&15)<10){if(alarm[1]&0x40)alarm[1]=uint8_t((alarm[1]&0xC0)|(hour+((hour&15)>=8?0x18:0x12)));}
                    else if(hour>=0x12&&hour<=0x23&&(hour&15)<10)alarm[1]|=0x40;
                    else alarm[1]&=0x80;
                    if(!nds_io_offline_rtc(reg,true,alarm,3))return false;
                }
            }
        }
        return reply(5,cmd,0,now);
    }
    const unsigned op=cmd&15;
    const bool write=cmd>=0x20;
    static constexpr uint8_t regs[10]={0x20,0x20,0x60,0x10,0x10,0x50,0,0x40,0x30,0x70};
    static constexpr uint8_t sizes[10]={7,4,3,1,3,3,1,1,1,1};
    static constexpr uint8_t offsets[10]={0,0,4,4,4,4,0,2,4,4};
    if(op==3||op==4||op==5){
        if(!nds_io_offline_rtc(0x40,false,status,1))return false;
        const bool allowed=op==3?((status[0]&11)==1):op==4?((status[0]&15)==4):(status[0]&0x40)!=0;
        if(!allowed)return reply(5,cmd,2,now);
    }
    if(write&&op==1){if(!nds_io_offline_rtc(0x60,false,shared+4,3))return false;}
    const unsigned size=write&&op==1?7:sizes[op];
    const unsigned offset=write&&op==1?0:offsets[op];
    if(!nds_io_offline_rtc(regs[op],write,shared+offset,size))return false;
    if(!write||(write&&op==1))if(!memory(0x027FFDE8,shared,8,true,now))return false;
    return reply(5,cmd,0,now);
}
bool firmware_request(const std::array<uint16_t,16>& c,uint64_t now){
    const unsigned cmd=c[0]>>8;
    const uint32_t addr=((uint32_t(c[0])&255)<<16)|c[1];
    uint32_t size=0,buffer=0;bool output=false;
    uint8_t opcode=0;
    if(cmd==0x20)opcode=6;else if(cmd==0x21)opcode=4;
    else if(cmd==0x22||cmd==0x2C){opcode=cmd==0x22?5:0x9F;size=cmd==0x22?1:3;buffer=(uint32_t(c[0]&255)<<24)|(uint32_t(c[1])<<8)|(c[2]>>8);output=true;}
    else if(cmd==0x23||cmd==0x24){opcode=cmd==0x23?3:0x0B;size=pair(c,2,3);buffer=pair(c,4,5);output=true;}
    else if(cmd==0x25||cmd==0x26){opcode=cmd==0x25?0x0A:2;size=std::min<uint32_t>(c[2],256-(addr&255));buffer=pair(c,3,4);}
    else if(cmd==0x27)opcode=0xDB;else if(cmd==0x28)opcode=0xD8;
    else if(cmd==0x29)opcode=0xB9;else if(cmd==0x2A)opcode=0xAB;else if(cmd==0x2B)opcode=0xC7;else if(cmd==0x2D)opcode=0xFF;
    if(size&&!ram(buffer,size))return reply(4,cmd,2,now);
    if((cmd>=0x23&&cmd<=0x28)||cmd==0x2B){
        const uint8_t poll[2]={5,0};uint8_t status[2]{};
        if(!nds_io_offline_spi(1,poll,status,2))return false;
        if((status[1]&1u)||((cmd>=0x25)&&!(status[1]&2u)))return reply(4,cmd,3,now);
    }
    std::vector<uint8_t> tx;tx.push_back(opcode);
    const bool address_bytes=cmd>=0x23&&cmd<=0x28;
    if(address_bytes){tx.push_back(uint8_t(addr>>16));tx.push_back(uint8_t(addr>>8));tx.push_back(uint8_t(addr));if(cmd==0x24)tx.push_back(0);}
    const auto header=tx.size();tx.resize(header+size);
    if(size&&!output&&!memory(buffer,tx.data()+header,size,false))return false;
    std::vector<uint8_t> rx(tx.size());if(!nds_io_offline_spi(1,tx.data(),rx.data(),uint32_t(tx.size())))return false;
    if(size&&output&&!memory(buffer,rx.data()+header,size,true,now))return false;
    return reply(4,cmd,0,now);
}
bool power_request(const std::array<uint16_t,16>& c,uint64_t now){
    const unsigned cmd=c[0]>>8,arg=c[0]&255;
    if(cmd==0x60)return reply(8,cmd,0,now);
    if(cmd==0x63){if(arg==14){nds_offline_sound_advance(now);nds_spu_write(0x04000504u,0,2);}if(!power_switch(arg))return reply(8,cmd,2,now);return reply(8,cmd,0,now);}
    if(cmd==0x64){if(!pm_write(uint8_t(c[1]),uint8_t(arg)))return false;return reply(8,cmd,0,now);}
    if(cmd==0x65){uint8_t value;if(!pm_read(uint8_t(arg),&value))return false;return reply(8,0x70+arg,value,now);}
    if(cmd==0x66){if(arg<=15){state.led=arg;state.led_frame=0;}return reply(8,cmd,0,now);}
    if(cmd==0x67)return reply(8,cmd,state.led,now);
    if(state.sleep)return reply(8,cmd,3,now);
    uint8_t previous;if(!pm_read(0,&previous))return false;
    state.sleep=true;state.sleep_pm=previous;state.sleep_flags=arg;
    state.sleep_rcnt=nds_io_offline_sleep_enter(state.sleep_flags,c[1],now);
    state.led=2;state.led_frame=0;led_mode(2);pm_mask(0,1);
    nds_offline_sound_sleep(true,now);nds_spu_write(0x04000504u,0,2);
    return true; // SDK sleep replies only after the selected wake source.
}
uint32_t mic_period(uint32_t value){if(value>=0x04000000u)return 0;if(!value)return 65536;const unsigned shift=value<0x10000?0:value<0x400000?6:value<0x1000000?8:10;return (value>>shift)<<shift;}
uint16_t mic_sample(uint32_t flags){const uint16_t value=(flags&1)?0xFFF0:0xFF;return value^((flags&2)?((flags&1)?0x8000:0x80):0);}
bool microphone_request(const std::array<uint16_t,16>& c,uint64_t now){
    const unsigned cmd=c[0]>>8,arg=c[0]&255;
    if(cmd==0x40){const auto sample=mic_sample(arg);if(!store(0x027FFF94,sample,2,now)||!store(0x027FFF90,0x027FFF94,4,now))return false;return reply(9,cmd,0,now);}
    if(cmd==0x41){if(state.mic_auto)return reply(9,cmd,3,now);const uint32_t base=pair(c,1,2),size=pair(c,3,4),period=mic_period(pair(c,5,6));if(!period||!size||!ram(base,size)||base+size>0x02400000u||((arg&1)&&((base|size)&1)))return reply(9,cmd,2,now);state.mic_auto=true;state.mic_flags=arg;state.mic_base=base;state.mic_size=size;state.mic_pos=0;state.mic_period=period;state.mic_next=now+period;store(0x027FFF90,0,4,now);store(0x027FFF94,0,2,now);return reply(9,cmd,0,now);}
    if(!state.mic_auto)return reply(9,cmd,3,now);
    if(cmd==0x42){state.mic_auto=false;state.mic_next=Never;return reply(9,cmd,0,now);}
    const uint32_t period=mic_period(pair(c,1,2));if(!period)return reply(9,cmd,2,now);state.mic_period=period;state.mic_next=now+period;return reply(9,cmd,0,now);
}
bool rtc_interrupt(uint64_t now){
    NdsRtcDebugState rtc{};nds_rtc_debug_state(&rtc);
    const unsigned alarms=(rtc.status1>>4)&3;
    if(!alarms){nds_io_offline_irq_clear(0x80);return true;}
    if(!nds_io_offline_reply_capacity(1))return false;
    uint8_t status1,status2;if(!nds_io_offline_rtc(0,false,&status1,1)||!nds_io_offline_rtc(0x40,false,&status2,1))return false;
    if(alarms&1)status2&=~15u;
    if(alarms&2)status2&=~0x40u;
    if(!nds_io_offline_rtc(0x40,true,&status2,1)||!reply(5,0x30,alarms,now))return false;
    nds_io_offline_irq_clear(0x80);return true;
}
bool sound_deadline(uint64_t now,uint64_t* deadline){
    const uint32_t alarm=game==1?0x038079D4u:0x038085F4u;
    const uint64_t fire=uint64_t(load32(alarm+0xC))|(uint64_t(load32(alarm+0x10))<<32);
    const uint64_t period=uint64_t(load32(alarm+0x1C))|(uint64_t(load32(alarm+0x20))<<32);
    return period==2728u&&nds_io_offline_tick_deadline(fire,now,deadline)&&*deadline>now;
}
}

bool nds_offline_arm7_configure(const std::string& sha,bool offline,std::string* error){
    game=offline?(sha=="691e00d9a5dd80b04f80cc7559503e8b06848785"?1:sha=="90164d1ac127ee5f9815ea4ae7de798c7b5fc629"?2:0):0;
    nds_io_offline_set_tick_epoch_address(game==1?0x03807888u:game==2?0x038084A8u:0u);
    nds_offline_card_configure(sha);nds_offline_sound_configure(sha.c_str());nds_offline_arm7_reset();
    return game?true:fail(error,"Offline ARM7 services require a supported retail image and offline networking");
}
bool nds_offline_arm7_selected(){return game!=0;}
bool nds_offline_arm7_active(){return state.active;}
bool nds_offline_arm7_reset_pending(){return state.reset_pending;}
void nds_offline_arm7_reset(){state=State{};next_takeover_check=0;nds_offline_sound_reset();nds_offline_card_reset();}
bool nds_offline_arm7_try_takeover(uint64_t now,std::string* error){
    if(state.active)return true;
    if(now<next_takeover_check)return false;
    next_takeover_check=next_line(now,192);
    if(!game||!nds_offline_sound_ready()||!nds_offline_card_ready()||!nds_io_offline_spi_idle())return fail(error,"Offline ARM7 components or SPI are not ready");
    const auto& a=address[game-1];
    constexpr uint32_t required=(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<12)|(1u<<13)|(1u<<14);
    if((load32(0x027FFF8C)&required)!=required||load32(a.queue+28)||load32(a.rtc+0x1D4)||load32(a.power+0x20))return fail(error,"Retail ARM7 bootstrap has pending services");
    uint32_t pending;if(nds_io_offline_peek(&pending))return fail(error,"Retail ARM7 bootstrap has pending FIFO requests");
    for(const auto& p:proofs[game-1]){std::vector<uint8_t> bytes(p.size);if(!memory(p.pc,bytes.data(),p.size,false)||gba::sha1(bytes.data(),bytes.size()).hex()!=p.hash)return fail(error,"Retail ARM7 peripheral code proof failed");}
    State next;
    const uint32_t buffers[4]={a.firmware,a.touch,a.power,a.mic};
    for(unsigned i=0;i<4;++i){uint8_t bytes[32];if(!memory(buffers[i],bytes,32,false))return fail(error,"ARM7 packet state unavailable");for(unsigned j=0;j<16;++j)next.packets[i][j]=uint16_t(bytes[j*2])|(uint16_t(bytes[j*2+1])<<8);}
    const uint32_t touch=load32(a.touch+0x20),mic=load32(a.mic+0x20);
    if((touch!=0&&touch!=2)||(mic!=0&&mic!=2))return fail(error,"ARM7 sampling transition is still pending");
    next.filter=load32(a.touch+0x24);next.led=load32(a.led);next.led_actual=load32(a.patterns-4);next.led_frame=load32(a.led-4);
    if(next.filter>255||next.led>15||next.led_actual>3)return fail(error,"ARM7 driver parameters are outside the native contract");
    uint8_t patterns[144];if(!memory(a.patterns,patterns,sizeof(patterns),false))return fail(error,"ARM7 power patterns unavailable");
    for(unsigned i=0;i<12;++i)for(unsigned j=0;j<3;++j){const auto* b=patterns+i*12+j*4;next.patterns[i][j]=uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);}
    next.touch_auto=touch==2;
    if(next.touch_auto){
        // The last PXI packet may be a filter request. Adopt the live SDK
        // VAlarms rather than treating that packet as the autosample setup.
        std::array<unsigned,4> lines{};
        for(unsigned i=0;i<4;++i){
            const uint32_t alarm=a.touch+0x2C+i*0x28,handler=load32(alarm);
            if(!handler)continue;
            const uint32_t position=load32(alarm+0x10);
            if(i!=next.samples||handler!=(game==1?0x03801774u:0x03804BB8u)||load32(alarm+4)!=i||
               load32(alarm+0x1C)!=1||load32(alarm+0x24)||(position>>16)!=10||(position&65535)>=263)
                return fail(error,"ARM7 touch VAlarm is not at a known idle schedule");
            lines[next.samples++]=position&65535;
        }
        if(!next.samples)return fail(error,"ARM7 touch schedule is empty");
        next.phase=lines[0];
        for(unsigned i=1;i<next.samples;++i)if(lines[i]!=(i*263/next.samples+next.phase)%263)
            return fail(error,"ARM7 touch schedule is inconsistent");
    }
    next.mic_auto=mic==2;
    if(next.mic_auto){next.mic_flags=load32(a.mic+0x24)&0xFFFF;next.mic_base=load32(a.mic+0x28);next.mic_pos=load32(a.mic+0x2C);next.mic_size=load32(a.mic+0x30);const uint32_t reload=load32(a.mic+0x34);const unsigned shifts[4]={0,6,8,10};next.mic_period=(65536-(reload&65535))<<shifts[(reload>>16)&3];if(!next.mic_size||next.mic_pos>=next.mic_size||!ram(next.mic_base,next.mic_size))return fail(error,"ARM7 microphone schedule is invalid");next.mic_next=now+next.mic_period;}
    uint64_t driver_tick;
    if(!sound_deadline(now,&driver_tick))return fail(error,"ARM7 SDK sound alarm is not at an idle known epoch");
    if(!nds_offline_sound_takeover(now,driver_tick))return fail(error,"ARM7 sound takeover declined");
    state=next;if(state.touch_auto)schedule_touch(now);state.led_next=next_line(now,192);state.active=true;return true;
}
bool nds_offline_arm7_preflight(uint32_t word){
    if(!state.active||state.reset_pending||nds_offline_card_terminated()||nds_powered_off())return false;
    const unsigned tag=word&31;const bool error=word&32;const uint32_t payload=word>>6;
    if(tag==0)return true;
    if(tag==7)return nds_offline_sound_preflight(payload,error);
    if(tag==11||tag==13||tag==14)return nds_offline_card_preflight(tag,payload,error);
    if(tag==12)return error||((payload>>8)&127)==0x10;
    if(tag==5)return error||command(tag,(payload>>8)&127);
    if(slot(tag)<0)return false; // WM and unknown services never echo success.
    if(error)return true;
    const auto c=preview(tag,payload);return command(tag,c[0]>>8);
}
bool nds_offline_arm7_receive(uint32_t word,uint64_t now){
    if(state.sleep||!nds_offline_arm7_preflight(word))return false;
    const unsigned tag=word&31;const bool error=word&32;const uint32_t payload=word>>6;
    if(tag==7&&!nds_offline_sound_can_receive(payload,error))return false;
    const bool completes=tag==5||(slot(tag)>=0&&(payload&0x01000000u));
    if(completes&&!error&&(!nds_io_offline_reply_capacity(1)||!nds_io_offline_spi_idle()))return false;
    if(!nds_io_offline_receive_word(word,now))return false;
    nds_io_offline_irq_clear(0x40000u);
    if(!tag)return true;
    if(tag==7)return nds_offline_sound_receive(payload,error,now);
    if(tag==11||tag==13||tag==14)return nds_offline_card_receive(tag,payload,error,now);
    if(error)return true;
    if(tag==12){state.reset_pending=true;return true;}
    if(tag==5)return rtc_request((payload>>8)&127,now);
    auto cells=preview(tag,payload);state.packets[slot(tag)]=cells;
    if(!(payload&0x01000000u))return true;
    if(tag==6)return touch_request(cells,now);
    if(tag==4)return firmware_request(cells,now);
    if(tag==8)return power_request(cells,now);
    return microphone_request(cells,now);
}
uint64_t nds_offline_arm7_next_event(uint64_t now){
    if(!state.active||state.reset_pending||nds_offline_card_terminated()||nds_powered_off())return Never;
    // PM_Sleep masks every callback IRQ except the selected wake sources.
    if(state.sleep)return (state.sleep_flags&2)?nds_io_offline_rtc_next_event(now):Never;
    uint64_t next=state.led_next;
    const bool reply_room=nds_io_offline_reply_capacity(1);
    auto event=[&](uint64_t at){if(at>now||reply_room)next=std::min(next,at);};
    event(state.mic_next);for(auto at:state.touch_next)event(at);
    if(reply_room||!(nds_io_offline_wake_flags()&0x80u))event(nds_io_offline_rtc_next_event(now));
    next=std::min(next,nds_offline_sound_next_event(now));return next;
}
void nds_offline_arm7_advance(uint64_t now){
    if(!state.active||state.reset_pending||nds_offline_card_terminated()||nds_powered_off())return;
    if(nds_io_offline_rtc_next_event(now)<=now)nds_tick_rtc(now);
    if(state.sleep){
        uint32_t mask=0;
        if(state.sleep_flags&1)mask|=0x1000;
        if(state.sleep_flags&2)mask|=0x80;
        if(state.sleep_flags&4)mask|=0x400000;
        if(state.sleep_flags&8)mask|=0x100000;
        if(state.sleep_flags&0x10)mask|=0x2000;
        if(!(nds_io_offline_wake_flags()&mask)||!nds_io_offline_reply_capacity(1)){
            nds_offline_sound_advance(now); // Hardware SPU only while software sleeps.
            return;
        }
        pm_write(0,uint8_t(state.sleep_pm));
        power_switch((state.sleep_flags&0x20)?6:7);power_switch((state.sleep_flags&0x40)?4:5);
        nds_io_offline_sleep_leave(state.sleep_flags,uint16_t(state.sleep_rcnt));
        pm_mask(1,0);nds_spu_write(0x04000504u,512,2);
        state.sleep=false;nds_io_offline_reply(wire(8,0),now);
        // Restoring the SDK IRQ mask dispatches one pending callback, not every
        // missed timer edge. VAlarms outside their ten-line tolerance are skipped.
        for(auto& at:state.touch_next)if(at!=Never&&at<=now){
            const unsigned line=unsigned((at%Frame)/Line);
            const unsigned late=(unsigned((now%Frame)/Line)+263-line)%263;
            if(late<=10){const uint64_t delta=uint64_t(late)*Line+(now%Line);at=now-delta;}
            else at=next_line(now,line);
        }
        if(state.mic_next!=Never&&state.mic_next<=now)state.mic_next=now-(now-state.mic_next)%state.mic_period;
        if(state.led_next!=Never&&state.led_next<=now)state.led_next=now-(now-state.led_next)%Frame;
        nds_offline_sound_sleep(false,now);
    }
    for(unsigned count=0;count<4;++count){
        unsigned i=state.touch_pending?state.touch_pending-1:unsigned(std::min_element(state.touch_next.begin(),state.touch_next.end())-state.touch_next.begin());
        if(state.touch_next[i]>now)break;
        if(!state.touch_pending){state.touch_raw=nds_io_offline_touch();if(!store(0x027FFFAA,state.touch_raw,4,now))return;state.touch_pending=i+1;}
        if(!nds_io_offline_reply_capacity(1))break;
        const unsigned pending=state.touch_pending-1;
        if(!reply(6,0x10,pending,now))return;
        state.touch_pending=0;state.touch_next[pending]+=Frame;
    }
    if(state.mic_auto&&state.mic_next<=now){
        const unsigned bytes=(state.mic_flags&1)?2:1;
        if(state.mic_pos+bytes>=state.mic_size&&!nds_io_offline_reply_capacity(1))return;
        const uint16_t sample=mic_sample(state.mic_flags);
        if(bytes==2||state.mic_pos&1){const uint32_t at=state.mic_base+state.mic_pos-(bytes==1);const uint16_t value=bytes==2?sample:uint16_t(state.mic_half|(sample<<8));store(at,value,2,now);store(0x027FFF90,at,4,now);store(0x027FFF94,value,2,now);}else state.mic_half=sample;
        state.mic_pos+=bytes;state.mic_next+=state.mic_period;
        if(state.mic_pos>=state.mic_size){if(!nds_io_offline_reply_capacity(1))return;reply(9,0x51,0,now);state.mic_pos=0;if(!(state.mic_flags&0x10)){state.mic_auto=false;state.mic_next=Never;}}
    }
    if(state.led_next<=now){
        if(state.led==0){state.led=1;state.led_frame=0;led_mode(1);}
        else if(state.led<4){if(state.led_actual!=state.led)led_mode(state.led);}
        else {const auto& p=state.patterns[state.led-4];const uint32_t length=p[2]>>16;const unsigned bit=length?state.led_frame%length:0;const bool on=bit<32?((p[1]>>(31-bit))&1):bit<64?((p[0]>>(63-bit))&1):false;pm_mask(on?0x10:0x30,on?0x20:0);++state.led_frame;}
        state.led_next+=Frame;
    }
    const uint32_t irq=nds_io_offline_wake_flags();
    if(irq&0x80)rtc_interrupt(now);
    uint32_t word;for(unsigned i=0;i<16&&nds_io_offline_peek(&word);++i)if(!nds_offline_arm7_receive(word,now))break;
    if(!state.reset_pending&&!nds_offline_card_terminated())nds_offline_sound_advance(now);
}

namespace {
void put32(std::vector<uint8_t>& out,uint32_t value){for(unsigned i=0;i<4;++i)out.push_back(uint8_t(value>>(i*8)));}
void put64(std::vector<uint8_t>& out,uint64_t value){put32(out,uint32_t(value));put32(out,uint32_t(value>>32));}
struct Reader {
    const std::vector<uint8_t>& in;size_t at=0;bool valid=true;
    uint32_t word(){if(in.size()-std::min(at,in.size())<4){valid=false;return 0;}uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(in[at++])<<(i*8);return value;}
    uint64_t wide(){const auto low=word();return uint64_t(low)|(uint64_t(word())<<32);}
    bool blob(std::vector<uint8_t>* out){const uint32_t size=word();if(!valid||size>0x800000||size>in.size()-std::min(at,in.size()))return false;out->assign(in.begin()+at,in.begin()+at+size);at+=size;return true;}
};
bool decode(const std::vector<uint8_t>& in,State* out,std::vector<uint8_t>* sound,std::vector<uint8_t>* card,std::string* error){
    Reader r{in};if(r.word()!=0x3746464Fu||r.word()!=2||r.word()!=game)return fail(error,"Offline ARM7 state identity is invalid");
    State next;
    uint32_t booleans[5];for(auto& value:booleans)value=r.word();
    for(auto value:booleans)if(value>1)return fail(error,"Offline ARM7 state has invalid flags");
    next.active=booleans[0];next.touch_auto=booleans[1];next.mic_auto=booleans[2];next.sleep=booleans[3];next.reset_pending=booleans[4];
    for(auto& packet:next.packets)for(auto& value:packet){const auto cell=r.word();if(cell>65535)return fail(error,"Offline ARM7 packet cell is invalid");value=uint16_t(cell);}
    uint32_t* fields[]={&next.samples,&next.phase,&next.filter,&next.led,&next.led_frame,&next.led_actual,&next.mic_flags,&next.mic_base,&next.mic_size,&next.mic_pos,&next.mic_period,&next.mic_half,&next.sleep_flags,&next.sleep_pm,&next.sleep_rcnt,&next.touch_pending,&next.touch_raw};
    for(auto* field:fields)*field=r.word();
    next.led_next=r.wide();next.mic_next=r.wide();
    for(auto& at:next.touch_next)at=r.wide();
    for(auto& pattern:next.patterns)for(auto& value:pattern)value=r.word();
    if(!r.valid||(!game&&next.active)||next.samples>4||next.phase>=263||next.filter>255||next.led>15||next.led_actual>3||next.mic_flags>255||next.mic_half>65535||next.sleep_pm>255||next.sleep_rcnt>65535||next.touch_pending>next.samples||
       (next.touch_auto&&!next.samples)||(next.mic_auto&&(!next.mic_period||!next.mic_size||next.mic_pos>=next.mic_size||!ram(next.mic_base,next.mic_size))))return fail(error,"Offline ARM7 driver state is invalid");
    for(unsigned i=0;i<4;++i)if((!next.touch_auto||i>=next.samples)&&next.touch_next[i]!=Never)return fail(error,"Offline ARM7 touch deadline is invalid");
    if(!next.mic_auto&&next.mic_next!=Never)return fail(error,"Offline ARM7 microphone deadline is invalid");
    if(!r.blob(sound)||!r.blob(card)||r.at!=in.size())return fail(error,"Offline ARM7 state size is invalid");
    if(!nds_offline_sound_validate(*sound,error)||!nds_offline_card_validate(*card,error))return false;
    *out=next;return true;
}
}
bool nds_offline_arm7_snapshot(std::vector<uint8_t>* out,std::string* error){
    if(!out)return fail(error,"Offline ARM7 snapshot output is null");
    std::vector<uint8_t> sound,card;if(!nds_offline_sound_snapshot(&sound)||!nds_offline_card_snapshot(&card))return fail(error,"Offline ARM7 component snapshot failed");
    std::vector<uint8_t> bytes;put32(bytes,0x3746464F);put32(bytes,2);put32(bytes,game);
    for(bool value:{state.active,state.touch_auto,state.mic_auto,state.sleep,state.reset_pending})put32(bytes,value);
    for(const auto& packet:state.packets)for(auto value:packet)put32(bytes,value);
    for(auto value:{state.samples,state.phase,state.filter,state.led,state.led_frame,state.led_actual,state.mic_flags,state.mic_base,state.mic_size,state.mic_pos,state.mic_period,state.mic_half,state.sleep_flags,state.sleep_pm,state.sleep_rcnt,state.touch_pending,state.touch_raw})put32(bytes,value);
    put64(bytes,state.led_next);put64(bytes,state.mic_next);for(auto at:state.touch_next)put64(bytes,at);
    for(const auto& pattern:state.patterns)for(auto value:pattern)put32(bytes,value);
    put32(bytes,uint32_t(sound.size()));bytes.insert(bytes.end(),sound.begin(),sound.end());put32(bytes,uint32_t(card.size()));bytes.insert(bytes.end(),card.begin(),card.end());
    *out=std::move(bytes);return true;
}
bool nds_offline_arm7_validate(const std::vector<uint8_t>& in,std::string* error){State next;std::vector<uint8_t> sound,card;return decode(in,&next,&sound,&card,error);}
bool nds_offline_arm7_import(const std::vector<uint8_t>& in,std::string* error){
    State next;std::vector<uint8_t> sound,card;if(!decode(in,&next,&sound,&card,error))return false;
    if(!nds_offline_sound_import(sound,error)||!nds_offline_card_import(card,error))return false;
    state=next;next_takeover_check=0;return true;
}
