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

Results of the night (details in the log): clean-picture plateau ~3.0-3.1 fps by cutting (shadows, particles,
async effects), unchanged by the fixes below; >= 5 fps (5.6) only with a broken picture. On the full game: the new
rule `deferstores on` (Texture.cpp PorterDeferReason) +5% (10 of 10 repeats); together with the HTILE item-mask fix
(Dispatch.cpp, a real bug: depth clears queued as dispatch items never noted their HTILE mask, so depth draws
pre-synced on the whole GPU; default-on since 296e05120, APS5_NO_ITEM_HTILE_MASKS=1 turns it off) +6.5%; the existing
draw-ahead switch (APS5_NO_DRAW_AHEAD off, switched live) adds ~1-2%; APS5_PIPELINE_DISPATCH_MISSES and a deeper committer queue (pipedepth 128/256) ~0 (el16; el18 agrees, but the game
opened its photo mode by itself during el18, so el18 is not a gameplay measurement).
The el17 trace of the clean set: queue-0 worker ~160 ms own work + ~100 ms waiting for the committer per frame
(drains 58 ms, full queue 42 ms), committer 193 ms busy with almost no GPU waits, GPU 113 ms.

Next, in this order:
1. Let the worker and the committer overlap (clean set potential ~4.5 fps): queue more packet kinds for the committer
   instead of draining it (compute misses already a switch; indirect dispatches, WAIT_REG_MEM on labels), then retry a
   deeper queue. Drains by opcode are in the profiled `[draw] pipelined` line.
2. Make the committer cheaper: Graphics::Draw draw_and_resource_release (~70-90 us per draw, Resources.Complete
   ~50 ms per frame); the descriptor-heap model (yotei-tsushima / descriptor heap memory notes).
3. Make `deferstores` safe enough to be the default (storage/depth tests in research-committer-syncs.md).
4. Port the porter L3 engine draw cache (porter/main) and fix the vertex-fetch pointer words.
5. Keep the element map current: rerun `porter/tools/elem_build_map.py` after new runs; `elem_steps.py --run <tag>`
   summarizes a plan run per step (fps, deferred stores, depth pre-syncs, queue-0 load).

Report in the style I have been getting: what was run, what it proved, what is next, with numbers.

---
