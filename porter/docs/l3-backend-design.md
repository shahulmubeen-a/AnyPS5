# L3 backend design (draft, 2026-10-24)

## What the probes established

- Yōtei's renderer issues draws through a small set of engine functions above AGC: DrawItem (`0xc53710`),
  DrawInstanced (`0xc51d40`, `0xc52010`), the depth/shadow and GPU-driven paths (`0xdfb200`, `0x157a3a0`,
  `0x13dbac0`, `0x1268630`, ...). Each call issues exactly one draw packet. BindMaterial (`0x13da590`) binds a
  material's T#/S# tables to the context before draws.
- Guest-hook handlers can run before and after each call without disturbing the game, and can write into the
  engine's command buffer.
- The driver's per-draw cost today (profile of 2026-10-21, 720p menu): about 20 us of worker phases per packet
  (register decode 1.4, key/lookup/validate 7.5, capture 1.8, vectors 1.7, recompile 1.3, lock wait 3.8) plus
  about 90 us per commit on the committer thread. The menu runs about 1,300 draws and dispatches per frame.

## Idea

Treat the engine call, not the PM4 packet, as the unit of work. A call `DrawItem(technique, ctx, item)` with
the same `(technique, geometry, material)` as last frame produces (almost) the same register writes and user
data. If the driver recognises the call, it can reuse the prepared draw (pipeline, resource plan, descriptor
tables) from the previous frame and only patch what changed (per-frame constant pointers, index/vertex ring
addresses), instead of re-decoding, re-hashing and re-validating the packets.

This is what the inFAMOUS port does with its "shader recipes" and what Tsushima PC's D3D12 layer does with
persistent descriptor slots per resource.

## Open question (probe 6)

How much of the packet stream between begin and end markers is identical across frames for the same key?

- If register writes are identical and only a few user-data pointers change: the prepared draw can be keyed by
  the engine objects and the pointers patched. Big win on key/lookup/validate and capture.
- If the stream differs in many places (e.g. the engine re-emits all SH registers with new ring addresses):
  the win is smaller and needs a per-call "patch list".

Probe 6 hashes the dwords between begin and end per key, compares with the previous frame's hash for that key,
and for differing streams records which packets differ (opcode and register offsets).

## Prototype plan (after probe 6)

1. `EngineDrawCache` in the driver keyed by `(technique, geometry, material)` + a stream hash of the register
   packets, holding the prepared draw (`PreparedDraw`, recipe, descriptor tables).
2. In the packet loop, when a begin marker opens a call whose key hits and whose register stream hash matches,
   skip decode/key/lookup/validate and go straight to commit with the cached prepared draw, applying the
   user-data pointer patches.
3. Measure draws per frame served from the cache and the frame time against the plain run (A/B via
   `tools/ab-run.ps1`), check frame images are identical.
4. Then widen: BindMaterial as the unit for descriptor tables (the material's T# table changes rarely; keep its
   bindless slots resident per material object instead of per draw).

## Non-goals for now

- Replacing the PM4 path for dispatches (the GPU-driven culling chain); those stay on the generic path.
- Any change to rendering output; every step must keep frames identical.
