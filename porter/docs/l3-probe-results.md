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
- Results: pending.
