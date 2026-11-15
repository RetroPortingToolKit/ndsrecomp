#include "native_3d.h"
#include "native_3d_raster.h"
#include "NDS.h"
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <stdexcept>

int main() {
    try {
        auto reference = std::make_unique<melonDS::NDS>();
        auto native = std::make_unique<melonDS::NDS>();
        auto& r = reference->GPU.GPU3D;
        auto& n = native->GPU.GPU3D;
        r.Reset(); r.SetEnabled(true, true); n.Reset(); n.SetEnabled(true, true);
        nds_native_3d::Service service;
        auto op = [&](uint8_t c, std::initializer_list<uint32_t> args = {}) {
            service.Operation(c, {args.begin(), args.size()});
            if (!args.size()) r.Write32(0x04000400u + c * 4u, 0);
            for (auto value : args) r.Write32(0x04000400u + c * 4u, value);
            reference->ARM9Timestamp += 100000; r.Run();
        };
        auto vertex = [&](int x, int y, int z) { op(0x23, {uint16_t(x) | (uint32_t(uint16_t(y)) << 16), uint16_t(z)}); };
        op(0x10,{0});op(0x15);op(0x10,{2});op(0x15);
        op(0x60,{0xBFFF0000});op(0x29,{0x001F10C0});op(0x20,{31});op(0x40,{0});
        vertex(-1024,-1024,0);vertex(1024,-1024,0);vertex(0,1024,0);op(0x50,{2});
        r.VBlank();service.VBlank();nds_native_3d_raster(service.Display(),n,false);
        if(r.RenderNumPolygons!=1||n.RenderNumPolygons!=1) throw std::runtime_error("triangle must reach both raster ABIs");
        bool equal=true;
        auto compare = [&](const char* name,int a,int b){if(a!=b){std::fprintf(stderr,"%s: reference=%d native=%d\n",name,a,b);equal=false;}};
        auto& a=*r.RenderPolygonRAM[0];auto& b=*n.RenderPolygonRAM[0];
        compare("front",a.FacingView,b.FacingView);compare("W buffer",a.WBuffer,b.WBuffer);
        compare("top",a.YTop,b.YTop);compare("bottom",a.YBottom,b.YBottom);
        for(unsigned i=0;i<3;++i){
            compare("depth",a.FinalZ[i],b.FinalZ[i]);compare("perspective W",a.FinalW[i],b.FinalW[i]);
            for(unsigned c=0;c<2;++c){compare("pixel",a.Vertices[i]->FinalPosition[c],b.Vertices[i]->FinalPosition[c]);compare("subpixel",a.Vertices[i]->HiresPosition[c],b.Vertices[i]->HiresPosition[c]);}
            for(unsigned c=0;c<3;++c)compare("colour",a.Vertices[i]->FinalColor[c],b.Vertices[i]->FinalColor[c]);
        }
        if(!equal)throw std::runtime_error("native triangle raster ABI mismatch");
        n.GetCurrentRenderer().RenderFrame(native->GPU);
        n.GetCurrentRenderer().VCount144(native->GPU);
        if((n.GetCurrentRenderer().GetLine(96)[128]&63)!=63)
            throw std::runtime_error("portable raster must draw native triangle packets");
        // Collapsed clip coordinates are accepted without a host divide fault.
        auto collapsed=service.Display();for(auto& v:collapsed.vertices)v.clip={0,0,0,0};
        nds_native_3d_raster(collapsed,n,false);
        // Large homogeneous W exercises normalization and the subsequent-list
        // W-buffer mode; Decal's texture alpha must not change draw ordering.
        op(0x10,{0});op(0x16,{4096,0,0,0,0,4096,0,0,0,0,4096,0,0,0,0,0x100000});
        op(0x2A,{1u<<26});op(0x29,{0x001F10D0});op(0x40,{0});
        vertex(-1024,-1024,0);vertex(1024,-1024,0);vertex(0,1024,0);op(0x50,{0});
        r.VBlank();service.VBlank();nds_native_3d_raster(service.Display(),n,false);
        if(r.RenderNumPolygons!=1||n.RenderNumPolygons!=1)throw std::runtime_error("W-buffer triangle missing");
        const auto& wa=*r.RenderPolygonRAM[0];const auto& wb=*n.RenderPolygonRAM[0];
        compare("subsequent W buffer",wa.WBuffer,wb.WBuffer);
        compare("Decal opacity",wa.Translucent,wb.Translucent);
        compare("normalized W",wa.FinalW[0],wb.FinalW[0]);
        compare("W depth",wa.FinalZ[0],wb.FinalZ[0]);
        if(!equal)throw std::runtime_error("native W-buffer raster ABI mismatch");
        for(const auto [address,value] : {std::pair{0x04000060u,0x678u},std::pair{0x04000340u,17u},
            std::pair{0x04000350u,0x1F001234u},std::pair{0x04000354u,0x7ABCu},
            std::pair{0x04000358u,0x001F03E0u},std::pair{0x0400035Cu,0x1234u},
            std::pair{0x04000360u,0x08040201u},std::pair{0x0400037Cu,0x7F604020u}}) {
            r.Write32(address,value);n.Write32(address,value);
        }
        op(0x50,{0});r.VBlank();service.VBlank();nds_native_3d_latch_registers(n);
        compare("fog offset latch",r.RenderFogOffset,n.RenderFogOffset);
        compare("fog shift latch",r.RenderFogShift,n.RenderFogShift);
        compare("clear depth latch",r.RenderClearAttr2,n.RenderClearAttr2);
        compare("alpha latch",r.RenderAlphaRef,n.RenderAlphaRef);
        for(unsigned i=0;i<34;++i)compare("fog density latch",r.RenderFogDensityTable[i],n.RenderFogDensityTable[i]);
        if(!equal)throw std::runtime_error("native render-state latch mismatch");
        std::puts("native triangle raster ABI and collapsed projection passed");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
