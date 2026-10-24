# Prompt for the next session (written 2026-10-25 night, updated during the element-cost night)

Paste the text between the lines into a new Claude Code session started in `C:\Users\Mubeen\Documents\Projects`.

---

Continue the ps5-porter work: our own PS5-to-PC porter for my own disc dumps, first target Ghost of Yōtei
(PPSA26344). Read these first, in this order, before doing anything:

1. Memory `yotei-elements-night-20261024` and `own-ps5-porter-project` (loaded automatically) and what they link.
2. `ps5-porter/porter/docs/elements-night-log.md`: the element-cost night (what was measured, root causes,
   configurations, switches).
3. The element map `yotei-work/porter/elements-map/index.html` (built by `porter/tools/elem_build_map.py`).
4. `yotei-work/porter/research-emulator-overhead.md`, `research-dispatch-misses.md`, `research-committer-syncs.md`.
5. `yotei-work/SYSTEM-CHANGES.md` (the new runtime `D:\Games\AnyPS5\PPSA26344-elem`).

State: branch `porter/elements` of ps5-porter = Celegans12 yotei/best 161097737 (shader cache SourceVersion
`6c403b0d2815d602`, warm in `C:\AnyPS5-cache-best\PPSA26344`) + our hook table (with far hooks), local fixes,
skip-failed-work, GPU lock priority, porter L3 markers and the Elements module (`APS5_ELEM_FILE` live rules: pass/
passx/passd/passc/prog/kind/va/mat/queue skips, guest gkind/gva/gmat, `lockprio`, `gpoke <va> <u32>`,
`switch <APS5_NAME> on|off`, `pipedepth`, `prepahead`). Runs: `porter/tools/elem-run.ps1 -Tag <t> -Plan <plan>
-PlanStartAfter 450 -Hooks porter/hooks/PPSA26344-l3marks.hooks -ExtraEnv "APS5_PORTER_L3=1"` started detached
(Win32_Process Create); the "Call of the Hunt" cutscene (~450 frames) plays before gameplay at HOME, so plans start
450 s after CONTINUE. Reports: `elem_report.py`, plans: `elem_plan.py`, map: `elem_build_map.py`.

Rules that still apply: everything stays local (nothing derived from the games leaves this machine, no public
posts without my OK), take the shared locks before game runs, Ghidra jobs and builds
(`python -I yotei-work/engine/tools/lock.py`), record every deployment in SYSTEM-CHANGES.md, prove claims with
checkable evidence, install nothing without asking, commits use my personal git identity, start game runs
detached, never from Claude's shell. Never build while a measurement run is going (it skews the fps).

Next, in this order:
1. Default APS5_PIPELINE_DISPATCH_MISSES for Yotei if the dispatch-miss research says it is safe, then make
   repeated dispatches hit the dispatch cache (the drains before ~1,100 dispatch misses per frame cost ~158 ms).
2. Remove the committer's GPU waits for data only the GPU consumes (research-committer-syncs.md).
3. Port the porter L3 engine draw cache (porter/main, rounds 1-11) onto porter/elements and fix the vertex-fetch
   pointer words (porter/main NEXT-SESSION item 1); measure on the world, not the menu.
4. Keep the element map current: rerun `elem_build_map.py` after new runs.

Report in the style I have been getting: what was run, what it proved, what is next, with numbers.

---
