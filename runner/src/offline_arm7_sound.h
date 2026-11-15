// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Fixed before bootstrap. Takeover supplies the next already-established
// sound-driver tick in system cycles; it must not invent a new timer phase.
bool nds_offline_sound_configure(const char *rom_sha1);
bool nds_offline_sound_takeover(uint64_t now, uint64_t next_driver_tick);
bool nds_offline_sound_ready();
bool nds_offline_sound_preflight(uint32_t payload, bool error);
// Capacity is separate from protocol support. The IPC owner must leave a
// supported word in the hardware FIFO until this returns true.
bool nds_offline_sound_can_receive(uint32_t payload, bool error);
const char *nds_offline_sound_takeover_status();
std::string nds_offline_sound_json();
bool nds_offline_sound_receive(uint32_t payload, bool error, uint64_t now);
uint64_t nds_offline_sound_next_event(uint64_t now);
void nds_offline_sound_advance(uint64_t now);
void nds_offline_sound_reset();
// PM masks the software-driver/alarm IRQs during sleep. Hardware PCM/capture
// clocks keep their existing SPU contract. Wake observes overdue callbacks
// once and retains their periodic phase, skipping elapsed periods.
void nds_offline_sound_sleep(bool entering, uint64_t now);
// Call before mutating shared sample/bank/sequence memory. Capture writes
// remain scheduled observations, since direct guest reads cannot trigger them.
void nds_offline_sound_before_write(const uint8_t *physical, uint32_t width,
                                    uint64_t now);
bool nds_offline_sound_source_overlap(const uint8_t *physical, uint32_t width);
bool nds_offline_sound_snapshot(std::vector<uint8_t> *out);
bool nds_offline_sound_validate(const std::vector<uint8_t> &in,
                                std::string *error);
bool nds_offline_sound_import(const std::vector<uint8_t> &in,
                              std::string *error);
