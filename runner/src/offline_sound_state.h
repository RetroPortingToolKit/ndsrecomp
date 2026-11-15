// SPDX-License-Identifier: MIT
// Packed SDK sound caller ABI. These are native semantic fields, not CPU state.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace nds_offline_sound {
inline constexpr unsigned VoiceCount = 16, PlayerCount = 16, TrackCount = 32,
                          AlarmCount = 8;
inline constexpr unsigned VoiceSize = 84, PlayerSize = 36, TrackSize = 64,
                          AlarmSize = 64;
inline constexpr unsigned PlayerOffset = VoiceCount * VoiceSize;
inline constexpr unsigned TrackOffset = PlayerOffset + PlayerCount * PlayerSize;
inline constexpr unsigned AlarmOffset = TrackOffset + TrackCount * TrackSize;
inline constexpr unsigned WorkSize = AlarmOffset + AlarmCount * AlarmSize;
static_assert(WorkSize == 0x1180);

struct View {
  uint8_t *bytes;
  template <class T> T get(unsigned offset) const {
    static_assert(std::is_integral_v<T>);
    T value{};
    std::memcpy(&value, bytes + offset, sizeof(value));
    return value;
  }
  template <class T> void set(unsigned offset, T value) const {
    static_assert(std::is_integral_v<T>);
    std::memcpy(bytes + offset, &value, sizeof(value));
  }
  uint8_t byte(unsigned offset) const { return bytes[offset]; }
  void byte(unsigned offset, uint8_t value) const { bytes[offset] = value; }
  bool flag(unsigned offset, uint8_t mask) const {
    return (bytes[offset] & mask) != 0;
  }
  void flag(unsigned offset, uint8_t mask, bool value) const {
    bytes[offset] = value ? bytes[offset] | mask : bytes[offset] & ~mask;
  }
};
namespace player {
enum : unsigned {
  Flags = 0,
  Id = 1,
  Priority = 4,
  Volume = 5,
  ExtraVolume = 6,
  Tracks = 8,
  Tempo = 24,
  TempoRatio = 26,
  TempoCounter = 28,
  Bank = 32
};
inline constexpr uint8_t Active = 1, Started = 2, Paused = 4;
} // namespace player
namespace track {
enum : unsigned {
  Flags = 0,
  PanRange = 1,
  Program = 2,
  Volume = 4,
  Expression = 5,
  PitchBend = 6,
  BendRange = 7,
  Pan = 8,
  ExtraPan = 9,
  ExtraVolume = 10,
  ExtraPitch = 12,
  Attack = 14,
  Decay = 15,
  Sustain = 16,
  Release = 17,
  Priority = 18,
  Transpose = 19,
  PortamentoKey = 20,
  PortamentoTime = 21,
  SweepPitch = 22,
  Lfo = 24,
  ChannelMask = 30,
  Wait = 32,
  Base = 36,
  Position = 40,
  Calls = 44,
  Loops = 56,
  Depth = 59,
  Voices = 60
};
inline constexpr uint8_t Active = 1, NoteWait = 2, Mute = 4, Tie = 8,
                         NoteFinishWait = 16, Portamento = 32, Compare = 64;
} // namespace track
namespace voice {
enum : unsigned {
  Id = 0,
  Type = 1,
  EnvelopeStage = 2,
  Flags = 3,
  PanRange = 4,
  RootKey = 5,
  ExtraVolume = 6,
  Key = 8,
  Velocity = 9,
  InitialPan = 10,
  UserPan = 11,
  UserVolume = 12,
  UserPitch = 14,
  Envelope = 16,
  SweepCounter = 20,
  SweepLength = 24,
  Attack = 28,
  Sustain = 29,
  Decay = 30,
  Release = 32,
  Priority = 34,
  Pan = 35,
  Volume = 36,
  Timer = 38,
  Lfo = 40,
  SweepPitch = 50,
  Length = 52,
  Wave = 56,
  Source = 68,
  Callback = 72,
  CallbackArg = 76,
  Next = 80
};
}
struct Work {
  std::array<uint8_t, WorkSize> bytes{};
  View voice(unsigned n) { return {bytes.data() + n * VoiceSize}; }
  View player(unsigned n) {
    return {bytes.data() + PlayerOffset + n * PlayerSize};
  }
  View track(unsigned n) {
    return {bytes.data() + TrackOffset + n * TrackSize};
  }
  View alarm(unsigned n) {
    return {bytes.data() + AlarmOffset + n * AlarmSize};
  }
};
} // namespace nds_offline_sound
