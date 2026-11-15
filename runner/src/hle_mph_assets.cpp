#include "hle_mph_assets.h"
#include "hle_vector_math.h"
#include "execution_model.h"
#include "sha1.h"
#include "state.h"
#include "tier3.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>

namespace mph_assets {
namespace {
constexpr uint32_t file_entry = 0x0204314Cu, file_resume = 0x0204320Cu;
constexpr uint32_t archive_entry = 0x02042F18u, archive_resume = 0x02042F54u;
constexpr uint32_t allocator = 0x0204488Cu, mount = 0x02060F04u;
constexpr uint32_t archive_manager_slot = 0x020EAA40u;
constexpr uint32_t rom_archive = 0x020FFC74u, current_directory = 0x020FFC4Cu;
constexpr uint32_t frame_magic = 0x4150484Du; // MHPA, build-specific continuation
constexpr uint32_t frame_size = 32u, max_output = 4u * 1024u * 1024u;
enum : uint32_t { File = 1, Archive = 2 };
enum Word : uint32_t { Magic, Kind, Offset, Length, Decoded, Argument, Manager, Return };
Services service;
Statistics count;
std::array<uint8_t, 0x10C> file_proof{};
std::array<uint8_t, 0x58> archive_proof{};
std::array<uint8_t, 0xBC> allocator_proof{};
std::array<uint8_t, 0x64> mount_proof{};
std::array<uint8_t, 0x20> mount_link_proof{};
const NdsStaticValidationRange dependencies[] = {
    {allocator, allocator_proof.size(), allocator_proof.data()},
    {mount, mount_proof.size(), mount_proof.data()},
    {0x02060AE4u, mount_link_proof.size(), mount_link_proof.data()},
};
NdsStaticValidation file_validation{file_entry, file_proof.size(),
    file_proof.data(), dependencies, 3};
NdsStaticValidation archive_validation{archive_entry, archive_proof.size(),
    archive_proof.data(), dependencies, 3};

bool ram(uint32_t address, uint32_t size) {
    if (!size || size - 1u > UINT32_MAX - address) return false;
    uint32_t first = 0, last = 0;
    const auto* a = nds_busf_classify(address, &first);
    const auto* b = nds_busf_classify(address + size - 1u, &last);
    if (!a || a != b || !a->data || !a->written || !a->gen ||
        uint64_t{first} + size > uint64_t{a->mask} + 1u ||
        last != first + size - 1u) return false;
    if (a != &g_busf_itcm && a != &g_busf_dtcm && g_busf_dtcm_limit &&
        uint64_t{address} < uint64_t{g_busf_dtcm_base} + g_busf_dtcm_limit &&
        uint64_t{address} + size > g_busf_dtcm_base) return false;
    return true;
}

bool overlaps(uint32_t a, uint32_t size, uint32_t b, uint32_t other) {
    uint32_t x = 0, y = 0;
    const auto* first = nds_busf_classify(a, &x);
    const auto* second = nds_busf_classify(b, &y);
    if (!first || !second || !first->data || !second->data) return true;
    const auto p = reinterpret_cast<uintptr_t>(first->data + x);
    const auto q = reinterpret_cast<uintptr_t>(second->data + y);
    return p < q + other && q < p + size;
}

bool read_string(uint32_t address, std::string& result, size_t maximum) {
    result.clear();
    for (size_t i = 0; i <= maximum; ++i) {
        if (i > UINT32_MAX - address || !ram(address + uint32_t(i), 1)) return false;
        const uint8_t value = bus_read_u8(address + uint32_t(i));
        if (!value) return !result.empty();
        result.push_back(char(value));
    }
    return false;
}

bool ordinary_root() {
    // FS_FindPath copies this complete FSDirPos for a relative name. A
    // relative path must never be interpreted against an invented ROM root.
    if (!ram(current_directory, 12) || !ram(rom_archive, 0x60) ||
        bus_read_u32(current_directory) != rom_archive ||
        bus_read_u32(current_directory + 4) || bus_read_u32(current_directory + 8))
        return false;
    constexpr std::pair<uint32_t, uint32_t> identity[] = {
        {0, 0x006D6F72u}, {0x28, 0},
        {0x30, 0x56D8u}, {0x38, 0xFE40u},
        {0x3C, 0x173000u}, {0x40, 0x163000u},
        {0x48, 0x0208CC94u}, {0x4C, 0x0208CC8Cu},
        {0x50, 0x0208CC94u}, {0x54, 0x0208CBFCu}, {0x58, 0x602u},
    };
    for (const auto& [offset, value] : identity)
        if (bus_read_u32(rom_archive + offset) != value) return false;
    const uint32_t flags = bus_read_u32(rom_archive + 0x1Cu);
    if (flags == 3) {
        if (bus_read_u32(rom_archive + 0x2Cu) != 0x173000u ||
            bus_read_u32(rom_archive + 0x34u) != 0x163000u ||
            bus_read_u32(rom_archive + 0x44u)) return false;
    } else if (flags == 7) {
        // FS_LoadArchiveTables replaces the logical table bases with RAM,
        // retaining the physical ROM bases. Prove the actual cached content;
        // treating changed/custom tables as the cartridge root is unsafe.
        const uint32_t cache = bus_read_u32(rom_archive + 0x44u);
        if (!cache || !ram(cache, 4)) return false;
        for (const auto& [slot, offset, size] : {
                std::array<uint32_t, 3>{0x2Cu, 0x173000u, 0x56D8u},
                std::array<uint32_t, 3>{0x34u, 0x163000u, 0xFE40u}}) {
            const uint32_t address = bus_read_u32(rom_archive + slot);
            FileView bytes;
            uint32_t at = 0;
            if (!ram(address, size) || !service.view(offset, size, &bytes) ||
                !bytes.data || bytes.offset != offset || bytes.size != size)
                return false;
            const auto* window = nds_busf_classify(address, &at);
            if (std::memcmp(window->data + at, bytes.data, size)) return false;
        }
    } else return false;
    // No queued/current archive operation is borrowed by this service.
    // +4/+8 are registered-archive next/previous links, not pending work.
    for (uint32_t offset : {0xCu, 0x10u, 0x14u, 0x18u, 0x20u, 0x24u})
        if (bus_read_u32(rom_archive + offset)) return false;
    return true;
}

bool normalized_path(std::string& path) {
    // Explicit/custom archives remain with the original FS implementation.
    if (path.find(':') != std::string::npos) return false;
    std::replace(path.begin(), path.end(), '\\', '/');
    if (!path.empty() && path.front() == '/') path.erase(path.begin());
    return !path.empty();
}

void fallback() {
    ++count.declines;
    tier3_step_one(g_cpu.R[15]);
    if (!runtime_unwinding() && !runtime_should_yield())
        runtime_dispatch(g_cpu.R[15] | ((g_cpu.cpsr & CPSR_T_BIT) ? 1u : 0u));
}

template <size_t N>
bool prove(uint32_t start, std::array<uint8_t, N>& proof, const char* digest) {
    std::array<uint8_t, N> live{};
    if (!bus_range_has_write_provenance(start, N) ||
        !bus_debug_copy(0, start, live.data(), N) ||
        gba::sha1(live.data(), N).hex() != digest) return false;
    proof = live;
    return true;
}

void dispatch(uint32_t pc, uint32_t cycles) {
    g_cpu.R[15] = pc;
    runtime_tick(cycles);
    if (!runtime_unwinding() && !runtime_should_yield())
        runtime_dispatch(g_cpu.R[15]);
}

bool manager(uint32_t& pointer) {
    if (!ram(archive_manager_slot, 4)) return false;
    pointer = bus_read_u32(archive_manager_slot);
    return !(pointer & 3u) && ram(pointer, 0x7Cu) &&
        !bus_read_u32(pointer + 0x18u) &&
        (bus_read_u32(pointer + 0x40u) & 0x11u) != 0x11u;
}

// Match the ordinary archive-name lookup. Existing mounted archives are a
// successful no-op; the request does not allocate or publish another copy.
bool find_archive(uint32_t manager_pointer, const std::string& name, bool& found) {
    found = false;
    // The SDK's bounded comparison uses its live ASCII case map, including
    // when an already mounted name differs only in capitalization.
    constexpr uint32_t case_map = 0x020B3654u;
    if (!ram(case_map, 128)) return false;
    const auto fold = [](uint8_t value) {
        return value < 128 ? bus_read_u8(case_map + value) : value;
    };
    uint32_t cursor = bus_read_u32(manager_pointer + 0x14u);
    for (unsigned i = 0; cursor && i < 256; ++i) {
        if ((cursor & 3u) || !ram(cursor, 0x28)) return false;
        std::string existing;
        if (!read_string(cursor, existing, 31)) return false;
        if (existing.size() == name.size() &&
            std::equal(existing.begin(), existing.end(), name.begin(),
                [&](char a, char b) { return fold(uint8_t(a)) == fold(uint8_t(b)); })) {
            found = true; return true;
        }
        const uint32_t next = bus_read_u32(cursor + 0x20u);
        if (next == cursor) return false;
        cursor = next;
    }
    return !cursor;
}

void begin(uint32_t kind) {
    std::string path, name;
    const uint32_t argument = g_cpu.R[0];
    uint32_t manager_pointer = 0;
    if (!ordinary_root() || !read_string(g_cpu.R[1], path, 255) ||
        !normalized_path(path)) { fallback(); return; }
    if (kind == File) {
        if ((argument & 3u) || !ram(argument, 4)) { fallback(); return; }
    } else {
        if (!read_string(argument, name, 31) || !manager(manager_pointer) ||
            path.size() < 4 || path.substr(path.size() - 4) != ".arc") {
            fallback(); return;
        }
        bool found = false;
        if (!find_archive(manager_pointer, name, found)) { fallback(); return; }
        if (found) { ++count.archives; g_cpu.R[0] = 1; nds_hle::finish(32); return; }
    }
    FileView file;
    uint32_t output_size = 0;
    if (!service.lookup(path.c_str(), &file) || !file.data || !file.size ||
        !service.inspect(file, &output_size) || !output_size ||
        output_size > max_output || (kind == Archive && output_size < 32)) {
        fallback(); return;
    }
    const uint32_t sp = g_cpu.R[13];
    if ((sp & 7u) || sp < frame_size || !ram(sp - frame_size, frame_size) ||
        overlaps(sp - frame_size, frame_size, file_entry, file_proof.size()) ||
        overlaps(sp - frame_size, frame_size, archive_entry, archive_proof.size()) ||
        overlaps(sp - frame_size, frame_size, allocator, allocator_proof.size()) ||
        overlaps(sp - frame_size, frame_size, mount, mount_proof.size()) ||
        overlaps(sp - frame_size, frame_size, argument,
                 kind == File ? 4u : uint32_t(name.size() + 1u))) {
        fallback(); return;
    }
    const std::array<uint32_t, 8> frame{frame_magic, kind, file.offset, file.size,
        output_size, argument, manager_pointer, g_cpu.R[14]};
    for (unsigned i = 0; i < frame.size(); ++i)
        bus_write_u32(sp - frame_size + i * 4u, frame[i]);
    g_cpu.R[13] = sp - frame_size;
    g_cpu.R[14] = kind == File ? file_resume : archive_resume;
    if (kind == File) {
        g_cpu.R[0] = output_size;
        dispatch(allocator, 48);
    } else {
        g_cpu.R[0] = argument;
        g_cpu.R[1] = allocator;
        // The mount helper adds its own record padding; it takes the decoded
        // payload size, not the allocation size calculated internally.
        g_cpu.R[2] = output_size;
        dispatch(mount, 48);
    }
}

void complete(uint32_t kind) {
    const uint32_t sp = g_cpu.R[13];
    if ((sp & 7u) || !ram(sp, frame_size)) { fallback(); return; }
    std::array<uint32_t, 8> frame{};
    for (unsigned i = 0; i < frame.size(); ++i) frame[i] = bus_read_u32(sp + i * 4u);
    // This PC is also a legitimate original continuation. A native-owned
    // frame must be proved before replacing it, including after save/load.
    if (frame[Magic] != frame_magic || frame[Kind] != kind) { fallback(); return; }
    if ((kind == File && ((frame[Argument] & 3u) ||
            !ram(frame[Argument], 4) || frame[Manager])) ||
        (kind == Archive && ((frame[Manager] & 3u) ||
            !ram(frame[Manager], 0x7Cu) ||
            bus_read_u32(archive_manager_slot) != frame[Manager]))) {
        nds_halt("MPH assets: invalid native frame ownership"); return;
    }
    FileView file;
    uint32_t size = 0;
    if (!service.view(frame[Offset], frame[Length], &file) || !file.data ||
        file.offset != frame[Offset] || file.size != frame[Length] ||
        !service.inspect(file, &size) || size != frame[Decoded] ||
        !size || size > max_output) {
        nds_halt("MPH assets: invalid native continuation"); return;
    }
    const uint32_t allocation = g_cpu.R[0];
    if (kind == File && overlaps(frame[Argument], 4u, sp, frame_size)) {
        nds_halt("MPH assets: output slot aliases native frame"); return;
    }
    if (kind == File && !allocation) {
        bus_write_u32(frame[Argument], 0);
        g_cpu.R[13] = sp + frame_size; g_cpu.R[14] = frame[Return];
        g_cpu.R[0] = 0; ++count.files; nds_hle::finish(32); return;
    }
    if (!allocation || (allocation & 3u) ||
        (kind == Archive && allocation > UINT32_MAX - 0x28u)) {
        nds_halt("MPH assets: guest allocation failed"); return;
    }
    const uint32_t destination = allocation + (kind == Archive ? 0x28u : 0u);
    if (!ram(allocation, size + (kind == Archive ? 0x28u : 0u))) {
        nds_halt("MPH assets: invalid guest allocation"); return;
    }
    // A restored allocator result must not overwrite the live continuation or
    // its ownership metadata through a different virtual mirror of the RAM.
    if (overlaps(destination, size, sp, frame_size) ||
        (kind == File && overlaps(destination, size, frame[Argument], 4u)) ||
        (kind == Archive &&
            (overlaps(destination, size, frame[Manager], 0x7Cu) ||
             overlaps(destination, size, archive_manager_slot, 4u) ||
             overlaps(destination, size, allocation, 0x28u)))) {
        nds_halt("MPH assets: allocation aliases native ownership"); return;
    }
    std::vector<uint8_t> decoded;
    if (!service.decode(file, &decoded) || decoded.size() != size) {
        nds_halt("MPH assets: immutable decode failed"); return;
    }
    if (kind == Archive && (std::memcmp(decoded.data(), "SNDFILE\0", 8) != 0 ||
        !ram(frame[Manager], 0x7Cu) ||
        bus_read_u32(frame[Manager] + 0x14u) != allocation ||
        bus_read_u32(allocation + 0x24u) != size)) {
        nds_halt("MPH assets: invalid mounted archive"); return;
    }
    if (!service.publish(destination, decoded.data(), size, g_runtime_cycles)) {
        nds_halt("MPH assets: physical publication failed"); return;
    }
    if (kind == File) { bus_write_u32(frame[Argument], allocation); ++count.files; }
    else ++count.archives;
    count.bytes += size;
    count.last_offset=file.offset;count.last_compressed=file.size;
    count.last_destination=destination;count.last_decoded=size;
    g_cpu.R[13] = sp + frame_size;
    g_cpu.R[14] = frame[Return];
    g_cpu.R[0] = kind == File ? size : 1u;
    // A fixed native-service charge, not a replay of card words or decoder
    // tokens. This candidate's timing profile still requires qualification.
    nds_hle::finish(64u + (size + 31u) / 32u);
}
} // namespace

bool initialize(const char* rom_sha1, const Services& services) {
    service = services;
    count = {};
    count.enabled = rom_sha1 && std::strcmp(rom_sha1,
        "90164d1ac127ee5f9815ea4ae7de798c7b5fc629") == 0 &&
        service.lookup && service.view && service.inspect &&
        service.decode && service.publish;
    return count.enabled;
}
Statistics statistics() { return count; }
bool is_entry(NdsCpu cpu, uint32_t pc, bool thumb) {
    return count.enabled && cpu == NDS_ARM9 && !thumb &&
        (pc == file_entry || pc == file_resume ||
         pc == archive_entry || pc == archive_resume);
}
Replacement resolve(NdsCpu cpu, uint32_t pc, bool thumb) {
    if (!is_entry(cpu, pc, thumb)) return {};
    const bool file = pc == file_entry || pc == file_resume;
    auto* validation = file ? &file_validation : &archive_validation;
    const uint32_t start = validation->addr, length = validation->size;
    if (!prove(allocator, allocator_proof, "b6c3d9ea2e93efd4d4ff118191560c4f7e018bed") ||
        !prove(mount, mount_proof, "297bcd7282ce228bc2e40c921f315e64a420e27c") ||
        !prove(0x02060AE4u, mount_link_proof, "c5a8977cc88d2cc302eb870bd382e126e7f30a03") ||
        !(file ? prove(file_entry, file_proof, "decccc76a602c4019afe3be6824dc92b9814afb7")
               : prove(archive_entry, archive_proof, "3a9be247d6f333f4f0a0497a6745a99a7f4f60fa")))
        return {nullptr, validation, start, length};
    return {run, validation, start, length};
}
void run() {
    // An admitted canonical frame remains owned until completion. Enabling a
    // debugger observer while the allocator is suspended cannot reinterpret
    // that frame as the original function's larger stack layout.
    if (count.enabled && g_cpu.R[15] == file_resume) { complete(File); return; }
    if (count.enabled && g_cpu.R[15] == archive_resume) { complete(Archive); return; }
    if (!count.enabled || !nds_performance_execution() || g_runtime_deep_trace) {
        fallback(); return;
    }
    switch (g_cpu.R[15]) {
    case file_entry: begin(File); break;
    case archive_entry: begin(Archive); break;
    case file_resume: complete(File); break;
    case archive_resume: complete(Archive); break;
    default: fallback(); break;
    }
}
} // namespace mph_assets
