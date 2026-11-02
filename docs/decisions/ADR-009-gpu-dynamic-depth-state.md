# ADR-009: VK_EXT_extended_dynamic_state required; GS draws share pipelines

Status: **Accepted** (User decision 2026-10-30, after the measurements and the hardware check of `../port/validation.md` §2ao). Raises the GPU requirement of ADR-003 (Vulkan 1.2, dual-source blending) by one device extension. Presentation only: no game code, no guest memory, no settings involved.

## Context

- A player's session (0.9.6, GTX 1050, 1080p) waited 14 ms per frame for the GPU, also at 1× internal scale (`../port/validation.md` §2ao).
- GPU timestamps (`--gpu-profile`, `../port/renderer.md` §3) show that most of the GPU time is in the GS render passes of the flight frame buffer, not in pixels or frame-buffer copies: about 1,250 draw calls per frame, of which about 1,070 changed the pipeline.
- 94 % of those switches alternate between two states the game uses for every object or terrain cell: RGBA with depth GREATER and depth write, then RGB with depth GEQUAL and no depth write. The draws overlap, so they cannot be reordered.

## Decision

- The renderer requires `VK_EXT_extended_dynamic_state` (core in Vulkan 1.3) with its `extendedDynamicState` feature. A GPU without it is skipped at device selection; with none left the port shows its "graphics renderer could not start" message (update the driver). There is no second path.
- GS pipelines no longer contain the depth compare op and depth write: they are set per draw (`vkCmdSetDepthCompareOpEXT`, `vkCmdSetDepthWriteEnableEXT`), only when they change.
- Blended GS pipelines always write A and blend it with the blend constant (srcAlpha `CONSTANT_ALPHA`, dstAlpha `ONE_MINUS_CONSTANT_ALPHA`): constant 1 writes As (As × 1 + Ad × 0), constant 0 keeps Ad (As × 0 + Ad × 1), as the colour write mask without A did. Unblended pipelines keep A in their mask (core Vulkan, no extension).
- Code: `gs_vulkan.cpp` (`SplitKey`, `CreatePipeline`, the draw loop's `bind`), `vk_context.cpp` (device selection and creation).

## Hardware check (gpuinfo.org, Windows, 2026-10-30)

- Coverage of the extension in Windows reports: 89.35 % (`VK_EXT_extended_dynamic_state3`, not used: 72.9 %).
- 230 GPU names never reported it. 215 have only a Vulkan 1.0 or 1.1 driver and cannot run the port anyway. The other 15: GTX 1050 Max-Q, GTX 1060 5GB, GTX TITAN Black, Radeon R5 / R5 M335 / R9 M380 / RX 550, all with reports only from drivers of 2020 or older (the same chips on later drivers support it; the desktop GTX 1050 and GTX 1060 are not in the list); and translation or emulation layers (Microsoft "Direct3D12 (…)", Vortek, Wrapper, Virtio-GPU Venus), not native Windows drivers. No Intel GPU is in the list.
- So no GPU usable today with Windows 10 or 11 and a current driver loses support (User rule 2026-10-30: hardware too old for today's Windows needs no support).

## Consequences

- Pipeline binds per frame: about 1,070 → 70 (mission 1); 36 → 11 pipelines; fewer pipelines to build at startup.
- GPU time: on the RX 9070 XT the draws' time drops only 1.43 → 1.33 ms, because on this GPU every state change between draws costs about the same whether it is a pipeline switch or dynamic state (§2ao GT-5). The gain on NVIDIA is not measured yet; the player's `--gpu-profile` log decides whether more work is needed.
- Guest RAM identical to the build before (every RAM checkpoint of the regression suite at 1× and 4×); pictures identical except three host-timing flakes (two already known, one that the build before shows too; §2ao GT-4).
