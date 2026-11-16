// SPDX-License-Identifier: GPL-3.0-or-later
// CPU-selected native-density survivors; final blend samples owned GPU 3D.
#pragma once
#include "../gpu3d_survivors.h"
#include <string>
namespace nds_survivor_present {
static_assert(nds_survivors::RowWords==12 && nds_survivors::Flags==9 &&
              nds_survivors::Backdrop==11 && nds_survivors::CompactObj==16);
inline constexpr const char* kNoFlipVertex=R"GLSL(
#version 430 core
out vec2 uv;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);}
)GLSL";
inline constexpr const char* kFragment=R"GLSL(
#version 430 core
layout(binding=0) uniform usampler2D layers;
layout(binding=1) uniform sampler2D fallback;
layout(binding=2) uniform sampler2D overlayBlack;
layout(binding=3) uniform sampler2D overlayWhite;
layout(binding=4) uniform usampler2D tex3dNative;
layout(binding=5) uniform sampler2D tex3dHi;
layout(binding=6) uniform usampler2D objects;
layout(std430,binding=2) readonly buffer FrameRows {uint rows[];};
layout(location=0) uniform uint screen;
layout(location=1) uniform uint width;
layout(location=2) uniform uint hasGpu;
layout(location=3) uniform uint hasOverlay;
layout(location=4) uniform uint resolveSamples;
layout(location=5) uniform uint source3DWidth;
layout(location=6) uniform uint source3DScale;
layout(location=7) uniform uint nativeView;
layout(location=8) uniform uint rowLimit;
layout(location=9) uniform float presentationOpacity=1.0;
in vec2 uv;out vec4 outColor;
const uint Bldcnt=0u,MasterBright=1u,Eva=2u,Evb=3u,Evy=4u,Priority3D=5u;
const uint RenderX=6u,Screen=7u,LayerWidth=8u,Flags=9u,Backdrop=11u;
uint rb;
uint r(uint field){return rows[rb+field];}
struct Layer {uvec3 color;uint target,alpha,alpha5,priority,order;bool valid;};
Layer emptyLayer(){return Layer(uvec3(0u),0u,0u,0u,4u,5u,false);}
bool ahead(Layer a,Layer b){return a.priority<b.priority||(a.priority==b.priority&&a.order<b.order);}
void pushLayer(Layer v,inout Layer top,inout Layer below){if(!v.valid)return;if(ahead(v,top)){below=top;top=v;}else if(ahead(v,below))below=v;}
uvec3 brighten(uvec3 c,uint a,uint bias){return min(c+(((uvec3(63u)-c)*a+uvec3(bias))>>4u),uvec3(63u));}
uvec3 darken(uvec3 c,uint a,uint bias){return c-((c*a+uvec3(bias))>>4u);}
uvec3 blend16(uvec3 a,uvec3 b,uint ca,uint cb){return min((a*ca+b*cb+uvec3(8u))>>4u,uvec3(63u));}
uvec3 compose(Layer top,Layer below,bool effects){
    uint bld=r(Bldcnt),second=below.target<<8u;
    if(top.alpha5!=0u){
        if((bld&second)!=0u){uint ca=top.alpha5+1u;return ca==32u?top.color:min((top.color*ca+below.color*(32u-ca)+uvec3(16u))>>5u,uvec3(63u));}
        if(effects&&(bld&top.target)!=0u){uint mode=(bld>>6u)&3u;if(mode==2u)return brighten(top.color,r(Evy),8u);if(mode==3u)return darken(top.color,r(Evy),7u);}return top.color;
    }
    if(top.alpha!=0u&&(bld&second)!=0u){uint ca=top.alpha==255u?r(Eva):top.alpha,cb=top.alpha==255u?r(Evb):16u-ca;return blend16(top.color,below.color,ca,cb);}
    if(effects&&(bld&top.target)!=0u){uint mode=(bld>>6u)&3u;if(mode==1u&&(bld&second)!=0u)return blend16(top.color,below.color,r(Eva),r(Evb));if(mode==2u)return brighten(top.color,r(Evy),8u);if(mode==3u)return darken(top.color,r(Evy),7u);}return top.color;
}
uvec3 master(uvec3 c){uint mode=r(MasterBright)>>14u,a=min(r(MasterBright)&31u,16u);if(mode==1u)return brighten(c,a,0u);if(mode==2u)return darken(c,a,15u);return c;}
Layer unpackLayer(uvec2 d){return Layer(uvec3(d.x,d.x>>8u,d.x>>16u)&uvec3(63u),d.y&63u,(d.y>>8u)&255u,0u,(d.y>>16u)&255u,(d.y>>24u)&255u,true);}
vec3 outputRgb8(uvec3 c){return vec3((c<<2u)|(c>>4u))/255.0;}
Layer frameTop,frameBelow;
bool hidden3d,effects,skyHit;
uvec4 skySample;
void selectCompact(ivec2 cell){
    uint color=r(Backdrop),word=texelFetch(objects,cell,0).r;
    frameTop=Layer(uvec3(color,color>>8u,color>>16u)&uvec3(63u),32u,0u,0u,4u,5u,true);
    frameBelow=frameTop;
    hidden3d=(word&64u)!=0u||(r(Flags)&2u)==0u;
    effects=(word&16384u)==0u;
    uint alpha=word>>24u;
    if(alpha!=0u){
        frameTop=Layer(uvec3(word,word>>8u,word>>16u)&uvec3(63u),16u,
            alpha==17u?0u:alpha,0u,(word>>22u)&3u,0u,true);
    }
}
int scroll(){int x=int(r(RenderX)&511u);return x>=256?x-512:x;}
// Clip the original hardware viewport before source centering.
int sourceX(int x){
    if(nativeView!=0u){x+=scroll();if(x<0||x>=256)return -1;return x+(int(source3DWidth)-256)/2;}
    if((r(Flags)&4u)!=0u){x-=(int(width)-256)/2;x+=scroll();if(x<0||x>=256)return -1;return x+(int(source3DWidth)-256)/2;}
    return x-(int(width)-int(source3DWidth))/2+scroll();
}
uvec4 nativeAt(int x,int y){int sx=x+scroll();ivec2 size=textureSize(tex3dNative,0);return sx<0||sx>=int(source3DWidth)||sx>=size.x||y>=size.y?uvec4(0u):texelFetch(tex3dNative,ivec2(sx,y),0);}
bool skyPixel(uvec4 p){uint id=(p.r>>6u)|((p.g>>6u)<<2u)|((p.b>>6u)<<4u);return (p.a&31u)!=0u&&any(notEqual(p.rgb&uvec3(63u),uvec3(0u)))&&id==2u;}
bool skyRepair(int x,int y,out uvec4 repaired){
    repaired=uvec4(0u);if(nativeView!=0u||(r(Flags)&8u)==0u)return false;
    uvec4 p=nativeAt(x,y);bool black=all(equal(p.rgb&uvec3(63u),uvec3(0u)));if((p.a&31u)!=0u&&!black)return false;int sw=int(source3DWidth);
    if((p.a&31u)!=0u){int run=1;for(int i=x-1;i>=0;--i){if(any(notEqual(nativeAt(i,y).rgb&uvec3(63u),uvec3(0u))))break;++run;}for(int i=x+1;i<sw;++i){if(any(notEqual(nativeAt(i,y).rgb&uvec3(63u),uvec3(0u))))break;++run;}if(run<8)return false;}
    int count=0,left=-1,leftRank=-1,last=-1;for(int i=0;i<sw;++i)if(skyPixel(nativeAt(i,y))){if(i<=x){left=i;leftRank=count;}last=i;++count;}
    if(count<2)return false;if(left<0){left=last-sw;leftRank=count-1;}int target=(leftRank+x-left)%count,rank=0;
    for(int i=0;i<sw;++i)if(skyPixel(nativeAt(i,y))){if(rank==target){repaired=nativeAt(i,y);return true;}++rank;}return false;
}
Layer sample3D(vec2 point,ivec2 cell){
    Layer v=emptyLayer();if(hidden3d||source3DWidth==0u)return v;
    if(skyHit){v.color=skySample.rgb&uvec3(63u);v.alpha5=skySample.a&31u;}
    else{
        if(source3DScale!=0u){
            int scale=int(source3DScale);vec2 bounded=clamp(point,vec2(0.0),vec2(float(width),192.0)-vec2(1.0/float(scale)));
            ivec2 p=ivec2(bounded*float(scale));
            if(nativeView!=0u||(r(Flags)&4u)!=0u){
                if(nativeView==0u)p.x-=(int(width)-256)/2*scale;
                p.x+=scroll()*scale;if(p.x<0||p.x>=256*scale)return v;p.x+=(int(source3DWidth)-256)/2*scale;
            }else p.x+=(scroll()-(int(width)-int(source3DWidth))/2)*scale;
            if(p.x<0||p.x>=int(source3DWidth)*scale||any(greaterThanEqual(p,textureSize(tex3dHi,0))))return v;
            vec4 c=texelFetch(tex3dHi,p,0);v.color=uvec3(round(c.bgr*63.0));v.alpha5=uint(round(c.a*31.0));
        }else{int sx=sourceX(cell.x);if(sx<0||sx>=int(source3DWidth))return v;ivec2 p=ivec2(sx,cell.y);if(any(greaterThanEqual(p,textureSize(tex3dNative,0))))return v;uvec4 c=texelFetch(tex3dNative,p,0);v.color=c.rgb&uvec3(63u);v.alpha5=c.a&31u;}}
    v.target=1u;v.priority=r(Priority3D);v.order=1u;v.valid=v.alpha5!=0u;return v;
}
vec3 sampleLcd(vec2 point,ivec2 cell){Layer top=frameTop,below=frameBelow;pushLayer(sample3D(point,cell),top,below);return outputRgb8(master(compose(top,below,effects)));}
void main(){
    // Derivatives precede divergent HUD/window/route branches.
    vec2 size=vec2(float(width),192.0),dx=dFdx(uv)*size,dy=dFdy(uv)*size;
    vec2 f=clamp(uv,vec2(0.0),vec2(0.999999))*size;ivec2 p=ivec2(f);rb=uint(p.y)*12u;
    if(rowLimit!=0u&&uint(p.y)>=rowLimit)discard;
    bool rowGpu=hasGpu!=0u&&r(Screen)==screen&&(r(Flags)&1u)!=0u;vec3 c;
    if(rowGpu){
        bool margin=nativeView==0u&&(r(Flags)&4u)!=0u&&(p.x<(int(width)-256)/2||p.x>=(int(width)+256)/2);
        if(margin)c=outputRgb8(master(uvec3(0u)));
        else{
            if((r(Flags)&16u)!=0u)selectCompact(p);
            else{uvec4 d=texelFetch(layers,p,0);frameTop=unpackLayer(d.xy);frameBelow=unpackLayer(d.zw);hidden3d=(d.y&64u)!=0u||(r(Flags)&2u)==0u;effects=(d.y&128u)!=0u;}
            int logical=p.x-(int(width)-int(source3DWidth))/2;skyHit=!hidden3d&&skyRepair(logical,p.y,skySample);
            if(resolveSamples<2u||source3DScale<=1u||hidden3d)c=sampleLcd(f,p);
            else if(resolveSamples==2u)c=(sampleLcd(f-0.25*(dx+dy),p)+sampleLcd(f+0.25*(dx+dy),p))*0.5;
            else{int cols=resolveSamples>=8u?4:2;c=vec3(0.0);for(int y=0;y<2;++y)for(int x=0;x<cols;++x){vec2 off=vec2((float(x)+0.5)/float(cols)-0.5,(float(y)+0.5)/2.0-0.5);c+=sampleLcd(f+off.x*dx+off.y*dy,p);}c/=float(cols*2);}}
    }else{int x=p.x-(int(width)-256)/2;c=x>=0&&x<256?texelFetch(fallback,ivec2(x,p.y),0).rgb:vec3(0.0);}
    if(hasOverlay!=0u){vec3 b=texelFetch(overlayBlack,p,0).rgb,w=texelFetch(overlayWhite,p,0).rgb;c=b+c*clamp(w-b,vec3(0.0),vec3(1.0));}outColor=vec4(c,presentationOpacity);
}
)GLSL";
}
