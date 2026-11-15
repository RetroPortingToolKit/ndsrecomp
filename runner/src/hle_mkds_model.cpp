// SPDX-License-Identifier: MIT
// Native one-material/one-shape drawing, derived from the owned title's ABI
// and NSBMD resource observations. No SDK or vendored geometry code is copied.
#include "hle_mkds_model.h"
#include "native_model.h"
#include "gpu3d.h"
#include "hle_vector_math.h"
#include "sha1.h"
#include "state.h"
#include "native_ram.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace mkds_model {
namespace {
constexpr uint32_t entry=0x0201290C;
bool enabled{};
uint64_t calls{},cache_hits{},content_hits{},cache_builds{},mesh_words_removed{},buffer_words{},texture_calls{},skipped{};
std::array<uint8_t,0x214> body{};
std::array<uint8_t,0x640> texture_body{};
std::array<uint8_t,0x2B8> buffer_body{};
std::array<uint8_t,4> formats{};
const NdsStaticValidationRange ranges[]={{entry,body.size(),body.data()},
    {0x01FF9300,texture_body.size(),texture_body.data()},
    {0x01FF9048,buffer_body.size(),buffer_body.data()},
    {0x0216185C,formats.size(),formats.data()}};
NdsStaticValidation validation{entry,body.size(),body.data(),ranges,4};
struct CachedMesh {
    const NdsBusFastWin* window{};
    const uint8_t* storage{};
    uint32_t offset{},bytes{};
    std::vector<uint32_t> generations;
    std::vector<uint32_t> source;
    nds_native_3d::Mesh mesh;
};
std::vector<CachedMesh> meshes;
size_t mesh_bytes{};
uint32_t Address(uint32_t base,uint32_t offset,uint32_t bytes=4) {
    const uint64_t address=uint64_t{base}+offset;
    if(address<0x02000000u||address+bytes>0x03000000u)throw std::runtime_error("model resource outside Main RAM");
    return static_cast<uint32_t>(address);
}
uint32_t Read(uint32_t address,unsigned bytes=4) {
    Address(address,0,bytes);std::array<uint8_t,4> value{};
    if(!bus_debug_copy(0,address,value.data(),bytes))throw std::runtime_error("unreadable model resource");
    return value[0]|(uint32_t(value[1])<<8)|(uint32_t(value[2])<<16)|(uint32_t(value[3])<<24);
}
void Op(uint8_t command,std::initializer_list<uint32_t> values) {
    nds_gpu3d_native_operation(command,{values.begin(),values.size()});
}
void FlushOldBuffer() {
    const uint32_t buffer=Read(0x021731E4);
    if(buffer) {
        const uint32_t count=Read(buffer);
        if(count>192)throw std::runtime_error("invalid model command buffer count");
        for(uint32_t i=0;i<count;++i)nds_gpu3d_write(0x04000400,Read(buffer+4+i*4),4);
        if(count)bus_write_u32(buffer,0);
        buffer_words+=count;
    }
    // This SDK flag describes command DMA input, which the native geometry
    // service already consumed as one job. Its interrupt still runs normally.
    if(Read(0x021731E8))bus_write_u32(0x021731E8,0);
}
const nds_native_3d::Mesh& Mesh(uint32_t address,uint32_t bytes) {
    Address(address,0,bytes);
    if((address|bytes)&3u)throw std::runtime_error("unaligned native model mesh");
    if(bytes/4>32768)throw std::runtime_error("native model mesh capacity");
    const auto view=NdsNativeRam::map<uint32_t>(address);
    const auto end=bytes?NdsNativeRam::map<uint8_t>(address+bytes-1):NdsNativeRam{};
    if(!view||!end||view.window!=end.window||uint64_t{view.offset}+bytes>uint64_t{view.window->mask}+1||
       end.offset!=view.offset+bytes-1)throw std::runtime_error("native model mesh mapping boundary");
    if(view.window!=&g_busf_dtcm&&g_busf_dtcm_limit&&address<uint64_t{g_busf_dtcm_base}+g_busf_dtcm_limit&&
       uint64_t{address}+bytes>g_busf_dtcm_base)throw std::runtime_error("native model mesh crosses TCM mapping");
    const uint32_t first=view.offset>>12;
    const uint32_t last=(view.offset+bytes-1)>>12;
    if(!bytes||last<first)throw std::runtime_error("invalid native model mesh span");
    const uint32_t* generations=view.window->gen+first;
    const size_t pages=last-first+1;
    for(auto& cached:meshes)if(cached.window==view.window&&cached.storage==view.window->data&&cached.offset==view.offset&&cached.bytes==bytes) {
        if(cached.generations.size()==pages&&std::equal(cached.generations.begin(),cached.generations.end(),generations)) {
            ++cache_hits;return cached.mesh;
        }
        // Material patches can share a page with an immutable mesh. Check the
        // mesh's own content on those writes rather than rebuilding its native
        // packet because an unrelated field on the same page changed.
        if(std::memcmp(cached.source.data(),view.window->data+view.offset,bytes)==0) {
            cached.generations.assign(generations,generations+pages);++cache_hits;++content_hits;return cached.mesh;
        }
    }
    std::vector<uint32_t> words(bytes/4);
    if(!bus_debug_copy(0,address,reinterpret_cast<uint8_t*>(words.data()),bytes))throw std::runtime_error("unreadable native model mesh");
    auto packet=nds_native_3d::Service::CompileMesh(words);
    CachedMesh cached{view.window,view.window->data,view.offset,bytes,{generations,generations+pages},std::move(words),std::move(packet)};
    auto cost=[](const CachedMesh& c){return c.mesh.operations.size()*sizeof(nds_native_3d::OperationEntry)+(c.mesh.parameters.size()+c.generations.size()+c.source.size())*4;};
    const size_t size=cost(cached);
    while(!meshes.empty()&&(meshes.size()>=128||mesh_bytes+size>4*1024*1024)) {
        mesh_bytes-=cost(meshes.front());meshes.erase(meshes.begin());
    }
    mesh_bytes+=size;meshes.push_back(std::move(cached));++cache_builds;
    return meshes.back().mesh;
}
uint32_t Resource(uint32_t list,uint32_t dictionary,uint32_t index) {
    if(index>=Read(dictionary+1,1))throw std::runtime_error("native model resource index");
    const uint32_t table=Address(dictionary,Read(dictionary+6,2));
    const uint32_t unit=Read(table,2);
    if(unit<4||unit>32)throw std::runtime_error("native model dictionary unit");
    return Address(list,Read(Address(table,4+index*unit)));
}
void Texture(uint32_t model,uint32_t material,uint32_t flags) {
    if(Read(model+0x16,1)!=0)throw std::runtime_error("unsupported model texture format");
    nds_native_model::TextureTransform transform;
    uint32_t at=material+0x2C;
    if(!(flags&2)){transform.scale_s=std::bit_cast<int32_t>(Read(at));transform.scale_t=std::bit_cast<int32_t>(Read(at+4));at+=8;}
    if(!(flags&4)){transform.sine=static_cast<int16_t>(Read(at,2));transform.cosine=static_cast<int16_t>(Read(at+2,2));at+=4;}
    if(!(flags&8)){transform.translate_s=std::bit_cast<int32_t>(Read(at));transform.translate_t=std::bit_cast<int32_t>(Read(at+4));}
    transform.width=static_cast<uint16_t>(Read(material+0x20,2));transform.height=static_cast<uint16_t>(Read(material+0x22,2));
    transform.magnify_s=std::bit_cast<int32_t>(Read(material+0x24));transform.magnify_t=std::bit_cast<int32_t>(Read(material+0x28));
    const auto matrix=nds_native_model::TextureMatrix(transform);
    std::array<uint32_t,16> words{};for(unsigned i=0;i<16;++i)words[i]=std::bit_cast<uint32_t>(matrix[i]);
    Op(0x10,{3});nds_gpu3d_native_operation(0x16,words);Op(0x10,{2});++texture_calls;
}
void Draw() {
    try {
        ++calls;
        const uint32_t model=g_cpu.R[0],material_id=g_cpu.R[1],shape_id=g_cpu.R[2],send=g_cpu.R[3];
        Address(model,0,64);
        const uint32_t size=Read(model);
        if(size<64)throw std::runtime_error("invalid native model size");
        Address(model,0,size);
        FlushOldBuffer();
        const uint32_t up=Read(model+0x1C);
        if(up!=4096)Op(0x1B,{up,up,up});
        if(send) {
            const uint32_t list=Address(model,Read(model+8));
            const uint32_t material=Resource(list,list+4,material_id);
            uint32_t attributes=Read(material+12);
            if(!(attributes&0x001F0000)){++skipped;nds_hle::finish(32);return;}
            const uint32_t flags=Read(material+0x1E,2);
            if(flags&32)attributes&=~0x001F0000u;
            Op(0x30,{Read(material+4)});Op(0x31,{Read(material+8)});Op(0x29,{attributes});
            Op(0x2A,{Read(material+0x14)});Op(0x2B,{Read(material+0x1C,2)});
            if(flags&1)Texture(model,material,flags);
        }
        const uint32_t list=Address(model,Read(model+12));
        const uint32_t shape=Resource(list,list,shape_id);
        const uint32_t bytes=Read(shape+12),data=Address(shape,Read(shape+8),bytes);
        nds_gpu3d_native_mesh(Mesh(data,bytes));mesh_words_removed+=bytes/4;
        const uint32_t down=Read(model+0x20);
        if(down!=4096)Op(0x1B,{down,down,down});
        const uint32_t result=down-4096;
        uint32_t flags=result&CPSR_N_BIT;if(!result)flags|=CPSR_Z_BIT;if(down>=4096)flags|=CPSR_C_BIT;
        if(((down^4096)&(down^result))&CPSR_N_BIT)flags|=CPSR_V_BIT;
        g_cpu.cpsr=(g_cpu.cpsr&~(CPSR_N_BIT|CPSR_Z_BIT|CPSR_C_BIT|CPSR_V_BIT))|flags;
        g_cpu.R[3]=down;nds_hle::finish(32);
    }catch(const std::exception& error){std::fprintf(stderr,"[native-model] %s\n",error.what());nds_halt(error.what());}
}
template<size_t N> bool Proof(uint32_t address,std::array<uint8_t,N>& out,const char* hash) {
    std::array<uint8_t,N> live{};
    if(!bus_range_has_write_provenance(address,N)||!bus_debug_copy(0,address,live.data(),N)||gba::sha1(live.data(),N).hex()!=hash)return false;
    out=live;return true;
}
}
void clear_cache(){meshes.clear();mesh_bytes=0;}
void draw(){Draw();}
void initialize(bool supported){enabled=supported;calls=cache_hits=content_hits=cache_builds=mesh_words_removed=buffer_words=texture_calls=skipped=0;clear_cache();}
bool is_entry(uint32_t pc){return enabled&&pc==entry;}
mkds_hle::Replacement resolve(uint32_t pc) {
    if(!is_entry(pc))return {};
    if(!Proof(entry,body,"65a13f5af28c69b633548dca5f227f227341c582")||
       !Proof(0x01FF9300,texture_body,"9f6798058f6822ecbde75a19ffa8d5ec71817974")||
       !Proof(0x01FF9048,buffer_body,"005719f453190d543db85afec757f71b067d4394")||
       !Proof(0x0216185C,formats,"a6ff93e8c7f654227260aed82dea9f249ac3d259"))
        return {nullptr,&validation,entry,body.size()};
    return {draw,&validation,entry,body.size()};
}
std::string statistics_json(){return "{\"calls\":"+std::to_string(calls)+",\"cache_hits\":"+std::to_string(cache_hits)+
    ",\"content_hits\":"+std::to_string(content_hits)+
    ",\"cache_builds\":"+std::to_string(cache_builds)+",\"mesh_words_removed\":"+std::to_string(mesh_words_removed)+
    ",\"buffer_words\":"+std::to_string(buffer_words)+",\"texture_calls\":"+std::to_string(texture_calls)+",\"skipped\":"+std::to_string(skipped)+"}";}
}
