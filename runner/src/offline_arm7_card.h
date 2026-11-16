#pragma once
#include <cstdint>
#include <string>
#include <vector>

bool nds_offline_card_configure(const std::string& rom_sha1);
void nds_offline_card_reset();
bool nds_offline_card_ready();
bool nds_offline_card_preflight(uint32_t tag, uint32_t payload, bool error);
bool nds_offline_card_can_receive(uint32_t tag, uint32_t payload, bool error);
void nds_offline_card_advance(uint64_t now);
uint64_t nds_offline_card_next_event(uint64_t now);
bool nds_offline_card_receive(uint32_t tag, uint32_t payload, bool error, uint64_t now);
bool nds_offline_card_terminated();
bool nds_offline_card_snapshot(std::vector<uint8_t>* out);
bool nds_offline_card_validate(const std::vector<uint8_t>& in, std::string* error = nullptr);
bool nds_offline_card_import(const std::vector<uint8_t>& in, std::string* error = nullptr);
