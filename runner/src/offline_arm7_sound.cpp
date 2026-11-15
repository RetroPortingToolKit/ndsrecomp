// SPDX-License-Identifier: MIT
// Native sound command/sequence service. Sequencer, ADSR, LFO, pitch and voice
// allocation algorithms adapted from CyberBotX/NCSF (MIT); see the complete
// permission notice in offline_sound_tables.h. The hardware decoder/mixer is
// the existing SPU, not NCSF's sample renderer.
#include "offline_arm7_sound.h"
#include "offline_sound_state.h"
#include "offline_sound_tables.h"
#include "savestate.h"
#include "spu.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <sstream>
#include <type_traits>

// Canonical IO owner supplies bounded physical memory access and FIFO IRQs.
extern bool nds_io_offline_memory(uint32_t, uint8_t *, uint32_t, bool,
                                  uint64_t);
extern bool nds_io_offline_reply_capacity(uint32_t);
extern bool nds_io_offline_reply(uint32_t, uint64_t);
extern bool nds_io_offline_tick_deadline(uint64_t, uint64_t, uint64_t *);
extern bool bus_device_resolve_ram(int, uint32_t, uint32_t, const uint8_t **);
extern "C" uint64_t g_busf_mapping_generation;

namespace {
using namespace nds_offline_sound;
namespace p = nds_offline_sound::player;
namespace t = nds_offline_sound::track;
namespace v = nds_offline_sound::voice;
constexpr uint64_t Never = std::numeric_limits<uint64_t>::max();
constexpr uint64_t DriverPeriod = 64u * 2728u;
constexpr uint8_t AllocationOrder[] = {4, 5, 6,  7,  2,  0,  3,  1,
                                       8, 9, 10, 11, 14, 12, 15, 13};
struct Profile {
  uint32_t work, shared, locks, pan, volume, masterpan, surround, rng, callback;
};
constexpr Profile Mkds{0x03807ECC, 0x03807EC8, 0x03807EA4,
                       0x03807970, 0x03807980, 0x0380759C,
                       0x0380796C, 0x038075A0, 0x037FE1C8};
// Both SDK builds share the ABI; addresses are the relocated read-only profile.
constexpr Profile Mph{0x03808AEC, 0x03808AE8, 0x03808AC4,
                      0x03808590, 0x038085A0, 0x038081E0,
                      0x0380858C, 0x038081E4, 0x0380157C};
struct Sound {
  Work work;
  Profile profile{};
  uint64_t now = 0, next = Never;
  std::array<uint64_t, 8> alarm_deadlines{};
  std::array<uint32_t, 8> queue{};
  std::array<uint32_t, 16> sources{}, lengths{};
  std::array<uint8_t, 16> pans{}, volumes{};
  uint32_t shared = 0, weaklock = 0, stronglock = 0, rng = 0;
  int32_t masterpan = -1, surround = 0;
  uint8_t queued = 0, profile_id = 0;
  uint8_t active = 0, fault = 0, in_advance = 0, sleeping = 0;
} g;
struct SourceSpan {
  const uint8_t *data;
  uint32_t size;
};
std::vector<SourceSpan> source_spans;
uint64_t source_generation = Never;
bool source_dirty = true;
const char *takeover_status = "not attempted";

bool memory(uint32_t a, void *data, uint32_t n, bool write = false) {
  if (!nds_io_offline_memory(a, static_cast<uint8_t *>(data), n, write,
                             g.now)) {
    g.fault = true;
    return false;
  }
  return true;
}
template <class T> T load(uint32_t a) {
  T x{};
  memory(a, &x, sizeof x);
  return x;
}
template <class T> void store(uint32_t a, T x) {
  memory(a, &x, sizeof x, true);
}
bool mainram(uint32_t a, uint32_t n) {
  return a >= 0x02000000u && a < 0x03000000u && n <= 0x03000000u - a;
}
uint32_t voiceptr(unsigned i) { return g.profile.work + i * VoiceSize; }
uint32_t trackptr(unsigned i) {
  return g.profile.work + TrackOffset + i * TrackSize;
}
int voiceindex(uint32_t a) {
  return a >= g.profile.work && a < g.profile.work + PlayerOffset &&
                 (a - g.profile.work) % VoiceSize == 0
             ? int((a - g.profile.work) / VoiceSize)
             : -1;
}
int trackid(unsigned pl, unsigned n) {
  if (pl >= 16 || n >= 16)
    return -1;
  unsigned id = g.work.player(pl).byte(p::Tracks + n);
  return id < 32 ? int(id) : -1;
}
void rawparam(View a, unsigned max, uint32_t offset, uint32_t value,
              uint32_t size) {
  if ((size != 1 && size != 2 && size != 4) || offset > max ||
      size > max - offset) {
    g.fault = true;
    return;
  }
  std::memcpy(a.bytes + offset, &value, size);
}
uint32_t reg(unsigned i) { return 0x04000400u + i * 16; }
void write(uint32_t a, uint32_t x, unsigned size) { nds_spu_write(a, x, size); }
int sustain(unsigned x) { return nds_sound_tables::Sustain[std::min(x, 127u)]; }
uint16_t volume(int x) {
  x = std::clamp(x, -723, 0);
  return nds_sound_tables::Volume[x + 723] | ((x < -240   ? 3
                                               : x < -120 ? 2
                                               : x < -60  ? 1
                                                          : 0)
                                              << 8);
}
uint16_t decay(unsigned x) {
  x &= 127;
  return x == 127   ? 65535
         : x == 126 ? 0x3C00
         : x < 50   ? x * 2 + 1
                    : 0x1E00 / (126 - x);
}
void attack(View a, unsigned x) {
  x = std::min(x, 127u);
  a.byte(v::Attack, x < 109 ? 255 - x : nds_sound_tables::Attack[127 - x]);
}
uint16_t timer(unsigned base, int pitch) {
  int n = -pitch, oct = 0;
  while (n < 0) {
    n += 768;
    --oct;
  }
  while (n >= 768) {
    n -= 768;
    ++oct;
  }
  uint64_t x = uint64_t(base) * (65536u + nds_sound_tables::Pitch[n]);
  int shift = oct - 16;
  if (shift <= 0)
    x = (-shift >= 64) ? 0 : x >> (-shift);
  else if (shift >= 32 || x > (uint64_t(65535) >> shift))
    return 65535;
  else
    x <<= shift;
  return uint16_t(std::clamp<uint64_t>(x, 16, 65535));
}
uint16_t random16() {
  g.rng = g.rng * 1664525u + 1013904223u;
  return g.rng >> 16;
}
int16_t variable(unsigned pl, unsigned n) {
  if (n >= 32) {
    g.fault = true;
    return 0;
  }
  return g.shared ? load<int16_t>(g.shared + (n < 16 ? 0x20 + pl * 36 + n * 2
                                                     : 0x260 + (n - 16) * 2))
                  : 0;
}
void variable(unsigned pl, unsigned n, int16_t value) {
  if (n >= 32) {
    g.fault = true;
    return;
  }
  if (g.shared)
    store<int16_t>(g.shared +
                       (n < 16 ? 0x20 + pl * 36 + n * 2 : 0x260 + (n - 16) * 2),
                   value);
}
void unlink(unsigned i) {
  auto a = g.work.voice(i);
  uint32_t vp = voiceptr(i);
  for (unsigned j = 0; j < 32; ++j) {
    auto tr = g.work.track(j);
    uint32_t q = tr.get<uint32_t>(t::Voices);
    if (q == vp) {
      tr.set<uint32_t>(t::Voices, a.get<uint32_t>(v::Next));
      break;
    }
    for (unsigned k = 0; k < 16 && q; ++k) {
      int x = voiceindex(q);
      if (x < 0) {
        g.fault = true;
        break;
      }
      auto prev = g.work.voice(x);
      if (prev.get<uint32_t>(v::Next) == vp) {
        prev.set<uint32_t>(v::Next, a.get<uint32_t>(v::Next));
        q = 0;
        break;
      }
      q = prev.get<uint32_t>(v::Next);
    }
  }
  a.set<uint32_t>(v::Callback, 0);
  a.set<uint32_t>(v::CallbackArg, 0);
  a.set<uint32_t>(v::Next, 0);
}
void kill(unsigned i, bool stop) {
  source_dirty = true;
  auto a = g.work.voice(i);
  unlink(i);
  a.byte(v::Priority, 0);
  a.flag(v::Flags, 1, false);
  a.set<uint16_t>(v::Volume, 0);
  if (stop)
    write(reg(i), nds_spu_read(reg(i), 4) & ~0x80000000u, 4);
}
void release_track(unsigned n, int release = -1, bool detach = false) {
  auto a = g.work.track(n);
  uint32_t q = a.get<uint32_t>(t::Voices);
  for (unsigned k = 0; k < 16 && q; ++k) {
    int i = voiceindex(q);
    if (i < 0) {
      g.fault = true;
      break;
    }
    auto ch = g.work.voice(i);
    uint32_t next = ch.get<uint32_t>(v::Next);
    if (release >= 0)
      ch.set<uint16_t>(v::Release, decay(release));
    ch.byte(v::Priority, 1);
    ch.byte(v::EnvelopeStage, 3);
    if (detach)
      unlink(i);
    q = next;
  }
}
void stop_track(unsigned n) {
  release_track(n, -1, true);
  auto a = g.work.track(n);
  a.set<uint32_t>(t::Base, 0);
  a.set<uint32_t>(t::Position, 0);
  a.flag(t::Flags, t::Active, false);
}
void stop_player(unsigned n) {
  if (n >= 16) {
    g.fault = true;
    return;
  }
  auto a = g.work.player(n);
  for (unsigned j = 0; j < 16; ++j) {
    int tr = trackid(n, j);
    if (tr >= 0)
      stop_track(tr);
    a.byte(p::Tracks + j, 255);
  }
  a.flag(p::Flags, p::Active, false);
}
void init_track(unsigned n) {
  auto a = g.work.track(n);
  std::memset(a.bytes, 0, TrackSize);
  a.byte(t::Flags, t::Active | t::NoteWait | t::Compare);
  a.byte(t::PanRange, 127);
  a.byte(t::Volume, 127);
  a.byte(t::Expression, 127);
  a.byte(t::BendRange, 2);
  for (unsigned x = t::Attack; x <= t::Release; ++x)
    a.byte(x, 255);
  a.byte(t::Priority, 64);
  a.byte(t::PortamentoKey, 60);
  a.byte(t::Lfo + 1, 16);
  a.byte(t::Lfo + 3, 1);
  a.set<uint16_t>(t::ChannelMask, 65535);
}
int alloc_track() {
  for (unsigned i = 0; i < 32; ++i)
    if (!g.work.track(i).flag(t::Flags, t::Active)) {
      init_track(i);
      return i;
    }
  return -1;
}
unsigned read8(View a) {
  uint32_t pos = a.get<uint32_t>(t::Position);
  unsigned x = load<uint8_t>(pos);
  a.set<uint32_t>(t::Position, pos + 1);
  return x;
}
unsigned read16(View a) {
  unsigned x = read8(a);
  return x | (read8(a) << 8);
}
uint32_t read24(View a) {
  unsigned x = read16(a);
  return x | (read8(a) << 16);
}
int vlv(View a) {
  uint32_t x = 0;
  for (unsigned i = 0; i < 5; ++i) {
    unsigned b = read8(a);
    if (x > 0xFFFFFFu) {
      g.fault = true;
      return 0;
    }
    x = x * 128 + (b & 127);
    if (!(b & 128))
      return int(x);
  }
  g.fault = true;
  return 0;
}
int value(View a, unsigned pl, int kind) {
  switch (kind) {
  case 0:
    return read8(a);
  case 1:
    return read16(a);
  case 2:
    return vlv(a);
  case 3:
    return variable(pl, read8(a));
  case 4: {
    int lo = int16_t(read16(a)), hi = int16_t(read16(a));
    return lo + int((int64_t(random16()) * (hi - lo + 1)) >> 16);
  }
  default:
    g.fault = true;
    return 0;
  }
}
void prepare(unsigned n, uint32_t base, uint32_t offset, uint32_t bank,
             bool start) {
  if (n >= 16) {
    g.fault = true;
    return;
  }
  stop_player(n);
  auto a = g.work.player(n);
  std::memset(a.bytes, 0, PlayerSize);
  a.byte(p::Id, n);
  a.byte(p::Flags, p::Active | (start ? p::Started : 0));
  a.byte(p::Priority, 64);
  a.byte(p::Volume, 127);
  a.set<uint16_t>(p::Tempo, 120);
  a.set<uint16_t>(p::TempoRatio, 256);
  a.set<uint16_t>(p::TempoCounter, 240);
  a.set<uint32_t>(p::Bank, bank);
  for (unsigned j = 0; j < 16; ++j) {
    a.byte(p::Tracks + j, 255);
    variable(n, j, -1);
  }
  if (g.shared)
    store<uint32_t>(g.shared + 0x40 + n * 36, 0);
  int tr = alloc_track();
  if (tr < 0)
    return;
  a.byte(p::Tracks, tr);
  auto b = g.work.track(tr);
  b.set<uint32_t>(t::Base, base);
  b.set<uint32_t>(t::Position, base + offset);
  if (read8(b) == 0xFE) {
    unsigned mask = read16(b);
    for (unsigned j = 1; j < 16; ++j)
      if (mask & (1u << j)) {
        int x = alloc_track();
        if (x < 0)
          break;
        a.byte(p::Tracks + j, x);
      }
  } else
    b.set<uint32_t>(t::Position, base + offset);
}
struct Instrument {
  uint8_t type = 0;
  std::array<uint8_t, 10> data{};
};
Instrument instrument(uint32_t bank, unsigned prog, unsigned key) {
  Instrument out;
  if (!bank || prog >= load<uint32_t>(bank + 0x38))
    return out;
  uint32_t word = load<uint32_t>(bank + 0x3C + prog * 4),
           addr = bank + (word >> 8);
  out.type = word & 255;
  if (out.type == 16) {
    unsigned lo = load<uint8_t>(addr), hi = load<uint8_t>(addr + 1);
    if (key < lo || key > hi) {
      out.type = 0;
      return out;
    }
    addr += 2 + (key - lo) * 12;
    out.type = load<uint16_t>(addr);
    addr += 2;
  } else if (out.type == 17) {
    unsigned i = 0;
    while (i < 8 && key > load<uint8_t>(addr + i))
      ++i;
    if (i == 8) {
      out.type = 0;
      return out;
    }
    addr += 8 + i * 12;
    out.type = load<uint16_t>(addr);
    addr += 2;
  }
  if (out.type >= 1 && out.type <= 3)
    memory(addr, out.data.data(), 10);
  else
    out.type = 0;
  return out;
}
void setup_pcm(unsigned i, uint32_t source, unsigned format, unsigned repeat,
               unsigned loop, unsigned length, unsigned vol, unsigned shift,
               unsigned period, unsigned pan) {
  g.sources[i] = source;
  g.lengths[i] = (loop + length) * 4;
  source_dirty = true;
  g.pans[i] = pan;
  g.volumes[i] = vol;
  unsigned physicalpan = g.masterpan >= 0 ? unsigned(g.masterpan) : pan;
  if (g.surround > 0 && i != 1 && i != 3) {
    if (physicalpan < 24)
      vol = (int64_t(vol) *
             (g.surround * (physicalpan + 40) + ((32767 - g.surround) << 6))) >>
            21;
    else if (physicalpan > 104)
      vol = (int64_t(vol) * (-g.surround * int(physicalpan - 40) +
                             ((g.surround + 32767) << 6))) >>
            21;
  }
  write(reg(i),
        (format << 29) | (repeat << 27) | (physicalpan << 16) | (shift << 8) |
            vol,
        4);
  write(reg(i) + 4, source, 4);
  write(reg(i) + 8, uint16_t(0x10000 - period), 2);
  write(reg(i) + 10, loop, 2);
  write(reg(i) + 12, length, 4);
}
int allocate_voice(unsigned mask, unsigned priority, bool allow_weak) {
  int best = -1;
  constexpr unsigned shifts[] = {0, 1, 2, 4};
  mask &= ~(g.stronglock | (allow_weak ? 0u : g.weaklock));
  for (unsigned i : AllocationOrder)
    if (mask & (1u << i)) {
      auto a = g.work.voice(i);
      if (best < 0) {
        best = i;
        continue;
      }
      auto b = g.work.voice(best);
      unsigned av = a.get<uint16_t>(v::Volume), bv = b.get<uint16_t>(v::Volume);
      int aa = (av & 255) * 16 >> shifts[(av >> 8) & 3],
          bb = (bv & 255) * 16 >> shifts[(bv >> 8) & 3];
      if (a.byte(v::Priority) < b.byte(v::Priority) ||
          (a.byte(v::Priority) == b.byte(v::Priority) && bb > aa))
        best = i;
    }
  if (best < 0 || priority < g.work.voice(best).byte(v::Priority))
    return -1;
  kill(best, true);
  return best;
}
void playnote(unsigned tr, unsigned pl, int key, unsigned velocity,
              int length) {
  auto a = g.work.track(tr);
  auto player = g.work.player(pl);
  int i = -1;
  if (a.flag(t::Flags, t::Tie))
    i = voiceindex(a.get<uint32_t>(t::Voices));
  if (i < 0) {
    auto inst = instrument(player.get<uint32_t>(p::Bank),
                           a.get<uint16_t>(t::Program), key);
    if (!inst.type)
      return;
    unsigned mask = inst.type == 1 ? 65535 : inst.type == 2 ? 0x3F00 : 0xC000;
    mask &= a.get<uint16_t>(t::ChannelMask);
    i = allocate_voice(mask,
                       std::min(255u, unsigned(player.byte(p::Priority)) +
                                          a.byte(t::Priority)),
                       a.flag(t::Flags, 128));
    if (i < 0)
      return;
    auto b = g.work.voice(i);
    std::memset(b.bytes, 0, VoiceSize);
    b.byte(v::Id, i);
    b.byte(v::Type, inst.type - 1);
    b.byte(v::Flags, 7);
    b.byte(v::PanRange, 127);
    b.byte(v::RootKey, inst.data[4]);
    b.byte(v::Priority, std::min(255u, unsigned(player.byte(p::Priority)) +
                                           a.byte(t::Priority)));
    b.set<uint16_t>(v::Volume, 127);
    b.set<int32_t>(v::Envelope, -92544);
    b.set<int32_t>(v::Length, a.flag(t::Flags, t::Tie) ? -1 : length);
    b.byte(v::InitialPan, uint8_t(inst.data[9] - 64));
    attack(b, inst.data[5]);
    b.set<uint16_t>(v::Decay, decay(inst.data[6]));
    b.byte(v::Sustain, inst.data[7]);
    b.set<uint16_t>(v::Release, decay(inst.data[8] == 255 ? 0 : inst.data[8]));
    if (inst.data[8] == 255)
      b.set<int32_t>(v::Length, -1);
    if (inst.type == 1) {
      unsigned wave = inst.data[0] | (inst.data[1] << 8),
               arc = inst.data[2] | (inst.data[3] << 8);
      if (arc >= 4) {
        kill(i, true);
        return;
      }
      uint32_t bank = player.get<uint32_t>(p::Bank),
               war = load<uint32_t>(bank + 0x18 + arc * 8);
      if (!war || wave >= load<uint32_t>(war + 0x38)) {
        kill(i, true);
        return;
      }
      uint32_t wav = load<uint32_t>(war + 0x3C + wave * 4);
      if (!(wav & 0xFF000000u))
        wav += war;
      memory(wav, b.bytes + v::Wave, 12);
      b.set<uint32_t>(v::Source, wav + 12);
    } else {
      b.byte(v::Wave, inst.data[0]);
      b.set<uint16_t>(v::Wave + 4, 8006);
    }
    b.set<uint32_t>(v::Callback, g.profile.callback);
    b.set<uint32_t>(v::CallbackArg, trackptr(tr));
    b.set<uint32_t>(v::Next, a.get<uint32_t>(t::Voices));
    a.set<uint32_t>(t::Voices, voiceptr(i));
  }
  auto b = g.work.voice(i);
  b.byte(v::Key, key);
  b.byte(v::Velocity, velocity);
  if (a.byte(t::Attack) != 255)
    attack(b, a.byte(t::Attack));
  if (a.byte(t::Decay) != 255)
    b.set<uint16_t>(v::Decay, decay(a.byte(t::Decay)));
  if (a.byte(t::Sustain) != 255)
    b.byte(v::Sustain, a.byte(t::Sustain));
  if (a.byte(t::Release) != 255)
    b.set<uint16_t>(v::Release, decay(a.byte(t::Release)));
  int sweep = a.get<int16_t>(t::SweepPitch);
  if (a.flag(t::Flags, t::Portamento))
    sweep += (int(a.byte(t::PortamentoKey)) - key) * 64;
  b.set<int16_t>(v::SweepPitch, sweep);
  unsigned port = a.byte(t::PortamentoTime);
  b.set<int32_t>(v::SweepLength,
                 port ? int((int64_t(port) * port * std::abs(sweep)) >> 11)
                      : length);
  if (!port)
    b.flag(v::Flags, 4, false);
  b.set<int32_t>(v::SweepCounter, 0);
}
void update_track(unsigned tr, unsigned pl, bool tick) {
  auto a = g.work.track(tr), player = g.work.player(pl);
  int vol = sustain(a.byte(t::Volume)) + sustain(a.byte(t::Expression)) +
            sustain(player.byte(p::Volume));
  if (a.flag(t::Flags, t::Mute))
    vol = -32768;
  vol = std::max(-32768, vol);
  int pitch =
      (int(a.get<int8_t>(t::PitchBend)) * a.byte(t::BendRange) * 64 >> 7) +
      a.get<int16_t>(t::ExtraPitch);
  int pan = a.get<int8_t>(t::Pan);
  if (a.byte(t::PanRange) != 127)
    pan = (pan * a.byte(t::PanRange) + 64) >> 7;
  pan = std::clamp(pan + a.get<int8_t>(t::ExtraPan), -128, 127);
  for (uint32_t q = a.get<uint32_t>(t::Voices), k = 0; q && k < 16; ++k) {
    int i = voiceindex(q);
    if (i < 0) {
      g.fault = true;
      break;
    }
    auto b = g.work.voice(i);
    b.set<int16_t>(v::ExtraVolume, int(a.get<int16_t>(t::ExtraVolume)) +
                                       player.get<int16_t>(p::ExtraVolume));
    if (b.byte(v::EnvelopeStage) != 3) {
      b.set<int16_t>(v::UserVolume, vol);
      b.set<int16_t>(v::UserPitch, pitch);
      b.byte(v::UserPan, pan);
      b.byte(v::PanRange, a.byte(t::PanRange));
      std::memcpy(b.bytes + v::Lfo, a.bytes + t::Lfo, 6);
      if (tick && b.get<int32_t>(v::Length) == 0) {
        b.byte(v::Priority, 1);
        b.byte(v::EnvelopeStage, 3);
      }
    }
    q = b.get<uint32_t>(v::Next);
  }
}
bool step_track(unsigned tr, unsigned pl, bool audible = true) {
  if (tr >= TrackCount || pl >= PlayerCount) { g.fault = true; return false; }
  auto a = g.work.track(tr);
  if (a.byte(t::Depth) > 3) { g.fault = true; return false; }
  for (uint32_t q = a.get<uint32_t>(t::Voices), k = 0; q && k < 16; ++k) {
    int i = voiceindex(q);
    if (i < 0) {
      g.fault = true;
      return false;
    }
    auto b = g.work.voice(i);
    int len = b.get<int32_t>(v::Length);
    if (len > 0)
      b.set<int32_t>(v::Length, len - 1);
    if (!b.flag(v::Flags, 4) &&
        b.get<int32_t>(v::SweepCounter) < b.get<int32_t>(v::SweepLength))
      b.set<int32_t>(v::SweepCounter, b.get<int32_t>(v::SweepCounter) + 1);
    q = b.get<uint32_t>(v::Next);
  }
  if (a.flag(t::Flags, t::NoteFinishWait)) {
    if (a.get<uint32_t>(t::Voices))
      return true;
    a.flag(t::Flags, t::NoteFinishWait, false);
  }
  int wait = a.get<int32_t>(t::Wait);
  if (wait > 0) {
    a.set<int32_t>(t::Wait, --wait);
    if (wait > 0)
      return true;
  }
  for (unsigned guard = 0;
       guard < 65536 && !g.fault && a.get<int32_t>(t::Wait) == 0 &&
       !a.flag(t::Flags, t::NoteFinishWait);
       ++guard) {
    unsigned cmd = read8(a);
    bool run = true;
    int kind = -1;
    if (cmd == 0xA2) {
      cmd = read8(a);
      run = a.flag(t::Flags, t::Compare);
    }
    if (cmd == 0xA0 || cmd == 0xA1) {
      kind = cmd == 0xA0 ? 4 : 3;
      cmd = read8(a);
    }
    if (cmd < 0x80) {
      unsigned vel = read8(a);
      int len = value(a, pl, kind < 0 ? 2 : kind);
      if (run) {
        int key = std::clamp(int(cmd) + a.get<int8_t>(t::Transpose), 0, 127);
        if (audible)
          playnote(tr, pl, key, vel, len ? len : -1);
        a.byte(t::PortamentoKey, key);
        if (a.flag(t::Flags, t::NoteWait)) {
          a.set<int32_t>(t::Wait, len);
          if (!len)
            a.flag(t::Flags, t::NoteFinishWait, true);
        }
      }
      continue;
    }
    if ((cmd & 0xF0) == 0x80) {
      int x = value(a, pl, kind < 0 ? 2 : kind);
      if (run) {
        if (cmd == 0x80)
          a.set<int32_t>(t::Wait, x);
        else if (cmd == 0x81 && x < 65536)
          a.set<uint16_t>(t::Program, x);
        else {
          g.fault = true;
          return false;
        }
      }
    } else if ((cmd & 0xF0) == 0x90) {
      if (cmd == 0x93) {
        unsigned n = read8(a);
        uint32_t off = read24(a);
        int nt = trackid(pl, n);
        if (run && nt >= 0 && nt != int(tr)) {
          release_track(nt, -1, true);
          auto b = g.work.track(nt);
          b.set<uint32_t>(t::Base, a.get<uint32_t>(t::Base));
          b.set<uint32_t>(t::Position, a.get<uint32_t>(t::Base) + off);
        }
      } else {
        uint32_t off = read24(a);
        if (run) {
          if (cmd == 0x95) {
            unsigned depth = a.byte(t::Depth);
            if (depth >= 3)
              continue;
            a.set<uint32_t>(t::Calls + depth * 4, a.get<uint32_t>(t::Position));
            a.byte(t::Depth, depth + 1);
          } else if (cmd != 0x94) {
            g.fault = true;
            return false;
          }
          a.set<uint32_t>(t::Position, a.get<uint32_t>(t::Base) + off);
        }
      }
    } else if ((cmd & 0xF0) == 0xB0) {
      unsigned n = read8(a);
      int16_t x = value(a, pl, kind < 0 ? 1 : kind), old = variable(pl, n);
      if (run) {
        int32_t z = old;
        switch (cmd) {
        case 0xB0:
          z = x;
          break;
        case 0xB1:
          z += x;
          break;
        case 0xB2:
          z -= x;
          break;
        case 0xB3:
          z *= x;
          break;
        case 0xB4:
          if (x)
            z /= x;
          break;
        case 0xB5:
          z = x >= 0 ? (x >= 16 ? 0 : uint16_t(old) << x)
                     : (-int(x) >= 16 ? (old < 0 ? -1 : 0) : old >> -x);
          break;
        case 0xB6: {
          int bound = std::abs(int(x));
          z = (uint32_t(random16()) * (bound + 1)) >> 16;
          if (x < 0)
            z = -z;
          break;
        }
        case 0xB8:
          a.flag(t::Flags, t::Compare, old == x);
          break;
        case 0xB9:
          a.flag(t::Flags, t::Compare, old >= x);
          break;
        case 0xBA:
          a.flag(t::Flags, t::Compare, old > x);
          break;
        case 0xBB:
          a.flag(t::Flags, t::Compare, old <= x);
          break;
        case 0xBC:
          a.flag(t::Flags, t::Compare, old < x);
          break;
        case 0xBD:
          a.flag(t::Flags, t::Compare, old != x);
          break;
        default:
          g.fault = true;
          return false;
        }
        variable(pl, n, z);
      }
    } else if ((cmd & 0xE0) == 0xC0) {
      unsigned x = uint8_t(value(a, pl, kind < 0 ? 0 : kind));
      if (!run)
        continue;
      switch (cmd) {
      case 0xC0:
        a.byte(t::Pan, x - 64);
        break;
      case 0xC1:
        a.byte(t::Volume, x);
        break;
      case 0xC2:
        g.work.player(pl).byte(p::Volume, x);
        break;
      case 0xC3:
        a.byte(t::Transpose, x);
        break;
      case 0xC4:
        a.byte(t::PitchBend, x);
        break;
      case 0xC5:
        a.byte(t::BendRange, x);
        break;
      case 0xC6:
        a.byte(t::Priority, x);
        break;
      case 0xC7:
        a.flag(t::Flags, t::NoteWait, x != 0);
        break;
      case 0xC8:
        a.flag(t::Flags, t::Tie, x != 0);
        release_track(tr, -1, true);
        break;
      case 0xC9:
        a.byte(t::PortamentoKey, x + a.get<int8_t>(t::Transpose));
        a.flag(t::Flags, t::Portamento, true);
        break;
      case 0xCA:
        a.byte(t::Lfo + 2, x);
        break;
      case 0xCB:
        a.byte(t::Lfo + 1, x);
        break;
      case 0xCC:
        a.byte(t::Lfo, x);
        break;
      case 0xCD:
        a.byte(t::Lfo + 3, x);
        break;
      case 0xCE:
        a.flag(t::Flags, t::Portamento, x != 0);
        break;
      case 0xCF:
        a.byte(t::PortamentoTime, x);
        break;
      case 0xD0:
        a.byte(t::Attack, x);
        break;
      case 0xD1:
        a.byte(t::Decay, x);
        break;
      case 0xD2:
        a.byte(t::Sustain, x);
        break;
      case 0xD3:
        a.byte(t::Release, x);
        break;
      case 0xD4: {
        unsigned d = a.byte(t::Depth);
        if (d < 3) {
          a.set<uint32_t>(t::Calls + d * 4, a.get<uint32_t>(t::Position));
          a.byte(t::Loops + d, x);
          a.byte(t::Depth, d + 1);
        }
        break;
      }
      case 0xD5:
        a.byte(t::Expression, x);
        break;
      case 0xD6:
        break;
      default:
        g.fault = true;
        return false;
      }
    } else if ((cmd & 0xF0) == 0xE0) {
      int x = int16_t(value(a, pl, kind < 0 ? 1 : kind));
      if (run) {
        if (cmd == 0xE0)
          a.set<uint16_t>(t::Lfo + 4, x);
        else if (cmd == 0xE1)
          g.work.player(pl).set<uint16_t>(p::Tempo, x);
        else if (cmd == 0xE3)
          a.set<int16_t>(t::SweepPitch, x);
        else {
          g.fault = true;
          return false;
        }
      }
    } else if ((cmd & 0xF0) == 0xF0) {
      if (cmd == 0xFE) {
        read16(a);
        continue;
      }
      if (!run)
        continue;
      unsigned d = a.byte(t::Depth);
      if (cmd == 0xFF)
        return false;
      if (cmd == 0xFD) {
        if (d) {
          a.byte(t::Depth, d - 1);
          a.set<uint32_t>(t::Position, a.get<uint32_t>(t::Calls + (d - 1) * 4));
        }
      } else if (cmd == 0xFC) {
        if (d) {
          unsigned n = a.byte(t::Loops + d - 1);
          if (n && !--n)
            a.byte(t::Depth, d - 1);
          else
            a.set<uint32_t>(t::Position,
                            a.get<uint32_t>(t::Calls + (d - 1) * 4));
          a.byte(t::Loops + d - 1, n);
        }
      } else {
        g.fault = true;
        return false;
      }
    } else {
      g.fault = true;
      return false;
    }
    if (guard == 65535)
      g.fault = true;
  }
  return !g.fault;
}
void sequence(bool tick) {
  for (unsigned pl = 0; pl < 16; ++pl) {
    auto a = g.work.player(pl);
    if (!a.flag(p::Flags, p::Active) || !a.flag(p::Flags, p::Started))
      continue;
    if (tick && !a.flag(p::Flags, p::Paused)) {
      unsigned count = a.get<uint16_t>(p::TempoCounter), ticks = count / 240;
      count %= 240;
      for (unsigned k = 0; k < ticks; ++k) {
        for (unsigned j = 0; j < 16; ++j) {
          int tr = trackid(pl, j);
          if (tr >= 0 && g.work.track(tr).get<uint32_t>(t::Position) &&
              !step_track(tr, pl)) {
            stop_track(tr);
            a.byte(p::Tracks + j, 255);
          }
        }
        if (g.shared) {
          uint32_t addr = g.shared + 0x40 + pl * 36;
          store<uint32_t>(addr, load<uint32_t>(addr) + 1);
        }
      }
      count += (unsigned(a.get<uint16_t>(p::Tempo)) *
                a.get<uint16_t>(p::TempoRatio)) >>
               8;
      a.set<uint16_t>(p::TempoCounter, count);
    }
    bool any = false;
    for (unsigned j = 0; j < 16; ++j) {
      int tr = trackid(pl, j);
      if (tr >= 0) {
        any = true;
        update_track(tr, pl, tick);
      }
    }
    if (!any)
      a.flag(p::Flags, p::Active, false);
  }
}
int sine(unsigned x) {
  x &= 127;
  return x < 32   ? nds_sound_tables::Sin[x]
         : x < 64 ? nds_sound_tables::Sin[64 - x]
         : x < 96 ? -nds_sound_tables::Sin[x - 64]
                  : -nds_sound_tables::Sin[128 - x];
}
void sync_voices() {
  for (unsigned i = 0; i < 16; ++i) {
    auto a = g.work.voice(i);
    unsigned sync = a.byte(v::Flags) >> 3;
    if (sync & 2)
      write(reg(i), nds_spu_read(reg(i), 4) & ~0x80000000u, 4);
    if (sync & 1) {
      unsigned cv = a.get<uint16_t>(v::Volume), ct = a.get<uint16_t>(v::Timer),
               pan = a.byte(v::Pan), type = a.byte(v::Type);
      if (type == 0)
        setup_pcm(i, a.get<uint32_t>(v::Source), a.byte(v::Wave) & 3,
                  a.byte(v::Wave + 1) ? 1 : 2, a.get<uint16_t>(v::Wave + 6),
                  a.get<uint32_t>(v::Wave + 8), cv & 255, cv >> 8, ct, pan);
      else {
        setup_pcm(i, 0, 3, 0, 0, 0, cv & 255, cv >> 8, ct, pan);
        if (type == 1)
          write(reg(i) + 3, 0x60 | (a.byte(v::Wave) & 7), 1);
      }
      write(reg(i), nds_spu_read(reg(i), 4) | 0x80000000u, 4);
    } else {
      if (sync & 4)
        write(reg(i) + 8, uint16_t(0x10000 - a.get<uint16_t>(v::Timer)), 2);
      if (sync & 8) {
        unsigned cv = a.get<uint16_t>(v::Volume);
        g.volumes[i] = cv & 255;
        uint32_t cnt = nds_spu_read(reg(i), 4);
        write(reg(i), (cnt & ~0x37Fu) | (cv & 255) | ((cv >> 8) << 8), 4);
      }
      if (sync & 16) {
        g.pans[i] = a.byte(v::Pan);
        write(reg(i) + 2, g.masterpan >= 0 ? g.masterpan : g.pans[i], 1);
      }
    }
    a.byte(v::Flags, a.byte(v::Flags) & 7);
  }
}
void voices(bool tick) {
  for (unsigned i = 0; i < 16; ++i) {
    auto a = g.work.voice(i);
    if (!a.flag(v::Flags, 1))
      continue;
    bool start = a.flag(v::Flags, 2);
    if (start) {
      a.flag(v::Flags, 8, true);
      a.flag(v::Flags, 2, false);
    }
    if (!start && !(nds_spu_read(reg(i), 4) & 0x80000000u)) {
      kill(i, false);
      continue;
    }
    int env = a.get<int32_t>(v::Envelope);
    if (tick) {
      switch (a.byte(v::EnvelopeStage)) {
      case 0:
        env = -int((int64_t(-env) * a.byte(v::Attack)) >> 8);
        if (!env)
          a.byte(v::EnvelopeStage, 1);
        break;
      case 1:
        env -= a.get<uint16_t>(v::Decay);
        if (env <= sustain(a.byte(v::Sustain)) * 128) {
          env = sustain(a.byte(v::Sustain)) * 128;
          a.byte(v::EnvelopeStage, 2);
        }
        break;
      case 3:
        env -= a.get<uint16_t>(v::Release);
        break;
      }
      a.set<int32_t>(v::Envelope, env);
    }
    int vol = sustain(a.byte(v::Velocity)) + (env >> 7) +
              a.get<int16_t>(v::ExtraVolume) + a.get<int16_t>(v::UserVolume);
    int pitch = (int(a.byte(v::Key)) - a.byte(v::RootKey)) * 64 +
                a.get<int16_t>(v::UserPitch);
    int sc = a.get<int32_t>(v::SweepCounter),
        sl = a.get<int32_t>(v::SweepLength);
    if (sc < sl && sl > 0) {
      pitch += int(int64_t(a.get<int16_t>(v::SweepPitch)) * (sl - sc) / sl);
      if (tick && a.flag(v::Flags, 4))
        a.set<int32_t>(v::SweepCounter, sc + 1);
    }
    int lfo = 0;
    unsigned delay = a.get<uint16_t>(v::Lfo + 6);
    if (delay >= a.get<uint16_t>(v::Lfo + 4)) {
      lfo = sine(a.get<uint16_t>(v::Lfo + 8) >> 8) * a.byte(v::Lfo + 2) *
            a.byte(v::Lfo + 3);
      lfo = int((int64_t(lfo) * (a.byte(v::Lfo) == 1 ? 60 : 64)) >> 14);
      if (tick)
        a.set<uint16_t>(
            v::Lfo + 8,
            (a.get<uint16_t>(v::Lfo + 8) + a.byte(v::Lfo + 1) * 64) & 0x7FFF);
    } else if (tick)
      a.set<uint16_t>(v::Lfo + 6, delay + 1);
    int pan = a.get<int8_t>(v::InitialPan) + (a.byte(v::Lfo) == 2 ? lfo : 0);
    if (a.byte(v::PanRange) != 127)
      pan = (pan * a.byte(v::PanRange) + 64) >> 7;
    pan = std::clamp(pan + a.get<int8_t>(v::UserPan) + 64, 0, 127);
    if (a.byte(v::Lfo) == 0)
      pitch += lfo;
    else if (a.byte(v::Lfo) == 1 && vol > -32768)
      vol += lfo;
    if (a.byte(v::EnvelopeStage) == 3 && vol <= -723) {
      kill(i, true);
      continue;
    }
    uint16_t cv = volume(vol), ct = timer(a.get<uint16_t>(v::Wave + 4), pitch);
    if (a.byte(v::Type) == 1)
      ct &= ~3u;
    if (cv != a.get<uint16_t>(v::Volume)) {
      a.set<uint16_t>(v::Volume, cv);
      a.flag(v::Flags, 64, true);
    }
    if (ct != a.get<uint16_t>(v::Timer)) {
      a.set<uint16_t>(v::Timer, ct);
      a.flag(v::Flags, 32, true);
    }
    if (pan != a.byte(v::Pan)) {
      a.byte(v::Pan, pan);
      a.flag(v::Flags, 128, true);
    }
  }
}
void status() {
  if (!g.shared)
    return;
  uint32_t players = 0;
  for (unsigned i = 0; i < 16; ++i)
    if (g.work.player(i).flag(p::Flags, p::Active))
      players |= 1u << i;
  store<uint32_t>(g.shared + 4, players);
  unsigned channels = 0, capture = 0;
  for (unsigned i = 0; i < 16; ++i)
    if (nds_spu_read(reg(i), 4) & 0x80000000u)
      channels |= 1u << i;
  for (unsigned i = 0; i < 2; ++i)
    if (nds_spu_read(0x04000508 + i, 1) & 128)
      capture |= 1u << i;
  store<uint16_t>(g.shared + 8, channels);
  store<uint16_t>(g.shared + 10, capture);
}
void alarm_start(unsigned i) {
  auto a = g.work.alarm(i);
  a.byte(0, 1);
  g.alarm_deadlines[i] = g.now + a.get<uint64_t>(4) * 64;
}
void alarm_stop(unsigned i) {
  auto a = g.work.alarm(i);
  if (a.byte(0)) {
    a.byte(0, 0);
    a.byte(1, a.byte(1) + 1);
  }
  g.alarm_deadlines[i] = Never;
}
void command(const uint32_t *q) {
  uint32_t id = q[1], a = q[2], b = q[3], c = q[4], d = q[5];
  switch (id) {
  case 0:
  case 2:
    prepare(a, b, c, d, id == 0);
    break;
  case 1:
    stop_player(a);
    break;
  case 3:
    if (a < 16)
      g.work.player(a).flag(p::Flags, p::Started, true);
    else
      g.fault = true;
    break;
  case 4:
    if (a < 16) {
      g.work.player(a).flag(p::Flags, p::Paused, b != 0);
      for (unsigned j = 0; b && j < 16; ++j) {
        int tr = trackid(a, j);
        if (tr >= 0)
          release_track(tr, 127, true);
      }
    } else
      g.fault = true;
    break;
  case 5:
    if (a < 16) {
      for (unsigned j = 0; j < 16; ++j) {
        int tr = trackid(a, j);
        if (tr >= 0)
          release_track(tr, 127, true);
      }
      for (unsigned j = 0; j < b && !g.fault; ++j)
        for (unsigned k = 0; k < 16; ++k) {
          int tr = trackid(a, k);
          if (tr >= 0 && !step_track(tr, a, false)) {
            stop_track(tr);
            g.work.player(a).byte(p::Tracks + k, 255);
          }
        }
      if (g.shared) {
        uint32_t addr = g.shared + 0x40 + a * 36;
        store<uint32_t>(addr, load<uint32_t>(addr) + b);
      }
    } else
      g.fault = true;
    break;
  case 6:
    if (a < 16)
      rawparam(g.work.player(a), PlayerSize, b, c, d);
    else
      g.fault = true;
    break;
  case 7: {
    unsigned pl = a & 0xFFFFFFu;
    if (pl >= 16) {
      g.fault = true;
      break;
    }
    for (unsigned j = 0; j < 16; ++j)
      if (b & (1u << j)) {
        int tr = trackid(pl, j);
        if (tr >= 0)
          rawparam(g.work.track(tr), TrackSize, c, d, a >> 24);
      }
    break;
  }
  case 8:
  case 9:
    if (a < 16) {
      for (unsigned j = 0; j < 16; ++j)
        if (b & (1u << j)) {
          int tr = trackid(a, j);
          if (tr >= 0) {
            if (id == 8) {
              g.work.track(tr).flag(t::Flags, t::Mute, (c & 1) != 0);
              if (c)
                release_track(tr, 127, true);
            } else {
              g.work.track(tr).set<uint16_t>(t::ChannelMask, c);
              g.work.track(tr).flag(t::Flags, 128, true);
            }
          }
        }
    } else
      g.fault = true;
    break;
  case 10:
    if (a < 16 && b < 16)
      variable(a, b, int16_t(c));
    else
      g.fault = true;
    break;
  case 11:
    if (a < 16)
      variable(0, a + 16, int16_t(b));
    else
      g.fault = true;
    break;
  case 12:
  case 13:
    source_dirty = true;
    for (unsigned i = 0; i < 16; ++i)
      if (a & (1u << i)) {
        uint32_t cnt = nds_spu_read(reg(i), 4);
        if (id == 12)
          cnt |= 0x80000000u;
        else {
          cnt &= ~0x80000000u;
          if (d & 1)
            cnt |= 0x8000;
        }
        write(reg(i), cnt, 4);
      }
    for (unsigned i = 0; i < 2; ++i)
      if (b & (1u << i)) {
        uint32_t cnt = nds_spu_read(0x04000508 + i, 1);
        write(0x04000508 + i, id == 12 ? cnt | 128 : cnt & ~128u, 1);
      }
    for (unsigned i = 0; i < 8; ++i)
      if (c & (1u << i)) {
        if (id == 12)
          alarm_start(i);
        else
          alarm_stop(i);
      }
    status();
    break;
  case 14:
    setup_pcm(a & 65535, b & 0x07FFFFFFu, (d >> 24) & 3, (d >> 26) & 3, a >> 16,
              c & 0x3FFFFF, (c >> 24) & 127, (c >> 22) & 3, d & 65535,
              (d >> 16) & 127);
    break;
  case 15:
  case 16:
    setup_pcm(a, 0, 3, 0, 0, 0, b & 127, (b >> 8) & 3, (c >> 8) & 65535,
              c & 127);
    if (id == 15)
      write(reg(a) + 3, 0x60 | (d & 7), 1);
    break;
  case 17: {
    unsigned i = c >> 31;
    write(0x04000508 + i,
          ((c >> 30) & 1) * 8 + (((c >> 29) & 1) ? 0 : 4) +
              (((c >> 28) & 1) << 1) + ((c >> 27) & 1),
          1);
    write(0x04000510 + i * 8, a, 4);
    write(0x04000514 + i * 8, b, 2);
    break;
  }
  case 18:
    if (a < 8) {
      alarm_stop(a);
      auto z = g.work.alarm(a);
      z.set<uint64_t>(4, b);
      z.set<uint64_t>(12, c);
      z.byte(1, d);
    } else
      g.fault = true;
    break;
  case 19:
  case 20:
  case 21:
    for (unsigned i = 0; i < 16; ++i)
      if (a & (1u << i)) {
        if (id == 19)
          write(reg(i) + 8, uint16_t(0x10000 - b), 2);
        else if (id == 20) {
          g.volumes[i] = b;
          uint32_t cnt = nds_spu_read(reg(i), 4);
          write(reg(i), (cnt & ~0x37Fu) | (b & 127) | ((c & 3) << 8), 4);
        } else {
          g.pans[i] = b;
          write(reg(i) + 2, g.masterpan >= 0 ? g.masterpan : b, 1);
        }
      }
    break;
  case 22:
    g.surround = a;
    for (unsigned i = 0; i < 16; ++i)
      if (i != 1 && i != 3) {
        unsigned pan = (nds_spu_read(reg(i), 4) >> 16) & 127,
                 vol = g.volumes[i];
        if (g.surround > 0 && pan < 24)
          vol = (int64_t(vol) *
                 (g.surround * (pan + 40) + ((32767 - g.surround) << 6))) >>
                21;
        else if (g.surround > 0 && pan > 104)
          vol = (int64_t(vol) *
                 (-g.surround * int(pan - 40) + ((g.surround + 32767) << 6))) >>
                21;
        write(reg(i), vol, 1);
      }
    break;
  case 23:
    write(0x04000500, a, 1);
    break;
  case 24:
    g.masterpan = int32_t(a);
    for (unsigned i = 0; i < 16; ++i)
      write(reg(i) + 2, g.masterpan >= 0 ? g.masterpan : g.pans[i], 1);
    break;
  case 25:
    write(0x04000501,
          (nds_spu_read(0x04000501, 1) & 128) | a | (b << 2) | (c << 4) |
              (d << 5),
          1);
    break;
  case 26:
    for (unsigned i = 0; i < 16; ++i)
      if ((a & ~g.stronglock) & (1u << i))
        kill(i, true);
    if (b & 1)
      g.weaklock |= a;
    else
      g.stronglock |= a;
    break;
  case 27:
    if (b & 1)
      g.weaklock &= ~a;
    else
      g.stronglock &= ~a;
    break;
  case 28:
    for (unsigned i = 0; i < 16; ++i)
      if ((a & ~g.stronglock) & (1u << i))
        kill(i, true);
    break;
  case 29:
    g.shared = a;
    break;
  case 30:
  case 31:
    for (unsigned i = 0; i < 16; ++i) {
      auto z = g.work.player(i);
      uint32_t bank = z.get<uint32_t>(p::Bank);
      bool invalid = bank >= a && bank <= b;
      if (id == 30) {
        invalid = false;
        for (unsigned j = 0; j < 16; ++j) {
          int tr = trackid(i, j);
          if (tr >= 0) {
            uint32_t pos = g.work.track(tr).get<uint32_t>(t::Position);
            invalid |= pos >= a && pos <= b;
          }
        }
      }
      if (invalid)
        stop_player(i);
    }
    break;
  case 32:
    for (unsigned i = 0; i < 16; ++i) {
      auto z = g.work.voice(i);
      uint32_t src = z.get<uint32_t>(v::Source);
      if (z.flag(v::Flags, 1) && z.byte(v::Type) == 0 && src >= a && src <= b)
        kill(i, true);
    }
    break;
  case 33: {
    memory(a, g.work.bytes.data(), WorkSize, true);
    for (unsigned i = 0; i < 16; ++i)
      store<uint32_t>(a + WorkSize + i * 4, nds_spu_read(reg(i), 4));
    store<uint32_t>(a + 0x11C0, g.profile.work);
    store<uint32_t>(a + 0x11C4, g.stronglock);
    break;
  }
  default:
    g.fault = true;
    break;
  }
}
bool list_valid(uint32_t ptr) {
  std::array<uint32_t, 6> q{};
  for (unsigned guard = 0; ptr && guard < 4096; ++guard) {
    if (!mainram(ptr, 24) || (ptr & 3) || !memory(ptr, q.data(), 24) ||
        q[1] > 33)
      return false;
    unsigned id = q[1], a = q[2];
    if ((id == 14 && (a & 65535) >= 16) ||
        ((id == 15 || id == 16) && a >= 16) || (id == 18 && a >= 8) ||
        (id == 33 && !mainram(a, 0x11C8)))
      return false;
    if (((id <= 6 || id == 8 || id == 9 || id == 10) && a >= 16) ||
        (id == 11 && a >= 16))
      return false;
    if (id == 6 && ((q[5] != 1 && q[5] != 2 && q[5] != 4) ||
                    q[3] > PlayerSize || q[5] > PlayerSize - q[3]))
      return false;
    if (id == 7) {
      unsigned size = a >> 24;
      if ((a & 0xFFFFFFu) >= 16 || (size != 1 && size != 2 && size != 4) ||
          q[4] > TrackSize || size > TrackSize - q[4])
        return false;
    }
    if (id == 10 && q[3] >= 16)
      return false;
    if (id == 29 && a && !mainram(a, 0x280))
      return false;
    if (q[0] == ptr)
      return false;
    ptr = q[0];
    if (!ptr)
      return true;
  }
  return !ptr;
}
void process() {
  for (unsigned n = 0; n < g.queued && !g.fault; ++n) {
    uint32_t ptr = g.queue[n];
    for (unsigned guard = 0; ptr && guard < 4096 && !g.fault; ++guard) {
      uint32_t q[6];
      if (!memory(ptr, q, sizeof q))
        break;
      command(q);
      ptr = q[0];
    }
    if (!g.fault && g.shared)
      store<uint32_t>(g.shared, load<uint32_t>(g.shared) + 1);
  }
  g.queued = 0;
}
void driver(bool tick) {
  sync_voices();
  process();
  sequence(tick);
  voices(tick);
  status();
  random16();
}
bool capture_active() {
  return ((nds_spu_read(0x04000508, 1) | nds_spu_read(0x04000509, 1)) & 128) !=
         0;
}
} // namespace

bool nds_offline_sound_configure(const char *sha1) {
  source_dirty = true;
  source_spans.clear();
  source_generation = Never;
  g = Sound{};
  g.alarm_deadlines.fill(Never);
  if (sha1 &&
      std::strcmp(sha1, "691e00d9a5dd80b04f80cc7559503e8b06848785") == 0) {
    g.profile = Mkds;
    g.profile_id = 1;
  } else if (sha1 &&
             std::strcmp(sha1, "90164d1ac127ee5f9815ea4ae7de798c7b5fc629") ==
                 0) {
    g.profile = Mph;
    g.profile_id = 2;
  }
  return g.profile_id != 0;
}
bool nds_offline_sound_ready() { return g.profile_id && !g.fault; }
bool nds_offline_sound_takeover(uint64_t now, uint64_t next) {
  const auto decline = [](const char *reason) {
    takeover_status = reason;
    return false;
  };
  if (!nds_offline_sound_ready() || next < now || next - now > DriverPeriod)
    return decline("driver phase not ready");
  if (g.active)
    return true;
  // Admission reads into a tentative semantic state. An early bootstrap
  // rejection must leave readiness, queues, source caches and retry intact.
  Sound candidate = g;
  candidate.now = now;
  candidate.next = next;
  candidate.alarm_deadlines.fill(Never);
  const auto read = [&](uint32_t addr, void *out, uint32_t size) {
    return nds_io_offline_memory(addr, static_cast<uint8_t *>(out), size, false,
                                 now);
  };
  if (!read(g.profile.work, candidate.work.bytes.data(), WorkSize) ||
      !read(g.profile.shared, &candidate.shared, 4) ||
      !read(g.profile.locks, &candidate.weaklock, 4) ||
      !read(g.profile.locks + 4, &candidate.stronglock, 4) ||
      !read(g.profile.pan, candidate.pans.data(), 16) ||
      !read(g.profile.volume, candidate.volumes.data(), 16) ||
      !read(g.profile.masterpan, &candidate.masterpan, 4) ||
      !read(g.profile.surround, &candidate.surround, 4) ||
      !read(g.profile.rng, &candidate.rng, 4))
    return decline("driver work unavailable");
  for (unsigned i = 0; i < 16; ++i) {
    auto a = candidate.work.voice(i);
    if (a.byte(v::Id) != i || a.byte(v::Type) > 2 ||
        a.byte(v::EnvelopeStage) > 3)
      return decline("voice initialization incomplete");
    if (a.get<uint32_t>(v::Callback) &&
        a.get<uint32_t>(v::Callback) != g.profile.callback)
      return decline("unknown voice callback");
  }
  for (unsigned i = 0; i < 8; ++i) {
    auto alarm = candidate.work.alarm(i);
    if (alarm.byte(0)) {
      const auto handler = alarm.get<uint32_t>(0x14);
      const auto expected = g.profile_id == 1 ? 0x037FF3D0u : 0x03802784u;
      if (handler == expected) {
        if (alarm.get<uint32_t>(0x18) != (i | (unsigned(alarm.byte(1)) << 8)))
          return decline("alarm callback identity mismatch");
        if (!nds_io_offline_tick_deadline(alarm.get<uint64_t>(0x20), now,
                                          &candidate.alarm_deadlines[i]))
          return decline("active alarm phase unavailable");
      } else if (handler || alarm.get<uint64_t>(12))
        return decline("unknown active alarm callback");
      // A fired one-shot retains the SDK active flag until cancellation, but
      // its embedded OSAlarm handler is null and it has no remaining event.
    }
  }
  NdsSpuSaveState hardware;
  if (!spu_savestate_export(&hardware))
    return decline("SPU state unavailable");
  for (unsigned i = 0; i < 16; ++i) {
    const auto &channel = hardware.channel[i];
    const uint64_t bytes = uint64_t(channel.loop) + channel.length;
    if (bytes > UINT32_MAX)
      return decline("SPU source extent overflow");
    candidate.sources[i] = channel.src;
    // The SPU snapshot has already converted register word counts to bytes.
    candidate.lengths[i] = uint32_t(bytes);
  }
  candidate.active = true;
  g = candidate;
  source_dirty = true;
  takeover_status = "active";
  return true;
}
const char *nds_offline_sound_takeover_status() { return takeover_status; }
std::string nds_offline_sound_json() {
  unsigned voices = 0, players = 0, tracks = 0;
  for (unsigned i = 0; i < 16; ++i) {
    voices += g.work.voice(i).flag(v::Flags, 1);
    players += g.work.player(i).flag(p::Flags, p::Active);
  }
  for (unsigned i = 0; i < 32; ++i)
    tracks += g.work.track(i).flag(t::Flags, t::Active);
  uint64_t fingerprint = 14695981039346656037ull;
  for (auto byte : g.work.bytes) {
    fingerprint ^= byte;
    fingerprint *= 1099511628211ull;
  }
  std::ostringstream out;
  out << "{\"ready\":" << (nds_offline_sound_ready() ? "true" : "false")
      << ",\"active\":" << (g.active ? "true" : "false")
      << ",\"fault\":" << (g.fault ? "true" : "false")
      << ",\"sleeping\":" << (g.sleeping ? "true" : "false")
      << ",\"now\":" << g.now << ",\"next\":" << g.next
      << ",\"queued\":" << unsigned(g.queued) << ",\"active_voices\":" << voices
      << ",\"active_players\":" << players << ",\"active_tracks\":" << tracks
      << ",\"work_fingerprint\":" << fingerprint << ",\"track_positions\":[";
  for (unsigned i = 0; i < 32; ++i) {
    if (i)
      out << ',';
    out << g.work.track(i).get<uint32_t>(t::Position);
  }
  out << "]}";
  return out.str();
}
void nds_offline_sound_reset() {
  source_dirty = true;
  source_spans.clear();
  source_generation = Never;
  auto profile = g.profile;
  auto id = g.profile_id;
  g = Sound{};
  g.profile = profile;
  g.profile_id = id;
  g.alarm_deadlines.fill(Never);
}
bool nds_offline_sound_preflight(uint32_t payload, bool error) {
  if (!nds_offline_sound_ready() || !g.active)
    return false;
  if (error)
    return true;
  if (payload < 0x02000000u)
    return true;
  const auto fault = g.fault;
  bool valid = list_valid(payload);
  g.fault = fault;
  return valid;
}
bool nds_offline_sound_can_receive(uint32_t payload, bool error) {
  return g.active && !g.fault && !g.sleeping &&
         (error || payload < 0x02000000u || g.queued < 8);
}
bool nds_offline_sound_receive(uint32_t payload, bool error, uint64_t now) {
  if (!nds_offline_sound_preflight(payload, error) ||
      !nds_offline_sound_can_receive(payload, error))
    return false;
  nds_offline_sound_advance(now);
  if (error)
    return true;
  if (payload >= 0x02000000u)
    g.queue[g.queued++] = payload;
  else if (!payload) {
    nds_tick_spu(now);
    g.in_advance = true;
    driver(false);
    g.in_advance = false;
  }
  return !g.fault;
}
uint64_t nds_offline_sound_next_event(uint64_t now) {
  if (!g.active || g.fault)
    return Never;
  uint64_t next = g.sleeping ? Never : g.next;
  const bool reply_room = nds_io_offline_reply_capacity(1);
  for (unsigned i = 0; i < 8; ++i)
    if (!g.sleeping && (g.alarm_deadlines[i] > now || reply_room))
      next = std::min(next, std::max(g.alarm_deadlines[i], now));
  if (capture_active())
    next = std::min(next, (now / 1024 + 1) * 1024);
  return next;
}
void nds_offline_sound_advance(uint64_t now) {
  if (!g.active || g.fault || g.in_advance)
    return;
  g.in_advance = true;
  if (g.sleeping) {
    nds_tick_spu(now);
    g.now = std::max(g.now, now);
    g.in_advance = false;
    return;
  }
  for (;;) {
    uint64_t next = g.next <= g.now ? now : g.next;
    const bool reply_room = nds_io_offline_reply_capacity(1);
    for (unsigned i = 0; i < 8; ++i)
      if (g.alarm_deadlines[i] > now || reply_room)
        next = std::min(
            next, g.alarm_deadlines[i] <= g.now ? now : g.alarm_deadlines[i]);
    if (next > now)
      break;
    nds_tick_spu(next);
    g.now = next;
    if (g.next <= next) {
      driver(true);
      g.next += ((next - g.next) / DriverPeriod + 1) * DriverPeriod;
    }
    for (unsigned i = 0; i < 8; ++i)
      if (g.alarm_deadlines[i] <= next) {
        if (!nds_io_offline_reply_capacity(1)) {
          // Keep the pending publication, but do not advertise it as an
          // expired CPU stop while ARM9 must run to drain the hardware FIFO.
          continue;
        }
        auto a = g.work.alarm(i);
        if (!nds_io_offline_reply(7u | ((i | (unsigned(a.byte(1)) << 8)) << 6),
                                  next))
          continue;
        uint64_t period = a.get<uint64_t>(12);
        if (period) {
          // The SDK reinserts a late periodic OSAlarm at its first future
          // phase after the callback returns. A blocked send is one callback,
          // not one reply for every period missed while the FIFO was full.
          const uint64_t cycles = period * 64;
          g.alarm_deadlines[i] +=
              ((next - g.alarm_deadlines[i]) / cycles + 1) * cycles;
        } else {
          g.alarm_deadlines[i] = Never;
          a.set<uint32_t>(0x14, 0);
        }
      }
    if (g.fault)
      break;
  }
  nds_tick_spu(now);
  g.now = now;
  g.in_advance = false;
}
void nds_offline_sound_sleep(bool entering, uint64_t now) {
  if (!g.active || g.fault || g.in_advance || bool(g.sleeping) == entering)
    return;
  if (entering) {
    nds_offline_sound_advance(now);
    g.sleeping = true;
  } else {
    // OSAlarm's periodic reinsertion chooses the first future occurrence of
    // its existing start/period after the delayed IRQ callback returns.
    nds_tick_spu(now);
    g.now = std::max(g.now, now);
    g.sleeping = false;
    nds_offline_sound_advance(now);
  }
}
bool nds_offline_sound_source_overlap(const uint8_t *physical, uint32_t width) {
  if (!g.active || !physical || !width)
    return false;
  if (source_dirty || source_generation != g_busf_mapping_generation) {
    source_spans.clear();
    for (unsigned i = 0; i < 16; ++i) {
      if (!g.sources[i] || !g.lengths[i] ||
          !(nds_spu_read(reg(i), 4) & 0x80000000u))
        continue;
      uint32_t addr = g.sources[i], remaining = g.lengths[i];
      while (remaining) {
        uint32_t size = std::min(remaining, 4096 - (addr & 4095));
        const uint8_t *data = nullptr;
        if (bus_device_resolve_ram(7, addr, size, &data)) {
          if (!source_spans.empty() &&
              source_spans.back().data + source_spans.back().size == data)
            source_spans.back().size += size;
          else
            source_spans.push_back({data, size});
        }
        addr += size;
        remaining -= size;
      }
    }
    source_dirty = false;
    source_generation = g_busf_mapping_generation;
  }
  const uintptr_t begin = reinterpret_cast<uintptr_t>(physical),
                  end = begin + width;
  for (const auto &span : source_spans) {
    uintptr_t a = reinterpret_cast<uintptr_t>(span.data);
    if (begin < a + span.size && end > a)
      return true;
  }
  return false;
}
void nds_offline_sound_before_write(const uint8_t *physical, uint32_t width,
                                    uint64_t now) {
  if (!g.in_advance && nds_offline_sound_source_overlap(physical, width))
    nds_offline_sound_advance(now);
}

// Fixed-size, explicitly versioned native semantic state. SPU is serialized by
// its existing owner; no CPU registers or SDK instruction stream are captured.
namespace {
struct Snapshot {
  uint32_t magic = 0x31444E53, version = 2;
  Sound sound;
};
} // namespace
bool nds_offline_sound_snapshot(std::vector<uint8_t> *out) {
  if (!out || g.in_advance)
    return false;
  Snapshot s{};
  s.sound = g;
  out->resize(sizeof s);
  std::memcpy(out->data(), &s, sizeof s);
  return true;
}
bool nds_offline_sound_validate(const std::vector<uint8_t> &in,
                                std::string *error) {
  auto fail = [&](const char *s) {
    if (error)
      *error = s;
    return false;
  };
  if (in.size() != sizeof(Snapshot))
    return fail("Invalid sound service state size");
  Snapshot s{};
  std::memcpy(&s, in.data(), sizeof s);
  if (s.magic != 0x31444E53 || s.version != 2 || s.sound.sleeping > 1 || s.sound.active > 1 ||
      s.sound.profile_id != g.profile_id ||
      std::memcmp(&s.sound.profile, &g.profile, sizeof(Profile)) ||
      s.sound.queued > 8 || s.sound.in_advance || s.sound.fault)
    return fail("Invalid sound service state identity");
  if (s.sound.active && (s.sound.next == Never ||
      (!s.sound.sleeping && s.sound.next < s.sound.now)))
    return fail("Invalid native sound driver deadline");
  for (unsigned i = 0; i < TrackCount; ++i)
    if (s.sound.work.track(i).byte(t::Depth) > 3)
      return fail("Invalid native sequence stack depth");
  for (unsigned i = 0; i < 16; ++i) {
    auto a = s.sound.work.voice(i);
    if (s.sound.active && (a.byte(v::Id) != i || a.byte(v::Type) > 2 ||
                           a.byte(v::EnvelopeStage) > 3))
      return fail("Invalid native voice record");
  }
  return true;
}
bool nds_offline_sound_import(const std::vector<uint8_t> &in,
                              std::string *error) {
  if (!nds_offline_sound_validate(in, error))
    return false;
  Snapshot s{};
  std::memcpy(&s, in.data(), sizeof s);
  g = s.sound;
  source_dirty = true;
  source_spans.clear();
  source_generation = Never;
  return true;
}
