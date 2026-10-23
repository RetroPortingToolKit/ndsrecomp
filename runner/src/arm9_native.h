#pragma once
#include <string>

// Optional execution machinery. The faithful runner remains the portable host
// implementation and owns device behavior, exceptions and unsupported opcodes.
bool nds_arm9_native_run();
void nds_arm9_native_note_write();
void nds_arm9_native_clear();
void nds_arm9_native_census(bool enabled, bool reset);
std::string nds_arm9_native_json();
