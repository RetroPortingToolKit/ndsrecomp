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

## Owner clarification: serial independent validation (2026-10-28)

Continue the complete28-entry NDS burndown serially. Keep one item active.
Spawn one subagent for that item whose entire job is independent validation,
not implementation. Resolve its findings. On success, file a PR; on failure
or final rejection, record the disposition and evidence in central Beads.
The validator must verify that handoff and explicitly instruct the primary
agent to move to the next named item. Do not self-advance around this gate.
Keep related changes together within the active item, reuse completed evidence
and do not repeat accepted gameplay or create benchmark matrices. Existing
resolved entries keep their final dispositions. PR creation does not authorize
merge, Nextcloud delivery or an interim owner playtest; final combined practical
review follows all candidate decisions. This explicitly authorizes the one
validation subagent per item for this campaign.
