// SPDX-License-Identifier: MIT
#pragma once
#include "hle_mkds.h"
#include <string>
namespace mkds_model {
void initialize(bool supported);
void clear_cache();
void draw();
bool is_entry(uint32_t pc);
mkds_hle::Replacement resolve(uint32_t pc);
std::string statistics_json();
}
