# Prompt for the next session (written 2026-10-30 ~02:30, after the Tsushima-comparison night)

Paste the text between the lines into a new Claude Code session started in `C:\Users\Mubeen\Documents\Projects`.

---

Continue the Ghost of Yōtei fps work in ps5-porter (`porter/elements`, runtime `D:\Games\AnyPS5\PPSA26344-elem`).

Read first: memory `yotei-frame-structure-20261030` (loaded automatically), then
`yotei-work/REPORT-2026-10-30-tsushima-compare.md` (the report I read in the morning) and
`ps5-porter/porter/docs/tsu-compare-night-20261030.md` (every number of that night), then
`yotei-work/porter/research-committer-syncs.md` (proposals P1 and P2).

State: the night of 2026-10-29/30 measured the frame causally. The main menu (benchmark) is GPU time 1:1 plus
~57 ms of sync gaps per frame; the world at HOME is limited by the draw committer (+20 µs per commit = +54 ms per
frame). The proposed change, waiting for my OK, is to make deferred storage write-backs the default (P1 of the
research note; today the porter rule `deferstores on`): +10-12 % menu and +7-8 % world, measured 4 of 4 steps each.
It removes the committer's GPU waits for labels and depth planes (world 105 -> 5 ms per frame); the waits partly
move to queues 0x20/0x30 as CPU readers. After it the committer is still the world's critical path (slope 0.77), so
the next change is cheaper commits (draw recipes: port the porter/main L3 engine cache, relocate the vertex-fetch
pointer words).

If I said OK: turn the rule into a default with the P1 conditions (each condition behind a switch first, so a plan
can A/B it live; see `porter/plans/wa1.plan` and `ma1.plan`; `porter/tools/elem-run.ps1 -StayAtMenu 1 -Plan ...` runs
a plan at the menu), run the tests the research note lists, then A/B menu (ma-style plan, profiled) and world
(wa-style, unprofiled, base steps between every variant) and compare screenshots in `runs-best/shots-<tag>`. Report
numbers per step with `yotei-work/tools/plan_steps_profile.py` (profiled runs) and the plan file.

Diagnostics added that night (uncommitted at first, see git status): `APS5_GPUTIME_TOP=0` (all programs in
[gputime]), `APS5_TRACE_DISPATCH_CACHE_FROM=<present>`, Elements rules `commitdelay <us>` and `q0delay <us>`
(causal profiling: the frame-time slope per added microsecond). Never put `switch APS5_LANE_SERIALIZE on` in a plan:
it deadlocked the GPU timelines live.

Rules that still apply: everything stays local, take the shared locks before game runs, Ghidra jobs and builds
(`python -I yotei-work/engine/tools/lock.py`), record every deployment in `yotei-work/SYSTEM-CHANGES.md`, prove
claims with checkable evidence, install nothing without asking, commits use my personal git identity, start game
runs detached (Win32_Process Create), never build while a measurement run is going, no public posts without my OK.

---
