#include "offline_arm7_card.h"
#include "io.h"
#include "state.h"
#include <array>

namespace {
uint32_t game = 0;
struct State {
    uint32_t command = 0, ordinal = 0, value = 0;
    bool completed = false, terminated = false, stopped_devices = false;
} state;
constexpr uint32_t payload_mask = 0x03ffffffu;
constexpr size_t snapshot_words = 8;

std::array<uint32_t, snapshot_words> words() {
    return {1u, game, state.command, state.ordinal, state.value,
        state.completed ? 1u : 0u, state.terminated ? 1u : 0u,
        state.stopped_devices ? 1u : 0u};
}
std::array<uint32_t, snapshot_words> decode(const std::vector<uint8_t>& in) {
    std::array<uint32_t, snapshot_words> out{};
    for (size_t i = 0; i < out.size(); ++i)
        for (unsigned byte = 0; byte < 4; ++byte)
            out[i] |= uint32_t{in[i*4+byte]} << (byte*8);
    return out;
}
}

bool nds_offline_card_configure(const std::string& sha1) {
    game = sha1 == "691e00d9a5dd80b04f80cc7559503e8b06848785" ? 1u :
        sha1 == "90164d1ac127ee5f9815ea4ae7de798c7b5fc629" ? 2u : 0u;
    nds_offline_card_reset();
    return game != 0;
}
void nds_offline_card_reset() { state = {}; }
bool nds_offline_card_ready() {
    if (!game || state.terminated) return false;
    // Bootstrap must finish any ARM7-originated filesystem transaction before
    // its thread/register representation can be replaced by native ownership.
    const uint32_t context = game == 1 ? 0x038090c0u : 0x03809ce0u;
    return !(bus_device_read32(7, context+0xfcu) & 4u) &&
        bus_device_read32(7, context+8u) == 0;
}
bool nds_offline_card_preflight(uint32_t tag, uint32_t payload, bool) {
    return game && !state.terminated && payload <= payload_mask &&
        (tag == 11 || tag == 13 || tag == 14);
}
bool nds_offline_card_receive(uint32_t tag, uint32_t payload, bool error, uint64_t now) {
    if (!nds_offline_card_preflight(tag, payload, error)) return false;
    if (tag == 11) {
        // The retail callback ignores normal requests. Error responses form
        // either a two-word result (opcode0, value), or a completed opcode2..12.
        // Host services own completion directly; there is no guest thread wake.
        if (!error) return true;
        if (!state.ordinal) { state.command = payload; state.completed = false; }
        if (state.command == 0 && state.ordinal == 1) {
            state.value = payload;
            state.completed = true;
        } else if (state.command >= 2 && state.command <= 12) {
            state.completed = true;
        }
        if (state.completed) state.ordinal = 0;
        else ++state.ordinal;
        return true;
    }
    // Tags13/14 signal cartridge removal, not backup-save commands. Correct
    // notifications stop sound/WiFi before OS termination; malformed ones reach
    // OS termination directly. Neither callback sends an acknowledgement.
    const bool cleanup = (payload & 63u) == (tag == 13 ? 2u : 1u);
    if (cleanup) nds_io_offline_card_stop(now);
    state.stopped_devices = cleanup;
    state.terminated = true;
    return true;
}
bool nds_offline_card_terminated() { return state.terminated; }
bool nds_offline_card_snapshot(std::vector<uint8_t>* out) {
    if (!out) return false;
    out->clear();
    for (uint32_t word : words())
        for (unsigned byte = 0; byte < 4; ++byte)
            out->push_back(static_cast<uint8_t>(word >> (byte*8)));
    return true;
}
bool nds_offline_card_validate(const std::vector<uint8_t>& in, std::string* error) {
    if (in.size() == snapshot_words*4) {
        const auto w = decode(in);
        if (w[0] == 1 && w[1] == game && w[2] <= payload_mask &&
            w[4] <= payload_mask && w[5] <= 1 && w[6] <= 1 && w[7] <= 1 &&
            (!w[7] || w[6]) && (!w[5] || w[3] == 0)) return true;
    }
    if (error) *error = "invalid native cartridge-service state";
    return false;
}
bool nds_offline_card_import(const std::vector<uint8_t>& in, std::string* error) {
    if (!nds_offline_card_validate(in, error)) return false;
    const auto w = decode(in);
    state = {w[2], w[3], w[4], w[5] != 0, w[6] != 0, w[7] != 0};
    return true;
}
