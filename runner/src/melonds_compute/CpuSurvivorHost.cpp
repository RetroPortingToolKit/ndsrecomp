// SPDX-License-Identifier: GPL-3.0-or-later
#include "CpuSurvivorHost.h"
#include "../gpu3d_survivors.h"
#include "../cpu_render_handoff.h"
#include <atomic>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32) && defined(NDS_HAVE_COMPUTE_RENDERER) && defined(NDS_HAVE_SDL3)
#include "../execution_model.h"
#include "../gpu3d.h"
#endif

bool nds_survivor_host_requested(){
    if(const char* p=std::getenv("NDS_3D_SURVIVORS");p&&*p)
        return std::strcmp(p,"1")==0;
#if defined(_WIN32) && defined(NDS_HAVE_COMPUTE_RENDERER) && defined(NDS_HAVE_SDL3)
    return nds_gpu3d_has_native_geometry()&&nds_performance_execution();
#else
    return false;
#endif
}

#if defined(NDS_HAVE_COMPUTE_RENDERER)
#include "ComputeHost.h"
#include "D3D11InteropTarget.h"
#include "SurvivorPresentShader.h"
#include "PresentShaders.h"
#include "../gpu2d.h"
#include "../gpu3d.h"
#include "../savestate.h"
#include "glad/glad.h"
#if defined(NDS_HAVE_SDL3)
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>
// Added by the frontend integration; also declared here so this isolated
// transport builds against the frozen producer commit independently.
void nds_compute_host_note_presented_width(uint16_t width);

namespace {
using nds_survivors::Frame;
struct Source {GLuint native{},hi{};int width{},height{},hiWidth{},hiHeight{};unsigned scale{};uint64_t identity{},epoch{};uint32_t buffer{};};
std::array<Source,2> sources;
Source scratch;
std::atomic<uint64_t> generation{1};
bool active=false,interop=false;
SDL_Renderer* bottomRenderer{};
std::array<D3D11InteropTarget,2> bottomTargets;
int bottomWidth{},bottomHeight{};unsigned bottomIndex{};
GLuint programs[2]{},vao{},rowsBuffer{},layerTexture{},objectTexture{},fallbackTexture{},overlayTextures[2]{},readTexture{},readFbo{};
uint64_t frames{},copies{},observerReads{},uploadedId{},uploadedEpoch{};
uint32_t uploadedBuffer{};bool uploadedNative{};
struct Cache {uint64_t id{},epoch{};uint32_t buffer{};int screen{-1};bool adaptive{};uint16_t width{};std::vector<uint32_t> pixels;};
Cache caches[2][2];

bool releaseBottomTargets(){
    // SDL texture destruction can flush a pending D3D9 resize/reset. Drop
    // every external registration/reference before calling SDL for any one.
    bool ready=true;std::string error;
    for(auto& target:bottomTargets)if(!target.prepare_reset(&error))ready=false;
    if(!ready){std::fprintf(stderr,"[gpu3d] interop reset detach failed: %s\n",error.c_str());return false;}
    for(auto& target:bottomTargets)target.reset();
    bottomWidth=bottomHeight=0;bottomIndex=0;
    return true;
}

struct State {
    GLint fbo{},readFbo{},program{},array{},viewport[4]{},pack{},unpack{},packing[8]{};
    static constexpr GLenum names[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS};
    State(){glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&array);glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);for(unsigned i=0;i<8;++i)glGetIntegerv(names[i],&packing[i]);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);for(unsigned i=0;i<8;++i)glPixelStorei(names[i],i%4==0?4:0);}
    ~State(){glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,readFbo);glUseProgram(program);glBindVertexArray(array);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);for(unsigned i=0;i<8;++i)glPixelStorei(names[i],packing[i]);}
};
GLuint compile(GLenum type,const char* text){
    GLuint s=glCreateShader(type);glShaderSource(s,1,&text,nullptr);glCompileShader(s);GLint ok{};glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[4096]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);std::fprintf(stderr,"survivor shader: %s\n",log);glDeleteShader(s);return 0;}return s;
}
GLuint link(const char* vertex){
    GLuint v=compile(GL_VERTEX_SHADER,vertex),f=compile(GL_FRAGMENT_SHADER,nds_survivor_present::kFragment);if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return 0;}
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);GLint ok{};glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[4096]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);std::fprintf(stderr,"survivor link: %s\n",log);glDeleteProgram(p);return 0;}return p;
}
void bind(unsigned unit,GLuint tex){glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,tex);}
void parameters(){glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
void release(Source& s){if(s.native)glDeleteTextures(1,&s.native);if(s.hi)glDeleteTextures(1,&s.hi);s={};}
bool cloneTexture(GLuint from,GLuint& to,int& oldWidth,int& oldHeight,GLenum format,int& width,int& height){
    if(!from)return false;
    bind(7,from);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);if(width<=0||height<=0)return false;
    if(!to)glGenTextures(1,&to);
    bind(7,to);if(width!=oldWidth||height!=oldHeight){glTexImage2D(GL_TEXTURE_2D,0,format,width,height,0,format==GL_RGBA8UI?GL_RGBA_INTEGER:GL_RGBA,GL_UNSIGNED_BYTE,nullptr);parameters();oldWidth=width;oldHeight=height;}
    glCopyImageSubData(from,GL_TEXTURE_2D,0,0,0,0,to,GL_TEXTURE_2D,0,0,0,0,width,height,1);++copies;return true;
}
bool captureSource(Source& s,const Frame& frame){
    if(!nds_compute_host_make_current())return false;
    State state;
    int width{},height{};glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
    if(!cloneTexture(nds_gpu3d_compute_output_texture(),s.native,s.width,s.height,GL_RGBA8UI,width,height))return false;
    GLuint hi=nds_gpu3d_compute_output_texture_hires();s.scale=0;
    if(hi){int hw{},hh{};if(!cloneTexture(hi,s.hi,s.hiWidth,s.hiHeight,GL_RGBA8,hw,hh))return false;s.scale=hw/width;}
    s.identity=frame.identity;s.buffer=frame.buffer;s.epoch=generation.load();glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);return glGetError()==GL_NO_ERROR;
}
Source* findSource(const Frame& f){Source& s=sources[f.buffer&1u];return s.identity==f.identity&&s.buffer==f.buffer&&s.epoch==generation.load()?&s:nullptr;}
bool upload(const Frame& f,bool native){
    const uint64_t epoch=generation.load();
    if(uploadedId==f.identity&&uploadedBuffer==f.buffer&&uploadedEpoch==epoch&&uploadedNative==native)return true;
    const auto& data=native?f.native_layers:f.wide_layers;
    const auto& objects=native?f.native_objects:f.wide_objects;
    const unsigned stride=native?256:448;
    bool general=false,compact=false;
    if(!f.capture)for(unsigned y=0;y<std::min<unsigned>(f.lines,192);++y){
        const uint32_t flags=f.row(y)[nds_survivors::Flags];
        if((flags&nds_survivors::Needs3D)==0)continue;
        if(flags&nds_survivors::CompactObj)compact=true;else general=true;
    }
    if(general&&data.size()<size_t(stride)*192*4)return false;
    if(compact&&objects.size()<size_t(stride)*192)return false;
    // Compact-only frames never upload the full two-layer representation.
    // Inactive samplers still receive a complete, valid integer texture.
    const uint32_t zero[4]{};
    bind(0,layerTexture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32UI,general?stride:1,general?192:1,0,GL_RGBA_INTEGER,GL_UNSIGNED_INT,general?data.data():zero);parameters();
    bind(6,objectTexture);glTexImage2D(GL_TEXTURE_2D,0,GL_R32UI,compact?stride:1,compact?192:1,0,GL_RED_INTEGER,GL_UNSIGNED_INT,compact?objects.data():zero);parameters();
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,rowsBuffer);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(f.rows),f.rows.data(),GL_STREAM_DRAW);
    uploadedId=f.identity;uploadedBuffer=f.buffer;uploadedEpoch=epoch;uploadedNative=native;return true;
}
bool draw(const Frame& f,int screen,bool native,bool lowres,Source* source,GLuint fbo,int vw,int vh,bool noFlip,const uint32_t* black,const uint32_t* white,unsigned aa,int vx=0,int vy=0){
    if(screen<0||screen>1||vw<=0||vh<=0)return false;
    bool gpu=!f.capture&&f.needs_gpu[screen];if(gpu&&!source)return false;
    const unsigned width=native?256:std::clamp<unsigned>(f.widths[screen],256,448);
    if(!f.native_base[screen]||!upload(f,native))return false;
    bind(0,layerTexture);bind(6,objectTexture);bind(1,fallbackTexture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,192,0,GL_BGRA,GL_UNSIGNED_BYTE,f.native_base[screen]);parameters();
    if(black&&white)for(unsigned i=0;i<2;++i){bind(2+i,overlayTextures[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,192,0,GL_BGRA,GL_UNSIGNED_BYTE,i?white:black);parameters();}
    bind(4,source?source->native:0);bind(5,source?source->hi:0);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,rowsBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(vx,vy,vw,vh);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);glUseProgram(programs[noFlip?1:0]);glBindVertexArray(vao);
    glUniform1ui(0,screen);glUniform1ui(1,width);glUniform1ui(2,gpu);glUniform1ui(3,black&&white);glUniform1ui(4,aa);glUniform1ui(5,source?source->width:0);glUniform1ui(6,!lowres&&source?source->scale:0);glUniform1ui(7,native);glUniform1ui(8,f.lines<192?f.lines:0);
    glDrawArrays(GL_TRIANGLES,0,3);return glGetError()==GL_NO_ERROR;
}
const uint32_t* readFrame(const Frame* f,int screen,bool adaptive,uint16_t* width,Source* overrideSource=nullptr,bool force=false){
    if(width)*width=256;
    if(!active||!f||screen<0||screen>1||!nds_compute_host_make_current())return nullptr;
    auto& cache=caches[screen][adaptive?1:0];
    const uint64_t epoch=generation.load();
    if(!force&&cache.id==f->identity&&cache.epoch==epoch&&cache.buffer==f->buffer&&cache.screen==screen&&cache.adaptive==adaptive){if(width)*width=cache.width;return cache.pixels.data();}
    const unsigned w=adaptive?std::clamp<unsigned>(f->widths[screen],256,448):256;
    State state;bind(7,readTexture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,192,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);parameters();glBindFramebuffer(GL_FRAMEBUFFER,readFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,readTexture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return nullptr;
    if(!draw(*f,screen,!adaptive,true,overrideSource?overrideSource:findSource(*f),readFbo,w,192,true,nullptr,nullptr,1))return nullptr;
    cache.pixels.resize(w*192);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glReadPixels(0,0,w,192,GL_BGRA,GL_UNSIGNED_BYTE,cache.pixels.data());++observerReads;if(glGetError()!=GL_NO_ERROR)return nullptr;
    cache.id=f->identity;cache.epoch=epoch;cache.buffer=f->buffer;cache.screen=screen;cache.adaptive=adaptive;cache.width=w;if(width)*width=w;return cache.pixels.data();
}
}

bool nds_survivor_host_enabled(){return active;}
bool nds_survivor_host_start(SDL_Renderer* renderer){
    if(active)return renderer==bottomRenderer;
    if(!nds_survivor_host_requested()||!nds_compute_host_active()||!nds_compute_host_make_current())return false;
    programs[0]=link(nds_compute_present::kPresentVertex);programs[1]=link(nds_survivor_present::kNoFlipVertex);if(!programs[0]||!programs[1]){nds_survivor_host_stop();return false;}
    glGenVertexArrays(1,&vao);glGenBuffers(1,&rowsBuffer);glGenTextures(1,&layerTexture);glGenTextures(1,&objectTexture);glGenTextures(1,&fallbackTexture);glGenTextures(2,overlayTextures);glGenTextures(1,&readTexture);glGenFramebuffers(1,&readFbo);
    bottomRenderer=renderer;interop=false;
    if(renderer){std::string error;if(!bottomTargets[0].create(renderer,256,192,&error)||!bottomTargets[0].lock(&error)||!bottomTargets[0].unlock(&error)){std::fprintf(stderr,"survivor interop unavailable: %s\n",error.c_str());nds_survivor_host_stop();return false;}bottomWidth=256;bottomHeight=192;interop=true;}
    active=true;frames=copies=observerReads=0;nds_gpu2d_set_hd_emit(false);nds_gpu2d_set_direct_present(false);nds_gpu2d_set_survivors(true);return true;
}
void nds_survivor_host_stop(){
    if(active)nds_gpu2d_set_survivors(false);
    active=false;interop=false;
    if(nds_compute_host_active()&&nds_compute_host_make_current()){
        if(!releaseBottomTargets())return;
        for(auto& s:sources)release(s);
        release(scratch);
        glDeleteProgram(programs[0]);glDeleteProgram(programs[1]);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&rowsBuffer);glDeleteTextures(1,&layerTexture);glDeleteTextures(1,&objectTexture);glDeleteTextures(1,&fallbackTexture);glDeleteTextures(2,overlayTextures);glDeleteTextures(1,&readTexture);glDeleteFramebuffers(1,&readFbo);
    }
    programs[0]=programs[1]=vao=rowsBuffer=layerTexture=objectTexture=fallbackTexture=readTexture=readFbo=0;overlayTextures[0]=overlayTextures[1]=0;bottomRenderer=nullptr;bottomWidth=bottomHeight=0;bottomIndex=0;uploadedId=uploadedEpoch=0;for(auto& screens:caches)for(auto& cache:screens)cache={};generation.fetch_add(1);
}
void nds_survivor_host_capture(){
    if(!active)return;
    if(nds_cpu_render_on_worker()){nds_cpu_render_main([]{nds_survivor_host_capture();});return;}
    // Pending is intentionally nonjoining: CPU scanline workers may still run.
    const Frame* f=nds_gpu2d_survivor_pending();if(!f||f->capture)return;
    if(captureSource(sources[f->buffer&1u],*f))++frames;
}
void nds_survivor_host_invalidate(){generation.fetch_add(1);}
bool nds_survivor_host_reset_bottom(){
    return !active||(nds_compute_host_make_current()&&releaseBottomTargets());
}
const uint32_t* nds_survivor_host_read(const Frame* f,int screen,bool adaptive,uint16_t* width){
    if(nds_cpu_render_on_worker()){const uint32_t* p{};nds_cpu_render_main([&]{p=readFrame(f,screen,adaptive,width);});return p;}return readFrame(f,screen,adaptive,width);
}
const uint32_t* nds_survivor_host_observe(int screen,bool adaptive,uint16_t* width){
    if(nds_cpu_render_on_worker()){const uint32_t* p{};nds_cpu_render_main([&]{p=nds_survivor_host_observe(screen,adaptive,width);});return p;}return readFrame(nds_gpu2d_survivor_current(),screen,adaptive,width);
}
bool nds_survivor_host_snapshot(NdsGpu2dSaveState* out){
    if(!active)return true;
    if(!out||out->framebuffers.size()!=4u*256u*192u)return false;
    if(nds_cpu_render_on_worker()){bool ok{};nds_cpu_render_main([&]{ok=nds_survivor_host_snapshot(out);});return ok;}
    const Frame* list[]={nds_gpu2d_survivor_current(),nds_gpu2d_survivor_pending(),nds_gpu2d_survivor_building()};
    for(unsigned i=0;i<3;++i){const Frame* f=list[i];if(!f||!f->lines)continue;Source* s=nullptr;
        if(i==2&&!f->capture){if(!captureSource(scratch,*f))return false;s=&scratch;uploadedId=0;for(auto& screens:caches)for(auto& cache:screens)cache.id=0;}
        for(int screen=0;screen<2;++screen){if(!f->needs_gpu[screen]||f->capture)continue;const uint32_t* p=readFrame(f,screen,false,nullptr,s,i==2);if(!p)return false;std::copy_n(p,size_t(f->lines)*256,out->framebuffers.data()+((f->buffer&1u)*2u+screen)*256u*192u);}
    }for(auto& screens:caches)for(auto& cache:screens)cache.id=0;return true;
}
bool nds_survivor_host_present_top(SDL_Window* window,const Frame* f,const uint32_t* black,const uint32_t* white,unsigned aa,NdsComputePresentTicks* ticks){
    if(!active||!window||!f||!nds_compute_host_make_current())return false;
    State state;const auto begin=SDL_GetPerformanceCounter();int w{},h{};
#if defined(NDS_HAVE_SDL3)
    if(!SDL_GetWindowSizeInPixels(window,&w,&h))return false;
#else
    SDL_GL_GetDrawableSize(window,&w,&h);
#endif
    const unsigned width=f->widths[0];const double aspect=double(width)/192.0;int vw=w,vh=int(w/aspect);if(vh>h){vh=h;vw=int(h*aspect);}glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,w,h);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    bool ok=draw(*f,0,false,false,findSource(*f),0,vw,vh,false,black,white,aa,(w-vw)/2,(h-vh)/2);if(!ok)return false;
    const auto swap=SDL_GetPerformanceCounter();
#if defined(NDS_HAVE_SDL3)
    ok=SDL_GL_SwapWindow(window);
#else
    SDL_GL_SwapWindow(window);
#endif
    if(ticks){ticks->draw+=swap-begin;ticks->swap+=SDL_GetPerformanceCounter()-swap;}if(ok)nds_compute_host_note_presented_width(width);return ok;
}
SDL_Texture* nds_survivor_host_present_bottom(const Frame* f,int w,int h,const uint32_t* black,const uint32_t* white,unsigned aa,NdsComputePresentTicks* ticks){
    if(!active||!interop||!f||w<=0||h<=0||!nds_compute_host_make_current())return nullptr;
    State state;const auto begin=SDL_GetPerformanceCounter();std::string error;
    if(w!=bottomWidth||h!=bottomHeight){if(!releaseBottomTargets())return nullptr;bottomWidth=w;bottomHeight=h;}
    auto& target=bottomTargets[bottomIndex++&1u];if(!target.texture()&&!target.create(bottomRenderer,w,h,&error))return nullptr;if(!target.lock(&error))return nullptr;
    bool ok=draw(*f,1,false,false,findSource(*f),target.framebuffer(),w,h,true,black,white,aa);if(!target.unlock(&error))ok=false;if(ticks)ticks->draw+=SDL_GetPerformanceCounter()-begin;return ok?target.texture():nullptr;
}
std::string nds_survivor_host_json(){return std::string("{\"enabled\":")+(active?"true":"false")+",\"interop_available\":"+(interop?"true":"false")+",\"frames\":"+std::to_string(frames)+",\"source_copies\":"+std::to_string(copies)+",\"observer_readbacks\":"+std::to_string(observerReads)+"}";}

#else
bool nds_survivor_host_start(SDL_Renderer*){return false;}
bool nds_survivor_host_enabled(){return false;}
void nds_survivor_host_stop(){}
void nds_survivor_host_capture(){}
void nds_survivor_host_invalidate(){}
bool nds_survivor_host_reset_bottom(){return true;}
bool nds_survivor_host_snapshot(NdsGpu2dSaveState*){return true;}
const uint32_t* nds_survivor_host_read(const nds_survivors::Frame*,int,bool,uint16_t*){return nullptr;}
const uint32_t* nds_survivor_host_observe(int,bool,uint16_t*){return nullptr;}
bool nds_survivor_host_present_top(SDL_Window*,const nds_survivors::Frame*,const uint32_t*,const uint32_t*,unsigned,NdsComputePresentTicks*){return false;}
SDL_Texture* nds_survivor_host_present_bottom(const nds_survivors::Frame*,int,int,const uint32_t*,const uint32_t*,unsigned,NdsComputePresentTicks*){return nullptr;}
std::string nds_survivor_host_json(){return "{\"enabled\":false,\"interop_available\":false,\"frames\":0,\"observer_readbacks\":0}";}
#endif
