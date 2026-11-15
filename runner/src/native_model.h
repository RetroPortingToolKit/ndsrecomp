// SPDX-License-Identifier: MIT
#pragma once
#include "native_3d.h"
namespace nds_native_model {
struct TextureTransform {
    int32_t scale_s{4096}, scale_t{4096};
    int16_t sine{}, cosine{4096};
    int32_t translate_s{}, translate_t{};
    uint16_t width{}, height{};
    int32_t magnify_s{4096}, magnify_t{4096};
};
nds_native_3d::Matrix TextureMatrix(const TextureTransform& transform);
}
