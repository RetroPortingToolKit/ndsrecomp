# Project validation policy

The user has set a permanent validation policy for this workspace:

- Do not run performance or validation matrices for routine validation or releases.
- Do not run multi-route, multi-repetition, cold/warm, or per-platform benchmark legs for routine validation or releases.
- Gameplay validation is limited to one multiplayer run and one campaign run total unless the user explicitly requests another run.
- Existing user-confirmed manual runs count toward that limit; do not rerun them to satisfy internal process preferences.
- Build, packaging, artifact integrity, bank inventory, cache identity, license, and private-payload checks are still allowed because they are artifact checks, not gameplay validation runs.
- Do not fabricate passing performance-gate JSON. If old gate files exist, treat them as historical diagnostics only.

## Owner clarification: bounded HLE profiling (2026-10-06)

The owner clarified that the restriction above was intended to stop excessive
benchmark matrices (for example, roughly 90 runs), and requested subsampling
for the current HLE investigation. Small, purpose-driven samples of different
game workloads are authorized for this work. This is not a requirement to run
every game, route, backend, platform, or cold/warm combination.

Reuse existing useful profiles and manual gameplay evidence. State a small run
budget before collecting fresh profiles, use one configuration per sampled
game, and expand only when a specific unresolved finding warrants it. Separate
bottleneck discovery from later LLE/HLE performance and gameplay qualification;
a short profile does not prove compatibility or absence of softlocks. Routine
release validation still must not trigger a matrix or repeat completed campaign
and multiplayer validation just to satisfy an internal checklist.
