#include "native_3d.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
using nds_native_3d::Service;
namespace {
void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void Op(Service& s, uint8_t code, std::initializer_list<uint32_t> p = {}) {
    s.Operation(code, {p.begin(), p.size()});
}
uint32_t XY(int x, int y) { return uint16_t(x) | (uint32_t(uint16_t(y)) << 16); }
void V(Service& s, int x, int y, int z = 0) { Op(s, 0x23, {XY(x,y), uint16_t(z)}); }
void Begin(Service& s, unsigned kind = 0, uint32_t attributes = 0x001F10C0) {
    Op(s,0x29,{attributes}); Op(s,0x40,{kind});
}
void Publish(Service& s) { Op(s,0x50,{0}); Check(s.VBlank(),"submitted draw job must publish at VBlank"); }
void GeometryAndObservation() {
    Service s;
    Begin(s); Op(s,0x20,{31}); V(s,-1024,-1024); V(s,1024,-1024); V(s,0,1024);
    Check(s.Counters().jobs==0,"commands must not run a per-word geometry engine");
    Check(s.Read(0x04000604)==0x00030001,"RAM observation must finish preceding native geometry");
    Check(s.Counters().jobs==1,"one observation is one native geometry job");
    Op(s,0x50,{0});
    Check(s.Read(0x04000600)&(1u<<27),"pending swap boundary remains observable");
    Check(s.Read(0x04000604)==0x00030001,"submitted list counts persist until VBlank");
    Check(s.Display().polygons.empty(),"draw job cannot become visible before VBlank");
    Check(s.VBlank(),"swap must publish");
    const auto& f=s.Display();
    Check(f.polygons.size()==1&&f.vertices.size()==3,"native triangle packet");
    Check(f.vertices[0].clip[0]==-1024&&f.vertices[0].clip[3]==4096,"homogeneous vertex contract");
    Check((f.vertices[0].color[0]>>12)==511&&f.vertices[0].color[1]==0,"renderer colour transport");
    Check(f.polygons[0].front==1,"front face orientation");
    Check(!(s.Read(0x04000600)&(1u<<27)),"completed swap must release observation boundary");
    Check(s.Read(0x04000604)==0,"new list counts reset after VBlank");
}
void MatricesAndResults() {
    Service s;
    Op(s,0x10,{2}); Op(s,0x1C,{4096,0,0}); Op(s,0x1B,{8192,8192,8192});
    Op(s,0x71,{XY(1024,0),0});
    Check(s.Read(0x04000620)==6144,"pre-multiply scale must not rescale prior translation");
    Check(s.Read(0x0400064C)==0&&s.Read(0x04000670)==4096,"clip matrix register layout");
    Op(s,0x72,{511});
    Check((s.Read(0x04000630)&65535)==4088,"scale in simultaneous mode must preserve directions and result precision");
    Op(s,0x11); Op(s,0x1C,{4096,0,0}); Op(s,0x12,{1});
    Check(s.Read(0x04000670)==4096,"position/direction stack restore");
    for(unsigned i=0;i<32;++i)Op(s,0x11);
    Check(s.Read(0x04000600)&0x8000,"invalid stack entry must set sticky error");
    s.AcknowledgeStatus(0x8000);
    Check(!(s.Read(0x04000600)&0x8000),"status acknowledge clears error");
    Service projection;
    Op(projection,0x12,{17});
    Check((projection.Read(0x04000600)&0xA000)==0xA000,"projection pop at reset wraps pointer and flags underflow");
    Service vector;
    Op(vector,0x72,{256});
    Check((vector.Read(0x04000630)&65535)==0x0800,"vector output has twelve fractional bits");
    Op(vector,0x72,{0x300});
    Check((vector.Read(0x04000630)&65535)==0xF800,"vector output sign extension");
    Op(vector,0x10,{2}); Op(vector,0x1A,{12288,0,0,0,4096,0,0,0,4096}); Op(vector,0x72,{256});
    Check((vector.Read(0x04000630)&65535)==0xF800,"vector overflow wraps at thirteen bits");
}
void ClippingAndStrips() {
    Service clipped;
    Begin(clipped); V(clipped,-8192,-1024);V(clipped,2048,-1024);V(clipped,0,2048); Publish(clipped);
    Check(clipped.Display().polygons.size()==1&&clipped.Display().polygons[0].count==4,"plane crossing must create clipped quad");
    for(const auto& v:clipped.Display().vertices)
        for(unsigned c=0;c<3;++c)Check(v.clip[c]>=-v.clip[3]&&v.clip[c]<=v.clip[3],"clip volume bounds");
    Service strip;
    Begin(strip,2,0x001F1080);V(strip,-1024,-1024);V(strip,1024,-1024);V(strip,-1024,1024);V(strip,1024,1024);Publish(strip);
    Check(strip.Display().polygons.size()==2,"triangle strip winding must preserve both front faces");
    Check(strip.Display().vertices.size()==4,"strip must reuse original vertices");
    Service far;
    Begin(far,0,0x001F00C0);V(far,-1024,-1024);V(far,1024,-1024);V(far,0,1024,8192);Publish(far);
    Check(far.Display().polygons.empty(),"far-intersecting polygon can request rejection");
}
void PackedAndCurrentState() {
    Service s;
    s.Packed(0x40152029);s.Packed(0x001F10C0);s.Packed(31);s.Packed(0);
    s.Packed(0x23);s.Packed(XY(-1024,-1024));
    auto bytes=s.Save();Service loaded;std::string error;
    Check(loaded.Load(bytes,&error),"partial packed command must restore");
    Check(loaded.Save()==bytes,"current-build state round trip");
    loaded.Packed(0);V(loaded,1024,-1024);V(loaded,0,1024);Publish(loaded);
    Check(loaded.Display().polygons.size()==1,"partial packed vertex resumes once");
    Check(loaded.Counters().command_counts[0x15]==1,"zero-parameter packed command executes once");
    const auto valid=loaded.Save();
    auto invalid=valid;invalid[0]^=1;
    Check(!loaded.Load(invalid,&error),"foreign state version rejection");
    Check(loaded.Save()==valid,"failed state import is transactional");
    Check(!loaded.Load({valid.data(),valid.size()-1},&error),"truncated state rejection");
}
void BoxAndPositionSideEffect() {
    Service s;
    Op(s,0x70,{XY(-1024,-1024),XY(-1024,2048),XY(2048,2048)});
    Check(s.Read(0x04000600)&2,"inside box visible");
    Op(s,0x70,{XY(12288,0),XY(0,1024),XY(1024,1024)});
    Check(!(s.Read(0x04000600)&2),"outside box invisible");
    Op(s,0x70,{XY(-8192,-8192),XY(-8192,16384),XY(16384,16384)});
    Check(!(s.Read(0x04000600)&2),"box containing frustum has no intersecting faces");
    Begin(s);Op(s,0x71,{XY(-1024,-1024),0});Op(s,0x27,{XY(-1024,0)});V(s,1024,-1024);V(s,0,1024);Publish(s);
    Check(s.Display().vertices[0].clip[0]==-1024,"position test updates relative vertex source");
}
void JobBoundariesAndPresentation() {
    Service s;
    s.AcknowledgeStatus(2u << 30);
    Check(s.IRQAsserted(),"empty native job asserts configured level IRQ");
    Op(s,0x20,{31});
    Check(!s.IRQAsserted(),"pending job cannot assert empty IRQ");
    s.Flush();Check(s.IRQAsserted(),"completed job reasserts empty IRQ");
    Op(s,0x12,{0});s.Flush();
    s.AcknowledgeStatus(0x8000,0xFFFF);
    Check((s.Read(0x04000600)&0xC000A000)==(2u<<30),"low-half stack acknowledgment preserves IRQ mode and clears projection underflow");
    Op(s,0x50,{3});Op(s,0x72,{256});
    Check(s.Read(0x04000600)&1,"test queued behind swap remains busy");
    Check(s.VBlank(),"pending frame publishes");
    Check(s.Display().swap_attributes==0,"SWAP options apply to the following list");
    Check((s.Read(0x04000630)&65535)==0x800,"queued test finishes after swap");
    Publish(s);Check(s.Display().swap_attributes==3,"following frame retains prior swap options");
    Service wide;wide.SetPresentation(448,false);
    Op(wide,0x70,{XY(5000,-100),XY(-100,500),XY(200,200)});
    Check(wide.Read(0x04000600)&2,"wide culling accepts scenery beyond native edge");
    Op(wide,0x71,{XY(5000,0),0});
    Check(wide.Read(0x04000620)==5000,"guest position results retain unmodified projection");
    wide.SetPresentation(448,true);
    Op(wide,0x70,{XY(5000,-100),XY(-100,500),XY(200,200)});
    Check(!(wide.Read(0x04000600)&2),"guest-wide projection must not widen culling twice");
}
void PartialStripStateSafety() {
    Service strip;Begin(strip,2);V(strip,-1234,-1024);V(strip,1234,-1024);strip.Flush();
    const auto state=strip.Save();Service restored;std::string error;
    Check(restored.Load(state,&error),"partial native strip restores");
    auto malformed=state;
    const std::array<int32_t,4> first{-1234,-1024,0,4096};
    const auto* begin=reinterpret_cast<const uint8_t*>(first.data());
    auto found=std::search(malformed.begin(),malformed.end(),begin,begin+sizeof(first));
    Check(found!=malformed.end(),"partial vertex is present in current state");
    const size_t identity=static_cast<size_t>(found-malformed.begin())+offsetof(nds_native_3d::Vertex,identity);
    Check(identity+sizeof(uint64_t)<=malformed.size(),"partial identity lies inside state");
    std::memset(malformed.data()+identity,255,sizeof(uint64_t));
    Check(!restored.Load(malformed,&error),"corrupt partial vertex identity is rejected before continuation");
    V(restored,-1234,1024);V(restored,1234,1024);Publish(restored);
    Check(restored.Display().polygons.size()==2&&restored.Display().vertices.size()==4,"restored partial strip shares vertices safely");
}
void CompiledDisplayLists() {
    Service s;Begin(s);
    const std::array<uint32_t,7> list{0x00232323,XY(-1024,-1024),0,XY(1024,-1024),0,XY(0,1024),0};
    s.Job(list);Publish(s);
    Check(s.Counters().cache_builds==1,"complete list is lowered once");
    Op(s,0x10,{1});Op(s,0x1C,{512,0,0});Begin(s);
    s.Job(list);Publish(s);
    Check(s.Counters().cache_hits==1&&s.Display().vertices[0].clip[0]==-512,"compiled list replays against current matrices");
    const auto bytes=s.Save();Service restored;std::string error;
    Check(restored.Load(bytes,&error),"compiled-list state loads without host cache");
    Begin(restored);restored.Job(list);Publish(restored);
    Check(restored.Counters().cache_hits==0&&restored.Counters().cache_builds==1,"restore rebuilds disposable decode cache");
    Service partial;Begin(partial);
    partial.Job({list.data(),2});
    Check(partial.Counters().cache_builds==0,"partial parameter boundary cannot become a cached packet");
    partial.Job({list.data()+2,list.size()-2});Publish(partial);
    Check(partial.Display().polygons.size()==1&&partial.Counters().cache_hits==0,"uncached continuation completes exactly once");
}
}
int main() {
    try {
        GeometryAndObservation();MatricesAndResults();ClippingAndStrips();PackedAndCurrentState();BoxAndPositionSideEffect();JobBoundariesAndPresentation();PartialStripStateSafety();CompiledDisplayLists();
        std::puts("native geometry command jobs, matrices, clipping, strips, observations and current states passed");
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
}
