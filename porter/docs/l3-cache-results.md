# L3 engine draw cache: results (2026-10-24)

The prototype from `l3-backend-design.md`: `EngineDrawCache` in the driver, behind `APS5_PORTER_L3_CACHE=1`
(needs `APS5_PORTER_L3=1` and the l3marks hook table). Code: `Execution/src/Driver/Draw/EngineDrawCache.cpp`,
`EngineMarks::CurrentDraw()`, and the changes in `Draw.cpp`, `DrawRelocate.cpp`, `DrawCapture.cpp`,
`DrawCache.cpp` (commits a75bbf8c3, 21b8ea63c).

## What it does

The driver's draw cache keys a prepared draw by the exact register state, user words included. Yōtei writes a
fresh per-draw constant block into a ring every frame and passes its address in the user words, so the exact key
of an engine draw is new every frame and last frame's entry is never found. Measured in run l3c1a (markers only):

| Phase | Draw-cache lookups / 10 s | No entry | Hits | Captures / 10 s | Worker us per draw |
|---|---|---|---|---|---|
| Main menu | 34,300 | 31,950 (93%) | 1,540 | 25,300 | 59 |
| Ezo world | 56,500 | 55,800 (99%) | 545 | 43,800 | 130 |

(Two lookups per draw packet: the register-key lookup and the stage validation count separately.)

The engine cache maps an alias made from the open engine call's objects (DrawItem: technique, geometry,
material; DrawInstanced: technique, geometry, part; the other functions: VA and arguments) to the entry inserted
for the previous call on the same objects. On an exact-key miss with a matching alias record (same stable
registers, auxiliary registers, shader set and device), the donor entry is relocated to the current user words
through the existing draw relocation (runs read through a moved pointer shift by its delta) and then validated
against guest memory like any other hit. The cache chooses which entry to validate; it never skips validation.
Per-frame constants behind the pointer come back as "data hits" (the words at the variant's data positions are
refreshed). The second sighting of an alias admits the insert, and the alias is learned at insertion.

## Round 1: runs l3c1a (markers only) and l3c1b (markers + cache), 16:23-16:36

Same recipe as the probes: zip-b save, 1080p fixed, 60 s at the main menu, CONTINUE, 180 s in the world; warm
shader cache (`fb0c69e214f31b8d`, 1 miss). Averages per 10 s window (`porter/tools/l3cache_report.py`):

| Metric | A menu | B menu | A world | B world |
|---|---|---|---|---|
| Presented frames / 10 s | 58.2 | 59.0 | 10.9 | 10.9 |
| Engine draws / 10 s | 17,170 | 17,730 | 27,980 | 28,380 |
| Worker us per draw packet | 59.4 | 52.4 | 130.4 | 127.7 |
| of it capture | 29.0 | 19.0 | 30.9 | 25.3 |
| of it key/lookup/validate | 13.4 | 16.7 | 30.2 | 34.4 |
| Worker draw ms / 10 s | 1,018 | 929 | 3,633 | 3,576 |
| Captures / 10 s | 25,340 | 20,570 | 43,760 | 45,790 |
| Draw-cache hits / 10 s | 1,540 | 6,900 | 545 | 1,390 |
| Stages compiled / 10 s | 34,080 | 20,740 | 70,700 | 51,170 |
| Committer commits / 10 s | 30,180 | 31,250 | 30,320 | 30,310 |
| Committer us per commit | 140.7 | 136.6 | 165.9 | 164.3 |
| Packet drains ms / 10 s | 3,283 | 3,396 | 1,676 | 1,578 |
| Engine cache: calls / 10 s | | 16,170 | | 27,810 |
| served by the alias | | 11,710 (72%) | | 13,130 (47%) |
| validated hits | | 4,990 (31%) | | 850 (3%) |
| of them data hits | | 4,950 | | 850 |

**What it proved.**
- The engine key finds the previous frame's entry: at the menu 72% of engine draws are served by the alias and
  31% validate as hits, against 9% draw-cache hits without it. Draw-cache hits rose from 1,540 to 6,900 per
  10 s, captures fell by 19% and compiled stages by 39%; the worker's draw cost fell 9% (1,018 to 929 ms per
  10 s). Nearly every hit is a data hit: only the per-frame constants differ, as probe 7 predicted.
- No crash, no unmatched exits, no validation error; the game rendered the menu and the world as before. Frames:
  the menu is static within a run (mean absolute difference 0.1-0.2 per channel between dumps 3 s and 100 s
  apart), but differs between runs even with the same driver (l3p6 vs l3p7 5.3/4.6/3.7, l3p7 vs l3c1a
  5.7/4.9/3.9 per channel); A vs B measured 8.4/7.9/6.9. So the frames are not bit-identical, the difference is
  of the order of the run-to-run drift of the plain build, and the pictures show the same scene with the same
  elements. Loading-screen frames 96-102 are bit-identical. Identity is not proven; see round 2.
- **Frame rate did not change** (menu 5.8 vs 5.9 fps, world 1.09 vs 1.09 fps). At the menu the queue-0 worker's
  draw work is about 10% of wall time and the frame sits on dispatches (3.7 s per 10 s in DISPATCH_DIRECT) and
  the draw-pipeline drains before them (3.3 s per 10 s), which the committer's 137-141 us per commit sets. In
  the world the worker's draw work is 36% of wall time, but the cache hit only 3% there.
- **Why the world misses.** From the `[l3-cache]` lines of the last world windows (per 10 s, about 32,000 calls):
  - "stable registers differ" 12,800: the same objects are drawn by several passes (shadow cascades, depth
    pre-pass, G-buffer) whose other registers differ, and one record per alias cannot hold them all.
  - "a stage lost every variant" 15,600: the relocation refused the front stage's variant because a moved user
    word was not relocatable. The moved registers are the geometry stage's user words 0x8c/0x8d (allowed: the
    constant-block pointer the walk reads as a base) and 0x8e, 0x92, 0x96, 0x9a (refused), plus the pixel
    stage's 0x22, 0x26, 0x2a and words up to 0xa8. The refused words are pointer pairs the SRT walk classifies
    as values (`asValue`: pointer arithmetic before the load or a V# built from the pointer) or never reads.

## Round 2: run l3c2b (commit 21b8ea63c)

Changes: the alias also carries the stable part of the register key (one record per pass), and a moved user
word the resource walk never read (`DispatchVariant::walkedWords`) no longer refuses the variant: the shader
reads it at run time through its forwarded words, which the relocation patches. Words the walk read as values
still refuse. Run l3c2b, 16:44-16:50, same recipe; compared with l3c1a:

| Metric | A menu | B2 menu | A world | B2 world |
|---|---|---|---|---|
| Presented frames / 10 s | 58.2 | 59.0 | 10.9 | 9.2 |
| Engine draws / 10 s | 17,170 | 17,380 | 27,980 | 23,870 |
| Worker us per draw packet | 59.4 | 53.5 | 130.4 | 156.5 |
| Worker draw ms / 10 s | 1,018 | 930 | 3,633 | 3,755 |
| Captures / 10 s | 25,340 | 21,940 | 43,760 | 43,780 |
| Stages compiled / 10 s | 34,080 | 17,690 | 70,700 | 35,930 |
| Draw-cache hits / 10 s | 1,540 | 6,660 | 545 | 1,180 |
| Committer us per commit | 140.7 | 137.0 | 165.9 | 195.3 |
| Engine cache: served by the alias | | 13,960 of 15,910 (88%) | | 16,630 of 23,080 (72%) |
| validated hits | | 5,120 (32%) | | 690 (3%) |

- The alias per pass works: "stable registers differ" went from 12,800 to 0 per 10 s in the world, and the share
  served rose to 72-81% there and 88% at the menu.
- The unwalked-word rule changed nothing: the refused registers are the same (0x8e 17,200, 0x92 6,700,
  0x96 6,400, 0x9a 4,400, 0x26 2,000, 0x22 1,700 per 10 s in the world), so those pointer words are read by the
  SRT walk as values. The front stage still loses every variant on 17,000-20,000 world calls per 10 s, and the
  world hit rate stays at 3%.
- World frame rate 9.2 vs 10.9 per 10 s and committer 195 vs 166 us per commit: within the run-to-run spread
  of this scene (Celegans12 measured 22% between two unchanged runs), but the direction is a warning: a served
  call that then misses pays the relocation and the validation on top of the capture (key/lookup/validate
  30 to 42 us per draw). Without a world hit rate, the cache should stay off in the world.
- Menu frames: l3c1b vs l3c2b 5.3/4.6/3.7, l3c1a vs l3c2b 6.9/6.2/5.0, l3p7 vs l3c2b 6.1/5.5/4.5 mean absolute
  difference per channel; identical-driver pairs measured 5.3-5.7. No visible difference in the pictures.

## Round 3: run l3c3b (commit 679b6a614), the six low-rate functions hooked

Their decompiles (`yotei-work/porter/out/dec-yotei-foreign3.c`) confirm the context argument: `0x159acb0` takes
it first, `0x1577dd0` and `0x15786f0` third, `0x13dbec0` second, and `0x1266b10` and `0x126cb10` read it from
the pointer their second argument points at (`ctx = *param_2`). New handlers `l3-other-rdi`, `l3-other-rdx`
and `l3-other-rsi-deref`; 22 hooks installed, 0 refused, no unmatched exits. In the world they issue one draw
per call (`[l3-other]`: 0x13dbec0 27/27, 0x1266b10 9/9, 0x126cb10 9/9, 0x1577dd0 8/8, 0x15786f0 4/4,
0x159acb0 9/17 calls/draws per 10 s). The foreign draws did not move (menu 600 direct + 780 indirect, world
15-25 direct + 950-1,250 indirect per 10 s): the remaining foreign draws are not from these six. The cache's
numbers repeated round 2 within noise (menu 60.0 frames per 10 s, 87% served, 32% hits; world 9.2 frames per
10 s, 72% served, 3% hits).

## Round 4: run l3c4b (commit 329ae0b42), draw recipes for engine-cache hits

Recipe records now match by the donor variant a relocated copy was made from, hits look up and attach recipes
on the donor entry, and `Graphics::DrawWithRecipe` refreshes a template's data words (per-stage walk,
`RefreshDataStages`) and re-keys the template instead of missing on a data hit. Stable run, no errors, cache
numbers as in rounds 2-3. **Draw recipe hits did not move: 12-13 per 10 s at the menu, "no recipe" 3,800-3,900,
attaches 227 (as without the change).** So the recipes are not built for these draws in the first place: the
candidates in `Graphics::Draw` are `cacheable`, `!waited`, direct, `Reusable()`, no address table, **no moved
buffers**, not multisampled. The menu's template hits re-point the moved constant block each frame
(`MovedReadOnlyBuffers`), which likely fails the "no moved buffers" rule. Run l3c5b counts the reasons
(`recipes not built because:` in the `[draws]` line).

## Round 5: run l3c5b (commit 77b53cc01), why no recipe is built

`[draws]` now counts the rule each recipe candidate fails. Menu, per 10 s, about 13,000 candidates: moved
buffers 11,750-11,820; not cacheable, not reusable and address table 1,240-1,260 (the address-based draws);
waited, indirect, multisampled 0. World: moved buffers 6,500-9,800, address table 3,400-5,500. So the rule
"no moved read-only buffers" refuses the recipe of nearly every engine draw: the engine's constant block sits
in a ring, so the template hit re-points that buffer each frame (`MovedReadOnlyBuffers`, applied by
`PrepareDrawBindings`), and a recipe was defined as a draw with nothing to re-point. Run l3c6b (commit
b6790e2fd) lets a recipe cover such a template and makes the hit re-point the buffers the same way.

## Round 6: run l3c6b (commit b6790e2fd), recipes over moved buffers

Draw recipes now serve engine-cache hits at the menu: **2,300-2,400 recipe hits per 10 s** (was 12-15), each
with the data refresh recorded (2,160 per 10 s), at 33-37 us per recorded draw against 146-148 us for a template
hit (proof 6.8 us, record 16.9 us). Recipes attached 9,435 per 10 s; still not built: moved buffers 9,360
(templates whose moves the recipe path could not take, or first sightings), the 1,250 address-based draws.
World: 0 recipe hits (the engine cache hits 3% there). Menu frames 60.4 per 10 s (A 58.2), committer 137.6 us
per commit (A 140.7), busy 43%: the 2,400 recipe hits save about 0.26 s of committer time per 10 s, and the
frame still sits on the dispatches and the drains. No errors; menu frames l3c5b vs l3c6b differ by 8.5/7.7/6.3
per channel and l3c1a vs l3c6b by 7.7/7.0/5.8, so two cache runs differ from each other as much as a cache run
differs from the plain run: the differences are run-to-run drift, not the cache.

**World regression.** Every cache run had 9.2-9.5 presented frames per 10 s in the world against 10.9 for the
plain run (l3p7: 10.2-12.0): the served-then-missed calls pay the relocation and a failing validation on top of
the capture (key/lookup/validate 30 to 42-53 us per draw). Commit after b6790e2fd adds a per-kind trial: a kind
whose served calls validate below 1 in 8 over 512 calls is not served for the next 8,192 calls.

## Round 7: run l3c7b (commit 07294354f), the per-kind trial

The trial idles the world's DrawItem, DrawInstanced and instanced-b calls (3,500 / 3,200 / 560 idled per 10 s
early, 7,500 / 5,400 / 2,600 later; served calls fell from 17,000 to 3,100 per 10 s, validated hits stayed at
713). Menu unchanged (60.0 frames per 10 s, 7,600 served, 5,060 hits, 2,220 recipe hits at 29 us). **The world
stayed at 9.4 frames per 10 s** with the worker at 176 us per draw (key/lookup/validate 54 us): the draw cache
validated 383 draws per 10 s at 1,138 us each, against 280 at 77 us in the plain run. So the regression is not
the served-then-missed calls. The remaining difference between the runs is the relocation eligibility of
vertex stages with fetch-shader resources, which splits every such variant's runs by their base word
(`tagVariantRuns`) for every draw, served or not. Run l3c8b keeps the plain eligibility
(`APS5_PORTER_L3_PLAIN_RUNS=1`) to test that.

## Round 8: run l3c8b (commit c31f87995), plain relocation eligibility

With `APS5_PORTER_L3_PLAIN_RUNS=1` the vertex stages with fetch-shader resources are not relocatable again:
the engine cache serves 920 calls per 10 s at the menu and validates 17 (the fetch stages refuse), the draw
cache validates at 100 us per draw (plain 77, round 7 1,138), the worker is back at 59.9 us per draw at the
menu. **The world still ran at 9.6 frames per 10 s** (plain run l3c1a 10.9), with the committer at 190 us per
commit (166). So neither the served-then-missed calls nor the run splitting explain the gap; what is left is
run-to-run drift (one plain sample at 16:23 against seven cache samples from 16:30 to 17:47; Celegans12
measured 22% between two unchanged runs). Runs l3c9a and l3c9b repeat the plain configuration twice, back to
back, to measure that drift.

## Round 9: runs l3c9a, l3c9b (plain, 17:48-18:01) and l3c10b (commit 670693fcf, 18:10-18:17)

The two plain repeats ran the world at 10.4 and 10.6 presented frames per 10 s (l3c1a 10.9), the seven cache
runs of rounds 2-8 at 9.2-9.6. That is not drift. The `[draws]` lines locate the loss: every address-based
build (the "BDA" kind of the committer's resources phase) took 452-553 us in the cache runs against 320-370 us
in the plain runs, rising through the world phase, while template hits (31-36 us) and builds (211-245 us) did
not change. The run monitors show why: the cache runs ended with 36.1-36.3 GB private bytes against 34.9 GB
plain, and the working set fell from 52 GB to 46 GB in the last minute (plain: 52 GB throughout). The alias
map kept every learned draw entry alive (`std::shared_ptr`): 30,000-62,000 entries against the draw cache's
own cap of 4,096 (`APS5_DRAW_CACHE_ENTRIES`), with the variants' captured words and compiled results behind
them, on a machine where the game already takes 52-59 of 63 GB. Round 1 (10.9 in the world) kept one record
per object, so about 30,000; from round 2 on (one record per object and pass) the map grew by 3,000-6,000
records per 10 s and the working-set trimming began.

Fix (670693fcf): `EngineAliasRecord::entry` is a `std::weak_ptr`; an evicted entry is a new alias miss
"entry evicted" and the record is dropped. Run l3c10b against the plain l3c9a:

| Metric | A plain (l3c9a) | B cache (l3c10b) |
|---|---|---|
| Presented frames / 10 s, menu | 59.8 | 58.3 |
| Presented frames / 10 s, world | 10.4 | 10.3 |
| BDA build us (last four world windows) | 325 / 323 / 333 / 324 | 325 / 343 / 351 / 370 |
| Private GB at the end of the world | 34.9 | 34.8 |
| Committer us per commit, world | 163.0 | 173.1 |
| Worker us per draw, menu / world | 59.7 / 131.4 | 53.9 / 128.8 |
| Captures / 10 s, menu | 25,998 | 18,563 (-29%) |
| Stages compiled / 10 s, menu | 35,059 | 26,442 (-25%) |
| Engine cache, menu: served / validated hits | | 7,356 (46%) / 4,864 (31%) |
| Engine cache, world: served / validated hits | | 3,474 (13%) / 836 (3%) |
| "entry evicted" misses, world, per 10 s | | about 530 |

The world runs at the plain rate again. The per-kind trial idles the kinds that validate below 1 in 8, which
is why the served share is 46% at the menu and 13% in the world; the validated hits are as in rounds 2-8. The
world's hit rate stays at 3% until the SRT walker change below.

## Round 10: the SRT walker counts a user-data V# base as a relocatable base (cold shader cache)

The refused world words (GS 0x8e, 0x92, 0x96, 0x9a = user words 2, 6, 10, 14; PS 0x22, 0x26) are 4 words
apart: buffer descriptors (V#) the game passes in user SGPRs, whose first dword is the base address. The SRT
walker evaluates every descriptor dword through the general evaluator, which marks a user word read by
`GetUserData` as a value (`asValue`), so the driver's relocation refuses the moved base. Change (in
`SrtDescriptorEvaluation.cpp` and `SrtTape.cpp`): the first dword of a 4-dword descriptor source that belongs
to a read-only buffer (`info.buffers`, not written, not atomic) and comes straight from a user-data word is
read as a base (`asBase`), as a pointer the shader reads through would be. The descriptor is evaluated again
from the current user words at every bind, so a moved base binds the moved buffer; nothing cached depends on
the base address. The files are in the shader cache's source list, so the SourceVersion changes and the
cache compiles cold once (about 53 minutes to the world); the user approved this at 18:02.

Runs: l3c11w (18:19, died after 3.5 min: `ezo-run.ps1` threw on a sharing violation reading the monitor csv,
now retried), l3c11x (18:26-19:09, the warm-up: menu after 3 min, 600 s menu, 1,800 s world; 47,062 cache
entries, 0 misses per 10 s at the end), l3c12a/l3c12b (19:10-19:24; l3c12a died 20 s into the world with
`AGC driver: truncated shader metadata` from ShaderRegistry.cpp:994, the shader-registration flake, first seen
in runs-best; l3c12b completed with the trial idling the item and instanced kinds), l3c13a/l3c13b (19:24-19:37,
plain and cache with the new `APS5_PORTER_L3_NO_TRIAL=1`, commit c5a6f17ba, so every call is served).

| Metric, world | l3c13a plain | l3c13b cache, trial off | l3c12b cache, trial on |
|---|---|---|---|
| Presented frames / 10 s | 10.6 | 9.7 | 9.7 |
| Engine calls / served / validated hits per 10 s | | 24,860 / 16,859 (68%) / 830 (3.3%) | 25,337 / 3,259 (13%) / 830 |
| Worker us per draw (key/lookup/validate) | 129.4 (30.7) | 156.5 (44.0) | 141.7 (39.1) |
| Committer us per commit; BDA build us | 164.3; 306-333 | 194.3; 389-468 | 183.1; 325-370 |
| Private GB at the end | 35.0 | 35.0 | |
| Moved 0x8e / refused (last 10 s) | | 17,485 / 7,079 | |
| Moved 0x92 / refused | | 8,003 / 2,863 | |
| Moved 0x96 / refused | | 7,378 / 2,859 | |
| Moved 0x9a / refused | | 4,405 / 4,084 | |
| Also refused: 0xa8 / 0x9e / 0xa4 | | 939 / 766 / 279 | |
| Variants allowed / refused per 10 s | | 13,066 / 19,041 | |
| Menu: served / validated hits per 10 s | | 13,977 (86%) / 6,141 (38%) | 7,249 / 5,815 |

- The change works where it applies: 60% of the moved 0x8e words, 64% of 0x92 and 61% of 0x96 are now
  relocatable (round 2: 0%), and the menu's validated hits rose from 32% to 38% (6,141 per 10 s).
- The world hit rate did not move (3.3%), because a variant is refused when any one of its moved words is not
  relocatable, and 19,041 variants per 10 s still are: 0x8e (40% of its moves), 0x9a (93%), 0x92, 0x96, and
  0xa8/0x9e/0xa4 (GS words 28, 18, 24). "A stage lost every variant" (19,001) is the whole miss count. Word 14
  (0x9a) and the words at 18-28 look like an 8-dword T# or a written buffer, which the change excludes; word 2
  must be read as a value somewhere in 40% of the GS shaders. Run l3c14d dumps the shader requests
  (`APS5_DUMP_SHADERS=1`) for the replay tool, so the next walker change (one more cold cache) is made from the
  IR instead of a guess.
- Serving every world call costs: a served call that misses pays the relocated copy and the validation (worker
  key/lookup/validate 44 us against 31), and the copies' churn slows the committer's address-based builds (390-470
  us against 306-333; private bytes equal, so it is not the memory retention of rounds 2-8). World 9.7 against
  10.6 frames per 10 s. The per-kind trial (default on) keeps that cost away until the hit rate is there.

## Round 11: what still refuses (runs l3c16d and l3c17b, 19:50-20:05)

Two diagnostics, both driver-side (same shader cache): `APS5_PORTER_L3_DUMP_STAGES=1` (commit 7411ece01)
writes one request file per engine-draw shader with the walk's asBase/asValue masks (`[l3-stage]`, summarised
by `porter/tools/l3stage_report.py`), and `APS5_PORTER_L3_TRACE_REFUSALS=N` (478b77dfe) prints the first N
refusals per register with every moved word (old -> new) and the donor's user words (`[l3-refused]`). Run
l3c15d died in the game's loader before the menu (access violation app.exe+0x11328f5, the known flake); l3c16d
dumped 1,485 engine-draw shaders (598 vertex on the NGG stage, base 0x8c; 877 fragment, base 0xc; 1 domain);
l3c17b traced the refusals with the cache on and the trial off. Request files: `yotei-work/porter/out/req-l3c16d`,
disassemblies and IR dumps (`agc_shader_replay --asm`, `APS5_DUMP_IR`) under `porter/out/asm-l3c14d`,
`porter/out/ir-l3c16d`.

What the walk now says about the vertex shaders' user words 2/6/10/14 (l3c16d, new walker): 440 of 598 read
word 2 as a base (the V# change at work), 44 as a value, 114 not at all; the common layouts are B B B (175),
B B (96), B B B B B (93), B B B B (84). Fragment shaders: 629 of 877 read words 2 to 28 as values (their user
data is mostly plain values and T#/S# descriptors, not pointers).

What the refusal trace shows (`[l3-refused]`, world): the refused words are of two shapes.

1. **Plain 64-bit pointer pairs (lo, 0x20)** that follow the V#s, e.g. shader 0x8000411600 (words 0-1 pointer,
   2-5 V#, 6-7 pointer, 8-9 pointer into 0x80...): `relocatable 0x5 walked 0x3ff`, word 6 moved by the same
   delta as words 0 and 2 (one ring block). Its IR shows word 6 is not read by the SRT walk at all (asBase 0x5,
   asValue 0x3a): the `s_load_dwordx4 s[8:11], s[14:15], vcc_hi` through it loads the **vertex-fetch buffer
   table** (`ShaderVertexStageInfo::fetchBufferReg`), and round 8 marks the fetch words as `excluded`, which
   is "walked, not relocatable" by construction. So the biggest refuser in the world (0x8e 7,079, 0x92 2,863,
   0x96 2,859, 0x9a 4,084, 0x9e 766, 0xa8 939 per 10 s) is the fetch-table pointer of the vertex stages, which
   moves with the ring every frame. That is a driver change, not a walker change: the fetch word must become a
   relocatable base whose reads (the V#s the fetch resolves at `ptr + vcc_hi`) are re-read at the new address
   and compared, like the SRT walk's reads.
2. **A V# whose base moves by a small offset** (shader 0x8028097500, word 10: `50a01da0 -> 50a01d20`,
   `relocatable 0x400001`): a descriptor built from a user-data pointer plus an offset, the "pointer plus
   offset" case; the walker's V# change covers only a base taken straight from the word. A walker extension
   (`IAdd32`/`IAddCarry32` of a user word as a read base or descriptor dword 0) would cover it, at the price of
   one more cold cache; it is the smaller share.

The walker change of round 10 was right for what it addressed (menu hits 32% -> 38%) but the world's refusals
are mostly the vertex-fetch words, which the recompiler never sees.

## Where the frame time is (committer, `[draws]` lines of l3c2b)

| Phase | Menu (17,166 draws / 10 s, avg 162 us) | World (17,710 draws / 10 s, avg 200 us) |
|---|---|---|
| readTarget (render-target lookup; pages walked) | 732 ms (3.9 M pages, 721 ms) | 562 ms (2.0 M pages) |
| lookup (resource cache) | 638 ms | 82 ms |
| resources (bindings, upload, address-based builds) | 753 ms (bindings 377, upload 155, 3,008 BDA builds 179) | 2,323 ms (bindings 1,255, 5,670 BDA builds 964) |
| record | 339 ms | 232 ms |
| vertex | 144 ms | 139 ms |
| per kind, avg us (count) | recipe hit 43 (20), template hit 127 (14,011), build 1,506 (127), BDA 274 (3,008) | template hit 41 (9,716), build 286 (2,324), BDA 459 (5,670) |

A draw recipe records in 43 us against 127 us for a template hit, but draw recipes get 17-20 hits per 10 s:
they are found under the exact draw key, which is new every frame, and a data hit (patched stages) excludes them.

## What this proves, and what is next

1. The engine key recovers the previous frame's prepared draw for 88% of the menu's engine draws and 72-81% of
   the world's, and the validation keeps rendering unchanged. This is the identity the recipes and the
   descriptor tables need.
2. The queue-0 worker's draw cost is not the frame: the menu frame sits on the dispatches and on the draw
   pipeline drains before them (3.3 s per 10 s), which the committer's 137-162 us per draw sets. Cutting the
   worker's captures by 19-39% moved nothing.
3. The committer's cost per draw is readTarget + lookup + resources + record. The recipe path skips the first
   three. Next step: let an engine-alias hit find and attach its draw recipe (match recipe records by the donor
   variants instead of the per-draw relocated copies, attach to the donor entry) and give draw recipes the data
   refresh that dispatch recipes already have, so a data hit still records from the recipe.
4. The world needs the SRT walker to treat "pointer plus offset" bases and descriptors built from a pointer as
   relocatable (0x8e, 0x92, 0x96, 0x9a of the geometry stage; 0x22, 0x26 of the pixel stage). That is a
   recompiler change in `core/shader/recompiler/Optimization/src/SrtWalker`, which is in the shader-cache
   source list: it changes the cache version and costs one cold compile (about 53 minutes to the world).
   Not started; it needs the user's go-ahead for the cold cache.
