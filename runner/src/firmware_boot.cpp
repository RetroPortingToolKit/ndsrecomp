#include "firmware_boot.h"

#include "direct_boot.h"
#include "io.h"

#include <array>

namespace {
uint16_t read16(const uint8_t* bytes) {
    return bytes[0] | (uint16_t{bytes[1]} << 8);
}

uint32_t read32(const uint8_t* bytes) {
    return read16(bytes) | (uint32_t{read16(bytes + 2)} << 16);
}

class BootStream {
public:
    BootStream(const std::vector<uint8_t>& flash, const uint32_t* key, size_t offset)
        : flash_(flash), key_(key), offset_(offset) {}

    bool read(uint8_t& byte) {
        if (index_ == 8) {
            if (offset_ > flash_.size() || flash_.size() - offset_ < 8) return false;
            block_[0] = read32(flash_.data() + offset_);
            block_[1] = read32(flash_.data() + offset_ + 4);
            nds_io_firmware_decrypt(block_.data(), key_);
            offset_ += 8;
            index_ = 0;
        }
        byte = static_cast<uint8_t>(block_[index_ / 4] >> ((index_ % 4) * 8));
        ++index_;
        return true;
    }

private:
    const std::vector<uint8_t>& flash_;
    const uint32_t* key_;
    size_t offset_;
    std::array<uint32_t, 2> block_{};
    unsigned index_ = 8;
};

bool unpack(BootStream stream, size_t limit, std::vector<uint8_t>& bytes) {
    std::array<uint8_t, 4> header{};
    for (auto& byte : header) {
        if (!stream.read(byte)) return false;
    }
    const size_t size = read32(header.data()) >> 8;
    if ((header[0] != 0 && header[0] != 0x10) ||
        size == 0 || size > limit || size % 4 != 0) return false;
    bytes.reserve(size);
    while (bytes.size() < size) {
        uint8_t flags;
        if (!stream.read(flags)) return false;
        for (unsigned mask = 0x80; mask != 0 && bytes.size() < size; mask >>= 1) {
            uint8_t first;
            if (!stream.read(first)) return false;
            if (!(flags & mask)) {
                bytes.push_back(first);
                continue;
            }
            uint8_t second;
            if (!stream.read(second)) return false;
            const size_t distance = ((size_t{first & 0xFu} << 8) | second) + 1;
            if (distance > bytes.size()) return false;
            const unsigned count = (first >> 4) + 3;
            for (unsigned i = 0; i < count && bytes.size() < size; ++i) {
                bytes.push_back(bytes[bytes.size() - distance]);
            }
        }
    }
    return true;
}

uint16_t crc16(const std::vector<uint8_t>& bytes, uint16_t crc) {
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xA001 : 0);
        }
    }
    return crc;
}

void copy_stage(NdsDirectBootMachine& machine, int cpu, uint32_t address,
                const std::vector<uint8_t>& bytes) {
    for (size_t i = 0; i < bytes.size(); i += 4) {
        machine.write32(cpu, address + static_cast<uint32_t>(i), read32(bytes.data() + i));
    }
}
}

bool nds_firmware_boot(NdsDirectBootMachine& machine,
                       const std::vector<uint8_t>& flash,
                       const std::vector<uint8_t>& arm7_bios,
                       std::string* error) {
    auto fail = [error](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (flash.size() != 0x40000 && flash.size() != 0x20000) {
        return fail("expected a 128 or 256 KiB Nintendo DS firmware dump");
    }
    const uint16_t shifts = read16(flash.data() + 0x14);
    auto offset = [&](size_t field, unsigned shift) {
        return uint32_t{read16(flash.data() + field)} << (2 + ((shifts >> shift) & 7));
    };
    const uint32_t arm9_distance = offset(0x0E, 3);
    const uint32_t arm7_distance = offset(0x12, 9);
    if (arm9_distance == 0 || arm9_distance > 0x800000 ||
        arm7_distance == 0 || arm7_distance > 0x10000) {
        return fail("firmware startup programs must load inside main RAM and ARM7 WRAM");
    }
    std::array<uint32_t, 0x412> key{};
    if (!nds_io_firmware_key1(read32(flash.data() + 8), arm7_bios.data(),
                              arm7_bios.size(), key.data())) {
        return fail("the original ARM7 BIOS did not provide the firmware key");
    }
    std::vector<uint8_t> arm9, arm7;
    if (!unpack(BootStream(flash, key.data(), offset(0x0C, 0)), arm9_distance, arm9) ||
        !unpack(BootStream(flash, key.data(), offset(0x10, 6)), arm7_distance, arm7) ||
        crc16(arm7, crc16(arm9, 0xFFFF)) != read16(flash.data() + 6)) {
        return fail("firmware startup programs could not be decoded or failed their checksum");
    }
    const uint32_t arm9_address = 0x02800000 - arm9_distance;
    const uint32_t arm7_address = 0x03810000 - arm7_distance;
    machine.set_wramcnt(3);
    copy_stage(machine, 0, arm9_address, arm9);
    copy_stage(machine, 1, arm7_address, arm7);
    machine.set_arm7_bios_prot(0x1204);
    machine.set_cpu_boot(0, arm9_address, 0x03002F7C, 0x03003F80, 0x03003FC0);
    machine.set_cpu_boot(1, arm7_address, 0x0380FD80, 0x0380FF80, 0x0380FFC0);
    return true;
}
