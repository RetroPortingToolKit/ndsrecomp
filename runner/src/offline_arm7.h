// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Selection is fixed before boot. Retail bootstrap remains responsible for
// initialization; takeover requires all native components and an idle device
// boundary. Reset preserves selection and revokes the previous takeover.
bool nds_offline_arm7_configure(const std::string& rom_sha1, bool offline,
                                std::string* error=nullptr);
bool nds_offline_arm7_selected();
bool nds_offline_arm7_active();
bool nds_offline_arm7_reset_pending();
void nds_offline_arm7_reset();
bool nds_offline_arm7_try_takeover(uint64_t now,std::string* error=nullptr);
uint64_t nds_offline_arm7_next_event(uint64_t now);
void nds_offline_arm7_advance(uint64_t now);
// Called only at the canonical display VBlank edge (or once for a masked
// pending event after real PM sleep). No independent native frame clock.
void nds_offline_arm7_vblank(uint64_t now);
bool nds_offline_arm7_preflight(uint32_t word);
// False leaves a supported request in the canonical FIFO for a later retry.
bool nds_offline_arm7_receive(uint32_t word,uint64_t now);
bool nds_offline_arm7_snapshot(std::vector<uint8_t>* out,std::string* error=nullptr);
bool nds_offline_arm7_validate(const std::vector<uint8_t>& in,std::string* error=nullptr);
bool nds_offline_arm7_import(const std::vector<uint8_t>& in,std::string* error=nullptr);
