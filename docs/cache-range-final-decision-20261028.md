# Final cache-range service decision

Owner `beads-yjp.106`, campaign `beads-2wy8.1`.

Reject the standalone MKDS invalidate-range, clean-range and drain/clean-
invalidate-range HLE proposal for this campaign. This replaces its earlier
held status with a final decision. Keep the normal implementation. Do not merge
the private `perf/cache-maintenance-jobs-20261028` experiment into the selected
package. No new timing or gameplay run is needed for this reconciliation.

The proposal was evaluated as three complete functions, including closed-form
range advance, physical-write publication, original register/flag/return state,
bounded event continuations and exact code proofs. It was not just an MCR
opcode substitution. The real CP15 model already treats the selected cache
operations as transparent; it does not maintain a second host cache to remove.
Pending writes already have an authoritative publication path.

Correctness evidence is good:216 original-ARM cases,7,533 exact boundaries,
four focused tests, current-build restore and a basic image inspection pass.
179 cases finish;37 deliberately bound huge/wrapping prefixes. No introduced
correctness defect or reproducible regression is claimed.

The retained180-frame natural-loading pair uses the exact same SDK/banks and
compute4x/AA8 settings, on one local host:

| Metric | Baseline | Range jobs |
| --- | ---: | ---: |
| Emulation-thread cycles |9,695,623,890 |9,477,776,540 |
| Mean paced frame |16.776ms |16.712ms |
| Maximum whole frame |24.474ms |24.899ms |
| Background logical CPU cores |9.275 |7.011 |

The2.25% cycle observation is neutral/inconclusive under unequal load; it is
not evidence of a useful loading or throughput gain. The reduction of about
200,000 reference stops and the96,966 serviced cache lines are structural
counts, not a runtime win. No historical passing gate is fabricated.

The architectural alternative was reviewed against the source implementation:
these separate wrappers still need code/provenance resolution, short deadline
continuations, dispatch/return and publication checks. They do not consolidate
the producer's copy/decode/resource-publication ownership. A dedicated cache
HLE profile adds another guest simulation identity for no demonstrated useful
benefit in this sample. The campaign therefore rejects this standalone package
on benefit versus its runtime/service contract, rather than repeatedly tuning
or waiting indefinitely for a small signal to become a win.

This is a decision not to adopt these functions, not proof that they can never
help another workload. It does not reject a complete asset-construction service
that absorbs its cache/publication contract, or the wider CPU/device/scheduling
work. Areas04/09/10/13 remain open. It also does not reopen the separately
resolved BIOS service evaluation.

Experiment source `6f381a795ecb338c374d8bca8831c43f4993270e`, baseline
`993ce7073fb9c0c787b6b1f974a0006a69194226`. Candidate executable SHA-256
`da78f55acf8a070694722de92696ece4d8ba3f1cb5c1e5c7183bb224a06634f7`;
baseline `751b7513f2de97ce7ae2716a8cefc080a5d6d9491b5c44198619b7f2b742eec3`.

Unchanged raw records and the new machine-readable decision are under
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches`:
`cache-maintenance-job-evidence.json`, `cache-job-local-pair`, and
`cache-range-final-decision.json`. Preserve source, raw samples and exact
binaries during the campaign; remove disposable build scratch when no longer
needed. Nextcloud and the internal checkpoint archives remain unchanged.
