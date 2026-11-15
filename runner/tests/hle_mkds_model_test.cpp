#include "hle_mkds_model.h"
#include "native_3d.h"
#include "gpu3d.h"
#include "state.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace {
std::vector<uint8_t> ram(4*1024*1024),written(ram.size(),1);
std::array<uint32_t,1024> generations{};
nds_native_3d::Service geometry;
constexpr uint32_t model=0x02200000,material=model+0x80,shape=model+0x100,data=model+0x120;
constexpr uint32_t buffer=0x02700100,busy=0x021731E8;
unsigned accepted{},returns{};bool check_pending{};
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
uint32_t Read(uint32_t address){uint32_t value;std::memcpy(&value,ram.data()+(address&0x3FFFFF),4);return value;}
void Write(uint32_t address,uint32_t value){std::memcpy(ram.data()+(address&0x3FFFFF),&value,4);++generations[(address&0x3FFFFF)>>12];}
void Cpu(){g_cpu={};g_cpu.cpsr=0x1F;g_cpu.R[0]=model;g_cpu.R[3]=1;g_cpu.R[13]=0x027E3000;g_cpu.R[14]=0x020F0001;
    for(unsigned r=4;r<12;++r)g_cpu.R[r]=0xF00D0000+r;}
uint64_t Count(const char* name){auto json=mkds_model::statistics_json();auto at=json.find(std::string("\"")+name+"\":");Check(at!=std::string::npos,"counter exists");return std::stoull(json.substr(at+std::strlen(name)+3));}
void Publish(){const uint32_t zero=0;geometry.Operation(0x50,{&zero,1});Check(geometry.VBlank(),"native frame published");}
void Contract(){Check(g_cpu.R[13]==0x027E3000&&g_cpu.R[15]==0x020F0000&&(g_cpu.cpsr&CPSR_T_BIT),"model return/SP/interworking");
    for(unsigned r=4;r<12;++r)Check(g_cpu.R[r]==0xF00D0000+r,"model preserves callee registers");}
void Test(){
    g_busf_main.data=ram.data();g_busf_main.written=written.data();g_busf_main.gen=generations.data();g_busf_main.mask=0x3FFFFF;
    Write(model,0x200);Write(model+8,0x40);Write(model+12,0xC0);Write(model+0x1C,4096);Write(model+0x20,4096);
    // Independent synthetic one-material/one-shape dictionaries.
    ram[(model+0x45)&0x3FFFFF]=1;Write(model+0x4A,8);Write(model+0x4C,4);Write(model+0x50,0x40);
    ram[(model+0xC1)&0x3FFFFF]=1;Write(model+0xC6,8);Write(model+0xC8,4);Write(model+0xCC,0x40);
    Write(material+4,0x801F);Write(material+12,0x001F10C0);Write(shape+8,0x20);Write(shape+12,32);
    const std::array<uint32_t,8> mesh{0x23232340,0,0xFC00FC00,0,0xFC000400,0,0x04000000,0};
    std::memcpy(ram.data()+(data&0x3FFFFF),mesh.data(),sizeof(mesh));
    Write(0x021731E4,buffer);Write(buffer,2);Write(buffer+4,0x72);Write(buffer+8,511);Write(busy,1);
    mkds_model::initialize(true);Cpu();check_pending=true;mkds_model::draw();check_pending=false;Contract();
    Check(accepted==2&&Read(buffer)==0&&Read(busy)==0,"pending input accepted before busy completion");
    Check((geometry.Read(0x04000630)&65535)==4088,"pending vector test precedes native model work");Publish();
    Check(geometry.Display().polygons.size()==1&&geometry.Display().vertices[0].clip[0]==-1024,"model resource becomes native geometry");
    Check(Count("cache_builds")==1,"first model resource compiles once");
    Write(material+4,0x83E0);Cpu();mkds_model::draw();Contract();Publish();
    Check(Count("cache_builds")==1&&Count("content_hits")==1,"material page write preserves unchanged native mesh");
    Check((geometry.Display().vertices[0].color[1]>>12)==511,"live model material reaches cached mesh");
    Cpu();g_cpu.R[0]=model+0x400000;mkds_model::draw();Publish();
    Check(Count("cache_builds")==1,"RAM aliases share the same physical native mesh");
    Write(data+8,0xFC00FE00);Cpu();mkds_model::draw();Publish();
    Check(Count("cache_builds")==2&&geometry.Display().vertices[0].clip[0]==-512,"actual mesh writes rebuild its native packet");
    mkds_model::clear_cache();Cpu();mkds_model::draw();Publish();Check(Count("cache_builds")==3,"reset/load invalidation rebuilds native mesh");
    Check(!mkds_model::resolve(0x0201290C).function,"changed code cannot acquire native binding");
}
}
bool bus_range_has_write_provenance(uint32_t,uint32_t){return true;}
bool bus_debug_copy(int,uint32_t address,uint8_t* out,uint32_t size){if(address<0x02000000||uint64_t{address}+size>0x03000000)return false;
    const uint32_t offset=address&0x3FFFFF;if(uint64_t{offset}+size>ram.size())return false;std::memcpy(out,ram.data()+offset,size);return true;}
void nds_gpu3d_write(uint32_t,uint32_t value,uint32_t){if(check_pending)Check(Read(busy)==1,"busy cannot clear before pending input acceptance");++accepted;geometry.Packed(value);}
void nds_gpu3d_native_operation(uint8_t command,std::span<const uint32_t> parameters){geometry.Operation(command,parameters);}
void nds_gpu3d_native_mesh(const nds_native_3d::Mesh& mesh){geometry.DrawMesh(mesh);}
void nds_halt(const char* reason){throw std::runtime_error(reason);}
extern "C" {
ArmCpuState g_cpu{};NdsCpu g_nds_active=NDS_ARM9;unsigned char g_nds_unwinding{};
unsigned long long g_runtime_cycles{};uint32_t g_runtime_deep_trace=1;
NdsBusFastWin g_busf_main{},g_busf_wram_lo[2]{},g_busf_wram_hi[2]{},g_busf_itcm{},g_busf_dtcm{};
uint32_t g_busf_itcm_limit{},g_busf_dtcm_base{},g_busf_dtcm_limit{};
void runtime_note_code_write(){}void runtime_note_code_write_pages(const uint32_t*,uint32_t){}
void runtime_note_live_write(uint32_t,uint32_t,uint32_t,uint32_t){}
uint32_t bus_read_u32_slow(uint32_t address){return Read(address);}
void bus_write_u32_slow(uint32_t address,uint32_t value){if(check_pending&&address==busy&&!value)Check(accepted==2,"all pending input precedes completion");Write(address,value);}
void runtime_tick(uint32_t cycles){Check(cycles!=0,"model timing boundary");}
bool runtime_unwinding(){return false;}bool runtime_should_yield(){return false;}bool runtime_slice_yield(){return false;}
int runtime_call_should_return(uint32_t){++returns;return 1;}void runtime_dispatch_with_exchange(uint32_t){throw std::runtime_error("unexpected exchange");}
}
int main(){try{Test();std::puts("native model resources, pending completion, mirrors and invalidation passed");return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
