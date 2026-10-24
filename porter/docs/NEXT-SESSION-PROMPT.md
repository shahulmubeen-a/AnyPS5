# Prompt for the next session (written 2026-10-24, 20:20)

Paste the text between the lines into a new Claude Code session started in `C:\Users\Mubeen\Documents\Projects`.

---

Continue the ps5-porter work: our own PS5-to-PC porter for my own disc dumps, first target Ghost of Yōtei
(PPSA26344). Read these first, in this order, before doing anything:

1. Memory `own-ps5-porter-project` (loaded automatically) and the memories it links.
2. `ps5-porter/PORTER.md`: the layer plan (L0/L1 reused from AnyPS5, L2 new GPU backend, L3 engine hooks).
3. `ps5-porter/porter/docs/l3-probe-results.md`: what the probe runs proved.
4. `ps5-porter/porter/docs/l3-cache-results.md`: the EngineDrawCache (`APS5_PORTER_L3_CACHE=1`), rounds 1-11:
   the alias retention bug and its fix, the SRT walker change, and round 11's finding that the world's
   refusals are the vertex-fetch table pointer words.
5. `yotei-work/porter/l3-engine-hooks.md`: the static analysis that found Yōtei's renderer functions.
6. `yotei-work/SYSTEM-CHANGES.md`: what is deployed and how to undo it.

Rules that still apply: everything stays local (nothing derived from the games leaves this machine, no public
posts without my OK), take the shared locks before game runs, Ghidra jobs and builds
(`python -I yotei-work/engine/tools/lock.py`), restore the runtime after a probe run
(`ps5-porter/porter/tools/deploy-porter.ps1 -Restore` plus the zip-b save; `ab-l3cache.ps1` does both at its
end), keep `core.autocrlf=false` in ps5-porter, record every deployment in SYSTEM-CHANGES.md, and prove claims
with checkable evidence. Install nothing without asking. Commits use my personal git identity. Start game runs
detached (Win32_Process Create via `porter/tools/ab-l3cache.ps1` or `l3probe-run.ps1`), never from Claude's
shell. The shader cache SourceVersion is now `2afd0fe3bdb94b88` (warm, 47,062 entries, written 18:26-19:09);
the old `fb0c69e214f31b8d` cache is still on disk. A recompiler change costs one more cold compile (about 45
min with `ab-l3cache.ps1 -SkipA -MenuHold 600 -WorldSeconds 1800 -TimeoutMin 60`); ask me first.

State: the engine cache serves 86% of the menu's engine draws and 68-77% of the world's (trial off), validates
38% at the menu and 3.3% in the world, and does not change the frame rate at the menu (60 per 10 s). With the
alias map holding entries weakly the world runs at the plain rate when the per-kind trial is on (10.3 vs
10.4-10.6 per 10 s); with every call served the misses cost 9% (9.7). The world's refusals are, by the
`[l3-refused]` trace, mostly the vertex stages' fetch-table pointer words (`ShaderVertexStageInfo::
fetchBufferReg`, excluded since round 8 = walked but not relocatable) that move with the ring every frame; the
smaller share is V# bases built from a pointer plus an offset (a walker extension, cold cache).

Work autonomously, as far as you can, in this order:

1. Make the vertex-fetch pointer word relocatable (driver only, same shader cache): in `DrawCapture.cpp` the
   fetch words are `excluded`; instead record the fetch's table reads (the V#s the embedded fetch resolves at
   `ptr + soffset`, see `EmbeddedVertexFetch.cpp` and `vertexInfos[i]->resources`) as reads with that word as
   origin, so `tagVariantRuns` tags them and `relocateDonor` shifts and re-reads them; validate the V#
   contents at the new address like the SRT walk's data. Measure with `ab-l3cache.ps1 -ExtraB
   "APS5_PORTER_L3=1;APS5_PORTER_L3_CACHE=1;APS5_PORTER_L3_NO_TRIAL=1;APS5_PORTER_L3_TRACE_REFUSALS=4"` against
   a plain run; the world's validated hits must rise well above 3.3% and `[l3-refused]` lines for 0x92/0x96/
   0x9a/0x9e/0xa8 must go.
2. Then, with my OK, the walker extension for pointer-plus-offset bases and descriptors (`IAdd32`/
   `IAddCarry32` of a user word in `SrtEvaluator.cpp` `EvaluateRawRead`'s `baseWord`, `SrtTape.cpp`'s
   `baseWord` detection, and `DescriptorBaseWord`), one cold cache.
3. When the world hit rate is there: the per-kind trial back on, A/B the world frame rate; then draw recipes
   on the world's hits and BindMaterial's descriptor tables as the unit for bindless slots.
4. Keep the Linux iGPU probe ready: Fedora stick files in `C:\Users\Mubeen\tools\_downloads\ventoy\usb-files-4gb`;
   when I say the iGPU report is on the USB, read `porter/out/igpu-report.txt` and plan the native-shader test.

Tools: `l3cache_report.py <runs-best> <tagA> <tagB>`, `l3stage_report.py <run.err.log>`, `frame_compare.py`,
`agc_shader_replay.exe --asm --mem <req>` (PATH: `build\core\libs\libs\unpatched` first), `APS5_DUMP_IR=<hex>
ANYPS5_NO_SHADER_CACHE=1` for the IR per pass. Known flakes to rerun: a loader access violation
app.exe+0x11328f5 before the menu, `AGC driver: truncated shader metadata` 20 s into the world.

Report in the style I have been getting: what was run, what it proved, what is next, with numbers.

---
