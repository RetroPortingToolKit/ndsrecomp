#include "hle_mkds_assets.h"
#include "execution_model.h"
#include "hle_vector_math.h"
#include "sha1.h"
#include "state.h"
#include "tier3.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace mkds_assets {
namespace {
constexpr uint32_t entry=0x02047E94,resume=0x02047F10,finish=0x02047F62;
constexpr uint32_t allocator=0x02036B74,heap_query=0x02017748;
constexpr uint32_t magic=0x414B444D,frame_size=32,max_output=0x200000;
enum Word { Magic,Offset,Length,Decoded,Context,Heap,Return,Cookie };
nds_assets::Services service;
Statistics count;
std::array<uint8_t,0xE8> code{};
std::array<uint8_t,24> getters{};
std::array<uint8_t,120> allocation_code{};
std::array<uint8_t,40> query_code{};
const NdsStaticValidationRange dependencies[]={
    {0x020364E0,getters.size(),getters.data()},
    {allocator,allocation_code.size(),allocation_code.data()},
    {heap_query,query_code.size(),query_code.data()},
};
NdsStaticValidation validation{entry,code.size(),code.data(),dependencies,3};

bool range(uint32_t address,uint32_t size,const uint8_t** data=nullptr) {
    if(!size||size-1u>UINT32_MAX-address)return false;
    uint32_t first=0,last=0;
    const auto* a=nds_busf_classify(address,&first);
    const auto* b=nds_busf_classify(address+size-1,&last);
    if(!a||a!=b||!a->data||!a->written||!a->gen||
       uint64_t(first)+size>uint64_t(a->mask)+1||last!=first+size-1)return false;
    if(a!=&g_busf_itcm&&a!=&g_busf_dtcm&&g_busf_dtcm_limit&&
       uint64_t(address)<uint64_t(g_busf_dtcm_base)+g_busf_dtcm_limit&&
       uint64_t(address)+size>g_busf_dtcm_base)return false;
    if(data)*data=a->data+first;
    return true;
}
uint32_t u32(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
bool ordinary_root() {
    constexpr uint32_t directory=0x0216F324,arc=0x0216F34C;
    if(!range(directory,12)||!range(arc,0x5c)||
       bus_read_u32(directory)!=arc||bus_read_u32(directory+4)||bus_read_u32(directory+8)||
       bus_read_u32(arc)!=0x006D6F72)return false;
    const auto flags=bus_read_u32(arc+0x1c);
    if(flags!=3&&flags!=7)return false;
    // Next/previous archive links at +4/+8 are allowed. Command and wait
    // queues must be idle; cached table contents remain caller-visible.
    for(uint32_t at:{0xCu,0x10u,0x14u,0x18u,0x20u,0x24u,0x28u})
        if(bus_read_u32(arc+at))return false;
    nds_assets::FileView header,fat,fnt;
    if(!service.view(0,0x50,&header)||!header.data)return false;
    const uint32_t fo=u32(header.data+0x48),fs=u32(header.data+0x4c);
    const uint32_t no=u32(header.data+0x40),ns=u32(header.data+0x44);
    if(bus_read_u32(arc+0x30)!=fs||bus_read_u32(arc+0x38)!=ns||
       bus_read_u32(arc+0x3c)!=fo||bus_read_u32(arc+0x40)!=no||
       bus_read_u32(arc+0x4c)!=0x0200505C||bus_read_u32(arc+0x50)!=0x02004710||
       bus_read_u32(arc+0x54)!=0x02004FCC||bus_read_u32(arc+0x58)!=0x602)return false;
    if(flags==3)return bus_read_u32(arc+0x2c)==fo&&bus_read_u32(arc+0x34)==no&&
        !bus_read_u32(arc+0x44)&&bus_read_u32(arc+0x48)==0x02004710;
    const uint32_t fp=bus_read_u32(arc+0x2c),np=bus_read_u32(arc+0x34);
    const uint8_t *fd=nullptr,*nd=nullptr;
    return bus_read_u32(arc+0x44)==fp&&bus_read_u32(arc+0x48)==0x02005064&&
        service.view(fo,fs,&fat)&&service.view(no,ns,&fnt)&&range(fp,fs,&fd)&&range(np,ns,&nd)&&
        !std::memcmp(fd,fat.data,fs)&&!std::memcmp(nd,fnt.data,ns);
}
bool path_at(uint32_t address,std::string& path) {
    path.clear();
    for(uint32_t i=0;i<128;++i) {
        if(!range(address+i,1))return false;
        auto c=bus_read_u8(address+i);
        if(!c)return !path.empty();
        path.push_back(c=='\\'?'/':char(c));
    }
    return false;
}
template<size_t N>bool prove(uint32_t address,std::array<uint8_t,N>& proof,const char* hash) {
    std::array<uint8_t,N> live{};
    if(!bus_range_has_write_provenance(address,N)||!bus_debug_copy(0,address,live.data(),N)||
       gba::sha1(live.data(),N).hex()!=hash)return false;
    proof=live;return true;
}
void fallback(uint32_t reason=0) {
    if(reason)count.decline_reason=reason;
    ++count.declines;tier3_step_one(g_cpu.R[15]);
    if(!runtime_unwinding()&&!runtime_should_yield())runtime_dispatch(g_cpu.R[15]|((g_cpu.cpsr&CPSR_T_BIT)?1u:0u));
}
void dispatch_arm(uint32_t pc,uint32_t next) {
    g_cpu.R[14]=next|1u;g_cpu.R[15]=pc;g_cpu.cpsr&=~CPSR_T_BIT;
    runtime_tick(32);
    if(!runtime_unwinding()&&!runtime_should_yield())runtime_dispatch(pc);
}
bool read_frame(std::array<uint32_t,8>& f) {
    const uint32_t sp=g_cpu.R[13];
    if((sp&7)||!range(sp,frame_size))return false;
    for(unsigned i=0;i<f.size();++i)f[i]=bus_read_u32(sp+i*4);
    const uint32_t signature=0xD347A551u^sp^f[Offset]^f[Length]^f[Decoded]^f[Context]^f[Heap]^f[Return];
    return f[Magic]==magic&&f[Cookie]==signature;
}
void begin() {
    count.entry_sp=g_cpu.R[13];
    count.archive_flags=bus_read_u32(0x0216F34C+0x1c);
    if(!range(0x021759A8,4)||!range(0x02174E50,4)||!ordinary_root()){fallback(1);return;}
    const uint32_t base=bus_read_u32(0x021759A8);
    if(base>UINT32_MAX-0x8c8-0x94){fallback(2);return;}
    const uint32_t context=base+0x8c8;
    const uint32_t manager=bus_read_u32(0x02174E50);
    if(!range(context,0x94)||!range(manager,16)){fallback(2);return;}
    const uint32_t heap=bus_read_u32(manager+12);
    if(!range(heap,0x28)){fallback(3);return;}
    std::string path;nds_assets::FileView file;uint32_t size=0;
    if(!path_at(context,path)||!service.lookup(path.c_str(),&file)||
       !service.inspect(file,&size)||!size||size>max_output){fallback(4);return;}
    const uint32_t sp=g_cpu.R[13];
    if((sp&7)||sp<frame_size||!range(sp-frame_size,frame_size)){fallback(5);return;}
    std::array<uint32_t,8> frame{magic,file.offset,file.size,size,context,heap,g_cpu.R[14],0};
    frame[Cookie]=0xD347A551u^(sp-frame_size)^frame[Offset]^frame[Length]^frame[Decoded]^frame[Context]^frame[Heap]^frame[Return];
    for(unsigned i=0;i<frame.size();++i)bus_write_u32(sp-frame_size+i*4,frame[i]);
    g_cpu.R[13]=sp-frame_size;g_cpu.R[0]=heap;g_cpu.R[1]=size;
    g_cpu.R[2]=bus_read_u32(context+0x90)?uint32_t(-32):32;
    dispatch_arm(allocator,resume);
}
void complete() {
    std::array<uint32_t,8> frame{};
    if(!read_frame(frame)){fallback();return;}
    nds_assets::FileView file;uint32_t size=0;
    if(!service.view(frame[Offset],frame[Length],&file)||!service.inspect(file,&size)||
       size!=frame[Decoded]||!size||size>max_output||!range(frame[Context],0x94)||!range(frame[Heap],0x28)){
        nds_halt("MKDS assets: invalid native continuation");return;
    }
    const uint32_t destination=g_cpu.R[0];
    if(destination) {
        std::vector<uint8_t> decoded;
        const uint8_t *dst=nullptr,*frame_bytes=nullptr,*context_bytes=nullptr;
        if(!range(destination,size,&dst)||!range(g_cpu.R[13],frame_size,&frame_bytes)||
           !range(frame[Context],0x94,&context_bytes)){
            nds_halt("MKDS assets: invalid destination");return;
        }
        const auto overlaps=[&](const uint8_t* p,uint32_t n){
            return uintptr_t(dst)<uintptr_t(p)+n&&uintptr_t(p)<uintptr_t(dst)+size;
        };
        if(overlaps(frame_bytes,frame_size)||overlaps(context_bytes,0x94)||
           !service.decode(file,&decoded)||decoded.size()!=size||
           !service.publish(destination,decoded.data(),size,g_runtime_cycles)){
            nds_halt("MKDS assets: output publication failed");return;
        }
        count.bytes+=size;
        count.last_offset=file.offset;count.last_compressed=file.size;
        count.last_destination=destination;count.last_decoded=size;
    }
    bus_write_u32(frame[Context]+0x84,destination);
    g_cpu.R[0]=frame[Heap];dispatch_arm(heap_query,finish);
}
void end() {
    std::array<uint32_t,8> frame{};
    if(!read_frame(frame)){fallback();return;}
    ++count.files;g_cpu.R[13]+=frame_size;g_cpu.R[14]=frame[Return];
    nds_hle::finish(64);
}
}
bool initialize(const char* sha,const nds_assets::Services& value) {
    service=value;count={};
    count.enabled=sha&&!std::strcmp(sha,"691e00d9a5dd80b04f80cc7559503e8b06848785")&&
        service.lookup&&service.view&&service.inspect&&service.decode&&service.publish;
    return count.enabled;
}
Statistics statistics(){return count;}
bool is_entry(NdsCpu cpu,uint32_t pc,bool thumb){return count.enabled&&cpu==NDS_ARM9&&thumb&&(pc==entry||pc==resume||pc==finish);}
mkds_hle::Replacement resolve(NdsCpu cpu,uint32_t pc,bool thumb) {
    if(!is_entry(cpu,pc,thumb))return {};
    if(!prove(entry,code,"2964ba079dd41fddd17ff1b6b99e6a6730e11269")||
       !prove(0x020364E0,getters,"b01f7d15b80068a103d74551b0c505af4eaf52ac")||
       !prove(allocator,allocation_code,"057344a457545006203786a6e482e1347d928756")||
       !prove(heap_query,query_code,"ad7ba9e94762e572e3f19d6348dd84f9d04c2c0f"))return {nullptr,&validation,entry,code.size()};
    return {run,&validation,entry,code.size()};
}
void run() {
    if(count.enabled&&g_cpu.R[15]==resume){complete();return;}
    if(count.enabled&&g_cpu.R[15]==finish){end();return;}
    if(!count.enabled||!nds_performance_execution()||g_runtime_deep_trace){fallback();return;}
    if(g_cpu.R[15]==entry)begin();else fallback();
}
}
