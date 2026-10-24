# Prompt for the next session (written 2026-10-24 evening)

Paste the text between the lines into a new Claude Code session started in `C:\Users\Mubeen\Documents\Projects`.

---

Continue the ps5-porter work: our own PS5-to-PC porter for my own disc dumps, first target Ghost of Yōtei
(PPSA26344). Read these first, in this order, before doing anything:

1. Memory `own-ps5-porter-project` (loaded automatically) and the memories it links.
2. `ps5-porter/PORTER.md`: the layer plan (L0/L1 reused from AnyPS5, L2 new GPU backend, L3 engine hooks).
3. `ps5-porter/porter/docs/l3-probe-results.md`: what the probe runs proved.
4. `ps5-porter/porter/docs/l3-cache-results.md`: the EngineDrawCache prototype (`APS5_PORTER_L3_CACHE=1`), its
   two A/B rounds, where the committer's time goes, and the two next levers.
5. `yotei-work/porter/l3-engine-hooks.md`: the static analysis that found Yōtei's renderer functions.
6. `yotei-work/SYSTEM-CHANGES.md`: what is deployed and how to undo it.

Rules that still apply: everything stays local (nothing derived from the games leaves this machine, no public
posts without my OK), take the shared locks before game runs, Ghidra jobs and builds
(`python -I yotei-work/engine/tools/lock.py`), restore the runtime after a probe run
(`ps5-porter/porter/tools/deploy-porter.ps1 -Restore` plus the zip-b save; `ab-l3cache.ps1` does both at its
end), keep `core.autocrlf=false` in ps5-porter so the shader cache key stays `fb0c69e214f31b8d`, record every
deployment in SYSTEM-CHANGES.md, and prove claims with checkable evidence. Install nothing without asking.
Commits use my personal git identity. Start game runs detached (Win32_Process Create via
`porter/tools/ab-l3cache.ps1` or `l3probe-run.ps1`), never from Claude's shell.

State: the engine cache finds last frame's prepared draw for 88% of the menu's engine draws and 72-81% of the
world's, validates 32% at the menu and 3% in the world, and does not change the frame rate (the frame sits on
the committer's 137-200 us per draw and on the dispatches). The world's 3% is the SRT walker marking the
geometry stage's pointer words 0x8e/0x92/0x96/0x9a as values, so the relocation refuses them.

Work autonomously, as far as you can, in this order:

1. Draw recipes for engine-cache hits (driver only, same shader cache): match `DrawRecipeRecord` by the donor
   variants (add an `origin` to the relocated `DispatchVariant` copies), attach recipes to the donor entry, and
   give `Graphics::DrawWithRecipe` the data refresh dispatch recipes have (`ShaderResources::RefreshData` is
   per stage and asserts `bindings.size()`; a draw template spans its stages), so a data hit records from the
   recipe (43 us) instead of the template path (127 us). Measure with `ab-l3cache.ps1` and
   `l3cache_report.py`; compare frames with `frame_compare.py`.
2. Ask me before changing the SRT walker (`core/shader/recompiler/Optimization/src/SrtWalker`): treating
   "pointer plus offset" bases and descriptors built from a pointer as relocatable would lift the world hit
   rate, but it changes the shader-cache version and costs one cold compile (about 53 min to the world).
3. BindMaterial's descriptor tables as the unit for bindless slots (material T#/S# tables resident per
   material object), after 1.
4. The six low-rate indirect functions: `yotei-work/porter/out/dec-yotei-foreign3.c` holds their decompiles;
   hook the ones whose second argument is the context with `l3-other` in `PPSA26344-l3marks.hooks`.
5. Keep the Linux iGPU probe ready: Fedora stick files in `C:\Users\Mubeen\tools\_downloads\ventoy\usb-files-4gb`;
   when I say the iGPU report is on the USB, read `porter/out/igpu-report.txt` and plan the native-shader test.

Report in the style I have been getting: what was run, what it proved, what is next, with numbers.

---
