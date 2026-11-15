// SPDX-License-Identifier: MIT
#include "emu_profile.h"
#include "offline_arm7_sound.h"
#include "offline_sound_state.h"
#include "savestate.h"
#include "spu.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
uint64_t g_busf_mapping_generation = 1;
}
namespace nds_emu_detail {
NdsEmuProfile g_profile{};
bool g_sampling = false, g_enabled = false;
uint8_t g_cur = NDS_EMU_NONE;
uint64_t g_last = 0;
} // namespace nds_emu_detail
namespace nds_dispatch_timing_detail {
uint64_t g_overhead_ticks = 0;
}
namespace {
std::array<uint8_t, 4 * 1024 * 1024> ram{};
std::array<uint8_t, 96 * 1024> wram{};
std::vector<uint32_t> replies;
unsigned checks = 0;
void check(bool b) {
  ++checks;
  if (!b) {
    std::fprintf(stderr, "check %u failed\n", checks);
    std::abort();
  }
}
uint8_t *ptr(uint32_t a, uint32_t n) {
  if (a >= 0x02000000 && a < 0x03000000) {
    unsigned off = a & 0x3FFFFF;
    return n <= ram.size() - off ? ram.data() + off : nullptr;
  }
  if (a >= 0x037F8000 && a < 0x03810000) {
    unsigned off = a - 0x037F8000;
    return n <= wram.size() - off ? wram.data() + off : nullptr;
  }
  return nullptr;
}
template <class T> void put(uint32_t a, T x) {
  auto p = ptr(a, sizeof x);
  assert(p);
  std::memcpy(p, &x, sizeof x);
}
template <class T> T get(uint32_t a) {
  T x{};
  auto p = ptr(a, sizeof x);
  assert(p);
  std::memcpy(&x, p, sizeof x);
  return x;
}
using Command = std::array<uint32_t, 6>;
uint64_t now = 0;
void commands(std::initializer_list<Command> input) {
  unsigned i = 0;
  for (auto q : input) {
    q[0] = ++i < input.size() ? 0x02001000 + i * 24 : 0;
    std::memcpy(ptr(0x02001000 + (i - 1) * 24, 24), q.data(), 24);
  }
  check(nds_offline_sound_receive(0x02001000, false, now));
  check(nds_offline_sound_receive(0, false, now));
}
void boot(bool mph = false, bool adopt = false) {
  ram.fill(0);
  wram.fill(0);
  replies.clear();
  now = 0;
  nds_spu_reset();
  nds_spu_write(0x04000500, 0x807F, 2);
  nds_spu_write(0x04000504, 512, 2);
  uint32_t base = mph ? 0x03808AEC : 0x03807ECC;
  nds_offline_sound::Work work;
  for (unsigned i = 0; i < 16; ++i) {
    auto a = work.voice(i);
    a.byte(nds_offline_sound::voice::Id, i);
    auto b = work.player(i);
    b.byte(nds_offline_sound::player::Id, i);
    std::memset(b.bytes + nds_offline_sound::player::Tracks, 255, 16);
  }
  if (adopt) {
    auto alarm = work.alarm(0);
    alarm.byte(0, 1);
    alarm.byte(1, 5);
    alarm.set<uint64_t>(12, 20);
    alarm.set<uint32_t>(0x14, mph ? 0x03802784 : 0x037FF3D0);
    alarm.set<uint32_t>(0x18, 5 << 8);
    alarm.set<uint64_t>(0x20, 100);
    nds_spu_write(0x04000404, 0x023FFFF0, 4);
    nds_spu_write(0x04000408, 0xFE00, 2);
    nds_spu_write(0x0400040A, 2, 2); // 8-byte loop prefix
    nds_spu_write(0x0400040C, 8,
                  4); // 32-byte sample, extent wraps physical RAM
    nds_spu_write(0x04000400, 0x8840007F, 4);
  }
  std::memcpy(ptr(base, work.bytes.size()), work.bytes.data(),
              work.bytes.size());
  put<int32_t>(mph ? 0x038081E0 : 0x0380759C, -1);
  check(nds_offline_sound_configure(
      mph ? "90164d1ac127ee5f9815ea4ae7de798c7b5fc629"
          : "691e00d9a5dd80b04f80cc7559503e8b06848785"));
  std::vector<uint8_t> before, after;
  check(nds_offline_sound_snapshot(&before));
  put<uint8_t>(base + 15 * nds_offline_sound::VoiceSize, 0);
  check(!nds_offline_sound_takeover(0, 174592));
  check(nds_offline_sound_ready());
  check(std::strcmp(nds_offline_sound_takeover_status(),
                    "voice initialization incomplete") == 0);
  check(nds_offline_sound_snapshot(&after));
  check(before == after);
  put<uint8_t>(base + 15 * nds_offline_sound::VoiceSize, 15);
  check(nds_offline_sound_takeover(0, 174592));
  check(nds_offline_sound_ready());
}
std::vector<int16_t> pcm(uint64_t first, unsigned count) {
  std::vector<int16_t> x(count * 2);
  check(nds_spu_debug_copy_output(first, x.data(), count) == count);
  return x;
}
} // namespace
bool nds_io_offline_memory(uint32_t a, uint8_t *b, uint32_t n, bool write,
                           uint64_t) {
  auto p = ptr(a, n);
  if (!p)
    return false;
  if (write)
    std::memcpy(p, b, n);
  else
    std::memcpy(b, p, n);
  return true;
}
bool nds_io_offline_reply_capacity(uint32_t) { return true; }
bool nds_io_offline_reply(uint32_t x, uint64_t) {
  replies.push_back(x);
  return true;
}
bool nds_io_offline_tick_deadline(uint64_t ticks, uint64_t now,
                                  uint64_t *deadline) {
  if (ticks > UINT64_MAX / 64)
    return false;
  *deadline = std::max(now, ticks * 64);
  return true;
}
bool bus_device_resolve_ram(int cpu, uint32_t a, uint32_t n,
                            const uint8_t **out) {
  assert(cpu == 7);
  *out = ptr(a, n);
  return *out != nullptr;
}
uint32_t bus_device_read32(int cpu, uint32_t a) {
  assert(cpu == 7);
  auto p = ptr(a, 4);
  uint32_t x = 0;
  if (p)
    std::memcpy(&x, p, 4);
  return x;
}
void bus_device_write32(int cpu, uint32_t a, uint32_t x, uint64_t) {
  assert(cpu == 7);
  put(a, x);
}
void bus_device_write16(int cpu, uint32_t a, uint16_t x, uint64_t) {
  assert(cpu == 7);
  put(a, x);
}

int main() {
  for (bool mph : {false, true}) {
    boot(mph, true);
    check(nds_offline_sound_source_overlap(ptr(0x023FFFF0, 16), 16));
    check(nds_offline_sound_source_overlap(ptr(0x02000000, 24), 24));
    check(!nds_offline_sound_source_overlap(ptr(0x02000018, 1), 1));
    check(nds_offline_sound_next_event(0) == 6400);
    nds_offline_sound_advance(6400);
    check(replies.size() == 1 && replies.back() == (7u | ((5u << 8) << 6)));
    check(nds_offline_sound_next_event(6400) == 7680);
    boot(mph);
    Command volume = {0, 23, 127, 0, 0, 0};
    std::memcpy(ptr(0x02001000, 24), volume.data(), 24);
    for (unsigned i = 0; i < 8; ++i)
      check(nds_offline_sound_receive(0x02001000, false, 0));
    check(nds_offline_sound_preflight(0x02001000, false));
    check(!nds_offline_sound_can_receive(0x02001000, false));
    std::vector<uint8_t> queued_before, queued_after;
    check(nds_offline_sound_snapshot(&queued_before));
    check(!nds_offline_sound_receive(0x02001000, false, 0));
    check(nds_offline_sound_snapshot(&queued_after));
    check(queued_before == queued_after);
    check(nds_offline_sound_can_receive(0, false));
    check(nds_offline_sound_receive(0, false, 0));
    check(nds_offline_sound_can_receive(0x02001000, false));
    for (unsigned i = 0; i < 64; ++i)
      put<int8_t>(0x02004000 + i, int(i * 7) - 100);
    commands({{0, 29, 0x02006000, 0, 0, 0},
              {0, 14, 0, 0x02004000, (127u << 24) | 16,
               (1u << 26) | (64u << 16) | 512},
              {0, 12, 1, 0, 0, 0}});
    check(nds_spu_read(0x04000400, 4) == 0x8840007Fu);
    check(nds_offline_sound_source_overlap(ptr(0x02404000, 1), 1));
    check(!nds_offline_sound_source_overlap(ptr(0x02008000, 1), 1));
    now = 32768;
    nds_offline_sound_advance(now);
    auto produced = pcm(0, 32);
    check(std::any_of(produced.begin(), produced.end(),
                      [](int16_t x) { return x != 0; }));
    NdsSpuSaveState hardware;
    check(spu_savestate_export(&hardware));
    std::vector<uint8_t> state;
    check(nds_offline_sound_snapshot(&state));
    std::string error;
    check(nds_offline_sound_validate(state, &error));
    now += 16384;
    nds_offline_sound_before_write(ptr(0x02004000, 64), 64, now);
    std::fill_n(ptr(0x02004000, 64), 64, 30);
    now += 16384;
    nds_offline_sound_advance(now);
    auto expected = pcm(32, 32);
    check(spu_savestate_import(hardware, &error));
    check(nds_offline_sound_import(state, &error));
    for (unsigned i = 0; i < 64; ++i)
      put<int8_t>(0x02004000 + i, int(i * 7) - 100);
    nds_offline_sound_before_write(ptr(0x02804000, 64), 64, 49152);
    std::fill_n(ptr(0x02004000, 64), 64, 30);
    nds_offline_sound_advance(65536);
    check(pcm(32, 32) == expected);
    check(get<uint32_t>(0x02006000) ==
          1); // list completion, independent of command count
    commands({{0, 18, 0, 10, 20, 3}, {0, 12, 0, 0, 1, 0}});
    now += 640;
    nds_offline_sound_advance(now);
    check(!replies.empty() && replies.back() == (7u | ((3u << 8) << 6)));
    std::vector<uint8_t> before, after;
    check(nds_offline_sound_snapshot(&before));
    put<uint32_t>(0x02001004, 34);
    check(!nds_offline_sound_preflight(0x02001000, false));
    check(nds_offline_sound_snapshot(&after));
    check(before == after);
  }
  boot();
  // Full SSEQ->bank->PSG voice path, then exact caller-visible packed writes.
  put<uint32_t>(0x02008038, 1);
  put<uint32_t>(0x0200803C, (0x40u << 8) | 2);
  const uint8_t instrument[] = {4, 0, 0, 0, 60, 127, 127, 127, 127, 64};
  std::memcpy(ptr(0x02008040, 10), instrument, 10);
  const uint8_t seq[] = {0xB0, 0, 7, 0, 0xC0, 96, 60, 127, 4, 0x80, 4, 0xFF};
  std::memcpy(ptr(0x02009000, sizeof seq), seq, sizeof seq);
  commands(
      {{0, 29, 0x02006000, 0, 0, 0}, {0, 0, 0, 0x02009000, 0, 0x02008000}});
  now = 174592;
  nds_offline_sound_advance(now);
  check(get<int16_t>(0x02006020) == 7);
  now += 174592;
  nds_offline_sound_advance(now);
  check((nds_spu_read(0x04000480, 4) & 0xE0000000u) == 0xE0000000u);
  commands({{0, 6, 0, 6, 0xFFAB, 2},
            {0, 7, (2u << 24), 1, 12, 0x1234},
            {0, 33, 0x0200A000, 0, 0, 0}});
  using namespace nds_offline_sound;
  check(get<int16_t>(0x0200A000 + PlayerOffset + player::ExtraVolume) ==
        int16_t(0xFFAB));
  check(get<int16_t>(0x0200A000 + TrackOffset + track::ExtraPitch) == 0x1234);
  check(get<uint32_t>(0x0200A000 + 0x11C0) == 0x03807ECC);
  check(nds_offline_sound_ready());
  std::printf(
      "offline sound: %u checks passed; real SPU PCM, both wire profiles, "
      "native SSEQ, packed ABI, alarm, ordering and current-state replay\n",
      checks);
}
