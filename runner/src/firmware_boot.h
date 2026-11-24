// Host loader for the startup programs in an original Nintendo DS firmware.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class NdsDirectBootMachine;

bool nds_firmware_boot(NdsDirectBootMachine& machine,
                       const std::vector<uint8_t>& firmware,
                       const std::vector<uint8_t>& arm7_bios,
                       std::string* error);
