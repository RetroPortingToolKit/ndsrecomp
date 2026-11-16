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
std::vector<uint64_t> reply_times;
bool reply_room = true;
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
void boot(bool mph = false, bool adopt = false, bool takeover = true) {
  ram.fill(0);
  wram.fill(0);
  replies.clear();
  reply_times.clear();
  reply_room = true;
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
  const uint32_t queue = base + nds_offline_sound::WorkSize;
  put<uint32_t>(queue + 0x10, queue + 0x20);
  put<uint32_t>(queue + 0x14, 8);
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
  if (!takeover)
    return;
  check(nds_offline_sound_takeover(0, 174592));
  check(nds_offline_sound_ready());
}
std::vector<int16_t> pcm(uint64_t first, unsigned count) {
  std::vector<int16_t> x(count * 2);
  check(nds_spu_debug_copy_output(first, x.data(), count) == count);
  return x;
}
void pending_queue_tests(bool mph) {
  const uint32_t queue = (mph ? 0x03808AEC : 0x03807ECC) +
                         nds_offline_sound::WorkSize;
  boot(mph, false, false);
  put(0x02002000, Command{0, 29, 0x02006000, 0, 0, 0});
  put<uint32_t>(queue + 0x1C, 1);
  put<uint32_t>(queue + 0x20, 0x02002000);
  const std::vector<uint8_t> original_wram(wram.begin(), wram.end());
  check(nds_offline_sound_takeover(0, 174592));
  check(nds_offline_sound_json().find("\"queued\":1") != std::string::npos);
  check(std::equal(original_wram.begin(), original_wram.end(), wram.begin()));
  check(get<uint32_t>(0x02006000) == 0);
  std::vector<uint8_t> pending, unchanged;
  check(nds_offline_sound_snapshot(&pending));
  check(nds_offline_sound_takeover(0, 174592));
  check(nds_offline_sound_snapshot(&unchanged));
  check(unchanged == pending);
  NdsSpuSaveState hardware;
  check(spu_savestate_export(&hardware));
  const std::vector<uint8_t> original_ram(ram.begin(), ram.end());
  check(nds_offline_sound_receive(0, false, 0));
  check(get<uint32_t>(0x02006000) == 1);
  check(nds_offline_sound_takeover(0, 174592));
  check(nds_offline_sound_receive(0, false, 0));
  check(get<uint32_t>(0x02006000) == 1);
  std::copy(original_ram.begin(), original_ram.end(), ram.begin());
  std::copy(original_wram.begin(), original_wram.end(), wram.begin());
  std::string error;
  check(spu_savestate_import(hardware, &error));
  check(nds_offline_sound_validate(pending, &error));
  check(nds_offline_sound_import(pending, &error));
  check(nds_offline_sound_takeover(0, 174592));
  check(nds_offline_sound_receive(0, false, 0));
  check(get<uint32_t>(0x02006000) == 1);
  check(nds_offline_sound_receive(0, false, 0));
  check(get<uint32_t>(0x02006000) == 1);
  check(get<uint32_t>(queue + 0x1C) == 1); // obsolete SDK header stays frozen

  // A full wrapped queue must keep all eight submissions and their order.
  boot(mph, false, false);
  put<uint32_t>(queue + 0x18, 5);
  put<uint32_t>(queue + 0x1C, 8);
  for (unsigned i = 0; i < 8; ++i) {
    const uint32_t head = 0x02002000 + i * 24;
    put(head, i ? Command{0, 23, 10 + i, 0, 0, 0}
                : Command{0, 29, 0x02006000, 0, 0, 0});
    put<uint32_t>(queue + 0x20 + ((5 + i) & 7) * 4, head);
  }
  check(nds_offline_sound_takeover(0, 174592));
  check(!nds_offline_sound_can_receive(0x02003000, false));
  check(nds_offline_sound_can_receive(0, false));
  check(nds_offline_sound_receive(0, false, 0));
  check(get<uint32_t>(0x02006000) == 8);
  check(nds_spu_read(0x04000500, 1) == 17);
  check(nds_offline_sound_takeover(0, 174592));
  check(nds_offline_sound_receive(0, false, 0));
  check(get<uint32_t>(0x02006000) == 8);
  check(nds_offline_sound_can_receive(0x02003000, false));

  for (unsigned invalid = 0; invalid < 6; ++invalid) {
    boot(mph, false, false);
    switch (invalid) {
    case 0: put<uint32_t>(queue + 0x10, queue + 0x24); break;
    case 1: put<uint32_t>(queue + 0x14, 7); break;
    case 2: put<uint32_t>(queue + 0x18, 8); break;
    case 3: put<uint32_t>(queue + 0x1C, 9); break;
    case 4: put<uint32_t>(queue + 0x1C, 1); break;
    case 5:
      put<uint32_t>(queue + 0x1C, 1);
      put<uint32_t>(queue + 0x20, 0x02002000);
      put(0x02002000, Command{0x02002018, 29, 0x02006000, 0, 0, 0});
      put(0x02002018, Command{0, 34, 0, 0, 0, 0});
      break;
    }
    std::vector<uint8_t> before, after;
    check(nds_offline_sound_snapshot(&before));
    const std::vector<uint8_t> ram_before(ram.begin(), ram.end());
    const std::vector<uint8_t> wram_before(wram.begin(), wram.end());
    const auto produced = nds_spu_debug_output_produced();
    check(!nds_offline_sound_takeover(0, 174592));
    check(nds_offline_sound_ready());
    check(nds_offline_sound_snapshot(&after));
    check(before == after);
    check(std::equal(ram_before.begin(), ram_before.end(), ram.begin()));
    check(std::equal(wram_before.begin(), wram_before.end(), wram.begin()));
    check(nds_spu_debug_output_produced() == produced);
  }
}
void inactive_player_tests(bool mph) {
  using namespace nds_offline_sound;
  boot(mph);
  put<uint32_t>(0x02008038, 1);
  put<uint32_t>(0x0200803C, (0x40u << 8) | 2);
  const uint8_t instrument[] = {4, 0, 0, 0, 60, 127, 127, 127, 127, 64};
  std::memcpy(ptr(0x02008040, 10), instrument, 10);
  const uint8_t music[] = {60, 127, 0x81, 0x00, 0xFF};
  const uint8_t ui[] = {0x80, 0x7F, 0xFF};
  std::memcpy(ptr(0x02009000, sizeof music), music, sizeof music);
  std::memcpy(ptr(0x02009100, sizeof ui), ui, sizeof ui);
  commands({{0, 0, 3, 0x02009000, 0, 0x02008000}});
  now = 174592;
  nds_offline_sound_advance(now);
  now += 174592;
  nds_offline_sound_advance(now);
  const uint32_t output = 0x0200A000;
  const auto export_work = [&]() {
    commands({{0, 33, output, 0, 0, 0}});
  };
  export_work();
  const unsigned music_track = get<uint8_t>(output + PlayerOffset + 3 * PlayerSize + player::Tracks);
  check(music_track == 0);
  const uint32_t music_row = output + TrackOffset + music_track * TrackSize;
  const auto position = get<uint32_t>(music_row + track::Position);
  const auto voice = get<uint32_t>(music_row + track::Voices);
  check(position && voice);
  const auto music_survives = [&]() {
    export_work();
    check(get<uint8_t>(music_row + track::Flags) & track::Active);
    check(get<uint32_t>(music_row + track::Position) == position);
    check(get<uint32_t>(music_row + track::Voices) == voice);
  };
  for (unsigned player_id : {8u, 9u}) {
    // Retail bootstrap leaves inactive track bytes zero. Public packed writes
    // recreate that legal state without granting ownership of music track0.
    commands({{0, 6, player_id, player::Tracks, 0, 4},
              {0, 1, player_id, 0, 0, 0}});
    music_survives();
    commands({{0, 0, player_id, 0x02009100, 0, 0x02008000}});
    music_survives();
    const auto ui_track = get<uint8_t>(output + PlayerOffset + player_id * PlayerSize + player::Tracks);
    check(ui_track != music_track && ui_track < TrackCount);
    commands({{0, 1, player_id, 0, 0, 0}, {0, 1, player_id, 0, 0, 0}});
    music_survives();
  }
  now += 174592;
  nds_offline_sound_advance(now);
  NdsSpuSaveState hardware;
  check(spu_savestate_export(&hardware));
  check(hardware.channel[8].cnt & 0x80000000u);
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
bool nds_io_offline_reply_capacity(uint32_t) { return reply_room; }
bool nds_io_offline_reply(uint32_t x, uint64_t at) {
  if (!reply_room)
    return false;
  replies.push_back(x);
  reply_times.push_back(at);
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
  pending_queue_tests(false);
  pending_queue_tests(true);
  inactive_player_tests(false);
  inactive_player_tests(true);
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
              {0, 14, 512u << 16, 0x02004000, (127u << 24) | 16,
               (1u << 26) | (64u << 16)},
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
    check(hardware.channel[0].reload == 65536 - 512);
    check(hardware.channel[0].loop == 0);
    check(hardware.channel[0].length == 16 * 4);
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

    // The streaming command packs period in arg0 high16 and loop-start in
    // arg3 low16. Exercise the stereo PCM16 setup across two full loops,
    // comparing its samples with the same explicit hardware register writes.
    boot(mph);
    for (unsigned i = 0; i < 1024; ++i) {
      put<int16_t>(0x021DA420 + i * 2, int(i) * 24 - 12288);
      put<int16_t>(0x021DAC20 + i * 2, 10000 - int(i) * 19);
    }
    nds_spu_write(0x04000500, 0xB97F, 2);
    commands({{0, 14, 0x02000001, 0x021DA420, 0x7F000200, 0x05000000},
              {0, 14, 0x02000003, 0x021DAC20, 0x7F000200, 0x057F0000},
              {0, 12, 0xA, 0, 0, 0}});
    check(spu_savestate_export(&hardware));
    for (unsigned i : {1u, 3u}) {
      check(hardware.channel[i].reload == 0xFE00);
      check(hardware.channel[i].loop == 0);
      check(hardware.channel[i].length == 2048);
    }
    check(hardware.capture[0].reload == 0xFE00);
    check(hardware.capture[1].reload == 0xFE00);
    now = 2304 * 1024;
    nds_offline_sound_advance(now);
    const auto streamed = pcm(0, 2304);
    auto levels = streamed;
    std::sort(levels.begin(), levels.end());
    check(std::unique(levels.begin(), levels.end()) - levels.begin() > 256);
    nds_spu_reset();
    nds_spu_write(0x04000500, 0xB97F, 2);
    nds_spu_write(0x04000504, 512, 2);
    for (unsigned i : {1u, 3u}) {
      const uint32_t reg = 0x04000400 + i * 16;
      nds_spu_write(reg + 4, i == 1 ? 0x021DA420 : 0x021DAC20, 4);
      nds_spu_write(reg + 8, 0xFE00, 2);
      nds_spu_write(reg + 10, 0, 2);
      nds_spu_write(reg + 12, 512, 4);
      nds_spu_write(reg, i == 1 ? 0xA800007F : 0xA87F007F, 4);
    }
    nds_tick_spu(now);
    check(pcm(0, 2304) == streamed);
  }
  // A full canonical reply FIFO must leave ARM9 free to drain it. Driver
  // service can cross its own deadline while one alarm publication waits;
  // draining publishes one pending callback at current time, never the past.
  boot(false, true);
  reply_room = false;
  nds_offline_sound_advance(174592);
  check(replies.empty());
  check(nds_offline_sound_next_event(174592) > 174592);
  nds_offline_sound_advance(200000);
  check(replies.empty());
  check(nds_offline_sound_next_event(200000) > 200000);
  reply_room = true;
  nds_offline_sound_advance(220000);
  check(replies.size() == 1 && reply_times[0] == 220000);
  check(nds_offline_sound_next_event(220000) == 220160);
  nds_offline_sound_advance(220000);
  check(replies.size() == 1);
  nds_offline_sound_advance(220159);
  check(replies.size() == 1);
  nds_offline_sound_advance(220160);
  check(replies.size() == 2 && reply_times[1] == 220160);
  // PM masks software sound IRQs. Keep hardware output time, then resume one
  // overdue callback at the first future original periodic phase.
  boot(false, true);
  nds_offline_sound_sleep(true, 0);
  check(nds_offline_sound_next_event(0) == UINT64_MAX);
  check(!nds_offline_sound_can_receive(0, false));
  nds_offline_sound_advance(200000);
  check(replies.empty());
  check(nds_spu_debug_output_produced() == 200000 / 1024);
  std::vector<uint8_t> asleep, after_status;
  check(nds_offline_sound_snapshot(&asleep));
  const auto sound_status = nds_offline_sound_json();
  check(sound_status.find("\"sleeping\":true") != std::string::npos);
  check(nds_offline_sound_snapshot(&after_status));
  check(asleep == after_status);
  std::string sleep_error;
  check(nds_offline_sound_import(asleep, &sleep_error));
  check(nds_offline_sound_next_event(200000) == UINT64_MAX);
  nds_offline_sound_sleep(false, 220000);
  check(replies.size() == 1 && reply_times[0] == 220000);
  check(nds_offline_sound_next_event(220000) == 220160);
  check(nds_offline_sound_can_receive(0, false));
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
  std::vector<uint8_t> valid_state, invalid_depth, unchanged;
  check(nds_offline_sound_snapshot(&valid_state));
  commands({{0, 7, (1u << 24), 1, track::Depth, 255}});
  check(nds_offline_sound_snapshot(&invalid_depth));
  std::string validation_error;
  check(!nds_offline_sound_validate(invalid_depth, &validation_error));
  check(nds_offline_sound_import(valid_state, &validation_error));
  check(!nds_offline_sound_import(invalid_depth, &validation_error));
  check(nds_offline_sound_snapshot(&unchanged));
  check(unchanged == valid_state);
  std::printf(
      "offline sound: %u checks passed; real SPU PCM, both wire profiles, "
      "native SSEQ, packed ABI, alarm, ordering and current-state replay\n",
      checks);
}
