// Exercise the actual production presentation shader against the native CPU
// compositor, including scanline changes and masked/absent layers. Reuse its
// existing device fixtures; this does not boot or play a game.
#define NDS_GPU2D_REFERENCE_ONLY 1
#include "gpu2d_window_test.cpp"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include "glad/glad.h"
#include "melonds_compute/PresentShaders.h"
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
void test_shader(bool scanline) {
    constexpr int width=448,height=192;
    std::vector<uint32_t> three(width*height),objects(width*height),raster(height*4),expected(width*height);
    for (int y=0;y<height;++y) {
        Unit u{};u.bldcnt=uint16_t(0x3131u|((scanline?y&3u:1u)<<6));u.eva=7;u.evb=11;u.evy=uint8_t(scanline?1+y%16:6);
        u.master_bright=scanline?uint16_t(((y/48)&3u)<<14|5u):0;
        const uint32_t background=0x000D2511u;
        const uint8_t priority3=scanline?uint8_t((y/16)&3):2u;
        raster[y*4]=background;raster[y*4+1]=u.bldcnt|(uint32_t(u.master_bright)<<16);
        raster[y*4+2]=u.eva|(uint32_t(u.evb)<<8)|(uint32_t(u.evy)<<16)|(uint32_t(priority3)<<24);
        raster[y*4+3]=0;
        for (int x=0;x<width;++x) {
            const uint8_t mask=scanline?uint8_t(x&63):63u,a3=uint8_t(y&31);
            const uint8_t alpha=std::array<uint8_t,3>{0,8,255}[(y/32)%3];
            const uint8_t priority=uint8_t((x/64)&3);
            const bool valid=x%7!=0;
            const uint32_t object_color=0x00200733u,three_color=0x00092B11u;
            const size_t i=size_t(y)*width+x;
            three[i]=three_color|(uint32_t(a3)<<24);
            objects[i]=nds_gpu2d_pack_direct_pixel(object_color,priority,alpha,valid,mask);
            Pixel top{background,32,0,4,5,true},below=top;
            auto push=[&](const Pixel& p) {
                auto ahead=[](const Pixel& a,const Pixel& b){return a.priority<b.priority||(a.priority==b.priority&&a.order<b.order);};
                if(ahead(p,top)){below=top;top=p;}else if(ahead(p,below))below=p;
            };
            if(valid&&(mask&16))push(Pixel{object_color,16,alpha,priority,0,true});
            if(a3&&(mask&1))push(Pixel{three_color,1,0,priority3,1,true,a3});
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
    GLuint output,fbo,vao;glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    ensure(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"test framebuffer");
    // Rebind the raster sampler after allocating the output texture.
    glActiveTexture(GL_TEXTURE6);glBindTexture(GL_TEXTURE_2D,inputs[3]);
    glUseProgram(program);glUniform1ui(0,1);glUniform1ui(1,width);glUniform1ui(10,1);glUniform1ui(11,0);glUniform1ui(13,scanline?1u:0u);
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
    glDeleteProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
    std::printf("Actual GPU %s RGB6 blending matches native compositor for86016 pixels; one-byte UNORM8 rounding=%u\n",scanline?"window/raster":"legacy direct",transport_rounding);
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
    int result=0;try{test_shader(true);test_shader(false);test_ssaa();}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());result=1;}
    SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();return result;
}
