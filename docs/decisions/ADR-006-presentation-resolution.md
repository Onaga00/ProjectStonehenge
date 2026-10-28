# ADR-006: Presentation resolution and output scaling

Status: **Accepted** (User decision, 2026-10-10; §3 and §4 decided 2026-10-11; §5 movie upscaling decided 2026-10-13). No open sub-decisions.

## Context (confirmed facts)

- `InitGraphics` 0x001044A8:
  - `sceGsResetGraph(0, 1, 2, 1)`, i.e. NTSC, interlaced, field mode;
  - a **512×448 draw buffer** and a **512×224 display buffer** (`../game/boot.md`: `draw_w/h`, `disp_w/h`).
- Every frame, `UpdateDisplayFrame` 0x00104950 calls `DrawDisplayTransfer` (H) 0x001049B0.
  - It copies or blends the 448-line draw buffer into the 224-line display buffer for the current field, in 32-pixel strips.
  - It uses `LerpRgba` with the factors at `ctx+0x80..0x88`, which looks like a flicker filter (H).
  - Field parity comes from `sceGsSyncV`. `disp_flags` 0x2000/0x8000 select the copy mode, and 0x4000 skips it.
- Movies switch to frame mode (`disp_h` = 448) and show 640×448 (SS cutscenes 640×336, letterboxed inside 448).
- The PS2 shows the 512-pixel-wide image across a 4:3 screen, so pixels are not square. Pixel aspect = (4/3) / (512/448) ≈ 1.167.
- Options `ApplyDisplayOffset` (±12 steps on DISPLAY DX/DY) move the picture on a TV; `ApplyBrightness` 0x00105988 (option level 0–6) scales the colours of the display copy.

## Decision (accepted parts)

1. The game renders at its **native internal resolution**, as on the PS2: 512×448 for the game and menus, 640×448 for movies.
   - Game logic and the renderer's coordinate handling (ADR-003) are unchanged.
2. A **final output pass** scales the finished image to the host window or screen.
   - It **preserves the original 4:3 aspect ratio**, with pillarboxing on wider screens and letterboxing on taller ones.
   - It works the same for 512- and 640-wide sources, since both map to the same 4:3 picture.
3. Interpolation (ADR-002) happens inside the native-resolution rendering. The output pass only scales.
4. The output pass is presentation only. It never changes what the game draws or reads back (render-to-texture, field copy inputs).
5. Screen-position and brightness options (`ApplyDisplayOffset`, `ApplyBrightness`) still run unchanged in game code, so the GS DISPLAY register values match the PS2.
   - Whether the output pass mirrors that offset on the PC screen is part of sub-decision §4.
   - Default proposal: ignore it, since there is no TV overscan to correct.

## 3. Scaling filter (decided 2026-10-11)

**Decision:** the port implements **two filters**, selected in `settings.json` (`"presentation": {"filter": "sharp" | "smooth"}`):
- `sharp` = sharp bilinear (A below): integer nearest pre-scale, then bilinear to the final size;
- `smooth` = plain bilinear (C below).

Default: `sharp` (User decision, 2026-10-11). There is no settings menu in the skeleton; the settings code is structured so one can be added later (see "Settings architecture").

Options considered:

| Filter | How | Pros | Cons |
|---|---|---|---|
| **A. Sharp bilinear** (integer pre-scale with nearest, then bilinear to the final size) | upscale 512×448 by the largest integer factor that fits, then bilinear the rest | crisp pixels without shimmer, handles the non-square 1.167 horizontal factor and any screen size; cheap | slight softness at the edges of pixels; still not "pure" pixels |
| B. Integer scaling (nearest, whole multiples only) | vertical ×N, horizontal chosen to keep 4:3 | perfectly uniform pixels vertically | **not possible on both axes at 4:3** because the pixels are not square: the horizontal factor is never an integer when the vertical one is. So the result is either not 4:3, or it falls back to nearest horizontally (uneven column widths). It also leaves large black borders (1080p: ×2 = 896 lines) |
| C. Plain bilinear | one bilinear pass | simplest, no borders, smooth | blurry at 2–5× scale |
| D. Nearest neighbour | one nearest pass | sharpest | uneven pixel widths (shimmer when things move), most visible with the 1.167 aspect |
| E. Area / Lanczos / bicubic | higher-order resampling | good detail on downscale or odd ratios | ringing (Lanczos), more shader work, softer than A at large factors |

B (integer) and D (nearest) are not implemented: B cannot keep 4:3 with integer factors on both axes, and D shimmers with the 1.167 pixel aspect. E is not needed at these scale factors.

## 4. Interlaced / field output → output image (decided 2026-10-11)

**Decision: option A**, present the full 448-line draw buffer. The field copy still executes in guest terms; its output is not presented. Movies (frame mode) are presented at 640×448.

Implementation note (slice S1, 2026-10-14; no change to the decision): the game applies its **brightness option** only in the field copy (`DrawDisplayTransfer` modulates the copy by `LerpRgba(0, 0x808080, brightness)`, `ApplyBrightness` 0x00105988). So that the option still has its effect, the renderer takes the colour scale of the field copy that reads the presented draw buffer (vertex colour / 128, GS MODULATE) and applies it in the output pass and in the native screenshot. The default level 3 gives 1.0; a save's level applies once the save is loaded (game behaviour, also on PCSX2). The flicker-filter blend (`disp_flags` 0x2000) is still not presented.

Options considered:

On the PS2, the game draws a full 448-line frame every 1/59.94 s. The field copy then sends only one 224-line field (copied or blended from that frame) to the TV per vblank. Options for the port:

| Option | How | Pros | Cons |
|---|---|---|---|
| **A. Present the full 448-line draw buffer** (progressive) | the output pass reads the 512×448 draw buffer after the frame is drawn, before or instead of the field copy | full vertical resolution every frame; no combing, no flicker; the field copy still runs in guest terms (the renderer still executes its GIF packet, so the GS state stays exact) | skips the flicker filter's blend, so the image is slightly sharper than on a TV (H: the filter is mild) |
| B. Weave the two most recent fields | rebuild a 448-line frame from the field-copy outputs of the last two steps | closest to "what the GS output" | combing on motion, since the two fields come from different frames |
| C. Bob (line-double the current field) | scale the 224-line field to 448 | exactly what one field showed, simple | half vertical resolution; flicker of thin lines between fields |
| D. Field output + blend/motion-adaptive deinterlacing | CRT-like processing | closest to a TV look | complexity; it is a look, which belongs to the modding layer |

Reason for A: the game already renders a full progressive 448-line frame every tick. Movies (frame mode) are presented directly at 640×448.
- Confirmation needed: a PCSX2 GS dump showing what the field-copy packet does in each `disp_flags` mode, so we know the draw buffer is complete when the copy starts.
- B/C stay available for comparison testing against PCSX2.

## 5. Movie upscaling stage (decided 2026-10-13)

**Decision (User):** decoded movie pictures go through an **upscaling stage** before the output pass. Approved modern-port baseline feature (`port-baseline.md`).

How the movie reaches the screen on the PS2 (`../formats/SMV.md`, `../subsystems/frontend.md`):
- libmpeg/IPU decodes one picture into a guest RAM image buffer (0x118000 bytes = 640×448×4, RGBA32) inside the 0x600000-byte movie block.
- Game code (`func_00213048` and the in-menu player of scene 4) uploads that buffer to GS memory and draws it as textured sprites, together with whatever the menu draws on top (fades, frames).
- MIS, INTRO and STAFF pictures are 640×448; SS01–SS12 are 640×336, letterboxed inside 448 by the draw.

Design:
1. **Placement.** The stage sits between the decoder and the renderer's texture source, so it runs before the output pass and before any menu drawing that is composited over the movie.
   - The `sceMpeg*` implementation in `src/sce/` (libmpeg2, `dependencies.md`) still writes every decoded picture into the guest image buffer exactly as the PS2 does: game code and guest RAM are unchanged.
   - It also hands the same picture (host copy, with a picture serial number) to the upscaling stage.
2. **Factor.** The output size is the source size × the renderer's internal-resolution factor N (`../port/renderer.md` §1): 640N×448N or 640N×336N.
   - **At N = 1 (today) the stage is a passthrough** and the renderer uses the normal GS upload path, bit-exact with the guest data.
   - At N > 1 the renderer replaces the texture uploaded from the movie image buffer with the upscaled picture when it draws the movie sprites (identified by the source address range of the image buffer); everything else of the GS emulation is unchanged.
3. **Algorithm.** AMD FidelityFX **FSR 1**: EASU (edge-adaptive spatial upsampling, 1×–4× area range, so N ≤ 2 per axis in one pass), followed by **RCAS** sharpening only if it improves the result (judged on the real movies in the movie slice; RCAS sharpness is a constant of the stage, not a user setting unless the User adds one). The FSR 1 shader headers are a fetched dependency (`dependencies.md`).
4. **Replaceable component.** The stage has one interface (`input picture + size + N → output image`), implemented in `src/platform/render/`; FSR 1 is its first implementation. Another algorithm can replace it without touching the decoder, the renderer's movie substitution or game code.
5. **Both sizes.** 640×448 and 640×336 are handled the same way; the stage keeps the source aspect, and the game's own draw does the letterboxing.
6. **Timing unchanged.** Movies are shown at 29.97 fps with the main loop at 59.94 Hz: each picture is held for exactly **2 game ticks**, paced by the game as on the PS2 (`../formats/SMV.md`, port notes). The stage runs once per new picture (keyed by its serial number) and its output is reused for the second tick. It never drops, repeats or re-times a picture, and presentation interpolation (ADR-002) does not apply to movie pictures.
7. Presentation only: the stage never writes guest memory and never feeds anything back to the game (§Decision 4).

**How the picture reaches the stage (User decision 2026-10-14, option b).** The renderer does not see a movie picture as one image: the game uploads it as 40 IMAGE transfers of one 16-pixel column each (all to the same GS block, 0x2300) and draws a 16-pixel sprite after each. Two ways were considered: (a) the renderer recognises the movie texture block and rebuilds the picture from the uploads; (b) the decoder hands the decoded picture to the renderer directly as a presentation-only side path. Chosen: **(b)**, with the stage still scaled by the internal-resolution factor (passthrough at 1×, upscaling above 1×).

Implementation (S3, 2026-10-14):
- Side path: `SceHost::movie_pictures` is set only when the factor is above 1. Then `sceMpegGetPicture` (`src/sce/libmpeg.cpp`) also writes a row-major host copy, with a serial number and the guest image-buffer range, to `DmaStats::movie_picture`. The DMA walker (`sceDmaSend`, GIF channel) marks the GIF data blocks it reads from that buffer (`render::gs::SourceMark`). At 1× none of this runs.
- Identification: `GsState` records the guest source of each IMAGE upload that starts in marked data. A draw textured from that block carries `texture_guest_source`/`texture_guest_width`, and `MovieTexelOrigin` (`src/platform/render/movie_texel_map.h`) maps it to the picture pixel (column x, row y). Covered by a unit test.
- Stage: `MovieUpscaler` interface plus `CreateFsr1MovieUpscaler` (`src/platform/render/movie_upscaler.*`, shader `shaders/fsr1_easu.comp`). It runs FSR 1 EASU in compute passes of at most 2× per axis (×3 and ×4 take two passes), once per new picture serial. RCAS is not applied yet: it is to be judged on the real movies.
- Substitution: when the GS is drawn at N > 1, those draws sample the stage output at (x + u, y + v)·N instead of the uploaded texture (built 2026-10-19 with the scaled GS backend, `../port/renderer.md` §1.7).
- Test hook (development builds): `--movie-upscale <2-4>` runs the side path and the stage at that factor while the GS stays at 1×; screenshots taken during a movie also write the stage output (`movie_x<n>_<vblank>.png`). Checked on SS01 at ×2 (1 pass) and ×3 (2 passes): the ×2 output, averaged back 2×2, lands on the native picture at the letterbox offset (y 56) with a mean difference of 3 levels. Native frames are byte-identical with and without the stage.

## Consequences

- `src/platform/render/` gets an output pass:
  - input is the native framebuffer (512×448 or 640×448);
  - output is the swapchain image, with the selected filter, 4:3 and black bars.
- The renderer's GS emulation stays at native resolution. ADR-003 option C is unaffected.
- The movie upscaling stage (§5) is a separate component in `src/platform/render/`, fed by the `sceMpeg*` implementation; a passthrough until the internal-resolution factor is raised.
- A higher internal resolution (factor 1–8, default 1) is a baseline feature since 2026-10-19 (`../port/renderer.md` §1). Widescreen stays in the modding layer (`port-baseline.md`).

### Settings architecture
- One `Settings` struct (plain data, defaults in code) owned by `src/platform/`; `settings.json` under `%LOCALAPPDATA%\ProjectStonehenge\` (ADR-005).
- JSON load at startup and save on change; missing or invalid keys fall back to defaults and are logged; unknown keys are kept.
- A change notification (subscribers per section: presentation, window, input, audio, debug) so a later settings menu or the debug overlay can change values at run time without restarting. The output pass re-reads the filter on notification.
- No settings menu in the skeleton. Game code never sees settings.
- Implementation note (2026-10-20, User decision): a file that cannot be read, is not valid JSON, has another `version`, an unknown key or an invalid value is now rejected as a whole and replaced with the defaults (unknown keys are no longer kept). The settings are changed in game through the GRAPHICS page under SCREEN SETTINGS (`../port/port-features.md` §3); game code still never reads settings (it goes through `src/common/port_hooks.h`).

## Research findings

### The game has its own 16:9 mode

- The options page has **SCREEN RATIO** (option byte 0x002A15CC bit 0). Turning it on sets ctx flag 0x10000.
- Menu layouts, the aircraft-viewer camera aspect, text boxes and the briefing map all branch on that flag (`../subsystems/frontend.md`). On the PS2 this produces an **anamorphic 16:9** picture for widescreen TVs: the same 512/640-wide frame, drawn for a 16:9 display.
- Consequence for this ADR: the output pass must use **16:9 when the in-game SCREEN RATIO option is on**, and 4:3 otherwise. That is original behaviour (category 1), not a new feature. The 4:3 rule above applies to the default setting.
- Implementation (2026-10-19): after each frame the main loop reads ctx flag 0x10000 and sets the output pass's display aspect (`Renderer::SetDisplayAspect`: 16:9 or 4:3). Before this the call was missing and the port always presented 4:3. Native screenshots stay the anamorphic draw buffer, like PCSX2's (`../port/validation.md` IR-3).

### 16:9 mode in the flight scene

- In 16:9 mode (ctx flag 0x10000) every HUD sprite is drawn 3/4 as wide, fonts switch to narrower tables, and the view cameras use a 16:9-aware width (`../subsystems/camera-hud.md`). The picture is anamorphic: the same 512-pixel frame is meant to be shown at 16:9.
- So the rule above applies to the flight scene too: the output pass presents at 16:9 when SCREEN RATIO is on.

### Ultrawide flight presentation (2026-10-28)

- `presentation.ultrawide` (default true) extends the 16:9 rule during scene 2 when the actual window aspect is more than 1 % wider than 16:9. It is Hor+, not a stretch.
- `func_0010C9D0` (native counterpart of PCSX2 patch address 0x0010C9F4) and the replay setup `func_0010D470` multiply the original 16:9 projection width by window_aspect / (16/9). Split screen still takes half of that expanded width per player.
- The output pass uses the reduced actual window ratio; `FitAspect` needs no special ultrawide case.
- The per-view 3D/HUD source point selects overlay draws for a vertex-stage X transform, narrowed by (16/9) / window_aspect around the view's scissor centre. A different scissor is not transformed. Full-view draws such as screen fades are excluded so they still cover the extended sides.
- World-anchored overlay markers (target boxes `func_00165AB8` and their labels, multi-lock boxes `func_00164BD8`, gun pipper `func_00164440`, special-weapon reticle and fall lines `func_00164F00`, flight-path marker `func_00160598`, look-around lines `func_00160200`) pre-expand their projected X about the view's frame centre by 1 / hud_x_scale (`port::UltrawideWorldMarkerX`), so after the narrowing they sit on their objects while their sizes stay narrowed. The nose-anchored gauge cluster, pitch ladder and nose marker stay narrowed with the rest of the HUD.
- Frontend menus and movies keep their original 4:3 / 16:9 output; in-flight pause and game-over overlays receive the same HUD correction. The 4:3 SCREEN RATIO mode is unchanged; validation runs disable the feature.
- This is presentation-only host configuration through `port::DisplayOptions`; game code does not read settings.json. No native draw-buffer size or GS display register is changed.
- Field-of-view basis (`presentation.ultrawide_4x3_fov`, default true). The game's 16:9 mode is not uniformly Hor+:
  - HUD-only and cockpit views: 640 or K 0x0040DAA0 (853.33 = 640 x 4/3) wide, 480 high, same distance K 0x0040DAB4 in both modes. Hor+, same vertical FOV as 4:3.
  - External (third-person) view: distance K 0x0040DAC8 in 4:3, K 0x0040DACC in 16:9. Carrier landing / takeoff camera: K 0x0040DAC0 / K 0x0040DAC4.
  - Replay: 640 or K 0x0040DADC wide, 480 high.
  - In-engine sequences (`func_00178DF8`): 640 x 480 or 640 x 360, which is Vert-.
  - With the setting on, ultrawide uses 640 x window_aspect / (4/3) for every one of these widths, height 480 and the 4:3 distances, so the vertical field of view is always the 4:3 one. Off: the 16:9 values widened by window_aspect / (16/9).
