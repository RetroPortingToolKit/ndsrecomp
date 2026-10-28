#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>

// Portable, resumable LZ77 chunk job. Continuations are semantic boundaries:
// input flags/tokens, overlapping backreferences, and completion. All state
// belongs to the guest frame; no host coroutine survives a scheduler boundary.
namespace nds_lz {
enum class Phase { Output, Token, Copy, AfterToken, Flag, Finish };
struct State {
    uint32_t input, available, output, remaining, flags, bits, token, pending;
    uint32_t scratch9, scratch10, cpsr;
    Phase phase;
};
inline void sub_flags(State& s, uint32_t a, uint32_t b) {
    const uint32_t value = a - b;
    s.cpsr = (s.cpsr & 0x0fffffffu) | (value & 0x80000000u) |
        (value == 0 ? 0x40000000u : 0u) | (a >= b ? 0x20000000u : 0u) |
        (((a ^ b) & (a ^ value) & 0x80000000u) ? 0x10000000u : 0u);
}
// Timing is the explicitly identified native-service profile: two CPU cycles
// per eliminated ARM instruction, including native byte exchanges. Never cross
// the supplied rendezvous. A short budget leaves the original guest PC intact.
template<class Memory> uint32_t run(State& s, Memory& m, uint32_t budget) {
    uint32_t used = 0;
    auto room = [&](uint32_t cycles) { return cycles <= budget - used; };
    while (s.phase != Phase::Finish) {
        switch (s.phase) {
        case Phase::Output: {
            const bool finished = std::bit_cast<int32_t>(s.remaining) <= 0;
            const uint32_t cost = finished ? 4u : 8u;
            if (!room(cost)) return used;
            used += cost; sub_flags(s, s.remaining, 0);
            if (finished) { s.phase = Phase::Finish; break; }
            sub_flags(s, s.bits, 0);
            s.phase = s.bits ? Phase::Token : Phase::Flag;
            break;
        }
        case Phase::Flag:
            if (!room(s.available ? 12u : 4u)) return used;
            sub_flags(s, s.available, 0);
            if (!s.available) { used += 4; s.phase = Phase::Finish; break; }
            used += 12; s.flags = m.input(s.input++); --s.available;
            s.bits = 8; s.phase = Phase::Output;
            break;
        case Phase::Token: {
            if (!s.available) {
                if (!room(4)) return used;
                used += 4; sub_flags(s, 0, 0); s.phase = Phase::Finish; break;
            }
            const bool compressed = (s.flags & 0x80u) != 0;
            const uint32_t cost = !compressed ? 20u :
                (s.pending ? 36u : (s.available == 1 ? 22u : 42u));
            if (!room(cost)) return used;
            used += cost;
            sub_flags(s, s.available, 0);
            s.cpsr = (s.cpsr & 0x3fffffffu) | (compressed ? 0u : 0x40000000u);
            if (!compressed) {
                const auto value = m.input(s.input++); --s.available;
                --s.remaining; s.scratch9 = m.output(s.output);
                m.write(s.output++, value); s.phase = Phase::AfterToken;
                break;
            }
            if (!s.pending) {
                s.token = m.input(s.input++); s.pending = 1; --s.available;
            }
            sub_flags(s, s.available, 0);
            if (!s.available) { s.phase = Phase::Finish; break; }
            s.scratch9 = ((s.token & 15u) << 8) | m.input(s.input++);
            ++s.scratch9; --s.available; s.pending = 0; s.scratch10 = 3;
            s.token = 3u + (s.token >> 4);
            s.cpsr &= 0x0fffffffu; // ADDS 3 + [0,15]: NZCV are all clear.
            s.phase = Phase::Copy;
            break;
        }
        case Phase::Copy: {
            const uint32_t count = std::min(s.token, (budget - used) / 12u);
            if (!count) return used;
            // Deliberately forward, byte by byte: distance-one and all other
            // overlapping backreferences consume bytes produced by this job.
            for (uint32_t i = 0; i < count; ++i) {
                const auto value = m.output(s.output - s.scratch9);
                --s.remaining; s.scratch10 = m.output(s.output);
                m.write(s.output++, value);
            }
            sub_flags(s, s.token - count + 1u, 1u);
            s.token -= count; used += count * 12u;
            if (!s.token) s.phase = Phase::AfterToken;
            break;
        }
        case Phase::AfterToken:
            if (!room(s.remaining ? 10u : 4u)) return used;
            sub_flags(s, s.remaining, 0);
            if (!s.remaining) { used += 4; s.phase = Phase::Finish; break; }
            used += 10; s.flags <<= 1;
            sub_flags(s, s.bits, 1); --s.bits;
            s.phase = s.bits ? Phase::Token : Phase::Flag;
            break;
        case Phase::Finish: break;
        }
    }
    return used;
}
} // namespace nds_lz
