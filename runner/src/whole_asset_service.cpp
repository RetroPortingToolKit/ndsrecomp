#include "whole_asset_service.h"
#include "native_asset_data.h"
#include "hle_mkds_assets.h"
#include "hle_mph_assets.h"
#include "execution_model.h"
#include "io.h"
#include "spu.h"
#include "state.h"
#include <cstring>
#include <sstream>

namespace nds_assets {
namespace {
bool enabled=false;
bool view(uint32_t offset,uint32_t size,FileView* result) {
    const uint8_t* rom=nullptr;uint32_t bytes=0;
    nds_asset_data::View file;
    // Once admitted, allocation may yield while another independent file
    // request runs. The retained input is immutable and borrows no card bus.
    if(!result||!nds_io_asset_cartridge(&rom,&bytes,false)||
       !nds_asset_data::slice(rom,bytes,offset,size,&file))return false;
    *result={file.offset,file.size,file.data};return true;
}
bool lookup(const char* path,FileView* result) {
    const uint8_t* rom=nullptr;uint32_t bytes=0;
    nds_asset_data::View file;
    if(!path||!result||!nds_io_asset_cartridge(&rom,&bytes)||
       !nds_asset_data::find_file(rom,bytes,path,&file))return false;
    *result={file.offset,file.size,file.data};return true;
}
bool inspect(const FileView& file,uint32_t* bytes) {
    return nds_asset_data::validate({file.data,file.offset,file.size},bytes);
}
bool decode(const FileView& file,std::vector<uint8_t>* bytes) {
    return nds_asset_data::decode({file.data,file.offset,file.size},bytes);
}
bool publish(uint32_t address,const uint8_t* bytes,uint32_t size,uint64_t arm9_cycles) {
    if(g_nds_active!=NDS_ARM9||!bytes||!size||size-1u>UINT32_MAX-address)return false;
    uint32_t first=0,last=0;
    const auto* a=nds_busf_classify(address,&first);
    const auto* b=nds_busf_classify(address+size-1,&last);
    if(!a||a!=b||!a->data||!a->written||!a->gen||last!=first+size-1||
       uint64_t(first)+size>uint64_t(a->mask)+1)return false;
    if(a!=&g_busf_itcm&&a!=&g_busf_dtcm&&g_busf_dtcm_limit&&
       uint64_t(address)<uint64_t(g_busf_dtcm_base)+g_busf_dtcm_limit&&
       uint64_t(address)+size>g_busf_dtcm_base)return false;
    const uint64_t now=arm9_cycles/2;
    // Finish old PCM before any complete-span mutation, including a request
    // admitted during the original ARM7 bootstrap. Publication follows data.
    nds_tick_spu(now);
    runtime_before_ram_write(a->data+first,size,now);
    if(g_runtime_deep_trace) {
        // An observer may have been enabled while the guest allocator was
        // suspended. Complete the owned operation with normal watched stores.
        for(uint32_t i=0;i<size;++i)bus_write_u8(address+i,bytes[i]);
        return true;
    }
    std::memcpy(a->data+first,bytes,size);
    nds_busf_note_write(a,first,size);
    return true;
}
}
bool configure(const char* sha,bool requested) {
    const Services callbacks{lookup,view,inspect,decode,publish};
    const char* selected_sha=requested?sha:"";
    const bool mkds=mkds_assets::initialize(selected_sha,callbacks);
    const bool mph=mph_assets::initialize(selected_sha,callbacks);
    enabled=requested&&(mkds||mph);return !requested||enabled;
}
bool selected(){return enabled;}
bool is_entry(NdsCpu cpu,uint32_t pc,bool thumb) {
    return enabled&&(mkds_assets::is_entry(cpu,pc,thumb)||mph_assets::is_entry(cpu,pc,thumb));
}
mkds_hle::Replacement resolve(NdsCpu cpu,uint32_t pc,bool thumb) {
    if(!enabled)return {};
    if(mkds_assets::is_entry(cpu,pc,thumb))return mkds_assets::resolve(cpu,pc,thumb);
    const auto m=mph_assets::resolve(cpu,pc,thumb);
    return {m.function,m.validation,m.guard_start,m.guard_size};
}
std::string json() {
    const auto a=mkds_assets::statistics();const auto b=mph_assets::statistics();
    std::ostringstream out;
    out<<"{\"selected\":"<<(enabled?"true":"false")
       <<",\"mkds_files\":"<<a.files<<",\"mkds_bytes\":"<<a.bytes<<",\"mkds_declines\":"<<a.declines
       <<",\"mph_files\":"<<b.files<<",\"mph_archives\":"<<b.archives<<",\"mph_bytes\":"<<b.bytes
       <<",\"mph_declines\":"<<b.declines
       <<",\"last\":{\"offset\":"<<(a.enabled?a.last_offset:b.last_offset)
       <<",\"compressed\":"<<(a.enabled?a.last_compressed:b.last_compressed)
       <<",\"destination\":"<<(a.enabled?a.last_destination:b.last_destination)
       <<",\"decoded\":"<<(a.enabled?a.last_decoded:b.last_decoded)<<"}}";
    return out.str();
}
}
