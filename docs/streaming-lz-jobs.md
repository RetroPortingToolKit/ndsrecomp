# Streaming LZ asset jobs

Owning issue `beads-yjp.105`; optimization campaign `beads-2wy8.1`.
This is a candidate, not an adopted performance claim.

The MKDS AMCE0 loading census identifies 1,564,781 byte-exchange reference
stops inside one complete 256-byte streaming LZ77 routine: 80.8% of ARM9
reference stops in that retained load. This establishes a job worth replacing,
not its host CPU share. The exact supported ROM and SHA1 of the whole routine
gate binding; no guest instruction payload is distributed with the service.

One short caller capture observes a 1,024-byte compressed input chunk, a
mirrored main-RAM state block, an output buffer, and a Thumb return address.
The native job implements flag groups, literals, overlapping 12-bit-distance
backreferences, partial compressed tokens, chunk exhaustion and completion.
It retains all registers, CPSR, the original 28-byte guest frame and context
writes, including otherwise dead saved-frame bytes. Original guest PCs carry
continuations; current-build save/load, IRQs and context switches need no new
host coroutine or serializer. A whole-operation proof owns every continuation,
even if an older generated bank does not expose that exact split point.

The identified `stream-lz-native-v1` build profile charges two CPU cycles per
eliminated ARM instruction, including native byte exchanges, bounded by the
published scheduler rendezvous. This differs from reference-floor byte-exchange
timing. It is explicitly part of the build/compatibility identity; no cross-build
or mixed-profile netplay qualification is claimed. Work stops at original
token/copy boundaries and materializes the corresponding exact guest state.

Direct RAM windows use the bus's physical mirrors and TCM priority. Every output
span is published through the existing physical generation/provenance interface
before scheduling, IRQ delivery, ARM7 access or cache lookup. Backreferences
copy forward to preserve overlap. Input/output aliases remain live. Writes to
the active code, wrap/remap/device ranges, invalid continuations, tracing and
insufficient budgets use the existing reference floor and normal dispatch.
No decompressor-specific interpreter is added. The faithful execution model
uses that existing path, and `NDS_MKDS_STREAM_IMPLEMENTATION=LLE` removes the
service/barriers entirely. The new build choice remains LLE by default pending
measurement and practical qualification. Other games receive no decoder binding.

Focused checks include authored split-token/distance-one cases and 60 cases
against the original private ARM image. The latter compare every materialized
boundary, all registers/CPSR and complete caller memory, including mirror,
input/output, context and saved-stack aliases and short/large scheduling budgets.
The private fixture stays outside source control. Built-game measurement and
basic visual inspection remain required before adoption.
