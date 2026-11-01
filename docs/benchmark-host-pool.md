# Benchmark host pool

Owner request2026-10-28; `beads-2wy8.2`, optimization campaign `beads-2wy8.1`.

Use this PC, SERVER or Steam Deck when the selected workload has enough measured
headroom. Another game, compiler or service is not an automatic rejection.
The pool does not stop owner work or require a quiet machine.

The shared installation is `C:/Users/Matthew/.local/bin/benchmark_hosts.py` with
its sibling agent and registry. Maintained source lives in `tools/benchmark_hosts*`.
Use the installed Python interpreter explicitly from PowerShell:

```powershell
& 'C:\Users\Matthew\.pyenv\pyenv-win\versions\3.12.5\python.exe' `
  'C:\Users\Matthew\.local\bin\benchmark_hosts.py' probe
```

The registry has three physical targets: `local`, `server`, `steamdeck`.
`steamdeck-mph-wifi` and `steamdeck-mph` are alternate SSH routes to the same
Deck and ledger. Wi-Fi is first because it was reachable during setup; the
wired route timed out. Existing SSH credentials/strict host-key checks remain.
SERVER uses its existing `C:/Python311/python.exe`; no remote runtime is installed.

`run` reserves CPU, GPU and RAM budgets on the selected host. Auto selection
rotates compatible targets and tries another if headroom is insufficient.
Use `--os windows` for an unqualified Windows-only executable; the Deck needs
a runnable Linux build or an actually working compatible runtime. Registration
does not establish game/backend/platform qualification. Keep each complete
matched A/B pair on one host, with one configuration; do not turn this pool into
a three-host test matrix.

Default requests are1.5 physical core equivalents,20% GPU capacity,2GiB RAM and
1GiB workspace space. Declare appropriate costs for the actual workload. The
probe leaves margins of0.25 cores,5% GPU,0.5GiB RAM and1GiB storage. GPU sampling
uses NVIDIA utilization when supported, WDDM aggregate engine counters as a
Windows fallback, and DRM busy counters on Deck. Unknown GPU telemetry rejects
graphics jobs; CPU-only work may explicitly request `--gpu 0`.

Reservations are capacity claims, not whole-machine exclusive locks. A short
host-local ledger lock makes admission atomic. Pending budgets are subtracted
from measured free capacity. Running work is already in the measured load and
is not charged twice; total declared claims still have to fit peak capacity.
Every live lease agent holds an individual OS file lock. EOF/normal release
removes its record; a dead agent's unlocked protocol-owned record can be cleaned
on the next admission. No age-based force unlock or unrelated process kill is
used. Existing legacy lock files are preserved. Those files are historical
coordination signals, not evidence that all remaining hardware capacity is gone.

For exact arguments on Windows, put `argv` and optional `env` in a JSON job file:

```json
{"argv":["C:/Python311/python.exe","G:/Temp/Codex/task/pair.py"],"env":{"SDL_AUDIODRIVER":"dummy"}}
```

```powershell
& 'C:\Users\Matthew\.pyenv\pyenv-win\versions\3.12.5\python.exe' `
  'C:\Users\Matthew\.local\bin\benchmark_hosts.py' run --os windows `
  --owner beads-2wy8.2 --cpu 1.5 --gpu 20 --memory 2 `
  --out 'G:\Temp\Codex\task\reservation.json' `
  --job-file 'G:\Temp\Codex\task\job.json'
```

The command executes on the selected host; stage and verify its inputs there
first. Remote Windows argv/env are base64 encoded and passed to Python's direct
process API, avoiding PowerShell5 quote loss and cmd file-operation fallbacks.
The agent source travels through SSH stdin, not a giant Windows command line.
All agent helpers are hidden; workload audio/window policy remains the caller's
responsibility. Use the game's debug interface and dummy/zero output for
unattended samples. No mouse control is needed.

Python harnesses can use `Pool.reserve(...)` as a context manager, mark it
`running()` when work begins, and keep the whole pair inside that context.
`lease.probe()` supplies a fresh reading between legs or after the interval.
Save before/after receipts and raw per-game costs. Headroom permits the work;
it does not prove a speedup or erase clock/load variation. Historical failed
quiet-host gates remain unchanged.

Validation: focused accounting/OS-lock tests; real Deck concurrent leases,
over-capacity rejection and release; SERVER exact quoted argv/env execution;
live probes of all three hosts. Only small dummy commands were used to validate
the reservation mechanism, with no extra game or performance matrix.
