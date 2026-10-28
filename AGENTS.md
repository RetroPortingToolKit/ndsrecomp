# Project validation policy

The user has set a permanent validation policy for this workspace:

- Do not run performance or validation matrices for routine validation or releases.
- Do not run multi-route, multi-repetition, cold/warm, or per-platform benchmark legs for routine validation or releases.
- Gameplay validation is limited to one multiplayer run and one campaign run total unless the user explicitly requests another run.
- Existing user-confirmed manual runs count toward that limit; do not rerun them to satisfy internal process preferences.
- Build, packaging, artifact integrity, bank inventory, cache identity, license, and private-payload checks are still allowed because they are artifact checks, not gameplay validation runs.
- Do not fabricate passing performance-gate JSON. If old gate files exist, treat them as historical diagnostics only.

## Owner clarification: finish candidate evaluation before the next release (2026-10-28)

For the current NDS subsystem optimization campaign, evaluate every remaining
identified candidate in docs/subsystem-performance-burndown.md before the next
Nextcloud release. Each candidate needs a final disposition: implemented and
validated, or rejected with evidence and a reason. Held, deferred evaluation,
unmeasured and inconclusive candidates remain open. Evaluate complete services
and ownership pipelines; one rejected local helper does not exhaust a subsystem.

Work serially and bundle related improvements. Reuse existing evidence and valid
checks, preserving the per-game no-regression rule and bounded profiling policy.
The current v0.0.11 package is an internal checkpoint. Continue engineering rather
than preparing interim release handoffs. Final combined practical review and
one-current-archive delivery follow completion of the candidate decisions.
Preserve the owner's Deferred review; do not prompt for or repeat an interim
playtest. Player-speed-under-load remains separately deferred.
