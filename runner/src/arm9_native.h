#pragma once
#include <cstdint>
#include <string>

// Optional execution machinery. The faithful runner remains the portable host
// implementation and owns device behavior, exceptions and unsupported opcodes.
bool nds_arm9_native_run();
bool nds_arm9_native_supported();
void nds_arm9_native_note_write();
void nds_arm9_native_flush_writes();
void nds_arm9_native_clear();
void nds_arm9_native_census(bool enabled, bool reset);
std::string nds_arm9_native_json();
// Explicit diagnostic export of the bounded, default-off compilation census.
// Kept separate so ordinary per-frame status does not serialize code locations.
std::string nds_arm9_native_compilation_json(bool reset=false);
void nds_arm9_native_prepare_initialize(const std::string& rom_sha1);
// Host-only work at a paused CPU boundary; never advances guest time/state.
bool nds_arm9_native_prepare_idle(uint32_t budget_us);
std::string nds_arm9_native_preparation_json();
