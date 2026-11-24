# Renderer studies and work: internal resolution, anti-aliasing, GPU texture work

Merged 2026-10-22 from `internal-resolution.md` ("Feasibility: higher internal rendering resolution", part 1) and `gpu-work.md` ("GPU work: what can move off the CPU, and the plan for GPU texture decoding", part 2), no content removed; their section numbers are kept with the prefix 1. and 2. (`internal-resolution.md` §4.5 is §1.4.5 here, `gpu-work.md` §3 is §2.3). The renderer itself: ADR-003, `../platform/gs-subset.md`, `../subsystems/rendering.md`.

## 1. Internal rendering resolution (feasibility study and implementation)

Status: **implemented 2026-10-19, factors 1–8** (User request; `../decisions/port-baseline.md`). §1.1–1.6 are the original study (2026-10-11); §1.7 says what was built and how it was checked.

Sources: `../subsystems/rendering.md`, `../platform/gs-subset.md` (static scan + eight GS dumps), `../subsystems/camera-hud.md`, `../subsystems/effects.md`, ADR-003 / ADR-006.

### 1.1 Why it cannot change game logic

- The game **never reads GS memory back**: the dumps show host → local uploads only, no local → host transfer and no local → local copy (`gs-subset.md`, "Measured in GS dumps"). No game function touches GS privileged registers or VU memory (`rendering.md`).
- So nothing the renderer produces flows back into guest RAM. The packets, GS register values, camera records (+0x160 screen size, +0x178 centre, +0x188 viewport) and every HUD/lock-on projection stay in native 512×448 coordinates in guest memory.
- Scaling is applied **inside the renderer only**, after it has parsed the guest packets. Game code, `src/sce/` and the guest GS state are unchanged at any N. The random sequence and draw-list build are unaffected (they run before the renderer sees anything).

### 1.2 How scaling works in a GS-replay renderer

A GS coordinate is `(XYZ − XYOFFSET) / 16` pixels (12.4 fixed point). With scale N the renderer maps a GS pixel (x, y) in a render target to host pixel (N·x, N·y) of a surface allocated N× larger. Everything the GS addresses in **pixels of a render target** scales; everything addressed in **texels of an uploaded texture** does not.

### 1.3 Draw paths that work unchanged

| Path | Why it scales cleanly |
|---|---|
| 3D models through the VU1 programs (A–E): triangle strips/fans, Gouraud, STQ, fog | VU1 HLE outputs GS vertices; only the final XY is multiplied by N. Perspective UV (STQ) is resolution-independent. Mip selection (trilinear, LCM 0 + K) must use the native-resolution LOD formula or textures get sharper/blurrier per level (it uses Q, not screen derivatives, so it is unchanged) |
| Terrain and sea (`DrawTerrain`, sea tiles clipped on the CPU) | same as above; CPU clipping is in 3D, not screen space |
| Sky, weather, world objects, projectiles | same VU1/GIF triangle paths |
| Effects drawn as textured triangles/sprites with STQ or with UV from uploaded textures | geometry scales; texture lookup unchanged |
| Screen fade (bucket 2 of the overlay table), full-screen tints | full-screen sprites: rectangle scales, flat colour |
| Scissor / viewport (`SCISSOR_1/2`, camera +0x188) | multiply by N (inclusive max: `N·(max+1) − 1`) |
| Depth test / Z buffers (Z24, Z32), DATE, alpha test, blending | per-pixel operations: identical at any N |
| Movies (640×448 MPEG frames; 640×336 for SS) | uploaded as textures and drawn as sprites: the sprite scales. At N > 1 the frame texture is replaced by the output of the movie upscaling stage (FSR 1, 640N×448N), User decision 2026-10-13 (`../decisions/ADR-006-presentation-resolution.md` §5); at 1× the 640×448 texture is used as on the PS2. The stage, the decoder side path and the draw identification (`DrawCommand::texture_guest_source`) exist since S3. Still to do with the scaled GS backend: sampling the stage output in those draws |

### 1.4 Paths that break, and the fix for each

| # | Path | What breaks at N > 1 | Fix |
|---|---|---|---|
| 1 | **2D sprites, HUD, text, menus** (≈40 textured SPRITE functions, `DrawHud`, fonts) | Sprites map texels 1:1 to native pixels with bilinear filtering. At N× each texel spans N pixels and bilinear sampling between texel centres softens text and shows seams where atlas sub-rectangles touch (REGION_CLAMP/REPEAT atlases) | For sprites (PRIM SPRITE with FST UV): **round UVs to the native pixel grid**, i.e. compute the native pixel the host pixel belongs to and sample at that pixel's UV ("round sprite"). Optionally switch 1:1 sprites to nearest when UV step = 1 texel per native pixel. Region clamp rules stay in texel space |
| 2 | **Lines and line strips, AA1** (≈20 functions: HUD pitch ladder, radar, lock boxes, map) | GS lines are 1 pixel wide: at N× they become 1/N as thick and AA1 coverage is computed at the wrong scale | Draw lines as quads **N pixels wide**; AA1 = coverage ramp over the outer pixel at host scale. HUD look must be checked against native screenshots |
| 3 | **Framebuffer used as a texture** (PSMCT32 1024×1024 "screen copies", field copy, glow composite, **heat-haze distortion** `func_001E88C0` with displacement offsets in pixels) | the texture is the render target itself: TEX0 TBP points at an FBP page, UVs are in GS texels of the native target | The render-target cache (§1.5) resolves TBP → scaled surface and divides UV by the **native** target size, so the sample lands on the same native location; bilinear at scaled resolution then reads N× more detail (desired) |
| 4 | **Field copy** `DrawDisplayTransfer` 0x001049B0 (448-line draw buffer → 224-line display buffer, 32-px strips, flicker blend) | scaled draw buffer → scaled display buffer: works, but the vertical halving and line blend operate on N×-scaled lines | With ADR-006 §4 option A (present the draw buffer), the copy still executes (GS state stays exact) but its output is not presented: no visible issue. Options B/C (weave/bob) would read the display buffer at N× and need line-pair selection at host scale. Decide together with §1.4 |
| 5 | **Render-to-texture chains: aircraft shadow** (page 0x2500, half-size copy, 3-offset blur, blend onto frame) and **small targets** FBP 0x120–0x1C0 (64/128/256 px wide) | If the small targets scale too, the blur's ±1 texel offsets become ±1 *native* texel = N host texels apart: the blur gets a dotted, aliased look. If they do not scale, the composite onto the scaled frame is magnified (soft, like the PS2) | **Per-target scale policy**: main framebuffer and depth at N; small off-screen targets at **1× by default** (keeps the PS2 look and the blur kernel exact), optional N later per chain after visual comparison |
| 6 | **Half-resolution glow pass** `func_00142460` (camera ×0.5 via `BuildScaledCameraMatrices`, 256-px target at block 0x2500, Z24 at 0x2A00, blur, add in 256×16 strips) | its Z buffer (0x2A00) is a separate half-size Z: fine. But if the main Z were sampled into it, sizes must agree. The strips are sampled from the half-size target | Treat as a small target (rule 5): 1× by default. Its vertices are scaled by the camera, so they land correctly in a 1× target; the composite sprite scales to the N× frame. If later scaled, scale the target **and** its Z by the same N |
| 7 | **Depth sampled as a texture** (PSMZ24 as texture, FBP 0x150) and **format reinterpretation** (CT32 target read as T8H palette index in the alpha byte, CT24 with TCC 0, CT16S target sampled as CT16S) | pixel-preserving reinterpretations work per pixel at any N. A reinterpretation that changes the pixel layout (e.g. a CT32 page read as PSMT8/PSMT4 swizzled data) would need native data | The dumps show only pixel-preserving cases. Rule: if the cache sees a layout-changing reinterpretation of a scaled target, it **downsamples to native** (one sample per native pixel, nearest) and converts; log it once. Built 2026-10-22 for 32-bit/16-bit views of one buffer (the flight's work page at FBP 0x180, CT32 and CT16S, sampled by the explosion debris): such a buffer is held at 1×, all its views (`validation.md` §2z) |
| 8 | **Uploads into a region that is also a render target** (IMAGE transfer whose DBP overlaps an RT) | a native-size upload into an N× surface | Upload to a native staging texture, then draw it scaled (nearest) into the surface. Movies and texture uploads normally go to texture pages, so this is a corner case; detect and log |
| 9 | **Sub-pixel rules** (top-left fill convention, 12.4 snapping, sprite edge rounding) | half-pixel offsets differ at N× (thin cracks between sprites, 1-host-pixel shifts) | Compute coverage in GS 12.4 units scaled by N (exact for integer N); sprite edges rounded in native units then multiplied by N |
| 10 | **Scissor-dependent strip work** (256×16 composite strips, 32-px field copy strips) | strips meet exactly at native pixel boundaries; at N× they still meet if edges are integer multiples | integer N only (2, 3, 4). Non-integer scales are not supported |

### 1.5 Effort

| Work | Effort | Notes |
|---|---|---|
| Scale plumbing: one GS→host transform, surfaces "GS region × scale", scissor/viewport | 2–3 days | cheap if designed in from day one (§1.6) |
| Render-target cache keyed by (page, width, PSM) with per-target scale, TBP→surface resolution, UV normalisation by native size | 0 extra | **needed at 1× anyway** (render-to-texture, framebuffer-as-texture) |
| Sprite UV rounding (fix 1), line quads + AA1 (fix 2) | 4–6 days | most visible work; HUD must match native screenshots |
| Per-target scale policy (fixes 5–6), downsample path (7), RT-overlap uploads (8) | 3–5 days | |
| Validation: replay the eight GS dumps at 1× and N×, downsample N× to native and diff against the 1× image (tolerance), plus side-by-side screenshots per scene | 4–6 days | tooling reusable for the 1× renderer |
| **Total** | **≈ 13–20 days** on top of the 1× renderer | renderer-only |

Nothing outside the renderer changes: no game code, no `src/sce/`, no guest memory layout, no timing. The output pass (ADR-006) scales from N×512×448 instead of 512×448; the `sharp`/`smooth` filters still apply (at N ≥ 2 the integer pre-scale of `sharp` is usually 1).

### 1.6 Recommendation: design for a scale factor now, ship 1×

1. **One transform function** maps every GS coordinate to host pixels: `HostXY = (XYZ − XYOFFSET) · N / 16`. Scissor, viewport and copies use the same function. No other code multiplies by a resolution.
2. **Render targets are allocated as "GS region × scale"**, with the scale stored per target, chosen by a policy function (main framebuffer/Z: N; small targets: 1). The policy returns 1 for everything while N is fixed at 1.
3. **A page-tracked render-target cache** (needed at 1× for render-to-texture): lookups by TBP/FBP page, width and PSM; every sample from a target normalises UV by the **native** size.
4. **Sprite and line rasterisation rules in one place** (fixes 1, 2, 9), parameterised by N, so they are exact at 1× and switch on with N > 1.
5. **Internal scale is a setting** (`settings.json`, `presentation.internal_scale`, accepted value 1 only) until each draw path in §1.4 is validated with the dump comparison; raising the allowed values is an User decision (baseline "Later" item).

This keeps the 1× renderer simple (N = 1 makes every rule the identity) and avoids a later rewrite of coordinate handling across the ≈233 packet-writing paths.

### Open points

- Which ADR-006 §4 interlace option is chosen (affects fix 4 only).
- Whether the shadow/glow chains look better scaled (fix 5) is a visual judgement for the User once the renderer runs.

### 1.7 Implementation (2026-10-19)

Setting `presentation.internal_scale` 1–8 (default 1; `--internal-scale <n>` for a session; changes in `settings.json` apply while running). Code: `src/platform/render/gs/gs_vulkan.*`, `shaders/gs.frag`, `shaders/gs_conv.comp`, `src/platform/render/renderer.*`.

| Study item | As built |
|---|---|
| §1.6.1 one transform | the vertex shader is unchanged: it maps GS pixels onto the whole target image, and each target's image is its native region × its scale, so the viewport does the scaling. Scissor, DATE copies, depth fills and copies multiply native rectangles by the target's scale. Host pixel *h* samples GS position (*h* + 0.5)/N − 0.5: the N sub-pixels of a GS pixel are centred on its sample point, so the N × N average lands on the native image |
| §1.6.2 per-target scale | `TargetScale`: targets 512 or more pixels wide (draw, display and sky work buffers) at N, the 64–256-wide shadow and glow targets at 1× (fixes 5, 6). Bounded by the device's maximum image size for 1024 rows. Scaled targets hold only the rows drawn so far (rounded up to 64, grown on demand, old images freed when their frame slot comes round again), so 8× costs about 64 MiB per 512 × 512 buffer |
| fix 1 sprites | textured sprites from local memory, at N > 1: UV taken at the native pixel each host pixel lies in (gs.frag flag 26), so HUD, text and atlas art keep their texel grid and atlas cells never bleed. Triangles are not rounded. The sample point of native pixel *n* is floor(g + ½) (g = GS position of the host pixel; floor(g) before 2026-10-22 shifted sprites by half a native pixel, `validation.md` §2y) |
| fix 2 lines | lines and points are quads one GS pixel wide, so N host pixels at N× (AA1 coverage still not modelled, as at 1×) |
| fix 3 frame buffer as texture | the copy into the scratch image keeps the source's scale; gs.frag samples it on that host texel grid with the wrap modes applied per GS texel (flag 16, `tex_scale`) |
| fix 7 reinterpretation | every format conversion (gs_conv.comp) and target sync works on GS pixels: one host sample per GS pixel (the centre sub-pixel), never a blend; a synced scaled target gets each converted pixel as an N × N block |
| movies (§1.3, ADR-006 §5) | draws whose texture is a movie column (DMA source marks, `MovieTexelOrigin`) sample the FSR 1 output bilinearly at the picture position (flag 27); no column seams |
| presentation | the field copy's source is copied at its scale; the native image is the presented size × N; the output pass and its filters are unchanged |
| fix 9 sprite edges (2026-10-20) | at N > 1 every sprite's edges move to the borders of the native pixels the GS covers (pixel *x* is drawn when x0 ≤ *x* < x1, so the edges become ceil(x0) − ½ and ceil(x1) − ½), the texture coordinates moved along. Without it, sprites that meet between two native sample points left host pixels uncovered: the glow pass's 32-pixel upsampling strips (one ends at 31.5, the next starts at 32.0) drew dark vertical lines every 32 pixels over the mission 1 opening and flight at 2× and above (`validation.md` §2m, ST-5). 1× is unchanged (`gs_vulkan.cpp`, sprite expansion) |

Exception to fix 1 (2026-11-22, `presentation.text_filter` = `sharp`): glyph sprites (a T4 texture whose palette is a coverage mask, `CoverageMaskAlpha`) are not rounded to the native grid but sampled at the host pixel with full-precision bilinear and a half-coverage alpha step (gs.frag flag 30); `port-features.md` §2 "Sharp text".

Not done: per-chain scaling of the 1× targets, AA1 edge coverage (as at 1×). Results: `validation.md` IR-1 to IR-3, §2m.

### 1.8 Anti-aliasing and anisotropic filtering: feasibility (User question 2026-10-20)

Status: FXAA and SMAA **built** 2026-10-20 at the User's request, applied before the HUD (`port-features.md` §2, setting `presentation.antialiasing`); the HUD marker described under "FXAA" below is the one used. The other options are still studies and need the User's approval (`../decisions/port-baseline.md`, "Later").

#### What the original does

The PS2 game has no anti-aliasing of 3D edges. Its picture is softened only by the display path: the field copy and the interlaced CRT output. The GS's AA1 flag is used for HUD lines only (edge coverage not modelled by the port, as at 1×). Textures are filtered by the GS rules: bilinear magnification, nearest / bilinear / trilinear minification with mip levels chosen per primitive from Q (LOD = log2(1/Q) · 2^L + K, TEX1), not from screen-space derivatives. `shaders/gs.frag` reproduces those rules by hand (`texelFetch` from the texture atlas), so no Vulkan sampler state (hardware mips, anisotropy) is involved.

#### What the port already has

The internal resolution (1–8×) is supersampling: every frame-sized buffer is drawn at N × N samples per GS pixel. The output pass then scales the N× image to the window with `sharp` or `smooth` bilinear filtering. When the N× image is larger than the window (for example 8× in flight = 4096 × 3584 into 2160 lines), bilinear filtering reads only 2 × 2 texels per screen pixel, so part of the supersampling is thrown away and fine edges still shimmer.

#### Options

| Option | What it needs | Cost at run time | Benefit over the original | Risk / effort | Recommendation |
|---|---|---|---|---|---|
| **Area (box) downsample in the output pass** | `shaders/output.frag`: when the source is larger than the output, average the whole source footprint of each screen pixel (or a mip chain of the native image) instead of one bilinear sample; a third filter value or automatic below 1:1 | one pass over the native image, < 0.3 ms at 4K | turns internal resolution 2–8× into full supersampling (SSAA) of 3D edges, textures and alpha-tested edges, the best quality of all options, with no change to how the game is drawn | low; 1 day, no dependency | **do first** |
| **FXAA** (or SMAA 1x) | one full-screen post pass on the native image before the output pass. The game composes HUD, text and menus into the same frame buffer, so a pass over the whole picture also softens fonts and HUD lines. Limiting it to the 3D part needs a marker for where the HUD begins in the frame (the HUD's ordering-table bucket, e.g. through a DMA source mark): extra work in the GS backend | 0.1–0.3 ms at 4K | smoother 3D edges at 1× and 2× for almost no cost; little gain at 4× and above once the downsample exists | low to medium; 2–3 days (without HUD masking: 1 day). FXAA 3.11 or SMAA would be third-party code (User approval and a GPL-3.0 licence check, `../decisions/dependencies.md`), or an own implementation of the published algorithm | optional, after the downsample, for low internal scales |
| **MSAA** | multisampled colour and depth images for every scaled target; a resolve before every read of a frame buffer as a texture (field copy, glow chain, heat haze, sky work pages: several per frame), per-sample DATE copies, format conversions (`gs_conv.comp`) on resolved images, pipelines per sample count; alpha-tested geometry (foliage, fences, sprites) gets no AA without alpha-to-coverage, which changes alpha-test results | memory × samples (4× MSAA at 4× scale: about 1 GB of targets); the resolves | geometric edges only, the same as supersampling already gives; nothing for textures or alpha edges | high; 1–2 weeks plus validation of every frame-as-texture path; risks the exactness of the GS blend and feedback effects | **not recommended**: internal resolution plus the downsample gives the same or better result |
| **Anisotropic filtering** | in `gs.frag`, for minified textured triangles: take the footprint from the UV derivatives, choose the mip level from its minor axis and average 2–16 trilinear taps along the major axis (manual, from the atlas mip levels the port already decodes). It replaces the GS LOD rule for those draws, so it changes which mip level the game's textures show | 8–32 more texel reads per affected pixel; fine on current GPUs | sharper ground textures at shallow angles (terrain, runways, the carrier deck at low altitude); the internal resolution cannot give this, because the GS LOD depends on Q, not on the resolution | medium; 2–4 days plus a comparison per mission; only textures with mip levels (terrain, `MTBA` / `MIPTBP`) gain | optional later; modest gain, changes the look of distant ground |

#### Recommendation

1. Area downsample in the output pass: highest quality per effort; makes the existing 2–8× setting work as supersampling at any window size.
2. Optional FXAA / SMAA for 1× and 2× (without HUD masking first, with a setting off by default).
3. Anisotropic filtering only if distant ground looks too soft after 1 and 2; MSAA not needed.

## 2. GPU work: what can move off the CPU, and GPU texture decoding

Status: **implemented 2026-10-22** (User: GPU texture decoding, content hashing cut and atlas replacement approved after the step 1 measurement; as built: §2.7). Written after `validation.md` §2r IN-2 (intro stutter). §2.1–§2.6 are the plan and the step 1 measurement as written before the work.

User decisions 2026-10-22:
- This document is the record; no ADR-003 amendment.
- Step 1 (measurement, §2.5) approved; stop after it and report.
- Scope if the plan is approved: only the first three rows of §2.2: GPU texture decoding (§2.3), reducing the content hashing, and replacing the atlas with per-texture images (§2.4, first item). The render thread and everything else in §2.2 and §2.4 are out of scope.

Sources: `src/platform/render/gs/` (`gs_state.cpp` DecodeTexture, `gs_vulkan.cpp` Record, `shaders/gs_conv.comp`), `../platform/gs-subset.md`, `validation.md` §2r, ADR-002, ADR-003.

### 2.1 Where the CPU time goes now

One thread does it all, in order, each host frame (`src/main.cpp`): the game ticks, then `Renderer::SubmitGsSteps` (GIF parsing, local-memory writes, **texture decode** in `GsState`), then `GsBackend::Record` (atlas packing, **staging copy of decoded RGBA**, command recording). The game thread is parked while the renderer works, so every millisecond here is a millisecond of frame time.

Measured in the intro (§2.2r IN-2): the slow frames were 40–50 ms of GS processing plus 20–40 ms of recording, almost all of it texture decode and RGBA upload. The remaining slow stretch (vblank ~2390–2460) is the terrain: one mipmapped 512×512 T8 texture at 0x2900, 600–990 of its 1,360 blocks rewritten between consecutive decodes, about 4 full decodes plus mip levels and uploads per frame.

### 2.2 Candidates

| Work | Today | Move to GPU? | Why |
|---|---|---|---|
| Texture decode: page/block/column layout → texels, CLUT lookup, TEXA expansion, mip levels | CPU, per draw at kick time (`DecodeTexture`), RGBA8 uploaded to the atlas | **Yes, §2.3** | Integer bit work, embarrassingly parallel; the shader already exists in reduced form (`gs_conv.comp` reads frame buffers through the same layout tables). Removes the intro stutter's cause. Upload shrinks to the raw bytes the game sent (T8 = ¼ of RGBA, T4 = ⅛). |
| Content hashing of texture pages (`RectHash`, `RegionHash`, FNV over 8 KiB pages) | CPU | Reduce, not move | Exists to avoid CPU re-decodes. With GPU decode a re-decode costs microseconds, so the key can be block generations instead of content hashes (measure: the hangar cycles textures through one slot). |
| Atlas packing and residency (`resident_`, shelf packer, growth to 8192²) | CPU bookkeeping, cheap | Optional later, §2.4 | Not a time problem. A per-texture image pool with descriptor indexing (Vulkan 1.2) would remove resets and the 256 MiB grown atlas; separate change. |
| Frame buffer read in another format / width | **Already GPU** (`gs_conv.comp`) | — | Becomes the same shader as §2.3 with a different source. |
| Blending (all equations), DATE, alpha test, fog, texture filtering, AA, movie upscaling (FSR 1), output scaling | **Already GPU** | — | — |
| Vertex setup (GS vertex → host vertex, sprites to quads) | CPU, cheap | No | Not measurable in any profile so far. |
| VU1 programs (model transform, lighting, clipping, GIF packet build) | CPU HLE (`vu1_programs.cpp`) | **No (for now)** | Not seen as a cost yet; branching programs whose output is GIF data the CPU `GsState` parses next, so a GPU version would need GS state handling on the GPU as well. Revisit only if a profile shows it. |
| Sampling textures straight from a GPU copy of GS memory in `gs.frag` (no decode step at all) | — | No | The game rewrites texture memory between draws in the same frame (terrain streaming, the HUD font over sky pages). Each draw would need its own memory version inside a render pass. The decode-to-atlas step in §2.3 handles that ordering cleanly. |
| MPEG-2 movie decode and colour conversion | CPU (`src/sce/libmpeg.cpp`) | No | Cheap at 640×448 / 30 fps; the game's image buffer in guest RAM must hold the picture (the game uploads it itself). The upscale is already GPU. |
| Game logic, VU0 macro math, RNG, SPU2 mixer | CPU | **Never** | Category 1 (original behaviour) and determinism: these must stay bit-exact and on the game thread / audio thread. |

Not GPU, but related: running `SubmitGsSteps` + `Record` on a render thread while the next game tick runs (ADR-002 allows platform threads) would hide the remaining CPU cost. A separate decision; not part of this plan.

### 2.3 Plan: GPU texture decoding

#### 2.3.1 Shape

1. **GPU mirror of GS local memory**: a 4 MiB storage buffer. The CPU `GsMemory` stays as it is (CLUT loads, tests, dumps, `--play-gsdump`, the CPU reference decoder). The game never reads GS memory back (`gs-subset.md`: no local → host transfers), so the mirror is one-way.
2. **Decode requests instead of decodes**: at kick time `GsState` keeps computing the texel range, mip levels and key, but instead of decoding it records a request: TEX0, TEX1/MIPTBP, TEXA, the rectangle, the palette (256 RGBA8 entries, already built by `ClutRgba` for frame sources) and the **memory epoch** it reads.
3. **Memory epochs**: every host → local or local → local transfer that lands between two decodes of the frame starts a new epoch. Per epoch the CPU puts the changed 256-byte blocks (their content at that point, from `GsMemory`, found with the existing `BlockGeneration` counters) into the staging buffer. Uploads with no decode in between merge into one epoch.
4. **Record**, step 1 becomes, in stream order: for each epoch, `vkCmdCopyBuffer` of its blocks (one region per run of blocks) into the mirror → barrier → one compute dispatch per decode request of that epoch, writing RGBA8 straight into the request's atlas slot → barrier. Then the render passes as today. `gs.frag` does not change: the atlas holds the same RGBA8 texels as before.
5. **Shader**: generalise `gs_conv.comp`. Its source becomes either the memory mirror (new) or a render target (today's conversion path); its forward tables, CLUT and TEXA code are reused. Adds the mip-level placement of `DecodedTexture` (levels to the right of level 0, stacked).
6. **Caching**: a texture decoded earlier and still resident is not decoded again (same residency map, keyed as now). Incremental decode (`base_serial`, `changed`) becomes unnecessary: a full GPU decode of the 512×512 terrain with mips is about 350 k texels.
7. **Draw merging**: `SameState` compares `texture` pointers; it will compare the request id instead (same meaning: same decoded source).

#### 2.3.2 What stays on the CPU

- GIF parsing, register state, `GsMemory` writes, CLUT loads (256 entries, cheap), texel ranges, keys.
- The current `DecodeTexture` as **reference decoder**: a development parameter (`--cpu-texture-decode`, name to confirm in `command-line.md`) switches back to it, and the tests compare both.

#### 2.3.3 Validation

- The decode is integer work, so the GPU result must equal the CPU result byte for byte. New test: for each PCSX2 VRAM image we have (`tools/research/gsvram.py` inputs) and every format the game uses (T4, T8, T8H, CT32, CT24, CT16; CT32/CT24/CT16 CLUTs; MTBA and MIPTBP mip levels; REGION_CLAMP ranges), GPU decode == `DecodeTexture`. Needs a Vulkan device in `ctest`; skipped with a message when none is present.
- Regression suite (`full.json`): all screenshots and RAM checkpoints **identical** to the baseline (RAM is untouched; pictures should be pixel-identical since the atlas content is the same).
- Speed: the intro run of §2.2r (vblanks 0–2600, normal speed): target 2,600 frames for 2,600 ticks, no frame over 16.7 ms in the seagull/terrain stretch; per-frame log of GS and record times before and after. Also the hangar and mission 1 at 1× and 8×.

#### 2.3.4 Steps

| # | Step | Result checked by |
|---|---|---|
| 1 | Profile once more with the per-frame log: GS processing, decode share, record share, staging bytes, in the intro stretch, the hangar and a mission | numbers in `validation.md` |
| 2 | GPU mirror + epoch upload (no decode change yet; the mirror compared with `GsMemory` at frame end in a debug check) | mirror == CPU memory over the intro and a mission |
| 3 | Decode shader (from `gs_conv.comp`) + the byte-exact test against `DecodeTexture` | new test passes |
| 4 | Switch `GsState`/`Record` to requests; CPU decoder behind the development parameter | regression suite identical |
| 5 | Remove what GPU decode makes useless (incremental decode, possibly content hashes → generations), only after measuring | same suite + timings |
| 6 | Docs: `gs-subset.md`, `port-features.md` (renderer caches), `validation.md` record | — |

#### 2.3.5 Risks and open points

- **Many epochs per frame**: if a frame interleaves hundreds of uploads with decodes, the barriers add up. Mitigation: only a decode that reads a changed block forces an epoch boundary; measure the count in step 1.
- **Staging size**: raw blocks are smaller than today's RGBA uploads, so no new memory is expected.
- **Frame-buffer sources** stay on today's path (a request whose source is a target, not the mirror); both paths share one shader.
- **No new dependency** and no new Vulkan feature (compute and storage buffers are core 1.0).
- ADR-003 ("own Vulkan renderer… at the packet level") is not contradicted; the User decides whether this needs an ADR-003 amendment note or only this document.

### 2.4 Later, separate options (not in this plan)

- Per-texture images with descriptor indexing instead of the atlas (no atlas resets, no 8192² growth).
- Render thread for `SubmitGsSteps` + `Record` (§2.2, last paragraph).
- VU1 on the GPU, only if a profile ever shows VU1 HLE as a cost.

### 2.5 Step 1: measurement (2026-10-22)

Method: a temporary per-frame probe (removed afterwards; the sources were restored byte for byte from a backup and the development build was rebuilt clean). Development build, normal speed, fullscreen 3840×2160 at 60 Hz, v-sync on (FIFO, 3 images), internal scale 1× unless noted, each scene run once in its own user-data folder, one after another. Times are wall-clock milliseconds on the main thread; "CPU" = game tick + GS processing + recording.

| Scene (vblanks) | Frames / ticks | CPU mean | Frames with CPU > 16 ms | Texture work in those frames (decode + key + record) | Atlas resets | Mean upload per frame |
|---|---|---|---|---|---|---|
| No-save intro, 1× (120–2620) | 94 % | 9.0 ms | 200 | 8.5 + 3.0 + 5.6 ms of 30 ms (tick 7.4, VU1 1.6) | 78 | 1.8 MiB |
| No-save intro, 8× | 84 % | 10.0 ms | 286 | 8.0 + 3.4 + 5.4 ms | 78 | 2.0 MiB |
| Hangar/shop route (card S, s4_shop pads, to 6400) | 84 % | 4.0 ms | 579 (vblank 4166–4799) | 3.8 + 3.4 + 1.8 ms | 4 | 0.06 MiB |
| Mission 1 opening (0x25) | 89 % | 9.5 ms | 86 | 3.4 + 3.0 + 2.1 ms (tick 10.5) | 0 | 0.01 MiB |
| Mission 1 flight (to 3010) | 87 % | 9.0 ms | 310 | 2.1 + 1.8 + 4.5 ms (tick 7.1) | 0 | 0.03 MiB |
| Mission 4 replay (b3_combined, to 6010) | 88 % | 10.7 ms | 621 | 6.4 + 1.3 + 3.8 ms (11.5 MiB uploaded) | **481** | 5.3 MiB |

Findings:
1. **Texture work is the largest CPU cost where frames are slow, but not the only one.** In the intro's slow frames texture decode, content hashing and atlas upload are about 17 of 30 ms; in the worst intro frames (two ticks, 52 ms) GS processing is 28.8 ms (decode 13.7) and recording 8.6 ms. The rest of GS processing (GIF parsing, register state) is 1.5–3 ms per frame on average and stays on the CPU in any design.
2. **Content hashing (key) costs 0.5–1.3 ms per frame on average and up to 7 ms**, with 500–1,400 texture lookups per frame: worth reducing (§2.2, second row).
3. **The atlas thrashes in combat**: mission 4 restarted the atlas 481 times in 5,890 ticks (about every 12 ticks), uploading 5.3 MiB per frame on average. Per-texture images (§2.2, third row) remove the resets.
4. **Memory epochs are few**: decodes that read a block written since the previous GPU upload point: mean ≤ 4 per frame, maximum 40 (shop), 29–31 (intro). The §2.3.5 risk is small. No local → local transfers occurred.
5. **VU1 HLE is cheap**: 0.3–1.6 ms per frame on average, maximum 5.7 ms. Confirms §2.2: not a GPU candidate.
6. **Separate problem, not a texture cost: every scene presents only ~50–53 frames per second.** Mean frame time is 18–20 ms in all six runs, also in the shop, where the CPU needs 4 ms. The time is spent blocked in `vkAcquireNextImageKHR` (7.8–15.7 ms per frame on average; GPU fence waits are 0–2 ms). So the swapchain hands back images at about 50 Hz on a 60 Hz FIFO display. GPU texture decoding will not change this. HYPOTHESIS: the windows were not in the foreground (the runs started while the User was working) and Windows throttled presentation; otherwise a presentation/pacing bug. Open question for the User (§2.6).

### 2.6 Open after step 1

- **Presentation rate** (finding 6): User 2026-10-22: the windows were not in focus during the runs, so the ~50 fps is taken as Windows throttling unfocused windows, not a port problem. Not re-measured with a focused window.
- Go-ahead: given 2026-10-22 for all three items (§2.2 rows 1–3).

### 2.7 As built (2026-10-22)

All three approved items are in; results and test records: `validation.md` §2s (GD-1 to GD-3).

| Item | Code | Notes |
|---|---|---|
| GPU copy of local memory | `GsMemory` dirty blocks (`TakeDirtyBlocks`), `TextureOp`, `GsBackend::gs_memory_` | Device-local 4 MiB buffer. Before a decode that reads a block written since the last update, `GsState` emits one memory op with every block written since then (their bytes at that point); the backend copies them in stream order with a barrier on each side. On average 3–12 updates per frame |
| GPU decode | `shaders/gs_decode.comp`, `GsBackend::RecordTextureWork` | One dispatch per rectangle (level 0, mip levels); palettes as RGBA8 (16-bit CLUTs expanded with TEXA on the CPU, as before). A decode op whose texture nobody holds any more is skipped (its memory op is kept) |
| CPU reference | `DecodeOnCpu`, `--cpu-texture-decode`, `--verify-texture-decode` | The same decode description (`DecodedTexture::rects`, `psm`, `texa`, `palette`) for both. Verification reads every GPU decode back and compares it byte for byte |
| Hashing cut | `GsState::CacheEntry` | Key without content; valid while the generations of the blocks read are unchanged, checked only after a write to local memory (`WriteCount`). The page hashes (`RectHash`, `RegionHash`) and the second "last decode" map are gone |
| Atlas replaced | `GsBackend::TexImage`, set 1 of the GS pipelines | One image per decoded texture (sizes rounded up to 32), its own descriptor set bound per draw; images freed when the cache drops the texture, reused by size after the frames in flight (free pool 128 MiB). `gs.frag` samples `tex` (set 1) instead of the atlas |

Differences from §2.3:
- The decode description is shared by both decoders and the CPU decoder was restructured around it, so a GPU-free test can replay the ops (§2.3.3's byte-exact check is that test plus `--verify-texture-decode` in real runs).
- One descriptor set per texture image instead of the atlas: no descriptor arrays, so no new Vulkan feature.
- Z formats sampled as a texture give 0 in both decoders (what the CPU decoder always gave). The one use the game makes of them, a Z buffer drawn into another Z buffer, is a depth copy between the targets' depth images since 2026-10-22 (`gs_depth_copy.frag`, `validation.md` §2ab ZC).
- Palettes drawn on the GPU (2026-10-22, `validation.md` §2z): an 8-bit CT32 CLUT loaded from a frame buffer drawn earlier (the sun flare's palette at FBP 0x190) is not in local memory. The texture is decoded with the identity palette (indices) and the draw looks the indices up in a 16 × 16 copy of the buffer's palette pixels taken right before it (`gs.frag` flag 29).

Measured (GD-3): the no-save intro at 1× has 4 frames with more than 16 ms of CPU work instead of 200 (worst 21 ms instead of 92); the stretch at vblank 2390–2460 peaks at 12.5 ms instead of 41. Mission 4 combat: 245 instead of 621 such frames, worst 21 ms instead of 39. In the hangar viewer the CPU cost is a little lower, but recording costs about 0.7 ms more per frame, because textures cycled through shared memory are now decoded again each time (about 158 GPU decodes per frame) instead of being found by content hash.

### 2.8 GS processing per frame (2026-10-27)

`validation.md` §2am (GP-1 to GP-4). With frame interpolation each game step is presented several times; GS processing had been done again for every presented frame.

| Item | Code | Notes |
|---|---|---|
| Draw list with one vertex buffer | `DrawList`, `DrawCommand::first_vertex` / `vertex_count`, `GsState::TakeDraws(DrawList&)` | A primitive only ever goes to the newest draw, so each draw's vertices are contiguous. No allocation per draw; buffers keep their capacity |
| Pipeline reuse | `GsBackend::Record` (`pipeline` lambda) | Consecutive draws with the same key skip `GetPipeline` |
| Blended copies replayed | `Renderer::SubmitBlendedStep`, `GsState::ProcessGifTraced` / `ReplayVertices`, `FrameInterpolator::StepSerial` | The first copy of a step is processed and traced (the kick of every draw-list vertex and of its colour, the XYOFFSET of every kick, the texel ranges of textured sprites, lines and points). A later copy of the same step is parsed for vertex writes only; everything else must be the traced bytes, and the texel ranges must not change. Its draws are the traced ones with the new vertices; no texture work (textures and the GPU's copy of local memory stay as the traced frame left them). Any other case processes the copy again |

### 2.9 Host uploads into local memory (2026-11-04)

`validation.md` IG-9. `GsMemory::WriteImage` keeps, per upload packet (destination, rectangle, start pixel, size), the bytes last written and the generations of the blocks they wrote: a packet that repeats them onto unchanged blocks is skipped.

| Path | When | Notes |
|---|---|---|
| Whole blocks | the rectangle lies on block boundaries and the packet holds whole block rows (the whole transfer, or a piece ending on a block row) | each block assembled in the GS layout, compared and stored at once; the assembled 256 bytes are kept with the packet record (`LastUpload::block_bytes`) |
| Kept blocks | the packet repeats its last bytes, but other uploads changed some of its blocks since (the hangar: room, light map and skin over the same memory every step) | the kept blocks are compared and stored again without assembling them; exact, since a block's bytes depend only on the packet's bytes and its key |
| Runs | other byte-sized and 4-bit formats | a run inside one block row per step, the block base once |
| Pixels | 24-bit and the rest | `WritePixel` per pixel |

## 3. GPU time measurement (2026-10-30)

`src/platform/render/gpu_profiler.*`; runs and results: `validation.md` §2ao.

- **Always on**: a top-of-pipe timestamp when the frame's command buffer starts and a bottom-of-pipe one when it ends (after the output pass): the frame's GPU time, averaged at exit over the frames with GS draws (at 340 fps most presented frames only show the last step again).
- **`--gpu-profile`** (all builds): `GpuProfiler::Mark` writes a bottom-of-pipe timestamp at the end of each section of the frame (`GpuSection`): texture work, each GS render pass (`end_pass`, tagged with its target and its draw-call count), frame as texture, field copy, DATE / shader-blend region, GPU palette, view sync, conversion, depth copy, anti-aliasing, present copy, movie upscaling, output pass. A section's time is the time from the previous timestamp to its own: what its commands added, including the wait of the barrier in front of them. Bottom-of-pipe timestamps wait for nothing and add no barrier. At most 2,048 timestamps per frame (marks over the budget are counted and their time goes to the next section).
- Results are read with `vkGetQueryPoolResults` (no wait flag) when the frame slot's fence has been waited on, so the figures arrive two frames later and never stall the render thread; the queries are reset in the frame's own command buffer.
- The output pass section includes the wait for the swapchain image (the submission waits on it at colour-attachment output).
- Draw-state counters (`GsBackend::PassStats`: draw calls, pipeline binds, push-constant updates, texture binds) are CPU counters, always on.
- Queues without timestamp support (`timestampValidBits` 0): everything is a no-op and the lines are not logged.
