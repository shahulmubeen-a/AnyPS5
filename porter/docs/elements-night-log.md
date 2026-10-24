# Element cost night, 2026-10-24/25: working log

Branch `porter/elements` in ps5-porter: Celegans12 yotei/best 161097737 (main 2026-10-24 merge, shader cache
SourceVersion `6c403b0d2815d602`) plus our hook table (with far hooks), local fixes, skip-failed-work, the porter L3
markers and the new element-cost module (`Execution/src/Driver/Elements.cpp`). Runtime `D:\Games\AnyPS5\PPSA26344-elem`
(make-runtime.ps1), 1080p-fixed app.exe, zip-b save. Runs via `porter/tools/elem-run.ps1` (plans in `porter/plans`),
logs in `yotei-work/runs-best` (run-elN.err.log, plan-elN.txt, shots-elN/).

## Tools added tonight
- `APS5_ELEM=1` / `APS5_ELEM_FILE=<file>`: per packet cost by pass (debug-marker stack), engine draw kind/VA,
  material, program; committer and preparer time carried by draw-pipeline items; worker phases ([elem-ph], with
  APS5_PROFILE_DRAW); live skip rules (pass/passx/passd/passc/prog/kind/va/mat/queue, guest gkind/gva/gmat),
  `lockprio on|off` (live APS5_GPU_LOCK_PRIORITY), `gpoke <va> <u32>` (live engine knob writes, restored on removal).
- `APS5_ELEM_NAMES=1`: names behind technique/material pointers (none found), `[elem-tech]` technique -> program.
- libc guest hooks: far hooks (14-byte absolute jump) because the new base reserves 0x7fff0000-0x200000000 around
  app.exe; hook table expected bytes from `hook_bytes.py`.
- elem_report.py (summaries, phases, GPU time), elem_plan.py (sweep plans, @autoplan), elem_spec.py + elem_html.py
  (the HTML map), elem_names.py (plain names).

## Measurements (gameplay at HOME after the "Call of the Hunt" cutscene; cutscene is frame-locked, ~450 frames)
- Baseline 17.5-19 presents per 10 s (1.75-1.9 fps; previous build l3c13a: 1.1). GPU busy ~320-360 ms per frame.
- Queue 0 worker 96% busy; committer 64%; per frame (el2 measure): worker ~500 ms, committer ~337 ms.
- Worker phases per frame: lock wait 83 ms (= Enqueue blocked, committer queue full), lookup 77 ms (= drains before
  dispatches such as Deferred wetness, which waits ~65 ms for the committer's G-buffer draws), capture 54,
  device call 29, recompile 17, WAIT_REG_MEM 51.
- Async queues: q38 prologue ~98 ms/frame (Cull CS Lights 57-68 ms per single dispatch), q30 prologue 38 ms/frame
  (Directional Shadow Combine 34-40 ms), q20 particles: lock wait 65 + GPU wait 24 + device 15 ms/frame.
- Committer ([draws]): template hit 83 us x ~2,160/frame, BDA 371 us x ~264/frame (98 ms/frame), hook waits inside
  draws ~60 ms/frame (Interior Volumes waits ~60-88 ms on the GPU each frame).

## Sweeps
- el2 (single elements, 40 s each): only the G-buffer pass ("Deferred") moves the frame much (+30%); instanced draws
  +17%, item draws +9%, everything else within about +-10% (noise ~+-5%).
- el3 floors: no queue-0 draws/dispatches = 3.1 fps (async queues alone hold the frame); nothing on any queue =
  40 fps (the game itself is fast). lockprio: no gain. Cascade count 2: +5%, 1: +10%.
- el3 cumulative: shadows 2.07, +particles 2.32, +async extras 2.9, +post/water/misc 3.4 fps (post skip = raw colours);
  +instanced 2.0 (worse, unexplained), +items 3.1 (black screen).
