#include "savestate_slots.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <utility>
#include <sstream>
#include <system_error>

#include "dispatch_timing.h"
#include "diagnostics.h"
#include "coverage_manifest.h"
#include "emu_profile.h"
#include "gpu2d.h"
#include "gpu3d.h"
#include "io.h"
#include "net/net_ring.h"
#include "scheduler.h"
#include "state.h"
#include "tier3.h"

namespace {

struct PendingSaveSlot {
    NdsSavestateSlotCommand command;
};
// Frontend/scheduler-owner only; the worker has no reference to this state.
std::optional<PendingSaveSlot> g_pending_save_slot;

std::string slot_name(unsigned slot) {
    std::ostringstream out;
    out << "slot" << std::setw(2) << std::setfill('0') << slot << ".nss";
    return out.str();
}

const char* action_verb(NdsSavestateSlotAction action) {
    return action == NdsSavestateSlotAction::Save ? "save" : "load";
}

std::string result_prefix(NdsSavestateSlotAction action, unsigned slot) {
    std::ostringstream out;
    out << (action == NdsSavestateSlotAction::Save ? "Saved" : "Loaded")
        << " state slot " << slot;
    return out.str();
}

}  // namespace

bool nds_savestate_slot_shortcut(unsigned function_key, bool shift,
                                 bool repeat,
                                 NdsSavestateSlotCommand* command) {
    if (function_key < 1u || function_key > NDS_SAVESTATE_SLOT_COUNT)
        return false;
    if (!command) return true;
    *command = {};
    if (repeat) return true;
    command->slot = function_key;
    command->action = shift ? NdsSavestateSlotAction::Save
                            : NdsSavestateSlotAction::Load;
    return true;
}

std::string nds_savestate_slot_path(const std::string& directory,
                                    unsigned slot,
                                    std::string* error) {
    if (directory.empty()) {
        if (error) *error = "savestate directory is not configured";
        return {};
    }
    if (slot < 1u || slot > NDS_SAVESTATE_SLOT_COUNT) {
        if (error) *error = "savestate slot must be between 1 and 12";
        return {};
    }
    const std::filesystem::path root(directory);
    if (!root.has_filename() && root.empty()) {
        if (error) *error = "savestate directory is invalid";
        return {};
    }
    return (root / slot_name(slot)).string();
}

NdsSavestateSlotMetadata nds_savestate_slot_metadata(
    const std::string& directory, unsigned slot) {
    NdsSavestateSlotMetadata out{};
    std::string ignored;
    const std::string path = nds_savestate_slot_path(directory, slot, &ignored);
    if (path.empty()) return out;
    std::error_code ec;
    out.exists = std::filesystem::is_regular_file(path, ec) && !ec;
    if (!out.exists) return out;
    out.size_bytes = std::filesystem::file_size(path, ec);
    if (ec) out.size_bytes = 0;
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        const auto system_time = std::chrono::time_point_cast<
            std::chrono::system_clock::duration>(
                modified - std::filesystem::file_time_type::clock::now() +
                std::chrono::system_clock::now());
        out.modified_unix_seconds =
            std::chrono::duration_cast<std::chrono::seconds>(
                system_time.time_since_epoch()).count();
    }
    return out;
}

NdsSavestateSlotResult nds_savestate_slot_request_save(
    const std::string& directory, const NdsSavestateIdentity& identity,
    const NdsSavestateSlotCommand& command) {
    NdsSavestateSlotResult result{};
    result.command = command;
    std::string error;
    const std::string path = nds_savestate_slot_path(directory, command.slot, &error);
    if (command.action != NdsSavestateSlotAction::Save) error = "not a save request";
    if (g_pending_save_slot) error = "a save is already pending; try again after it finishes";
    if (!error.empty()) {
        result.message = "Cannot save state slot " + std::to_string(command.slot) + ": " + error;
        return result;
    }
    PendingSaveSlot pending{command};
    result.message = "Saving state slot " + std::to_string(command.slot);
    if (!nds_savestate_save_core_async(path, identity, &error)) {
        result.message = "Cannot save state slot " + std::to_string(command.slot) + ": " + error;
        return result;
    }
    g_pending_save_slot = std::move(pending);
    result.success = result.pending = true;
    return result;
}

bool nds_savestate_slot_poll_save(NdsSavestateSlotResult* result) {
    if (!result || !g_pending_save_slot) return false;
    NdsSavestateAsyncSaveResult completed{};
    if (!nds_savestate_poll_save_core(&completed)) return false;
    NdsSavestateSlotResult value{};
    value.command = g_pending_save_slot->command;
    value.success = completed.success;
    value.metadata = {completed.success, completed.size_bytes,
                      completed.modified_unix_seconds};
    value.message = completed.success ? result_prefix(value.command.action, value.command.slot) :
        "Cannot save state slot " + std::to_string(value.command.slot) + ": " + completed.error;
    g_pending_save_slot.reset();
    *result = std::move(value);
    return true;
}

NdsSavestateSlotResult nds_savestate_slot_execute(
    const std::string& directory, const NdsSavestateIdentity& identity,
    const NdsSavestateSlotCommand& command) {
    NdsSavestateSlotResult result{};
    result.command = command;
    std::string error;
    const std::string path =
        nds_savestate_slot_path(directory, command.slot, &error);
    if (path.empty()) {
        result.message = "Cannot " + std::string(action_verb(command.action)) +
            " state slot " + std::to_string(command.slot) + ": " + error;
        return result;
    }

    if (!nds_savestate_check_eligibility(&error)) {
        result.message = "Cannot " + std::string(action_verb(command.action)) +
            " state slot " + std::to_string(command.slot) + ": " + error;
        return result;
    }

    if (command.action == NdsSavestateSlotAction::Load) {
        result.metadata = nds_savestate_slot_metadata(directory, command.slot);
        if (!result.metadata.exists) {
            result.message = "State slot " + std::to_string(command.slot) +
                " is empty";
            return result;
        }
        result.success = nds_savestate_load_core(path, identity, &error);
        if (result.success) {
            nds_savestate_reset_host_history();
            result.message = result_prefix(command.action, command.slot);
        }
    } else {
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec) {
            error = "could not create savestate directory: " + ec.message();
        } else {
            result.success = nds_savestate_save_core(path, identity, &error);
        }
        if (result.success) {
            result.metadata =
                nds_savestate_slot_metadata(directory, command.slot);
            result.message = result_prefix(command.action, command.slot);
        }
    }

    if (!result.success && result.message.empty()) {
        result.message = "Cannot " + std::string(action_verb(command.action)) +
            " state slot " + std::to_string(command.slot) + ": " + error;
    }
    return result;
}

void nds_savestate_reset_host_history() {
    runtime_savestate_reset_host_history();
    bus_debug_history_reset();
    nds_io_debug_history_reset();
    net_ring_reset();
    scheduler_profile_reset();
    nds_emu_profile_reset();
    nds_gpu2d_profile_reset();
    nds_gpu3d_debug_history_reset();
    nds_dispatch_timing_reset();
    tier3_reset();
    coverage_pages_reset();
    nds_diagnostics_reset_performance_history();
}
