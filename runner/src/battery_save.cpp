#include "battery_save.h"
#include <recomp_persistence/atomic_file.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

void set_error(std::string* error, const std::string& message) {
    if (error) *error = message;
}

}  // namespace

NdsBatterySaveLoadResult nds_battery_save_load_exact(
    const std::string& path, uint8_t* data, size_t size, std::string* error) {
    if (error) error->clear();
    if (path.empty() || !data || size == 0u) {
        set_error(error, "invalid battery-save load request");
        return NdsBatterySaveLoadResult::Error;
    }

    std::error_code ec;
    const std::filesystem::path save_path(path);
    if (!std::filesystem::exists(save_path, ec)) {
        if (ec) {
            set_error(error, "cannot inspect save file: " + ec.message());
            return NdsBatterySaveLoadResult::Error;
        }
        return NdsBatterySaveLoadResult::Missing;
    }

    const uintmax_t file_size = std::filesystem::file_size(save_path, ec);
    if (ec) {
        set_error(error, "cannot determine save size: " + ec.message());
        return NdsBatterySaveLoadResult::Error;
    }
    if (file_size != size) {
        set_error(error, "expected " + std::to_string(size) +
                         " bytes, found " + std::to_string(file_size));
        return NdsBatterySaveLoadResult::InvalidSize;
    }

    std::ifstream input(save_path, std::ios::binary);
    if (!input ||
        !input.read(reinterpret_cast<char*>(data),
                    static_cast<std::streamsize>(size))) {
        set_error(error, "cannot read battery save");
        return NdsBatterySaveLoadResult::Error;
    }
    return NdsBatterySaveLoadResult::Loaded;
}

bool nds_battery_save_write_atomic(
    const std::string& path, const uint8_t* data, size_t size,
    std::string* error) {
    if (error) error->clear();
    if (path.empty() || !data || size == 0u) {
        set_error(error, "invalid battery-save write request");
        return false;
    }

    const std::filesystem::path save_path(path);
    const std::filesystem::path parent = save_path.parent_path();
    std::error_code ec;
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            set_error(error, "cannot create save directory: " + ec.message());
            return false;
        }
    }

    if (!recomp_atomic_file_store(path.c_str(), data, size)) {
        set_error(error, "cannot atomically publish battery save");
        return false;
    }
    return true;
}
