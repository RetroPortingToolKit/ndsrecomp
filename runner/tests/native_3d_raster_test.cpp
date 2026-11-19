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
        // Near-view perspective polygons also normalize W upward. Uneven
        // homogeneous depths expose both the missing precision and inverted
        // viewport division's subpixel rounding.
        op(0x10,{0});op(0x16,{512,0,0,0,0,512,0,0,0,0,uint32_t(-512),uint32_t(-512),0,0,uint32_t(-64),0});
        op(0x10,{2});op(0x15);op(0x2A,{0});op(0x29,{0x001F10C0});op(0x40,{0});
        vertex(-257,-99,-511);vertex(257,-99,-1001);vertex(17,101,-4001);op(0x50,{0});
        r.VBlank();service.VBlank();nds_native_3d_raster(service.Display(),n,false);
        if(r.RenderNumPolygons!=1||n.RenderNumPolygons!=1)throw std::runtime_error("near perspective triangle missing");
        const auto& near_ref=*r.RenderPolygonRAM[0];const auto& near_native=*n.RenderPolygonRAM[0];
        for(unsigned i=0;i<3;++i){
            compare("near normalized W",near_ref.FinalW[i],near_native.FinalW[i]);
            compare("near depth",near_ref.FinalZ[i],near_native.FinalZ[i]);
            for(unsigned c=0;c<2;++c){
                compare("near pixel",near_ref.Vertices[i]->FinalPosition[c],near_native.Vertices[i]->FinalPosition[c]);
                compare("near subpixel",near_ref.Vertices[i]->HiresPosition[c],near_native.Vertices[i]->HiresPosition[c]);
            }
        }
        if(!equal)throw std::runtime_error("native near perspective raster ABI mismatch");
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
        // Texture-coordinate generation (TEXIMAGE_PARAM bits 30-31) must match
        // the reference engine in every mode. Normal-source texgen is MPH's
        // environment-mapped morph ball (GitHub issue 44): a short shift tiled
        // its texture 16x. Distinct matrix terms catch transposes; distinct
        // triangle rows keep the polygon sort order unambiguous.
        op(0x10,{0});op(0x15);op(0x10,{2});op(0x15);
        op(0x10,{3});op(0x16,{0x400000u,0x100000u,0,0, uint32_t(-0x200000),0x300000u,0,0,
                              0x080000u,uint32_t(-0x180000),0,0, 0x2345u,uint32_t(-0x1234),0,0});
        op(0x29,{0x001F10C0});
        int row=0;
        auto st=[](int s,int t){return uint32_t(uint16_t(s))|(uint32_t(uint16_t(t))<<16);};
        auto tri=[&](std::initializer_list<uint32_t> normals){
            const int y=-3600+row++*900;const int xs[3]={-1024,1024,0},ys[3]={y,y,y+400};
            op(0x40,{0});op(0x22,{st(37,-21)});
            auto n=normals.begin();
            for(unsigned i=0;i<3;++i){if(n!=normals.end())op(0x21,{*n++});vertex(xs[i],ys[i],-300+int(i)*700);}
        };
        op(0x2A,{1u<<30});tri({});                                   // texcoord source
        op(0x2A,{2u<<30});tri({0x0802018Cu,0x1FF7FC01u,0x3E00FE00u}); // normal source
        op(0x2A,{3u<<30});tri({});                                   // vertex source
        op(0x2A,{0});op(0x40,{0});                                   // vertex texgen must not leak
        for(unsigned i=0;i<3;++i)vertex(-1024+int(i)*1024,-3600+row*900+int(i&1)*400,0);
        op(0x50,{0});r.VBlank();service.VBlank();nds_native_3d_raster(service.Display(),n,false);
        compare("texgen polygons",r.RenderNumPolygons,n.RenderNumPolygons);
        if(!equal||r.RenderNumPolygons!=4)throw std::runtime_error("texgen triangles missing");
        for(unsigned p=0;p<4;++p){
            const auto& ra=*r.RenderPolygonRAM[p];const auto& na=*n.RenderPolygonRAM[p];
            compare("texgen mode",int(ra.TexParam>>30),int(na.TexParam>>30));
            for(unsigned i=0;i<3;++i)for(unsigned c=0;c<2;++c){
                const char* what[]={"texcoord-source S","texcoord-source T","normal-source S","normal-source T",
                                    "vertex-source S","vertex-source T","post-vertex-source S","post-vertex-source T"};
                compare(what[(ra.TexParam>>30)==0?6+c:((ra.TexParam>>30)-1)*2+c],
                        ra.Vertices[i]->TexCoords[c],na.Vertices[i]->TexCoords[c]);
            }
        }
        if(!equal)throw std::runtime_error("native texture-coordinate generation mismatch");
        std::puts("native triangle raster ABI, collapsed projection and texgen passed");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
