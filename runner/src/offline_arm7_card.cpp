#include "offline_arm7_card.h"
#include "io.h"
#include "state.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace {
uint32_t game=0;
constexpr uint32_t payload_mask=0x03FFFFFFu, request_size=0x3Cu;
constexpr uint32_t completion=(1u<<6)|0x20u|11u;
struct State {
    uint32_t command=0,ordinal=0,request=0;
    bool pending=false,executed=false,backup_initialized=false;
    bool terminated=false,stopped_devices=false;
} state;
constexpr size_t snapshot_words=10;
using Request=std::array<uint32_t,request_size/4>;
uint32_t context(){return game==1?0x038090C0u:0x03809CE0u;}
bool ram(uint32_t address,uint32_t size){return address>=0x02000000u&&address<0x02800000u&&size<=0x02800000u-address;}
bool memory(uint32_t address,void* bytes,uint32_t size,bool write,uint64_t now=0){return nds_io_offline_memory(address,static_cast<uint8_t*>(bytes),size,write,now);}
uint32_t request_pointer(){return state.request?state.request:bus_device_read32(7,context());}
bool load_request(uint32_t pointer,Request* out){
    std::array<uint8_t,request_size> bytes{};
    if(!ram(pointer,request_size)||!memory(pointer,bytes.data(),request_size,false))return false;
    for(unsigned i=0;i<out->size();++i)(*out)[i]=uint32_t(bytes[4*i])|uint32_t(bytes[4*i+1])<<8|uint32_t(bytes[4*i+2])<<16|uint32_t(bytes[4*i+3])<<24;
    return true;
}
bool put(uint32_t address,uint32_t value,uint64_t now){uint8_t bytes[4]={uint8_t(value),uint8_t(value>>8),uint8_t(value>>16),uint8_t(value>>24)};return memory(address,bytes,4,true,now);}
bool parameters(uint32_t command,const Request& r){
    const uint32_t length=r[5];
    if(command==4||command==6){if(length&&!ram(r[4],length))return false;}
    if(command==7||command==8||command==9){if(length&&!ram(r[3],length))return false;}
    if(command==6||command==7||command==8||command==9||command==11){if(r[9]<1||r[9]>3)return false;}
    if(command==7||command==8){const uint32_t page=r[8];if(!page||page>0x800000u||(page&(page-1u)))return false;}
    return true;
}
bool spi(std::vector<uint8_t>& bytes,std::vector<uint8_t>* received,uint64_t now){
    if(received)received->resize(bytes.size());
    return nds_io_offline_card_spi(bytes.data(),received?received->data():nullptr,uint32_t(bytes.size()),now);
}
bool ready_status(uint64_t now){std::vector<uint8_t> bytes{5,0},received;return spi(bytes,&received,now)&&!(received[1]&1u);}
bool write_enable(uint64_t now){std::vector<uint8_t> bytes{6};return spi(bytes,nullptr,now);}
std::vector<uint8_t> address_command(uint8_t command,uint32_t address,uint32_t width){
    std::vector<uint8_t> bytes{uint8_t(command|(width==1?((address>>5)&8u):0u))};
    for(uint32_t i=width;i;--i)bytes.push_back(uint8_t(address>>(8*(i-1))));
    return bytes;
}
uint32_t execute(const Request& r,uint64_t now){
    if(state.command==0)return 0;
    if(state.command==3){
        uint8_t bytes[4]{};
        if(!nds_io_offline_card_read(0xB8,0,bytes,4,now))return 3;
        const uint32_t id=uint32_t(bytes[0])|uint32_t(bytes[1])<<8|uint32_t(bytes[2])<<16|uint32_t(bytes[3])<<24;
        return put(state.request+8,id,now)?0u:3u;
    }
    if(state.command==4){
        std::vector<uint8_t> bytes(r[5]);
        if(!nds_io_offline_card_read(0xB7,r[3],bytes.data(),uint32_t(bytes.size()),now))return 3;
        return bytes.empty()||memory(r[4],bytes.data(),uint32_t(bytes.size()),true,now)?0u:3u;
    }
    // Opcodes5,10,12 reach the SDK's explicit unsupported-result branch.
    if(state.command==5||state.command==10||state.command==12)return 3;
    if(state.command==2){
        if(r[1]==0xF03u&&!state.backup_initialized){
            std::vector<uint8_t> bytes{5,0},received;
            if(!spi(bytes,&received,now))return 3;
            if(received[1]){if(!write_enable(now))return 3;bytes={1,0};if(!spi(bytes,nullptr,now))return 3;}
            state.backup_initialized=true;
        }
        return 0;
    }
    // The SDK converts failure of the initial write-in-progress wait into6.
    // The existing cartridge chip owns status; do not manufacture success.
    if(!ready_status(now))return 6;
    const uint32_t width=r[9],length=r[5];
    if(state.command==11){
        if(!write_enable(now))return 3;
        auto bytes=address_command(0xD8,r[4],width);
        if(!spi(bytes,nullptr,now))return 3;
        return ready_status(now)?0u:4u;
    }
    if(state.command==6||state.command==9){
        const uint32_t address=state.command==6?r[3]:r[4];
        const uint32_t pointer=state.command==6?r[4]:r[3];
        auto bytes=address_command(3,address,width);const auto header=bytes.size();bytes.resize(header+length);
        std::vector<uint8_t> received;if(!spi(bytes,&received,now))return 3;
        if(state.command==6)return !length||memory(pointer,received.data()+header,length,true,now)?0u:3u;
        std::vector<uint8_t> expected(length);if(length&&!memory(pointer,expected.data(),length,false))return 3;
        return std::equal(expected.begin(),expected.end(),received.begin()+header)?0u:1u;
    }
    if(state.command==7||state.command==8){
        const uint32_t page=r[8];uint32_t offset=0;
        while(offset<length){
            const uint32_t address=r[4]+offset,amount=std::min(length-offset,page-(address&(page-1u)));
            if(!write_enable(now))return 3;
            auto bytes=address_command(state.command==7?0x0A:2,address,width);const auto header=bytes.size();bytes.resize(header+amount);
            if(!memory(r[3]+offset,bytes.data()+header,amount,false)||!spi(bytes,nullptr,now))return 3;
            if(!ready_status(now))return 4;
            offset+=amount;
        }
        return 0;
    }
    return 3;
}
std::array<uint32_t,snapshot_words> words(){return {2u,game,state.command,state.ordinal,state.request,state.pending,state.executed,state.backup_initialized,state.terminated,state.stopped_devices};}
std::array<uint32_t,snapshot_words> decode(const std::vector<uint8_t>& in){std::array<uint32_t,snapshot_words> out{};for(size_t i=0;i<out.size();++i)for(unsigned b=0;b<4;++b)out[i]|=uint32_t(in[4*i+b])<<(8*b);return out;}
}

bool nds_offline_card_configure(const std::string& sha1){
    game=sha1=="691e00d9a5dd80b04f80cc7559503e8b06848785"?1u:sha1=="90164d1ac127ee5f9815ea4ae7de798c7b5fc629"?2u:0u;
    nds_offline_card_reset();return game!=0;
}
void nds_offline_card_reset(){state={};}
bool nds_offline_card_ready(){
    if(!game||state.terminated||state.pending||state.ordinal||!nds_io_offline_card_idle())return false;
    // Busy, queued, pending-remote and asynchronous work all retain the guest
    // worker. A complete packet can be pending even with ordinal already zero.
    return !(bus_device_read32(7,context()+0xFC)&0x5Cu)&&bus_device_read32(7,context()+8)==0;
}
bool nds_offline_card_preflight(uint32_t tag,uint32_t payload,bool error){
    if(!game||state.terminated||payload>payload_mask||(tag!=11&&tag!=13&&tag!=14))return false;
    if(tag!=11||!error)return true;
    if(state.ordinal)return state.command==0&&ram(payload,request_size);
    if(payload==0)return true;
    if(payload<2||payload>12)return false;
    Request request{};return load_request(request_pointer(),&request)&&parameters(payload,request);
}
bool nds_offline_card_can_receive(uint32_t tag,uint32_t payload,bool error){
    if(!nds_offline_card_preflight(tag,payload,error))return false;
    if(tag!=11||!error)return true;
    return !state.pending&&(state.ordinal||payload==0||nds_io_offline_card_idle());
}
void nds_offline_card_advance(uint64_t now){
    if(!state.pending||state.terminated)return;
    if(!state.executed){
        if(!nds_io_offline_card_idle())return;
        Request request{};
        if(!load_request(state.request,&request)||!parameters(state.command,request))return;
        if(!put(state.request,0,now))return;
        const uint32_t result=execute(request,now);
        if(!put(state.request,result,now))return;
        state.executed=true;
    }
    if(nds_io_offline_reply_capacity(1)&&nds_io_offline_reply(completion,now)){state.pending=false;state.executed=false;}
}
uint64_t nds_offline_card_next_event(uint64_t now){
    if(!state.pending||state.terminated)return UINT64_MAX;
    return state.executed?(nds_io_offline_reply_capacity(1)?now:UINT64_MAX):(nds_io_offline_card_idle()?now:UINT64_MAX);
}
bool nds_offline_card_receive(uint32_t tag,uint32_t payload,bool error,uint64_t now){
    if(!nds_offline_card_can_receive(tag,payload,error))return false;
    if(tag==11){
        if(!error)return true;
        if(state.ordinal){state.request=payload;state.ordinal=0;state.pending=true;}
        else if(payload==0){state.command=0;state.ordinal=1;return true;}
        else {state.command=payload;state.request=request_pointer();state.pending=true;}
        nds_offline_card_advance(now);return true;
    }
    const bool cleanup=(payload&63u)==(tag==13?2u:1u);
    if(cleanup)nds_io_offline_card_stop(now);
    state.pending=false;state.executed=false;state.ordinal=0;
    state.stopped_devices=cleanup;state.terminated=true;return true;
}
bool nds_offline_card_terminated(){return state.terminated;}
bool nds_offline_card_snapshot(std::vector<uint8_t>* out){if(!out)return false;out->clear();for(auto word:words())for(unsigned b=0;b<4;++b)out->push_back(uint8_t(word>>(8*b)));return true;}
bool nds_offline_card_validate(const std::vector<uint8_t>& in,std::string* error){
    if(in.size()==snapshot_words*4){const auto w=decode(in);
        if(w[0]==2&&w[1]==game&&(w[2]==0||(w[2]>=2&&w[2]<=12))&&w[3]<=1&&(!w[4]||ram(w[4],request_size))&&
           w[5]<=1&&w[6]<=1&&w[7]<=1&&w[8]<=1&&w[9]<=1&&(!w[3]||(!w[2]&&!w[5]))&&
           (!w[5]||(!w[3]&&w[4]))&&(!w[6]||w[5])&&(!w[8]||(!w[3]&&!w[5]))&&(!w[9]||w[8]))return true;
    }
    if(error)*error="invalid native cartridge task state";
    return false;
}
bool nds_offline_card_import(const std::vector<uint8_t>& in,std::string* error){
    if(!nds_offline_card_validate(in,error))return false;
    const auto w=decode(in);
    State next;next.command=w[2];next.ordinal=w[3];next.request=w[4];next.pending=w[5];next.executed=w[6];next.backup_initialized=w[7];next.terminated=w[8];next.stopped_devices=w[9];state=next;return true;
}
