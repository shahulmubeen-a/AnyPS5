# Yotei vs Tsushima PC frame structure, night of 2026-10-29/30: working log

Task (yotei-work/tsushima/NEXT-SESSION-PROMPT-tsu-live.md): measure Yotei's frame (draws, dispatches, command
buffers/submits, cross-queue waits, async-queue GPU share and overlap, CPU per call) at the menu benchmark scene,
put it next to the Tsushima PC capture tsu6, say which of hypotheses 1-3 hold, propose one change. No code change
toward that proposal without the user's OK.

Build/runtime: ps5-porter `porter/elements` 5ae65aa83 + uncommitted diagnostic switch APS5_GPUTIME_TOP (Recorder.cpp,
prints every timed program in [gputime]; shader cache 6c403b0d unchanged), runtime D:\Games\AnyPS5\PPSA26344-elem
(1080p-fixed app.exe, zip-b save), README env of 161097737 (elem-run.ps1). APS5_PROGRAM_COUNTS (c5153eca0) is not in
this tree; the Elements module's [elem-prog]/[elem-pass] tables give the same per-program and per-pass counts.
elem-run.ps1 gained `-StayAtMenu <s>` (dismiss the pre-order popup, stay at the main menu).

Tools written: yotei-work/tools/profile_rounds.py (per-10-s rounds of the profile lines, medians, per-queue packet
table), trace_chain.py (GPU busy/idle/overlap and lane split from a trace, per-thread wait groups, what the CPU did
while the GPU idled, queue 0 label waits), gpu_by_queue.py ([gputime] programs joined with [elem-prog] labels).

## Runs
- mt1 (23:17, menu, APS5_PROFILE_DRAW + trace frames 2500:40): 6.3 fps profiled (162 ms p50, p99 170).
- mt2 (23:26, menu, APS5_PROFILE_DRAW + APS5_PROFILE_GPU + APS5_GPUTIME_TOP=0 + worker/committer/thread samplers):
  6.1 fps.
- wt1 (23:35, world at HOME): trace frames 2850:16 in base-0, summary every 10 s + bench, plan wt1.plan (A/B of
  APS5_LANE_SERIALIZE and APS5_PIPELINE_DISPATCH_MISSES).

## Menu numbers (mt1 rounds 15-26 medians; trace 39 frames; mt2 GPU classes over 24 rounds)
- Per frame: 303 draw packets, 574 dispatch packets (q0 300, q38 112, q20 81, q52 21, q50 21, q48 20, q30 16, q51 5,
  q53 3), 52 WAIT_REG_MEM (q0 only 4.1, 2.7 ms), ~10 guest submissions, 105 Vulkan batches = 105 vkQueueSubmit.
- GPU busy 103 ms of 162 (63.5 %), idle 59 ms in ~100 gaps (46 of 0.1-1 ms = 17 ms, 13.5 of 1-5 ms = 26 ms,
  2 of 5-20 ms = 15 ms). Async lane (ring 0x51, second VkQueue): 2.0 ms busy, 35 % of it overlapping lane 0.
- GPU time by kind (mt2): game dispatches 52.8 ms (q0 19.6, unlabelled 13.9, q30 7.5, q38 4.9, q20 2.0, q51 2.0,
  q48 1.7, q52 1.0, q50 0.4), game draws 19.1 ms (194 render-pass ranges), driver copies 27.5 ms (staging-in 8.8,
  shadow-publish 8.0, storage-upload 3.6, storage-writeback 3.2, staging-out 2.4, copy 1.5), barriers ~1.5 ms.
- Queue worker 0x0 per frame: own work 47 ms, waiting for the committer (drains) 65 ms, GPU mutex 23 ms, GPU fence
  6.5 ms, label waits 2 ms, no packets 13 ms (frame start). Sampler: Drain 43 % of q0 samples, GPU mutex 11 %,
  FlushForAccess/SyncThrough (GPU waits before guest reads) ~8 %.
- Drains: 34 per frame (67 ms), at DISPATCH_DIRECT 26, DISPATCH_INDIRECT 3.7, WAIT_REG_MEM 2.8. Cause: dispatch-cache
  misses cannot be committer items (cumulative [dispatch-item]: per 10 s 1,600 items, 10,000 drained as miss, 4,100
  as no recipe). q0 direct-dispatch cache hits: 411 of 14,181 per 10 s (3 %).
- Committer: 152 us per commit, 553 commits per frame, 53 % busy. Sampler: commitDraw 26 %, commitLabel 20 %
  (StoreReadbackBeforeLabel -> Recorder::finish/FinishUpTo/Sync: GPU waits before label writes), GPU mutex 11 %.
- CPU per call on q0: draw packet 70 us (capture 29, vectors 21, recompile 7), direct dispatch 219 us (GPU mutex 82,
  lookup 60, device call 28, queued-label check 18, capture 11), indirect dispatch 573 us (device call 383);
  other queues' dispatches 521 us (GPU mutex 195, prologue 150, device call 99).
- While the GPU idled (59 ms per frame): q0 own work 18.5, drain 23.6, GPU mutex 8.9; committer working 21.8.

## World numbers (wt1, HOME after the cutscene, base env, APS5_PROFILE_DRAW; trace frames 2850:16; rounds 52-70)
- 1.80 fps profiled (trace window 570 ms p50, p99 612). Per frame: 2,697 draw packets (2,568 recorded draws), 2,238
  dispatch packets (q0 1,566, q20 389, q38 112, q48 54, q51 46, q30 37, q52 21, q50 21, q53 3), 65 WAIT_REG_MEM (q0 14,
  46 ms), ~10 guest submissions, 172-179 Vulkan batches (= vkQueueSubmit).
- GPU busy 293 ms of 570 (51.5 %), idle 277 ms (42 gaps of 1-5 ms = 105 ms, 10.5 of 5-20 ms = 86 ms, 2 of >= 20 ms =
  68 ms per frame). Lane 1 (ring 0x51): 50 ms busy, 94 % overlapping lane 0 (47 ms of real concurrency per frame).
- q0 per frame: work 220, committer drain 166, committer room 51, label waits 50 (GPU idle during 81 % of them;
  q30 materializing, q38/q50 themselves in label waits), GPU mutex 32, idle 17. Committer: work 264, GPU fence 69.
  While q0 drains, the committer works 91 ms and waits on GPU fences 67 ms.
- Dispatch cache in the world: 40,448 lookups per 10 s, 31,039 no entry, 1,418 equal (3.5 %); q0 direct 28,579
  dispatches, 16 hits per 10 s. Items 84 per 10 s vs 26,710 drained as misses.
- CPU per call: q0 draw packet 70.7 us, q0 direct dispatch 134 us (lookup 72), indirect 549 us, other queues 564 us;
  committer 122 us per commit, 63 % busy.
- wt1 A/B: APS5_LANE_SERIALIZE switched on live at 23:49 deadlocked the GPU timelines ("recorded batch 332615 still
  running on the GPU after 585 s (timeline wait)"); every later step measured 0 presents. Only base-0 (17.77 presents
  per 10 s) is valid. The switch is unusable live; lane value needs a startup A/B (APS5_ASYNC_LANE unset).
- Frame pacing (bench): HOME 291 frames, 0 over 1.5x the rolling median. Cutscene 834 frames: 9 spikes adding 10.9 s
  (2.2 % of the time); the largest (5.8 s, cutscene start) in an interval with Shader.ResolveAbi 383 ms per frame.

## GPU time per frame by kind (APS5_PROFILE_GPU + APS5_GPUTIME_TOP=0; mt2 menu 24 rounds, wt2 world 29 rounds)
- Menu (104.6 ms): game dispatches 52.8 (q0 19.6, unlabelled 13.9, q30 7.5, q38 4.9, q20 2.0, q51 2.0, q48 1.7, q52 1.0,
  q50 0.4), game draws 19.1, driver copies 27.5 (staging-in 8.8 = 165 MB, shadow-publish 8.6 = 213 MB, storage-upload
  3.6 = 235 MB, storage-writeback 3.4 = 228 MB, staging-out 2.4 = 165 MB, copy 1.5 = 32 MB), barriers 1.5.
  Biggest programs: FSR3 accumulate 0x141ecbf00 5.2 ms (1 dispatch), 0x8000208b00 4.8, water lighting 4.3, SSS 3.5.
- World (319.7 ms): game dispatches 189.4 (q20 particles 71.9 = emit 50.1 + update 21.1; q51 cloth 44.4 (second
  queue); q0 41.2; q30 10.0; q38 9.6; q48 6.8; q52 3.7; q50 1.6), game draws 49.9 (682 render-pass ranges), driver
  copies 67.5 (staging-in 40.2 = 707 MB, staging-out 8.6 = 702 MB, shadow-publish 9.3 = 207 MB, storage-upload 3.8,
  storage-writeback 3.1, copy 2.5), barriers 5.8. Async rings = 148 ms of GPU per frame (46 %), 44 ms of it on lane 1.
  Particle emit 0x801f64cc00 alone: 28.7 ms for one dispatch per frame.

## A/B and probes (presents per 10 s)
- ma1 (menu, profiled): base 62.0 / 64.3 / 64.3; miss items 62.3 / 64.3 (0 %); deferred stores 69.7 / 70.0 (+10 %);
  gpu6 (6 single-dispatch programs skipped, ~22-28 ms GPU) 71.6 / 74.7 (+15 %).
  Per step (plan_steps_profile.py): ds: GPU busy unchanged (98-104), GPU idle 58 -> 40 ms, batches 105 -> 63,
  committer 152 -> 111 us per commit, locked GPU waits 3.4 -> 1.3 s per 10 s, committer GPU fence waits 29 -> 4 ms per
  frame (FlushForAccess waits on other threads grow 40-46 -> 72-86 ms summed: the readback moves to the reader).
  gpu6: GPU busy -22..-28 ms, GPU idle unchanged (57-59): frame time follows GPU time 1:1 at the menu.
  miss: drains 2,170 -> 1,850 but 4.1 -> 4.9 s; committer 53 -> 63 % busy: the wait moves, no gain.
- wa1 (world, unprofiled): base 18.8 / 17.84 / 18.6; miss 18.8 / 19.0 (+3 %, noise level); deferred stores 20.0 / 19.8
  (+8 %); emit (particle emit skipped, ~47 ms GPU per frame) 18.6 / 18.8 (+2 %): world frame does not follow GPU time.

## Causal profiling (new Elements rules commitdelay/q0delay: a busy wait per commit / per q0 draw-dispatch packet)
- mc1 (menu, profiled): base 62.3 / 60.3 / 60.3; cd40 (+40 us per commit; measured +28 us, committer 52 -> 61 % busy)
  61.0 / 61.0; q0d40 (+40 us per q0 call, ~+24 ms of q0 work per frame) 60.0 / 59.7. Frame +2..3 ms for +15..22 ms
  of committer work (slope ~0.1), +4..5 ms for +24 ms of q0 work (slope ~0.2). At the menu neither the queue-0
  worker nor the committer is on the critical path; the frame = GPU busy (slope 1, gpu6) + ~57 ms of GPU idle that
  sync points cause (ds removed 18 ms of it), not CPU throughput.
- Gap feeders (gap_feeders.py) are unreliable: Recorder.Submit slices under 20 us are folded out of the trace, so
  most gaps show no feeding submit.
- wc1 (world at HOME, unprofiled): base 20.0 / 20.0 / 19.8 (502 ms); cd20 (+20 us per commit, ~2,870 commits per frame
  = +57 ms of committer work) 18.2 / 17.8 (556 ms, +54 ms: slope 0.95); q0d20 (+20 us per q0 draw/dispatch, ~4,285
  per frame = +86 ms) 18.89 / 18.6 (533 ms, +31 ms: slope 0.36). In the world the committer is the critical path:
  every microsecond per commit costs ~2.7-2.9 ms of frame time.

## Later runs (01:30-02:30)
- ws1 (world, committer + q0 samplers, 18.76 per 10 s): committer samples 49.6 % commitDraw (prepareDraw 37 %:
  resolveDrawResources, buildComplete, ShaderResources ctor, cachedTexture, resolveImageBinding), 20.5 %
  StorageTexture::FlushPending GPU waits (47 % from StoreReadbackBeforeLabel, 46 % from FlushDepthPlanes <-
  StorageTexture::refresh <- DepthSurface::SyncFromMemory), 7.7 % GuestArena write collection.
- wn1 (world, APS5_ASYNC_LANE unset): 18.6 / 18.2 / 18.44, same as lane-on wa1 (18.8 / 17.84 / 18.6); wc1's 19.8-20.0
  is the night's high. The cloth overlap is worth 0-8 %, within run spread.
- wd1 (world, profiled, base/ds alternating 150 s): base 17.87 / 18.13, ds 19.21 / 19.33 (+7 %). With ds the
  committer's GPU fence waits fall 102-107 -> 5 ms per frame (both the label and the depth-plane paths go), committer
  122 -> 87 us per commit and 63 -> 48 % busy, locked GPU waits 2.0 -> 0.33 s per 10 s, batches 170 -> 134, GPU idle
  227 -> 195 ms. But q20's fence waits rise 26-30 -> 82-84 ms and q30's 21 -> 44 ms per frame (FlushForAccess
  pending-write 139-145 -> 212 ms summed): the read-backs move to the CPU readers, and the frame only drops 37 ms.
  So deferstores already covers the depth half too; the next lever is either the committer's own work (if it stays
  critical: wc2) or the readers' syncs (q20 particle validation, q30 DMA_DATA sources).
- ma2 (menu, profiled): base 62.67 / 63.0 / 60.65; ds 70.0 / 70.67; gpu6 72.0 / 72.0; ds+gpu6 78.39 / 81.0. Per step:
  ds idle 57 -> 38.5-38.8 ms (busy unchanged), gpu6 busy 103 -> 82 ms (idle unchanged), both busy 83 + idle 40 = 123 ms
  frame (base 159): the two menu levers add up; frame = GPU busy + GPU idle with both on the critical path.
- wc2 (world, ds on in every step, unprofiled): ds 20.0 / 20.6 / 20.6 (490 ms); ds+cd20 19.0 / 18.43 (534 ms, +44 ms
  for +57 ms: slope 0.77); ds+q0d20 19.22 / 19.6 (515 ms, +25 ms for +86 ms: slope 0.29). With ds the committer
  stays the world's critical path.
- Proposal corrected after wd1: deferstores-as-default (P1) is the one change (+10-12 % menu, +7-8 % world, measured);
  it already covers the depth-plane half. Next: cheaper commits (draw recipes).
