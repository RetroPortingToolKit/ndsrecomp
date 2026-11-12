#include "spu.h"
#include "savestate.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

// Real SPU implementation; only the ARM7 sample/capture bus is synthetic.
uint32_t bus_device_read32(int, uint32_t address) {
    return (address * 0x9e3779b1u) ^ (address >> 7);
}
void bus_device_write32(int, uint32_t, uint32_t) {}

namespace {
bool ok = true;
uint64_t cycles = 0;
void expect(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ok = false; }
}
uint32_t reg(unsigned channel, unsigned offset) {
    return 0x04000400u + channel * 16u + offset;
}
void write(uint32_t address, uint32_t value, unsigned width = 4) {
    nds_spu_write(address, value, width);
}
NdsSpuSaveState snapshot() {
    NdsSpuSaveState state{};
    expect(spu_savestate_export(&state), "export SPU");
    return state;
}
void accepted(const char* message) {
    std::string error;
    const auto state = snapshot();
    expect(spu_savestate_validate(state, &error), message);
    expect(spu_savestate_import(state, &error), "import reachable SPU state");
}
void reset() {
    nds_spu_reset(); cycles = 0;
    write(0x04000500u, 0x807fu, 2);
    write(0x04000504u, 0x200u, 2);
}
void advance(unsigned samples) {
    for (unsigned i = 0; i < samples; ++i) {
        cycles += 1024;
        nds_tick_spu(cycles);
    }
}
void setup(unsigned channel, unsigned format, unsigned repeat, unsigned words) {
    write(reg(channel, 4), 0x02100000u);
    write(reg(channel, 8), 0xfe00u, 2);
    write(reg(channel, 10), 0, 2);
    write(reg(channel, 12), words);
    write(reg(channel, 0), (format << 29) | (repeat << 27) | 0x0040007fu);
}
void start(unsigned channel) {
    write(reg(channel, 3), nds_spu_read(reg(channel, 3), 1) | 0x80u, 1);
}
void stop(unsigned channel) {
    write(reg(channel, 3), nds_spu_read(reg(channel, 3), 1) & 0x7fu, 1);
}
std::vector<int16_t> continuation() {
    std::array<int16_t, 16384> audio{};
    nds_spu_read_output(audio.data(), 8192); // discard previous timeline samples
    advance(64);
    const auto frames = nds_spu_read_output(audio.data(), 8192);
    expect(frames == 64, "continuation produces 64 stereo frames");
    return {audio.begin(), audio.begin() + frames * 2};
}
void roundtrip_continuation() {
    const auto state = snapshot();
    const auto saved_cycles = cycles;
    const auto reference = continuation();
    std::string error;
    if (!spu_savestate_import(state, &error)) {
        expect(false, "restore before continuation"); return;
    }
    cycles = saved_cycles;
    expect(reference == continuation(), "restored continuation has identical audio");
}
void retrigger() {
    reset(); setup(0, 0, 2, 64); start(0); advance(400);
    expect(!(snapshot().channel[0].cnt & 0x80000000u), "first note finished");
    write(reg(0, 12), 16);
    expect(snapshot().channel[0].source_offset > 64, "old cursor exceeds new note");
    accepted("stopped voice with shorter next note");
    setup(0, 0, 2, 16); start(0);
    expect(snapshot().channel[0].key_on == 1, "new note waits for mixer");
    accepted("key-on pending voice with stale cursor");
    roundtrip_continuation();

    reset(); setup(1, 0, 1, 200); start(1); advance(37); stop(1);
    expect(snapshot().channel[1].fifo_read & 1u, "PCM8 voice has an odd read cursor");
    setup(1, 1, 1, 4000);
    accepted("stopped voice changes to PCM16 before cursors reset");
    start(1); accepted("PCM16 key-on pending with old PCM8 cursor");
    roundtrip_continuation();
}
void captures() {
    reset(); write(reg(1, 8), 0xfe00, 2);
    write(0x04000510u, 0x02200000u); write(0x04000514u, 8, 2);
    write(0x04000508u, 0x85, 1); advance(64);
    expect(snapshot().capture[0].cnt == 5, "finished one-shot retains bit zero");
    accepted("completed one-shot capture");

    reset(); write(reg(1, 8), 0xfe00, 2);
    write(0x04000510u, 0x02200000u); write(0x04000514u, 64, 2);
    write(0x04000508u, 0x80, 1); advance(50);
    write(0x04000508u, 0, 1); write(0x04000514u, 4, 2);
    expect(snapshot().capture[0].write_offset >= 16, "capture retains old write offset");
    accepted("stopped capture given shorter length");
    write(0x04000508u, 0x80, 1); accepted("capture restart resets offset");

    reset(); write(reg(1, 8), 0xffff, 2);
    write(0x04000514u, 1, 2); write(0x04000508u, 0x84, 1); advance(1);
    expect(!(snapshot().capture[0].cnt & 0x80u) &&
           snapshot().capture[0].timer > 0xffffu, "one-shot ends with pending timer ticks");
    accepted("stopped one-shot preserves pending timer ticks");
}
void stopped_manual_voice() {
    reset(); setup(2, 0, 0, 16); start(2); advance(100);
    expect(snapshot().channel[2].fifo_level > 32, "manual voice drains past source end");
    stop(2); accepted("stopped manual voice retains exhausted FIFO level");
    start(2); accepted("key-on pending resets exhausted FIFO before use");
    advance(1); accepted("restarted manual voice has rebuilt cursors");
}
void malformed_states() {
    reset(); setup(0, 0, 1, 64); start(0); advance(20);
    const auto state = snapshot();
    auto rejected = [](const NdsSpuSaveState& bad) {
        std::string error;
        expect(!spu_savestate_validate(bad, &error) && !error.empty(), "reject malformed SPU");
        const auto before = snapshot();
        expect(!spu_savestate_import(bad, &error), "reject import before mutation");
        const auto after = snapshot();
        expect(before.channel[0].source_offset == after.channel[0].source_offset &&
               before.channel[0].cnt == after.channel[0].cnt, "failed import preserves voice");
    };
    auto bad = state; bad.channel[0].source_offset = 260; rejected(bad);
    bad = state; bad.channel[0].fifo_level += 32; rejected(bad);
    bad = state; bad.channel[0].fifo_read = 32; rejected(bad);
    bad = state; bad.channel[0].key_on = 2; rejected(bad);
    bad = state; bad.channel[0].adpcm_index = 89; rejected(bad);
    bad = state; bad.capture[0].fifo_read = 16; rejected(bad);
    bad = state; bad.capture[0].cnt = 0x80; bad.capture[0].write_offset = 4; rejected(bad);
}
} // namespace

int main() {
    retrigger(); captures(); stopped_manual_voice(); malformed_states();
    if (ok) std::puts("reachable SPU states and restored audio pass");
    return ok ? 0 : 1;
}
