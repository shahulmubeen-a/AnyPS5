# Prompt for the next session (written 2026-10-30 17:30, after the engine draw cache miss trace and round 10's walker change)

Paste the text between the lines into a new Claude Code session started in `C:\Users\Mubeen\Documents\Projects`.

---

Continue the Ghost of Yōtei fps work in ps5-porter. The engine draw cache ("cheaper commits", branch `porter/l3-cache`)
is at a decision point; I decide what comes next (see "Options" below). Take autonomy within what I chose.

Read first: memory `yotei-l3-trace-20261030` (loaded automatically), then `yotei-work/REPORT-2026-10-30-l3-trace.md`
(the whole file; it follows "Cheaper commits, first measurement" in `REPORT-2026-10-30-p1-default.md`).

State:
- Play runtime `D:\Games\AnyPS5\PPSA26344-elem` is back on porter/elements (code of f9292db86, built at c4700a70b:
  driver 4E20AA2F2181, deployed 2026-10-30 17:10): the GPU-ordered write-backs and the label-overwrite detector. I play it with
  `yotei-work/tools/play-elem.ps1` (it swaps my save in from `save-import/anyps5-player` and copies it back).
- Branch `porter/l3-cache` (head 452104a35, local only): the engine draw cache (off unless `APS5_PORTER_L3_CACHE=1` with
  `APS5_PORTER_L3=1` and the l3marks hooks) plus today's work: the miss trace `APS5_PORTER_L3_TRACE_MISSES=<n>`
  (c96ad3480, 2377ab1c6, 8ac725c62: classes per kind in an `[l3-miss]` line, the first n misses per kind and window
  with their words, a followed-pointer check for fixed-address runs), round 10's SRT walker change (1a58a0a55, from
  porter/main 8c7304adb: a user-data V#'s base word is a relocatable base; shader cache SourceVersion
  4331c3025cbe8dc4) and its rendering fix 40bf8d8e2 (a relocated copy binds a moved user-data V# at its current words;
  without it the menu showed speckles and a sharp background, `yotei-work/runs-best/l3-menu-crops-20261030.png`).
  porter/main's 8c7304adb has the same gap and no fix.
- Shader cache of that branch: `C:\AnyPS5-cache-l3\PPSA26344` (14 GB, warm for the boot, the menu and HOME). Runs of
  porter/l3-cache must use it (`yotei-work/tools/p1-series-1030.ps1` scenes `world-l3c` and `world-l3tc`), never my
  play cache `C:\AnyPS5-cache-best`. Tool: `yotei-work/tools/l3_steps.py <err.log> --plan <plan>` (per-step
  `[elem-l3]`, `[l3-cache]`, `[l3-miss]` sums).
- Results of 2026-10-30 afternoon: (1) before the walker change, DrawItem and DrawInstanced missed at stage 0 because
  the relocation refused a moved inline V# in the user words (56 % of the misses), on DrawInstanced's instance data
  behind a pointer in the per-draw block (31 %) and on V# tables whose bases moved with their table (7 %); per-frame
  values were 1 %. (2) With the walker change the refusals are gone (-99 %), but validated hits stay at 3.3 % of
  engine calls (all "other" draws; item 39 per 10 s, instanced 0) and the world is 7.5 % slower with the cache
  (l3w2 without the fix: -9.6 %; l3w1 before the walker change: -3.5 %). What is left: pointers kept in the per-draw
  block to other per-frame allocations, which the relocation does not follow (DrawInstanced 49 % and DrawItem 19 % of
  the misses), and V# bases of per-frame buffers behind them (DrawInstanced's instance V#: only dword 0 differs).

Options (I pick one; nothing is started):
(a) Nested relocation plus a V# base refresh (driver only, same shader cache): runs read through a pointer kept in a
    run are re-based on that pointer (relocateDonor reads the pointer at its relocated place and shifts them; the
    pointer words are taken at their current value), and a differing V# dword 0 of a read-only GuestBuffers element
    whose stride, records and format are equal is accepted like a data position and patched into the compiled
    descriptor (the bufferPatches path of 40bf8d8e2) so Graphics re-points the buffer. Then l3p (trace, `world-l3tc`)
    for the hit share per kind and a world A/B with base steps; check the menu frames as on 2026-10-30.
(b) Stop the engine draw cache here (it stays off) and go back to the committer by other means.
(c) Something else.

How to build and run (learned on 2026-10-30):
- Build with `ninja -C build core/libs/libs` (plus test targets). The runtime loads the NID-patched libraries from
  `build/core/libs/libs`. Deploy with `porter/tools/make-runtime.ps1 -LibsOnly` (refuses while app.exe runs) and
  record the driver hash it prints in SYSTEM-CHANGES. Rebuild once after committing, so the driver's revision string
  names the commit. Start series with `yotei-work/tools/p1-series-1030.ps1 -RunList "<tag>|<scene>" -Name <name>`
  detached (Win32_Process Create).
- elem-run.ps1 does not watch the game during its 450 s wait before the plan: after "continue pressed" check that
  app.exe still runs (the known game crash in app.exe+0x113291f's callee hit l3p2 25 s after CONTINUE).
- A build of porter/l3-cache changes the shader cache SourceVersion only through the walker files; driver-only changes
  keep using the warm `C:\AnyPS5-cache-l3`.
- After the measurements put the play runtime back: `git switch porter/elements`, `ninja -C build core/libs/libs`,
  `make-runtime.ps1 -LibsOnly`, record it. Do that before I play.
- Line endings: these sources are LF. Python on Windows writes CRLF in text mode; open files with `newline=""`.
  Bash heredocs mangle backslashes: write scripts with the Write tool. Check `git show --stat` of every commit.
- Tests: `porter/tools/p1-tests.sh <log>` (0 failures expected), `build/tests/agc_driver_draw_cache_relocation_tests.exe`
  (also with `APS5_PORTER_L3_CACHE=1`), `agc_driver_mesh_draw_relocation_tests.exe`,
  `agc_driver_recorder_tests.exe --label-clobber-only`; from the repo root with `build/core/libs/libs/unpatched` and
  the MinGW bin first on PATH, `ANYPS5_NO_SHADER_CACHE=1` and no other APS5_* variables. The test executables load the
  driver DLL from `unpatched`, so a driver rebuild is enough.

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
