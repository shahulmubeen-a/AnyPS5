# Prompt for the next session (written 2026-10-30, after the GPU-ordered write-backs became the default)

Paste the text between the lines into a new Claude Code session started in `C:\Users\Mubeen\Documents\Projects`.

---

Continue the Ghost of Yōtei fps work in ps5-porter (`porter/elements`, runtime `D:\Games\AnyPS5\PPSA26344-elem`).
Take autonomy; I check back later.

Read first: memory `yotei-p1-default-20261030` (loaded automatically), then `yotei-work/REPORT-2026-10-30-p1-default.md`
(every number of 2026-10-30) and `yotei-work/porter/proposal-cheaper-commits-20261030.md` (the next change, waiting
for my OK).

State: the GPU-ordered storage write-backs (P1) are the default on porter/elements (590cafaa9, 2f6d135e0, 7583a9306,
all local, deployed: driver 7F1F794C690D). The address-based lease wait was their whole cost; with my OK it is skipped
by default and a lease guard samples every 16th lease-covered flush (first stamp turns the waits back on and prints a
`[defer] lease guard` line). Measured: menu +14.6 %, world +8.5 to +11.5 % without the lease wait; 0 of 53,250 lease
waits stamped the stored blocks. Switches: APS5_NO_DEFER_STORES (old waits), APS5_NO_DEFER_OVER_LEASES (always wait
for leases), APS5_NO_LEASE_GUARD (no samples), APS5_NO_FLUSH_LANE_WAIT (full flush waits), and the condition drops
APS5_DEFER_UNWATCHED / APS5_DEFER_OVER_COMPLETIONS / APS5_DEFER_OVER_LANE / APS5_NO_DEFER_KEEP_WRITER.

To do, in this order:
1. If `yotei-work/runs-best/plan-pm2.txt` and `plan-pw2.txt` are missing or incomplete, run the A/B of the final
   default: `yotei-work/tools/p1-series-1030.ps1 -RunList "pm2|menu,pw2|world" -Name p1c` started detached
   (Win32_Process Create). Report per step with `yotei-work/tools/plan_gains.py`, `plan_steps_profile.py` and
   `shots_compare.py` (look at the shots: glare, black foliage, missing objects). Check every play and run log for a
   `[defer] lease guard` line.
2. First-time content stalls 1-3 minutes: on 2026-10-30 moving the horse into a new area froze the game until one
   NVIDIA pipeline compile (`nvgpucomp64.dll`) ended, while the title spun waiting (found with
   `yotei-work/tools/thread_rip_sample.py <pid> <tid,...>`); the Kusarigama freeze was likely the same. Find which
   pipeline takes that long (shader address, size, spanned wave?) and whether it can compile off the title's path;
   report before changing anything. Also look at the 8-9 tessellation hull shaders the recompiler refuses in every
   world run ("tessellation lowering: the hull program uses a lane mask other than its own lane's bit"), a possible
   cause of the "black tears" I see while playing.
3. Do not start the cheaper-commits change without my OK.

Rules that still apply: everything stays local, take the shared locks before game runs, Ghidra jobs and builds
(`python -I yotei-work/engine/tools/lock.py`), prove claims with checkable evidence, install nothing without asking,
start game runs detached (Win32_Process Create), never build while a measurement run is going, no public posts or
PRs without my OK, personal git identity, do not push, record deployments in `yotei-work/SYSTEM-CHANGES.md`. Never
put `switch APS5_LANE_SERIALIZE on` in a plan. Tell me before a measurement run starts: I sometimes play in the game
window with a controller (`yotei-work/tools/play-elem.ps1` is my play launcher; it takes the locks, and
`after-play-series.ps1 -KeepSaveAs <name>` keeps my save before a series swaps the Ezo save back in). Foreground
`sleep` over a few minutes is blocked in Bash; wait with run_in_background until-loops or Monitor.

At the end: write a report in `yotei-work/` with numbers per step, update memory, rewrite this prompt file for the
session after.

---
