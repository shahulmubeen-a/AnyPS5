# Prompt for the next session (written 2026-10-24)

Paste the text between the lines into a new Claude Code session started in `C:\Users\Mubeen\Documents\Projects`.

---

Continue the ps5-porter work: our own PS5-to-PC porter for my own disc dumps, first target Ghost of Yōtei
(PPSA26344). Read these first, in this order, before doing anything:

1. Memory `own-ps5-porter-project` (loaded automatically) and the memories it links.
2. `ps5-porter/PORTER.md`: the layer plan (L0/L1 reused from AnyPS5, L2 new GPU backend, L3 engine hooks).
3. `ps5-porter/porter/docs/l3-probe-results.md`: what the probe runs proved so far, and the open questions.
4. `yotei-work/porter/l3-engine-hooks.md`: the static analysis that found Yōtei's renderer functions, with
   addresses and struct offsets.
5. `yotei-work/SYSTEM-CHANGES.md`: what is deployed and how to undo it.

Rules that still apply: everything stays local (nothing derived from the games leaves this machine, no public
posts without my OK), take the shared locks before game runs, Ghidra jobs and builds
(`python -I yotei-work/engine/tools/lock.py`), restore the runtime driver and the zip-b save after a probe run
(`yotei-work/tools/restore-runtime.ps1 -SkipCache`), keep `core.autocrlf=false` in ps5-porter so the shader
cache key stays `fb0c69e214f31b8d`, record every deployment in SYSTEM-CHANGES.md, and prove claims with
checkable evidence. Install nothing without asking. Commits use my personal git identity.

Work autonomously, as far as you can, in this order:

1. If run l3p3 or later probe runs are not yet written up in `l3-probe-results.md`, analyse their logs in
   `yotei-work/runs-best/run-l3p*.err.log` first (the `[l3]` and `[l3-foreign]` lines) and write them up.
2. Name the foreign draws (draws no engine function issued) by shader program and locate the engine functions
   that issue them; hook them too.
3. Design and start the L3 backend: intercept DrawItem/DrawInstanced/BindMaterial and prepare each engine draw
   once per engine object (technique, geometry, material), keyed by those pointers, so the PM4 path no longer
   re-translates them every frame. Start with a design note in `ps5-porter/porter/docs/`, then a prototype that
   replaces one draw kind and compares frames and fps against the plain run.
4. Keep the Linux iGPU probe (idea 1) ready: the Fedora stick files are in
   `C:\Users\Mubeen\tools\_downloads\ventoy\usb-files-4gb`; when I say the iGPU report is on the USB, read
   `porter/out/igpu-report.txt` from the stick and plan the native-shader test.

Report in the style I have been getting: what was run, what it proved, what is next, with numbers.

---
