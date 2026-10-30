# Prompt for the next session (written 2026-10-30 14:15, after the first measurement of the engine draw cache)

Paste the text between the lines into a new Claude Code session started in `C:\Users\Mubeen\Documents\Projects`.

---

Continue the Ghost of Yōtei fps work in ps5-porter: the engine draw cache ("cheaper commits") on branch
`porter/l3-cache`. Take autonomy; I check back later.

Read first: memory `yotei-p1-default-20261030` (loaded automatically), then in `yotei-work/REPORT-2026-10-30-p1-default.md`
the sections "Cheaper commits, first measurement", "Label-overwrite detector" and "Noon A/B of the final default",
then `yotei-work/porter/proposal-cheaper-commits-20261030.md` and, on porter/main, `porter/docs/l3-cache-results.md`
(rounds 1-11 of 2026-10-24; `git show porter/main:porter/docs/l3-cache-results.md`).

State:
- Play runtime `D:\Games\AnyPS5\PPSA26344-elem` runs porter/elements f9292db86 (driver hash 2D9F75D3ACC4): the
  GPU-ordered write-backs (default since 7583a9306) and the label-overwrite detector. I play it with
  `yotei-work/tools/play-elem.ps1`; it swaps my save in from `save-import/anyps5-player` and copies it back afterwards.
- Branch `porter/l3-cache` (head a566191fd, local only): porter/main's L3 engine draw cache and draw recipes
  (rounds 1-11, without round 10's SRT walker change 8c7304adb), `21ed02638` relocation of vertex stages whose fetch
  tables moved, runtime switches `APS5_PORTER_L3_NO_CACHE` and `APS5_PORTER_L3_NO_FETCH_RELOCATION`, the `[elem-l3]`
  round line (`7a4fe3742`), plans `porter/plans/l3w1.plan` (world A/B, unprofiled) and `l3p1.plan` (profiled).
  The cache is off unless `APS5_PORTER_L3_CACHE=1` with `APS5_PORTER_L3=1` and the l3marks hook table.
- Result of 2026-10-30 (l3w1, l3p1): the cache makes the world 3.5 % slower. Served for 79-81 % of engine draws,
  validated for 3 %; all hits are in the "other" kind (mostly data hits); DrawItem and DrawInstanced (28,000 of 36,000
  engine calls per 10 s) validate 0 %, and every validation miss is at stage 0. The most refused register is 0x8e:
  `[l3-refused]` shows word 2 of a vertex stage's user words is the base of an inline V# (`136f5bc0 00100020 00000013
  0004dfac`) whose address moves with the ring every frame. Logs: `yotei-work/runs-best/run-l3w1.err.log`,
  `run-l3p1.err.log`.

To do, in this order (both steps have my OK):
1. Find what fails stage-0 validation for DrawItem and DrawInstanced. Add a trace (for example
   `APS5_PORTER_L3_TRACE_MISSES=<n>`: the first n served misses per kind) that names the first run whose words
   differ: its address range, its base word (or none), the donor's and the current words at the first differing
   dwords, and what the run is (the per-draw constant block behind a relocated pointer, a fetch table, an inline V#,
   a fixed-address run). Check also why DrawItem and DrawInstanced get no stage data hits while "other" does. Then
   one profiled world run with the trace (a new plan, e.g. l3p2, scene `world-l3p` plus the trace variable). Report
   what differs. If it is per-frame data (matrices, time, wind) that no relocation can make equal, say what would
   be needed instead (the data-refresh path) and stop before step 2; otherwise go on.
2. Port round 10's walker change 8c7304adb (the SRT walker counts a user-data V# base as a relocatable base) onto
   `porter/l3-cache`. It changes the shader cache SourceVersion, so the l3 runs must use their own cache folder,
   for example `C:\AnyPS5-cache-l3\PPSA26344` (elem-run.ps1 `-Cache`; add a scene to
   `yotei-work/tools/p1-series-1030.ps1` that passes it). Never point a run of this build at
   `C:\AnyPS5-cache-best\PPSA26344`: that is my play cache (84 GB). The first run with the empty cache compiles for
   about 50 minutes, so give it `-TimeoutMin 90` and treat it as a warm-up, then repeat the A/B (l3w1 and l3p1
   again under new tags). Report per step: `yotei-work/tools/plan_gains.py`, the `[elem-l3]` sums per step, the
   `[l3-cache]` and `[l3-refused]` lines, `shots_compare.py` plus a look at the shots (glare, black foliage,
   missing or wrong objects). Record the new cache folder (and its undo) in `yotei-work/SYSTEM-CHANGES.md`.

How to build and run (learned on 2026-10-30):
- Build with `ninja -C build core/libs/libs` (plus the test targets you need). The runtime loads the NID-patched
  libraries from `build/core/libs/libs`; building only `libSceAgcDriver` leaves the old patched driver in place.
  Deploy with `porter/tools/make-runtime.ps1 -LibsOnly` (refuses while app.exe runs) and record the driver hash it
  prints in SYSTEM-CHANGES. Start series with `yotei-work/tools/p1-series-1030.ps1 -RunList "<tag>|<scene>" -Name
  <name>` detached (Win32_Process Create); scenes `world-l3` / `world-l3p` add the hooks and the cache variables.
- After the measurements put the play runtime back: `git switch porter/elements`, `ninja -C build core/libs/libs`,
  `make-runtime.ps1 -LibsOnly`, record it. Do that before I play.
- Line endings: these sources are LF. Python on Windows writes CRLF in text mode; open files with `newline=""`
  when a script edits them, or the commit rewrites whole files. Bash heredocs mangle backslashes: write scripts
  with the Write tool. Check `git show --stat` of every commit for whole-file churn.
- Tests: `porter/tools/p1-tests.sh <log>` (0 failures expected), `build/tests/agc_driver_draw_cache_relocation_tests.exe`,
  `agc_driver_mesh_draw_relocation_tests.exe`, `agc_driver_recorder_tests.exe --label-clobber-only`; run them from
  the repo root with `build/core/libs/libs/unpatched` and the MinGW bin first on PATH and no APS5_* variables.

Rules that still apply: everything stays local, take the shared locks before game runs, Ghidra jobs and builds
(`python -I yotei-work/engine/tools/lock.py`), prove claims with checkable evidence, install nothing without asking,
start game runs detached (Win32_Process Create), never build while a measurement run is going, no public posts or
PRs without my OK, personal git identity, do not push. Never put `switch APS5_LANE_SERIALIZE on` in a plan. Tell me
before a measurement run starts and before you install a build into the play runtime; do not build while I play
(it slows the game) unless I say so. Foreground `sleep` over a few minutes is blocked in Bash; wait with
run_in_background until-loops or Monitor. If I play: `yotei-work/tools/stall-watch.ps1 -GamePid 0` in the background
logs compile freezes to `runs-best/play-stalls.txt` and stops on a real hang; a hang with `label clobber` lines in the
play log names the write that overwrote a GPU label (the 11:05 hang of 2026-10-30).

Parked (do not start without my OK): the first-time shader compile freezes (which pipelines take minutes), the 8-9
refused tessellation hull shaders, the lease guard's sampling rate (1 in 16 costs about 6 points at the menu; keep it
until the detector has seen a few play sessions).

At the end: write a report in `yotei-work/` with numbers per step, update memory, rewrite this prompt file for the
session after.

---
