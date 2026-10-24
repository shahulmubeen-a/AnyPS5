# L3 probe results

## Probe 1: call counts (run l3p1, 2026-10-24)

**Setup.**
- Runtime: `PPSA26344-best` (driver 1afe29a87) on the Ezo save, at 1080p-fixed.
- Hook table: `porter/hooks/PPSA26344-l3probe.hooks`. It holds count, args and dump handlers only; nothing in the
  game changes.
- Switches: `APS5_PROFILE_DRAW=1`.
- Sequence: 60 s at the main menu, then CONTINUE and 3 min in the world.
- Logs: `yotei-work/runs-best/run-l3p1.err.log` and `monitor-l3p1.csv`.
- Report command:
  `python -I porter/tools/l3probe_report.py <err.log> <monitor.csv> 14:24:01`.

All 9 hooks installed and none were refused. At the menu the frame rate was 5.8 fps, against 6.3 fps measured
before at 1080p-fixed without hooks and without `APS5_PROFILE_DRAW`.

**Calls per presented frame, steady main menu** (about 58 frames per 10 s):

| Function | Calls per frame |
|---|---|
| DrawItem `0xc53710` | 61 |
| DrawInstanced `0xc51d40` | 54 |
| DrawInstanced-b `0xc52010` | 10 |
| BindMaterial `0x13da590` | 97 |
| Image binder `0x13da860` | 97 (always equal to BindMaterial) |
| Sampler binder `0x13d9bd0` | 216 |
| Pass draw job `0xc545f0` | 35 |
| Dispatch `0xb81270` | 128 |
| DispatchIndirect `0xb813f0` | 22 |

- The driver counts about 17,100 draw packets per 10 s at the menu, which is about 295 per frame.
- The engine draw functions account for about 125 per frame, so roughly 40% of the menu's draw packets.
- The rest come from paths not hooked yet. Candidates are the GPU-driven indirect cluster, full-screen passes
  and UI, and DrawIndex call sites that Ghidra left outside any function.

**World (Ezo, about 1 fps).** DrawItem + DrawInstanced + DrawInstanced-b come to about 1,700-2,600 calls per
frame. The driver sees about 1,850-2,900 draw packets per frame, so the engine functions cover roughly 90-95% of
world draws. Frame boundaries are coarse at 1 fps, so this is an estimate.

**Arguments.** They match the static analysis. In DrawItem and BindMaterial, rdi is the technique and rsi is the
same context. Memory at ctx (rsi) shows:

| Offset | Meaning | Value |
|---|---|---|
| +0x00 | buffer start | 0x201069d800 |
| +0x08 | start + 64 KiB | |
| +0x10 | cursor | 0x201069d8c8 |
| +0x18 | end | 0x20106ab6a0 |
| +0x20 | reserve callback | 0x140ad2370, inside app.exe |
| +0x28 | user (equals ctx) | |
| +0x30 | reserved dwords | 4 |

## Probe 2: draw attribution through NOP markers

- Hook table: `porter/hooks/PPSA26344-l3marks.hooks`.
- Driver code: `Execution/src/Driver/EngineMarks.cpp`, enabled with `APS5_PORTER_L3=1`.
- The handlers write an 11-dword NOP into the engine's command buffer before each engine draw and material
  bind. The driver then attributes every draw packet to the marker that precedes it.

### Run l3p2 (2026-10-24 14:50)

Same sequence as l3p1, with the ps5-porter driver 0793cbea2 deployed. Its shader cache version is
`fb0c69e214f31b8d`, so the cache was warm. The driver was restored afterwards.

**Markers do not disturb the game.**
- Menu frames are identical to l3p1.
- The world cutscene renders normally.
- Frame rate is unchanged: menu 5.7-5.9 fps, world about 1.0 fps.
- No errors in the log.
- Markers written: about 35,000 per 10 s. Skipped for lack of room: 0.
- The driver saw every marker that was written.
- Every draw marker was followed by at least one draw (`markers-0-draws` = 0).

**Attribution of draw packets** (per 10 s, steady phases):

| Phase | Draw packets | First draw after an engine marker | Further draws before the next marker | No marker since the submission began |
|---|---|---|---|---|
| Main menu | about 17,200 | 7,300 (42%) | 8,200 (48%) | 1,690 (10%): 120 direct + 1,566 indirect |
| World / cutscene | 19,000-31,000 | 85-90% | 10-15% | under 0.3% |

- **Most draw markers lead to exactly one draw.**
  - World: about 98% of markers.
  - Menu: 92% of markers.
- **The "further draws" bunch up behind a few markers.**
  - Menu: about 520 markers per 10 s carry all 8,200 of them, roughly 16 each.
  - World: about 150 DrawItem markers carry 2,000-3,600, roughly 20 each.
- **Those batches may not be engine draws.** Without an end-of-function marker, an unhooked batch issued after an
  engine draw looks the same as a multi-draw item. Candidates: UI, post passes, and the GPU-driven indirect
  draws.

**Conclusion.** The engine renderer layer issues nearly all world draws: at least 85-90%, and all but 0.3% if the
batches belong to it. At the menu it issues at least 42%. Hooking that layer is enough to take over world
rendering.

## Probe 3: end markers (run l3p3, 15:16)

The draw handlers now push the real return address on a thread-local stack and return into `L3ExitStub`, which
writes an end marker and jumps back (`EngineMarks.cpp`). Draws between begin and end belong to the call.

- 13 hooks installed, no refusals, no unmatched exits, no crash; the game renders as before.
- **Every engine draw call issues exactly one draw packet.** Menu: 7,370 calls, 7,370 draws (`calls-1-draw` =
  all, `extra-draws` = 0). World: 22,000-30,000 calls per 10 s, under 30 with more than one draw.
- So the probe-2 "batches" were foreign draws issued right after an engine draw returned.
- Foreign share: menu 10,000 of 17,300 draws per 10 s (58%: 5,900 direct + 4,050 indirect); world
  3,900-4,500 of 25,000-34,000 (12-16%: about one third direct, two thirds indirect).
- Foreign programs (`[l3-foreign]`): the biggest sets pair a vertex program with the driver's own stand-in pixel
  program (host address `0x7ffaf35fb100`), that is depth-only draws (shadow and depth passes), plus GPU-driven
  indirect sets.

## Probe 4: foreign call sites (run l3p4, 15:24)

`sceAgcDcbDraw*` in libSceAgc report their return address to the driver; outside an open engine call the site is
counted (`[l3-sites]`, eboot VAs).

- Only **16 call sites at the menu, 21 in the world**, in 17 functions. Ghidra's function list maps them:

| Containing function | Menu draws / 10 s | World draws / 10 s | Note |
|---|---|---|---|
| `0xdfb200` | 3,127 | 150-280 (+ `0xdfc067` 170-240) | binds samplers itself, calls DrawIndex: a depth/shadow draw path |
| `0x157a3a0` | 2,714 | 850-1,350 | indirect cluster member with an irregular prologue |
| `0x13dbac0` | 1,898 | 330-430 | |
| `0xcce740` | 590 | 3-15 | |
| `0x1268630` | 767 (2 sites) | 790-1,030 | indirect cluster, DrawIndexIndirectMulti |
| `0xba7e70` | 0 | 320-500 | world only |
| `0xdf9e60` | 0 | 115-205 | world only |
| `0x13ac930`, `0x1579220`, `0x1257ee0`, `0x13db610`, `0x1266b10`, `0x159acb0`, `0x1577dd0`, `0x15786f0`, `0x13dbec0`, `0x126cb10` | 4-240 each | 1-165 each | |

- Decompiled (`yotei-work/porter/out/dec-yotei-foreign{1,2}.c`): the first twelve take the context as their
  second argument (they use `param_2+0x10`, `+0x18`, `+0x38+stage*0x70`), like DrawItem. The last five are
  low-rate and their context argument is not confirmed.

## Probe 5: markers on all seventeen (run l3p5)

Hook table `PPSA26344-l3marks.hooks` as of commit 3963e96b3: the twelve confirmed functions get `l3-other`
markers (the marker carries the function VA), the five unconfirmed ones are counted. 22 hooks installed, no
refusals, no unmatched exits, rendering unchanged (menu 5.8 fps, world 1.0 fps).

**Foreign draws drop to 8% at the menu and 3.5% in the world:**

| Phase | Draws / 10 s | Foreign direct | Foreign indirect | Remaining sites |
|---|---|---|---|---|
| Menu | 16,920 | 621 | 803 | `0x1267978` (in `0x1266b10`), `0x159b455` (in `0x159acb0`), `0x15782fd`, `0x1578c2f` |
| World | 34,000 | 60-90 | 1,090-1,180 | the same four plus `0x13dc06d` (in `0x13dbec0`) and `0x126cefc` (in `0x126cb10`) |

**Per-function behaviour** (`[l3-other]`, VA calls/draws):

| Function | Menu | World | Shape |
|---|---|---|---|
| `0xdfb200` | 171 / 3,021 | 47 / 480 | one call, about 10-18 draws: a batch loop (shadow/depth cascades) |
| `0x157a3a0` | 115 / 2,645 | 22 / 1,122 | one call, 23-51 draws: GPU-driven indirect batches |
| `0x13dbac0` | 1,847 / 1,847 | 402 / 402 | exactly one draw per call |
| `0xba7e70` | - | 11 / 363 | world only, about 33 draws per call |
| `0xdf9e60` | - | 11 / 154 | world only, 14 per call |
| `0x1579220` | 229 / 458 | 57 / 114 | two draws per call |
| `0x13ac930`, `0x13db610`, `0x1257ee0` | 1 draw per call | 1 draw per call | |

Together with DrawItem, DrawInstanced and DrawInstanced-b, **17 engine functions issue 92% of the menu's and
96.5% of the world's draw packets.** The remainder are GPU-driven indirect draws from five more functions whose
context argument is not yet confirmed.

**Next.**
1. Probe 6: for repeated engine draw calls on the same (technique, geometry, material), hash the PM4 dwords
   between begin and end and compare frame to frame. That decides what an engine-level prepared draw can skip.
2. Design note for the L3 backend, then a prototype for DrawItem.
