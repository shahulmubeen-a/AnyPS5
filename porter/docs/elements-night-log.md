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

## Root causes (el4 trace + samplers, el3 commit phases)
- Queue-0 worker per frame (trace, exclusive): waiting for the committer to drain 158 ms, committer queue full
  52 ms, WAIT_REG_MEM 41 ms, GPU lock 30 ms, own draw translation ~121 ms, own compute translation ~73 ms.
- Committer per frame: draw recording 114 ms + resources 76 ms + GPU syncs (Recorder.Sync) 67 ms + more; idle ~214 ms.
- Nearly every dispatch misses the dispatch cache (~23,000 misses per 10 s, [dispatch-item]), so it is not pipelined:
  the worker drains the committer and runs it itself. APS5_PIPELINE_DISPATCH_MISSES (runtime switch) hands misses to
  the committer: +14% on the lean configuration (el5).
- Single-item stalls of 40-60 ms per frame each: Interior Volumes (commit prepare = GPU wait), Deferred wetness
  (drain), Cull CS Lights and Ambient Spec Update (q38 prologue), Directional Shadow Combine (q30 prologue). Removing
  one moves the wait to the next sync point; that is why single cuts barely help.
- GPU (APS5_PROFILE_GPU, el4): ~193 ms of program time per frame; particles (0x801f64cc00 Emit 29 ms,
  0x801f808700 19 ms), cloth (q51, ~40 ms), staging-in copies 41 ms, draws 52 ms.

## Lean configurations (el5, no profiling)
- lean-a (shadows, particles, async extras, water, grass, misc, post children): 3.47 fps, image broken (post).
- lean-b (lean-a + Vcull, Proxy Set Fill Streams, CS Lights, cloth, terrain generation, CS Mesh, Displacement,
  clipmap/deformation updates): **5.58 fps** (base in the same run 1.84-2.04), image: ground ok, sky area garbage.
- Runtime switches on lean-c: pipeline dispatch misses +14%, quiet publish ~+9%, defer flip store ~+2%,
  draw-ahead 0%, draw inputs in place ~+2% (noise ~5%).

## Recognizable configurations (el6-el9)
- Cuts that break the picture: post-processing children (SSGI, tone mapping chain), the depth pre-pass (the G-buffer
  draws test EQUAL against it, so most objects vanish), and something in the misc set (dark fog: probably
  "Composite depth bias"); the lean extras (Vcull, proxy streams, CS lights, cloth, terrain generation, compute
  meshes, displacement) give most of the speed but garble geometry and lighting.
- Picture intact: r0 = shadows + particles + async extras (+ the two switches) = 3.08 fps (el9), c3 = 2.9 fps (el3).
- Broken but >= 5 fps: lean-b 5.58 (el5), l2-sw-c1 5.53, l2-safe-post 5.25 (el6).
- Engine knobs on the full game (el9): shadow distance 5000 0%, cascades 2 0%, cascades 1 + distance 2500 +5-9%.
- el7: pipeline depth 256 + preparer look-ahead 64 = -17% (worse); Vcull +17%, cloth +13% on top of b0.
- Research agents: dispatch misses cost 156 us each on the worker, 85 us of it the drain; a cache hit would save
  <= 15 us, enqueuing (miss items) removes the 85 us (yotei-work/porter/research-dispatch-misses.md). Committer GPU
  waits come from StorageTexture::FlushPending, StoreReadbackBeforeLabel, depth-plane syncs; proposal P1/P2 in
  yotei-work/porter/research-committer-syncs.md.

## Periodic passes and optimizations (el10-el12)
- el10 (each cut alone on r0): grass, interior/wetness/decals, deformer, SSDO/SSS, composite depth bias, GPU culling,
  cloth keep the picture (+0-12% each); water (its label covers scene lighting), proxy streams, CS lights, compute
  meshes/displacement, terrain generation break it. el11: all "harmless" cuts together (4.07 fps) break the picture;
  running passes every 2nd-4th frame breaks it too and is slower (2.6-2.9 fps). Clean plateau: ~3.1-3.3 fps.
- el12 deferred stores (new rule "deferstores on", Texture.cpp PorterDeferReason): ~90 write-backs per frame
  recorded behind their writer instead of waited for; full game 19.78 vs 18.44 presents per 10 s (+7%), three
  alternating repeats all positive, picture unchanged. APS5_PIPELINE_DISPATCH_MISSES on the full game: 18.44 vs
  18.44 (0%).
- el13 (profiled trace with deferred stores on): committer GPU fence waits by thread drop from 10.5 s to 0.5 s per
  60 s ([recorder] untagged), address-based syncs 8.0 s -> 0.1 s; but the trace shows Recorder.WaitSerial 71 ms per
  frame on the committer (was 0.6) and the worker's room_wait 132 ms (was 52), drain 71 ms (was 158): the wait
  moves to the next reader. Frame p50 552 ms (profiled). The chain worker -> committer -> GPU is the limit; the
  CPU-side validation of GPU-written memory is the coupling.
- Flake seen once (el14 first try, 35 s after launch, logos): `AGC driver: unsupported PM4 packet type: header
  0x10326b40 at DWORD 408` right after a POP_MARKER whose two dwords replaced a SET_SH_REG header; log kept as
  runs-best/run-el14-crash1.err.log. Rerun was clean.
- The new committer wait with deferred stores is Recorder::WaitSerial via VulkanDevice::WaitRecorded, called from the
  HTILE pre-sync (PresyncDrawDepth, VulkanDevice.cpp ~4580) and the DCC-key presync (~5234): the committer reads GPU-written metadata on the
  CPU before depth draws. Next on this path: expand HTILE on the GPU (DepthSurface.cpp expandHtileOnGpu, agent P2).
- el14 (final numbers, deferred stores four more times, 75 s steps): base 18.75/18.75/18.5/18.29, defer
  19.75/19.75/18.86/19.17 presents per 10 s, average 19.38 vs 18.57 (+4.4%), all four positive. Together with el12:
  7 of 7 repeats faster, +5.6% on average. On the clean r0 configuration the effect drowns in noise (el12 clean
  40.0/40.7 and 33.0/38.3; el14 r0 30.6/31.9 and 30.0/25.7); deferstores + APS5_PIPELINE_DISPATCH_MISSES on r0
  31.4 and 27.8. The element map's optimization cards were empty until elem_build_map.py learned to load every run
  its cards name (fixed 06:45).

## HTILE masks for dispatch items (el15, 06:52-07:20)
- Cause found in the profiled el13 log: the depth pre-sync's newest writers were "q0x0 op 0x15 prog 0x0 exact <htile
  range>", i.e. depth clears recorded by dispatch items on the committer (no write origin there). The dispatch-item
  hit path (APS5_PIPELINE_DISPATCH) never called NoteHtileMask, unlike the miss item and the direct path, so the next
  depth draw's store-back could not expand the HTILE on the GPU and PresyncDrawDepth waited for the whole GPU.
- Fix: Dispatch.cpp notes the mask after recording the item (opt-in switch APS5_ITEM_HTILE_MASKS for the A/B; flipped
  to default-on with APS5_NO_ITEM_HTILE_MASKS afterwards). New unprofiled counter: the [elem] round line prints depth
  pre-syncs and their wait per 10 s (VulkanDevice::DepthPresyncCounts); porter/tools/elem_steps.py sums it per step.
- el15, 3 alternating rounds (presents per 10 s / 10 = fps): base 1.887, htm 1.873 (-0.7%, noise), deferstores 1.971
  (+4.5%), deferstores + htm 2.010 (+6.5%, 3/3 repeats faster). Depth pre-syncs per 10 s: base ~42 waiting ~0.2 s,
  htm 0.4, deferstores ~115 waiting ~2.65 s (the waits grow because more work is queued on the GPU), deferstores +
  htm ~45 waiting ~0.13 s (the remaining writers with deferred stores are unattributed without profiling). The
  committer's busy time drops from ~6.6 s to ~5.2 s per 10 s with both, but the queue-0 worker stays at ~97%: the
  worker is the limit now. Pictures identical (shots-el15).

## Miss items, draw-ahead and the clean set with the fixes (el16, 07:20-08:05)
- 3 alternating rounds, fps (presents per 10 s / 10): base 1.93/1.86/1.80 (mean 1.861), dh = deferstores + HTILE
  masks 2.02/1.69/1.91, dhm (+ APS5_PIPELINE_DISPATCH_MISSES) 2.00/1.78/1.89, dhda (+ draw-ahead, APS5_NO_DRAW_AHEAD
  switched off live) 2.05/1.91/1.93 (mean 1.963, +5.5%, 3/3 faster than the base of its round), dhmda 2.00/1.86/1.93.
  Round 2 was slower for every setup (scene noise); without it dh +5.4%, dhm +4.3%, dhda +6.7%.
- Clean set r0 (2 rounds): r0 3.05/2.86, r0-dh 3.06/2.94, r0-dhm 3.11/3.08, r0-dhda 3.06/3.02, r0-dhmda 3.06/3.06:
  the fixes barely move it (+2-5%, within noise); the clean plateau stays ~3.0-3.1 fps.
- Committer busy per 10 s: base ~6.7 s, dh ~5.1 s, dhm ~5.4 s, dhda ~5.0 s; the queue-0 worker stays ~97% busy.
- Pictures unchanged for all of them (shots-el16). Miss-item frames showed fewer fallen leaves on the ground in one
  pair, which also varies with time; not verified either way.
