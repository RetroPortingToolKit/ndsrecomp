// Exercise the actual production presentation shader against the native CPU
// compositor, including scanline changes and masked/absent layers. Reuse its
// existing device fixtures; this does not boot or play a game.
#define NDS_GPU2D_REFERENCE_ONLY 1
#include "gpu2d_window_test.cpp"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include "glad/glad.h"
#include "melonds_compute/PresentShaders.h"
#include "melonds_compute/SurvivorPresentShader.h"
#include "screen_layout.h"
#include "GPU3D_OpenGL_Output.h"
#include <stdexcept>

namespace {
void ensure(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
GLuint shader(GLenum type, const char* source) {
    GLuint s=glCreateShader(type); glShaderSource(s,1,&source,nullptr); glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if (!ok) { char log[4096]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);std::fprintf(stderr,"%s\n",log); }
    ensure(ok,"production presentation shader compile");return s;
}
GLuint texture(unsigned slot, GLenum format, GLenum type, int w, const void* data) {
    GLuint t;glGenTextures(1,&t);glActiveTexture(GL_TEXTURE0+slot);glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,format,w,192,0,GL_RGBA_INTEGER,type,data);return t;
}
void test_shader(bool scanline, unsigned scale = 1) {
    constexpr int width=448,height=192;
    std::vector<uint32_t> three(width*height),objects(width*height),raster(height*4),expected(width*height);
    for (int y=0;y<height;++y) {
        Unit u{};u.bldcnt=uint16_t(0x3131u|((scanline?y&3u:1u)<<6));u.eva=7;u.evb=11;u.evy=uint8_t(scanline?1+y%16:6);
        u.master_bright=scanline?uint16_t(((y/48)&3u)<<14|5u):0;
        const uint32_t background=0x000D2511u;
        const uint8_t priority3=scanline?uint8_t((y/16)&3):2u;
        raster[y*4]=background;raster[y*4+1]=u.bldcnt|(uint32_t(u.master_bright)<<16);
        raster[y*4+2]=u.eva|(uint32_t(u.evb)<<8)|(uint32_t(u.evy)<<16)|(uint32_t(priority3)<<24);
        const uint32_t scroll = scanline && scale > 1 ?
            std::array<uint32_t,3>{0,5,507}[y%3] : 0;
        const bool fallback = scanline && scale > 1 && y%31 == 0;
        raster[y*4+3]=fallback ? 0x80000000u : scroll;
        for (int x=0;x<width;++x) {
            const uint8_t mask=scanline?uint8_t(x&63):63u,a3=uint8_t(y&31);
            const uint8_t alpha=std::array<uint8_t,3>{0,8,255}[(y/32)%3];
            const uint8_t priority=uint8_t((x/64)&3);
            const bool valid=x%7!=0;
            const uint32_t object_color=0x00200733u;
            const auto three_color = [&](int sx) {
                return uint32_t((sx+17)&63) | (uint32_t((sx/3+43)&63)<<8) | (9u<<16);
            };
            const size_t i=size_t(y)*width+x;
            three[i]=three_color(x)|(uint32_t(a3)<<24);
            objects[i]=nds_gpu2d_pack_direct_pixel(object_color,priority,alpha,valid,mask);
            if (fallback) {
                objects[i]=0xFFCD7931u; // already-composed, asymmetric BGRA8.
                expected[i]=0xFF3179CDu;
                continue;
            }
            Pixel top{background,32,0,4,5,true},below=top;
            auto push=[&](const Pixel& p) {
                auto ahead=[](const Pixel& a,const Pixel& b){return a.priority<b.priority||(a.priority==b.priority&&a.order<b.order);};
                if(ahead(p,top)){below=top;top=p;}else if(ahead(p,below))below=p;
            };
            if(valid&&(mask&16))push(Pixel{object_color,16,alpha,priority,0,true});
            const int sx = x + (scroll & 0x100u ? int(scroll)-512 : int(scroll));
            if(a3&&(mask&1)&&sx>=0&&sx<width)
                push(Pixel{three_color(sx),1,0,priority3,1,true,a3});
            uint32_t c=compose(u,top,below,(mask&32)!=0);
            if((u.master_bright>>14)==1)c=brighten(c,5,0);
            else if((u.master_bright>>14)==2)c=darken(c,5,15);
            auto channel=[&](unsigned s){return (((c>>s)&63u)*255u+31u)/63u;};
            expected[i]=channel(0)|(channel(8)<<8)|(channel(16)<<16)|0xFF000000u;
        }
    }
    GLuint vertex=shader(GL_VERTEX_SHADER,nds_compute_present::kPresentVertex);
    GLuint fragment=shader(GL_FRAGMENT_SHADER,nds_compute_present::kPresentFragment);
    GLuint program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);ensure(linked,"presenter link");
    GLuint inputs[]={texture(0,GL_RGBA8UI,GL_UNSIGNED_BYTE,width,three.data()),
        texture(1,GL_RGBA8UI,GL_UNSIGNED_BYTE,width,objects.data()),
        texture(2,GL_RGBA8UI,GL_UNSIGNED_BYTE,width,three.data()),
        texture(6,GL_RGBA32UI,GL_UNSIGNED_INT,1,raster.data())};
    GLuint hires = 0;
    if (scale > 1) {
        std::vector<uint32_t> hi(size_t(width)*height*scale*scale);
        const auto expand=[](unsigned value, unsigned max) { return (value*255u+max/2)/max; };
        for (unsigned y=0;y<height*scale;++y) for (unsigned x=0;x<width*scale;++x) {
            const uint32_t p=three[size_t(y/scale)*width+x/scale];
            hi[size_t(y)*width*scale+x] = expand((p>>16)&63u,63) |
                (expand((p>>8)&63u,63)<<8) | (expand(p&63u,63)<<16) |
                (expand(p>>24,31)<<24);
        }
        glGenTextures(1,&hires);glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,hires);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width*scale,height*scale,0,GL_RGBA,GL_UNSIGNED_BYTE,hi.data());
    }
    GLuint output,fbo,vao;glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    ensure(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"test framebuffer");
    // Rebind the raster sampler after allocating the output texture.
    glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,inputs[3]);
    if (hires) { glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,hires); }
    glUseProgram(program);glUniform1ui(0,1);glUniform1ui(1,width);glUniform1ui(10,scale);glUniform1ui(11,0);glUniform1ui(13,scanline?1u:0u);
    glUniform1ui(14,scale>1 ? 8u : 0u);
    glUniform1ui(2,0);glUniform3ui(3,17,37,13);glUniform1ui(4,0x3171);
    glUniform1ui(5,7);glUniform1ui(6,11);glUniform1ui(7,6);glUniform1ui(8,0);glUniform1ui(9,2);
    glViewport(0,0,width,height);glGenVertexArrays(1,&vao);glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);
    std::vector<uint32_t> actual(width*height);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
    ensure(glGetError()==GL_NO_ERROR,"GL shader contract execution");
    unsigned transport_rounding=0;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const uint32_t got=actual[size_t(y)*width+x],want=expected[size_t(height-1-y)*width+x];
        bool equal=(got>>24)==255u;
        for(unsigned shift : {0u,8u,16u}) {
            const unsigned a=(got>>shift)&255u,b=(want>>shift)&255u;
            // The observable integer contract is RGB6. UNORM8 conversion may
            // dither/round by one byte on a host; it must recover the exact
            // same six-bit result, never hide a blend/mask error.
            equal &= (a*63u+127u)/255u==(b*63u+127u)/255u &&
                     (a>b?a-b:b-a)<=1u;
        }
        if(!equal){std::fprintf(stderr,"direct pixel x=%d y=%d actual=%08X expected=%08X\n",x,height-1-y,got,want);throw std::runtime_error("GPU/native RGB6 compositor mismatch");}
        transport_rounding+=got!=want;
    }
    glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&output);glDeleteTextures(4,inputs);
    if (hires) glDeleteTextures(1,&hires);
    glDeleteProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
    std::printf("Actual GPU %s at%ux RGB6 blending matches native compositor for86016 pixels; one-byte UNORM8 rounding=%u\n",scanline?"window/raster":"legacy direct",scale,transport_rounding);
}
void test_ssaa() {
    GLuint vertex=shader(GL_VERTEX_SHADER,nds_compute_present::kPresentVertex);
    GLuint fragment=shader(GL_FRAGMENT_SHADER,nds_compute_present::kPresentFragment);
    GLuint program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);ensure(linked,"SSAA presenter link");
    std::vector<uint32_t> native(192,0x1F00003Fu),objects(192,0),hi(4*768);
    for(int y=0;y<768;++y)for(int x=0;x<4;++x)
        hi[y*4+x]=x<2?0xFFFF0000u:0xFF0000FFu; // BGR transport: red / blue.
    GLuint inputs[]={texture(0,GL_RGBA8UI,GL_UNSIGNED_BYTE,1,native.data()),
        texture(1,GL_RGBA8UI,GL_UNSIGNED_BYTE,1,objects.data()),
        texture(2,GL_RGBA8UI,GL_UNSIGNED_BYTE,1,native.data())};
    GLuint hires;glGenTextures(1,&hires);glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,hires);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,768,0,GL_RGBA,GL_UNSIGNED_BYTE,hi.data());
    GLuint output,fbo,vao;glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    ensure(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"SSAA output framebuffer");
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,hires);
    glUseProgram(program);glUniform1ui(0,1);glUniform1ui(1,1);glUniform1ui(2,0);
    glUniform3ui(3,0,0,0);glUniform1ui(4,0);glUniform1ui(8,0);glUniform1ui(9,2);
    glUniform1ui(10,4);glUniform1ui(11,0);glUniform1ui(13,0);
    glViewport(0,0,1,1);glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    const auto draw=[&](unsigned samples) {
        glUniform1ui(14,samples);glDrawArrays(GL_TRIANGLES,0,3);
        uint32_t pixel=0;glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&pixel);
        ensure(glGetError()==GL_NO_ERROR,"SSAA draw/readback");return pixel;
    };
    ensure((draw(0)&0xFFFFFFu)==0xFF0000u,"point source must select blue half");
    for(unsigned samples:{2u,4u,8u}) {
        const uint32_t p=draw(samples);const int r=p&255u,g=(p>>8)&255u,b=(p>>16)&255u;
        ensure(r>=127&&r<=128&&g==0&&b>=127&&b<=128,"SSAA must integrate real red/blue subpixels");
    }
    // The same native HUD pixel is resolved at every sample, never blurred
    // with its neighbours or altered by the 3D source's channel order.
    std::fill(objects.begin(),objects.end(),nds_gpu2d_pack_direct_pixel(0x003F00u,0,17,true,63));
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,inputs[1]);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,192,GL_RGBA_INTEGER,GL_UNSIGNED_BYTE,objects.data());
    ensure((draw(8)&0xFFFFFFu)==0x00FF00u,"SSAA must preserve opaque native HUD");
    std::fill(objects.begin(),objects.end(),0u);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,192,GL_RGBA_INTEGER,GL_UNSIGNED_BYTE,objects.data());
    glUniform1ui(10,1);ensure(draw(0)==draw(8),"native source must remain unchanged");
    // Exercise the actual HD-layer path too: a window can hide BG0/3D
    // without downgrading the complete frame to native resolution.
    std::vector<uint32_t> layers(192*4);
    for(int y=0;y<192;++y){layers[y*4]=0x003F00u;layers[y*4+1]=32u|128u|(4u<<16)|(5u<<24);}
    GLuint hdInputs[]={texture(4,GL_RGBA32UI,GL_UNSIGNED_INT,1,layers.data()),
        texture(5,GL_RGBA32UI,GL_UNSIGNED_INT,1,layers.data())};
    glUniform1ui(10,4);glUniform1ui(11,1);glUniform1ui(12,1);
    const uint32_t resolved=draw(8);
    ensure((resolved&255u)>=127u&&((resolved>>16)&255u)>=127u&&((resolved>>8)&255u)==0u,
        "HD layers must resolve actual high-resolution subpixels");
    for(int y=0;y<192;++y)layers[y*4+1]|=64u;
    glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D,hdInputs[0]);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,192,GL_RGBA_INTEGER,GL_UNSIGNED_INT,layers.data());
    ensure((draw(8)&0xFFFFFFu)==0x00FF00u,"HD window must hide 3D at every subpixel");
    for(int y=0;y<192;++y)layers[y*4+1]=16u|128u; // opaque foreground HUD
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,192,GL_RGBA_INTEGER,GL_UNSIGNED_INT,layers.data());
    ensure((draw(8)&0xFFFFFFu)==0x00FF00u,"HD SSAA must preserve foreground HUD");
    glDeleteTextures(2,hdInputs);
    glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&output);
    glDeleteTextures(3,inputs);glDeleteTextures(1,&hires);glDeleteProgram(program);
    glDeleteShader(vertex);glDeleteShader(fragment);
    std::puts("Actual GPU SSAA integrates HD samples, preserves HUD and leaves native source unchanged");
}

void test_cpu_survivors() {
    using namespace nds_survivors;
    constexpr unsigned sw=448;
    nds_survivors::Frame frame;frame.lines=192;
    std::vector<uint32_t> source(sw*192),base(256*192,0xFF7953CDu);
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<sw;++x)
        source[y*sw+x]=((x+3*y)&63u)|(((x/7+y)&63u)<<8u)|(17u<<16u)|(((y%31)+1)<<24u);
    GLuint vertex=shader(GL_VERTEX_SHADER,nds_survivor_present::kNoFlipVertex);
    GLuint fragment=shader(GL_FRAGMENT_SHADER,nds_survivor_present::kFragment);
    GLuint program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
    GLint linked{};glGetProgramiv(program,GL_LINK_STATUS,&linked);ensure(linked,"CPU survivor shader link");
    GLuint input,compactTexture;glGenTextures(1,&input);glGenTextures(1,&compactTexture);GLuint native=texture(4,GL_RGBA8UI,GL_UNSIGNED_BYTE,sw,source.data());
    GLuint fallback;glGenTextures(1,&fallback);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,fallback);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,192,0,GL_RGBA,GL_UNSIGNED_BYTE,base.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    GLuint rowBuffer;glGenBuffers(1,&rowBuffer);GLuint output,fbo,vao;glGenTextures(1,&output);glGenFramebuffers(1,&fbo);glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    for(unsigned pass=0;pass<7;++pass){
        const bool nativeView=pass==0||pass==4,center=pass==2;const unsigned w=nativeView?256:448;
        const bool compactOnly=pass==4||pass==5;
        std::vector<uint32_t> metadata(w*192*4),objects(w*192),expected(w*192);
        for(unsigned y=0;y<192;++y){
            auto* row=frame.row(y);row[Bldcnt]=0x3171;row[MasterBright]=((y/48)%3u)<<14u|5u;row[Eva]=7;row[Evb]=11;row[Evy]=y%17;row[Priority3D]=(y/16)&3u;row[RenderX]=y%3==0?5:y%3==1?507:0;row[Screen]=(y/24)&1u;row[LayerWidth]=w;row[Flags]=(y%11?Needs3D:0u)|(y%13?Visible3D:0u)|(center?CenterNative:0u);
            const bool compact=compactOnly||(pass==6&&(y&1u)==0u);
            if(compact)row[Flags]|=CompactObj;
            row[Backdrop]=0x000D2511u;
            Unit u{};u.bldcnt=row[Bldcnt];u.eva=7;u.evb=11;u.evy=row[Evy];u.master_bright=row[MasterBright];
            for(unsigned x=0;x<w;++x){
                const bool hidden=x%7==0,effects=x%5!=0;
                Pixel top{nativeView?0x00331B07u:0x00131B27u,16,uint8_t(x%3==0?8:x%3==1?255:0),uint8_t((x/32)&3u),0,true};
                Pixel below{0x000D2511u,32,0,4,5,true};
                const size_t i=size_t(y)*w+x;
                if(compact){
                    uint8_t mask=uint8_t((hidden?0u:1u)|(effects?32u:0u)|(x%13?16u:0u));
                    objects[i]=nds_gpu2d_pack_direct_pixel(top.color,top.priority,top.alpha,x%11!=0,mask);
                    if(x%11==0||!(mask&16u))top=below;
                }
                auto meta=[&](const Pixel& p){return uint32_t(p.target)|(hidden?64u:0u)|(effects?128u:0u)|(uint32_t(p.alpha)<<8u)|(uint32_t(p.priority)<<16u)|(uint32_t(p.order)<<24u);};
                metadata[i*4]=top.color;metadata[i*4+1]=meta(top);metadata[i*4+2]=below.color;metadata[i*4+3]=meta(below);
                // A compact row must not consume the general texture, even
                // when its neighbour changes back to the general pipeline.
                if(compact)std::fill_n(metadata.data()+i*4,4,0xDEADBEEFu);
                if(row[Screen]!=0||(row[Flags]&Needs3D)==0){int nx=int(x)-(int(w)-256)/2;expected[i]=nx>=0&&nx<256?base[y*256+nx]:0xFF000000u;continue;}
                uint32_t c{};const int pad=(int(w)-256)/2;bool margin=center&&(int(x)<pad||int(x)>=pad+256);
                if(!margin){int scroll=int(row[RenderX]);if(scroll>=256)scroll-=512;int sx=int(x)+scroll;bool valid=true;
                    if(nativeView||center){if(center)sx-=pad;valid=sx>=0&&sx<256;sx+=(int(sw)-256)/2;}
                    else sx-=(int(w)-int(sw))/2;
                    if(!hidden&&(row[Flags]&Visible3D)&&valid&&sx>=0&&sx<int(sw)){
                        uint32_t p=source[y*sw+sx];Pixel v{p&0x3F3F3Fu,1,0,uint8_t(row[Priority3D]),1,true,uint8_t(p>>24)};
                        auto ahead=[](const Pixel& a,const Pixel& b){return a.priority<b.priority||(a.priority==b.priority&&a.order<b.order);};if(ahead(v,top)){below=top;top=v;}else if(ahead(v,below))below=v;
                    }c=compose(u,top,below,effects);
                }
                if((u.master_bright>>14)==1)c=brighten(c,5,0);else if((u.master_bright>>14)==2)c=darken(c,5,15);
                auto channel=[&](unsigned s){unsigned v=(c>>s)&63u;return (v<<2u)|(v>>4u);};expected[i]=channel(0)|(channel(8)<<8u)|(channel(16)<<16u)|0xFF000000u;
            }
        }
        const uint32_t zero[4]{};
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,input);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32UI,compactOnly?1:w,compactOnly?1:192,0,GL_RGBA_INTEGER,GL_UNSIGNED_INT,compactOnly?zero:metadata.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,compactTexture);glTexImage2D(GL_TEXTURE_2D,0,GL_R32UI,w,192,0,GL_RED_INTEGER,GL_UNSIGNED_INT,objects.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,rowBuffer);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(frame.rows),frame.rows.data(),GL_STREAM_DRAW);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,rowBuffer);
        glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,192,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);ensure(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"survivor output FBO");
        glUseProgram(program);glUniform1ui(0,0);glUniform1ui(1,w);glUniform1ui(2,1);glUniform1ui(3,0);glUniform1ui(4,1);glUniform1ui(5,sw);glUniform1ui(6,0);glUniform1ui(7,nativeView);glUniform1ui(8,pass==3?97:0);glViewport(0,0,w,192);glDisable(GL_DITHER);glClearColor(0.1f,0.2f,0.3f,1);glClear(GL_COLOR_BUFFER_BIT);uint32_t untouched{};glReadPixels(0,100,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&untouched);glDrawArrays(GL_TRIANGLES,0,3);
        std::vector<uint32_t> actual(w*192);glReadPixels(0,0,w,192,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());ensure(glGetError()==GL_NO_ERROR,"survivor GL execution");
        for(unsigned y=0;y<(pass==3?97:192);++y)for(unsigned x=0;x<w;++x)if(actual[y*w+x]!=expected[y*w+x]){std::fprintf(stderr,"survivor pass%u x%u y%u got%08X want%08X\n",pass,x,y,actual[y*w+x],expected[y*w+x]);throw std::runtime_error("survivor/native integer blend mismatch");}
        if(pass==3)ensure(actual[100*w]==untouched,"unrecorded prefix rows untouched");
    }
    // Keep metadata on its native cell while AA follows its full sample
    // position, including neighbouring native 3D cells during downscaling.
    std::vector<uint32_t> cells(256*192*4);
    for(unsigned i=0;i<256*192;++i)cells[i*4+1]=cells[i*4+3]=32u|128u|(4u<<16u)|(5u<<24u);
    std::vector<uint32_t> emptyObjects(sw*192,nds_gpu2d_pack_direct_pixel(0,0,0,false,33));
    glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,compactTexture);glTexImage2D(GL_TEXTURE_2D,0,GL_R32UI,sw,192,0,GL_RED_INTEGER,GL_UNSIGNED_INT,emptyObjects.data());
    for(unsigned y=0;y<192;++y){auto* row=frame.row(y);std::fill_n(row,RowWords,0u);row[Flags]=Needs3D|Visible3D;row[LayerWidth]=256;}
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,input);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32UI,256,192,0,GL_RGBA_INTEGER,GL_UNSIGNED_INT,cells.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,rowBuffer);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(frame.rows),frame.rows.data(),GL_STREAM_DRAW);
    GLuint hi;glGenTextures(1,&hi);glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_2D,hi);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    std::vector<uint32_t> pattern(sw*4*768,0xFF000000u);
    for(unsigned y=0;y<768;++y)for(unsigned x=0;x<sw*4;++x)if((y&3u)>=2u&&(x&3u)!=1u)pattern[y*sw*4+x]=0xFFFF0000u;
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,sw*4,768,0,GL_RGBA,GL_UNSIGNED_BYTE,pattern.data());
    glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,192,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glUniform1ui(1,256);glUniform1ui(6,4);glUniform1ui(7,1);glUniform1ui(8,0);glViewport(0,0,256,192);
    const unsigned sampleCounts[]={2,4,8},reds[]={128,64,96};
    for(unsigned i=0;i<3;++i){glUniform1ui(4,sampleCounts[i]);glDrawArrays(GL_TRIANGLES,0,3);uint32_t p{};glReadPixels(0,96,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&p);ensure(std::abs(int(p&255u)-int(reds[i]))<=1&&(p&0xFFFFFF00u)==0xFF000000u,"survivor exact two/four/eight AA patterns");}
    for(unsigned y=0;y<192;++y)frame.row(y)[Flags]|=CompactObj;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,rowBuffer);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(frame.rows),frame.rows.data(),GL_STREAM_DRAW);
    for(unsigned i=0;i<3;++i){glUniform1ui(4,sampleCounts[i]);glDrawArrays(GL_TRIANGLES,0,3);uint32_t p{};glReadPixels(0,96,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&p);ensure(std::abs(int(p&255u)-int(reds[i]))<=1&&(p&0xFFFFFF00u)==0xFF000000u,"compact survivor exact two/four/eight AA patterns");}
    for(unsigned y=0;y<768;++y)for(unsigned x=0;x<sw*4;++x)pattern[y*sw*4+x]=((x/4)&1u)?0xFF000000u:0xFFFF0000u;
    glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_2D,hi);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,sw*4,768,GL_RGBA,GL_UNSIGNED_BYTE,pattern.data());glViewport(0,0,128,96);glUniform1ui(4,2);glDrawArrays(GL_TRIANGLES,0,3);uint32_t downscaled{};glReadPixels(0,48,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&downscaled);ensure(std::abs(int(downscaled&255u)-128)<=1,"downscaled survivor AA samples neighbouring 3D cells");
    cells.assign(sw*192*4,0);source.assign(sw*192,0);
    for(unsigned y=0;y<192;++y){frame.row(y)[Flags]=Needs3D|Visible3D|SkyboxFill;for(unsigned x=0;x<sw;++x)cells[(y*sw+x)*4+1]=cells[(y*sw+x)*4+3]=32u|128u|(4u<<16u)|(5u<<24u);source[y*sw+100]=0x1F0000BFu;source[y*sw+102]=0x1F3F0080u;}
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,input);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32UI,sw,192,0,GL_RGBA_INTEGER,GL_UNSIGNED_INT,cells.data());glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D,native);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,sw,192,GL_RGBA_INTEGER,GL_UNSIGNED_BYTE,source.data());glBindBuffer(GL_SHADER_STORAGE_BUFFER,rowBuffer);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(frame.rows),frame.rows.data(),GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,sw,192,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glViewport(0,0,sw,192);glUniform1ui(1,sw);glUniform1ui(4,1);glUniform1ui(6,0);glUniform1ui(7,0);glDrawArrays(GL_TRIANGLES,0,3);uint32_t repaired{};glReadPixels(101,96,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&repaired);ensure(repaired==0xFFFF0000u,"wide sky repair follows retained packed polygon IDs");
    glViewport(0,0,256,192);glUniform1ui(1,256);glUniform1ui(7,1);glDrawArrays(GL_TRIANGLES,0,3);uint32_t canonical{};glReadPixels(5,96,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&canonical);ensure(canonical==0xFF000000u,"canonical native observation ignores adaptive sky repair");
    for(unsigned y=0;y<192;++y)frame.row(y)[Flags]|=CompactObj;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,rowBuffer);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(frame.rows),frame.rows.data(),GL_STREAM_DRAW);
    glViewport(0,0,sw,192);glUniform1ui(1,sw);glUniform1ui(7,0);glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(101,96,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&repaired);ensure(repaired==0xFFFF0000u,"compact wide sky repair follows retained packed polygon IDs");
    glViewport(0,0,256,192);glUniform1ui(1,256);glUniform1ui(7,1);glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(5,96,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&canonical);ensure(canonical==0xFF000000u,"compact native observer ignores adaptive sky repair");
    // The same production shader draws an opaque main LCD, then blends only
    // the inset. Guest/observer output remains opaque after that presentation.
    glUniform1ui(2,0);glUniform1f(9,1.0f);glDrawArrays(GL_TRIANGLES,0,3);
    const auto placement=nds_screen_placement(NdsScreenLayout::PictureInPicture,256,256);
    const auto& inset=placement.screens[1];const uint32_t insetColor=0xFF2277CCu;
    const uint32_t mainColor=base[0];std::fill(base.begin(),base.end(),insetColor);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,fallback);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,192,GL_RGBA,GL_UNSIGNED_BYTE,base.data());
    glViewport(inset.x,192-inset.y-inset.h,inset.w,inset.h);
    glUniform1f(9,float(nds_screen_opacity(placement,1))/255.0f);
    glEnable(GL_BLEND);glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES,0,3);glDisable(GL_BLEND);
    std::vector<uint32_t> composed(sw*192);glReadPixels(0,0,sw,192,GL_RGBA,GL_UNSIGNED_BYTE,composed.data());
    const auto blended=nds_screen_blend_pixel(mainColor,insetColor,nds_screen_opacity(placement,1));
    for(int y=0;y<192;++y)for(int x=0;x<256;++x){
        const bool inside=x>=inset.x&&x<inset.x+inset.w&&y>=192-inset.y-inset.h&&y<192-inset.y;
        const uint32_t actual=composed[y*sw+x],expected=inside?blended:mainColor;
        ensure((actual>>24u)==255u,"PiP composition keeps the main framebuffer opaque");
        for(unsigned shift:{0u,8u,16u})ensure(std::abs(int((actual>>shift)&255u)-int((expected>>shift)&255u))<=1,"PiP alpha affects only inset pixels");
    }
    glUniform1f(9,1.0f);glViewport(0,0,256,192);glDrawArrays(GL_TRIANGLES,0,3);
    glReadPixels(5,96,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&canonical);ensure(canonical==insetColor,"canonical observer pixels remain opaque after PiP");
    ensure(glGetError()==GL_NO_ERROR,"survivor AA execution");glDeleteTextures(1,&hi);
    glDeleteBuffers(1,&rowBuffer);glDeleteTextures(1,&input);glDeleteTextures(1,&compactTexture);glDeleteTextures(1,&native);glDeleteTextures(1,&fallback);glDeleteTextures(1,&output);glDeleteFramebuffers(1,&fbo);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
    std::puts("General/compact/mixed CPU survivors match native integer effects/route/visibility/windows, native vs wide HUD, scroll clipping, centered margins, prefix limit and inset-only PiP opacity");
}

void test_native_gl_hd_transport() {
    // Run both production output shaders on the same asymmetric colour.
    // HD's BGR transport must decode to the native integer RGB6 contract.
    GLuint vertex=shader(GL_VERTEX_SHADER,nds_compute_present::kPresentVertex);
    GLuint source,attr,output,fbo,vao;
    glGenTextures(1,&source);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source);
    const uint32_t color=0xFFCD7931u;
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,&color);
    glGenTextures(1,&attr);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,attr);
    const uint32_t zero=0;
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,&zero);
    glGenTextures(1,&output);glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(0,0,1,1);
    uint32_t results[2]{};
    for(int pass=0;pass<2;++pass) {
        GLuint fragment=shader(GL_FRAGMENT_SHADER,pass?melonDS::NativeHiResFS:melonDS::NativePackFS);
        GLuint program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
        GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);ensure(linked,"native GL output shader link");
        glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,output);
        glTexImage2D(GL_TEXTURE_2D,0,pass?GL_RGBA8:GL_RGBA8UI,1,1,0,pass?GL_RGBA:GL_RGBA_INTEGER,GL_UNSIGNED_BYTE,nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
        ensure(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"native output framebuffer");
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,attr);
        glUseProgram(program);glUniform1i(glGetUniformLocation(program,"ColorInput"),0);
        if(!pass){glUniform1i(glGetUniformLocation(program,"AttrInput"),1);glUniform1i(glGetUniformLocation(program,"SourceScale"),1);}
        glDrawArrays(GL_TRIANGLES,0,3);
        glReadPixels(0,0,1,1,pass?GL_RGBA:GL_RGBA_INTEGER,GL_UNSIGNED_BYTE,&results[pass]);
        ensure(glGetError()==GL_NO_ERROR,"native GL output shader execution");
        glDeleteProgram(program);glDeleteShader(fragment);
    }
    for(unsigned channel=0;channel<3;++channel) {
        const unsigned native=(results[0]>>(channel*8))&63u;
        const unsigned hd=((results[1]>>((2-channel)*8))&255u)*63u;
        ensure(native==(hd+127u)/255u,"HD presenter BGR decode must match native RGB6 colours");
    }
    glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fbo);
    glDeleteTextures(1,&source);glDeleteTextures(1,&attr);glDeleteTextures(1,&output);glDeleteShader(vertex);
    std::puts("Native GL HD transport matches native RGB6 without red/blue inversion");
}
}
int main() {
    if(!SDL_Init(SDL_INIT_VIDEO))return 77;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window=SDL_CreateWindow("GPU compositor contract test",64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    auto context=window?SDL_GL_CreateContext(window):nullptr;
    if(!context||!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)){
        if(context)SDL_GL_DestroyContext(context);if(window)SDL_DestroyWindow(window);SDL_Quit();return 77;
    }
    std::printf("GL %s / %s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER));
    int result=0;try{test_shader(true);test_shader(true,4);test_shader(false);test_ssaa();test_native_gl_hd_transport();test_cpu_survivors();}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());result=1;}
    SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();return result;
}
