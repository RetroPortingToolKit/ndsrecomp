// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the production offset shader and raster work-index mapping at the
// portable per-axis dispatch boundary, including the partially occupied row.
#include <SDL3/SDL.h>
#include "glad/glad.h"
#include "GPU3D_Compute_shaders.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace melonDS;
void require(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}
GLuint program(const std::string& source) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr); glCompileShader(shader);
    GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetShaderInfoLog(shader, sizeof log, nullptr, log);
        throw std::runtime_error(log);
    }
    GLuint result = glCreateProgram(); glAttachShader(result, shader);
    glLinkProgram(result); glDeleteShader(shader);
    glGetProgramiv(result, GL_LINK_STATUS, &ok);
    require(ok, "compute program link failed"); return result;
}
void tests() {
    constexpr unsigned variants = 256;
    constexpr unsigned countOffset = variants * 4 + variants;
    constexpr unsigned sortOffset = countOffset + variants;
    const std::string prefix = "#version 430\n#define MaxVariants 256\n#define NumVariants 2u\n"
        "#define TilesPerLine 1\n#define TileLines 1\n#define CoarseBinStride 2\n#define BinStride 64\n"
        "#define MaxWorkTiles 2097152\n";
    GLuint offsets = program(prefix + ComputeRendererShaders::CalcOffsets);
    GLuint sort = program(prefix + ComputeRendererShaders::SortWork);
    GLuint raster = program(prefix + ComputeRendererShaders::BinningBuffer +
        "layout(local_size_x=1) in;\n"
        "layout(location=0) uniform uint CurVariant;\n"
        "layout(std430,binding=8) buffer Visits { uint visits[]; };\n" +
        ComputeRendererShaders::RasterWorkIndex +
        "void main(){ uint i; if(GetRasterWorkIndex(i)) atomicAdd(visits[i],1u); }\n");
    GLuint buffers[4]; glGenBuffers(4, buffers);
    // Different simultaneous variants exercise the original atomic prefix
    // allocation as well as the split, without relying on invocation order.
    for (unsigned count : {0u, 1u, 65534u, 65535u, 65536u, 131071u, 2097120u}) {
        std::vector<GLuint> header(sortOffset + 4, 0);
        header[0] = header[1] = header[4] = header[5] = 1;
        header[2] = count; header[6] = 17;
        header[3] = count + 17; // total unsorted work, retained by production
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, header.size()*sizeof(GLuint), header.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, buffers[0]);
        glUseProgram(offsets); glDispatchCompute(1,1,1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, header.size()*sizeof(GLuint), header.data());
        require(glGetError() == GL_NO_ERROR, "offset shader/readback GL error");
        require(header[countOffset] == count && header[countOffset+1] == 17,
                "per-variant work counts changed");
        unsigned first = header[variants*4], second = header[variants*4+1];
        require((first == 0 && second == count) || (second == 0 && first == 17),
                "sorted variant ranges overlap or leave a gap");
        require(header[3] == count+17 && header[7] == count+17,
                "binning totals were damaged");
        unsigned sx=header[sortOffset], sy=header[sortOffset+1], sz=header[sortOffset+2];
        require(sx >= 1 && sx <= 65535 && sy <= 65535 && sz == 1,
                "production sort dispatch exceeds portable GL limit");
        std::vector<GLuint> polygons(20,0); polygons[17]=1; // two 40-byte Polygon records
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[2]);
        glBufferData(GL_SHADER_STORAGE_BUFFER,polygons.size()*4,polygons.data(),GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffers[2]);
        constexpr unsigned sortedStart=2097152;
        std::vector<GLuint> descriptors((sortedStart+count+17)*2,0);
        for(unsigned i=0;i<count+17;++i) {
            descriptors[i*2]=i+1; // independent identity retained by sort
            descriptors[i*2+1]=i<count ? i<<11 : ((i-count)<<11)|1u;
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[3]);
        glBufferData(GL_SHADER_STORAGE_BUFFER,descriptors.size()*4,descriptors.data(),GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,7,buffers[3]);
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,buffers[0]);
        glUseProgram(sort); glDispatchComputeIndirect(sortOffset*4);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,sortedStart*2u*4u,(count+17u)*2u*4u,descriptors.data());
        require(glGetError()==GL_NO_ERROR,"bounded sort dispatch GL error");
        for(unsigned i=0;i<count+17;++i) {
            unsigned at=i<count ? first+i : second+i-count;
            require(descriptors[at*2]==i+1 && descriptors[at*2+1]==((i<<11)|(i<count?0u:1u)),
                    "production sort omitted or misdirected work");
        }
        for (unsigned variant = 0; variant < 2; ++variant) {
            unsigned n = variant ? 17 : count;
            unsigned x = header[variant*4], y = header[variant*4+1], z = header[variant*4+2];
            require(x >= 1 && x <= 65535 && y == 1 && z <= 65535,
                    "production indirect dispatch exceeds portable GL limit");
            require(x*z >= n && x*z-n < x, "unbounded rectangular over-dispatch");
            std::vector<GLuint> visits(n + x + 1, 0);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
            glBufferData(GL_SHADER_STORAGE_BUFFER, visits.size()*sizeof(GLuint), visits.data(), GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, buffers[1]);
            glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, buffers[0]);
            glUseProgram(raster); glUniform1ui(0, variant);
            glDispatchComputeIndirect(variant*4*sizeof(GLuint));
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, visits.size()*sizeof(GLuint), visits.data());
            require(glGetError() == GL_NO_ERROR, "bounded raster dispatch GL error");
            require(std::all_of(visits.begin(), visits.begin()+n, [](GLuint v){return v == 1;}),
                    "raster work was omitted or visited more than once");
            require(std::all_of(visits.begin()+n, visits.end(), [](GLuint v){return v == 0;}),
                    "partial row accessed work beyond the variant");
        }
        std::printf("%u work items: bounded, complete, unique, tail protected\n", count);
    }
    glDeleteBuffers(4,buffers); glDeleteProgram(offsets); glDeleteProgram(sort); glDeleteProgram(raster);
}
}
int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 77;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window=SDL_CreateWindow("bounded compute dispatch test",64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    auto context=window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context || !gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        if(context) SDL_GL_DestroyContext(context);
        if(window) SDL_DestroyWindow(window);
        SDL_Quit(); return 77;
    }
    int result=0;
    try { tests(); } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what()); result=1;}
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit(); return result;
}
