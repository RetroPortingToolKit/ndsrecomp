# Offline ARM7 services

The supported enhanced MKDS and MPH builds can replace their offline ARM7
drivers after a verified boot handoff. `NDS_OFFLINE_ARM7` is a build option;
selection is fixed before boot, requires native geometry and the ARM9
observation service, and excludes Faithful, network, WFC, local wireless and
replay profiles. The separate reference build retains original execution.

ARM7 executes normally during initialization. Handoff requires a stopped
processor without a pending IRQ wake, DMA, IPC request, private driver task,
or unsupported wireless transition. Native services own firmware, RTC, touch,
sound, power, microphone, card/filesystem and supported wireless shutdown
requests. ARM9 then runs to real display, timer and native-service deadlines
instead of fixed ARM9/ARM7 lockstep rounds. Existing replies remain in the
canonical FIFO. Online and wireless profiles keep their original ARM7 path.

## Connected profiles

The offline driver implements no wireless manager, WFC or local-wireless
services, so `--network on`, `--wfc on`, `--local-wireless on` and the replay
backend never select it. Connectivity changes nothing else in the performance
model: the original ARM7 driver runs through native ARM7 execution on the
normal scheduler, beside native ARM9, native geometry, title HLE and the
whole-asset service. Whole assets are ARM9-only file operations that already
ran beside the original ARM7 driver during the offline bootstrap; their
publication still completes old PCM before mutating it. Selection is
`nds_service_profile_select` (`runner/src/execution_model.h`), fixed before
boot and reported on stderr and in the execution identity. `NDS_OFFLINE_ARM7=0`
and `NDS_WHOLE_ASSETS=0` remain independent diagnostic switches.

Peers interact only through guest protocols: the emulated radio (local
wireless) or Nintendo WFC servers, where real DS units also participate. No
host simulation state is exchanged, so no host capability negotiation applies;
the requirement is guest-correct behaviour of each profile.

Sound adopts the SDK's live players, tracks, voices, alarms and pending
eight-slot command queue atomically, preserving FIFO order and one completion
per command list. The production SPU still synthesizes PCM. Before any writer
changes live PCM memory, the old sound span completes through the mutation
time. This applies to generated banks, translated stores, buses, DMA and live
compiler outputs. The bank header hash and live-shard identity reject code
without that pre-mutation contract. Unsupported or malformed ownership
declines handoff without partially modifying the guest or native driver.

The fixed offline profile never resumes the old ARM7 driver. Its private SDK
queue header remains frozen after ownership transfers; native queue ownership
and pending heads are serialized. Current-build state load restores the driver
and deadlines. Warm reset restores the boot profile, persistent firmware and
native save data, then performs a fresh handoff. Old snapshot compatibility is
not provided.

## Windows qualification

One matched hidden normal-frontend, unpaced pair per game compared the accepted
ARM9 observation package with the complete offline package. Guest audio was
retained and output discarded; diagnostics and presentation settings matched.
Each package had its own current-build fixture. These shared-host samples
measure complete packages, including the write fences and scheduler costs.

| Game | CPU ms/frontend frame | Wall ms/frontend frame | ARM7 guest instructions/frame |
| --- | --- | --- | --- |
| MKDS | 6.571 to 6.327 (-3.7%) | 4.789 to 4.197 (-12.4%) | 45,273 to 0 |
| MPH | 6.431 to 5.536 (-13.9%) | 4.659 to 4.690 (+0.7%, neutral) | 21,922 to 0 |

The MKDS CPU change is small and is not a general speedup guarantee. Internal
instruction/draw trajectories vary. The combined ARM7/sound/service family
fell 47.7% in MKDS and 40.3% in MPH; ARM9 execution-family cost increased 7.7%
and 14.1%, respectively. Scheduler switches and fixed-round bookkeeping fell
to zero during native ownership. Sampled exclusive costs use actual
round/sample deltas; the old scheduler SPU field omits nested native SPU work
and must not be used alone to claim a sound saving.

The production-SPU component passes 524 checks across both SDK profiles,
including pending queue retirement, wrapped FIFO order, state import and
atomic rejection. Focused video-state, ARM9, ARM7, execution-profile and sound
tests pass. Fresh retail startup, active PCM, continuing ARM9 execution with
zero ARM7 instructions, and current-build scene/pixel restore were checked.
The basic image inspection passes. Paused startup controls cover default,
explicit disable, Faithful, network and local-wireless selection.

Only Windows delivery is qualified here. No new host assembly or platform
specific runtime dependency was added; the existing portable reference build
remains available. Human audio, touch, native-save and power review remains
part of the final combined review before integration or release.
