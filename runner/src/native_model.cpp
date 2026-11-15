// SPDX-License-Identifier: MIT
#include "native_model.h"
#include <bit>
namespace nds_native_model {
namespace {
int32_t Wrap(int64_t n) { return std::bit_cast<int32_t>(static_cast<uint32_t>(n)); }
int32_t Q(int32_t a, int32_t b, unsigned bits=12) { return Wrap((int64_t{a}*b)>>bits); }
int32_t Aspect(uint16_t numerator, uint16_t denominator) {
    // SDK fixed-point division rounds the Q32 quotient to Q12. A zero
    // denominator produces the rounded zero of its signed divide-by-zero result.
    return denominator ? Wrap((((uint64_t{numerator}<<32)/denominator)+0x80000)>>20) : 0;
}
}
nds_native_3d::Matrix TextureMatrix(const TextureTransform& t) {
    nds_native_3d::Matrix m{};m[15]=4096;
    const int32_t sc=Q(t.scale_s,t.cosine),ss=Q(t.scale_s,t.sine);
    const int32_t tc=Q(t.scale_t,t.cosine),ts=Q(t.scale_t,t.sine);
    m[0]=sc;m[5]=tc;
    m[1]=Wrap(-int64_t{ts}*Aspect(t.height,t.width))>>12;
    m[4]=Wrap(int64_t{ss}*Aspect(t.width,t.height))>>12;
    m[12]=Wrap(uint32_t{t.width}*(uint32_t(t.scale_s)-uint32_t(ss)-uint32_t(sc))*8u-
               uint32_t{t.width}*uint32_t(Q(t.scale_s,t.translate_s,8)));
    m[13]=Wrap(uint32_t{t.height}*(8192u+uint32_t(ts)-uint32_t(tc)-uint32_t(t.scale_t))*8u+
               uint32_t{t.height}*uint32_t(Q(t.scale_t,t.translate_t,8)));
    for(unsigned i:{0u,1u,12u})m[i]=Q(m[i],t.magnify_s);
    for(unsigned i:{4u,5u,13u})m[i]=Q(m[i],t.magnify_t);
    return m;
}
}
