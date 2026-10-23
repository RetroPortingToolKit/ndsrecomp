#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "function_finder.h"

// Emit one region over a group of same-mode discovered functions. Existing
// validation metadata must prove the COMPLETE group before entering this body.
std::string nds_emit_flat_region(const std::vector<ndsrecomp::Function>& functions,
    std::size_t first, std::size_t last, const uint8_t* image,
    std::size_t image_size, uint32_t image_base);
