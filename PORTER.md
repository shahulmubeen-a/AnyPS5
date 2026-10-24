# ps5-porter

A private, local PS5-to-PC porter for games dumped from discs we own. The first target is Ghost of Yōtei (PPSA26344).
This repository has no public remote and is never pushed anywhere. Licence: GPL-2.0-only, inherited from AnyPS5
(see `LICENSE`).

## Base

The repository starts as a fork of AnyPS5's `yotei/local-perf` branch (31b3fb4ae): Celegans12's `yotei/best` plus
our feature branches. The deployed runtime `D:\Games\AnyPS5\PPSA26344-best` runs that code (driver build 1afe29a87).
The remote `anyps5-local` points to the local AnyPS5 clone and has pushing disabled. Work goes on `porter/main` and
`porter/<topic>` branches.

## Layers

- **L0 relinker** and **L1 OS HLE** (libkernel, libc, AGC driver and the other `.prx` libraries): reused from AnyPS5.
- **L2 GPU emulation**: AnyPS5's PM4 path, to be replaced piece by piece. The planned pieces are ahead-of-time
  shader translation, a recompiler that uses uniformity analysis, a descriptor heap that treats guest descriptors
  as memory, and detiling on the GPU.
- **L3 engine hooks**: entry hooks on the Sucker Punch renderer functions that sit above AGC: DrawItem,
  DrawInstanced, BindMaterial and the pass draw job. With them the porter can see, and later replace, the engine's
  own draws instead of reverse-translating PM4. Static analysis that found these functions:
  `yotei-work/porter/l3-engine-hooks.md`.

## L3 hook tables

`porter/hooks/*.hooks` use the existing guest-hook loader in libc (`APS5_GUEST_HOOKS=<file>`, format in
`core/libs/prx/libc/include/GuestHooks.hpp`). Each entry hook checks the function's first bytes, so a table only
patches the eboot build it was written for (Yōtei v02.002.000).
