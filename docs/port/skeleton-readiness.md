# Skeleton readiness

Every function the port skeleton needs to boot the game to the first scene and run the main loop, with its status and what it depends on. Nothing is in `src/` yet.

Status values:
- **understood**: every statement has been read in the decompiler (and the disassembly where MMI/VU0 is involved). It can be translated as is.
- **partly**: control flow is clear, but the meaning of some data (tables, flags) is still open. Translation is possible, but names stay `func_`/`unk_`.
- **unknown**: not read yet.

Names: Ghidra names (`../symbols.csv`). `(H)` = hypothesis (plate comment only).

## Scope and method

- Closure of direct `jal`/`j` calls from `entry` 0x00100008 and `main` 0x001001F0. Scene handlers are excluded: `SceneStep` is the boundary (`tools/research/closure.py`).
- Result: **516 functions = 199 game + 317 SDK/libc**. The game code calls **99 SDK/libc entry points** directly. The other 218 SDK functions are internal to the libraries and are not translated, because `src/sce/` reimplements each SDK function at its API (CLAUDE.md layout).
- Indirect calls on the path: only `SceneStep`'s `scene_table[ctx->scene]()` (0x0029FDA0, 8 entries) and the thread entry points of the HDD module (`CreateThread`).

## Summary

| Group | Functions | understood | partly | unknown |
|---|---|---|---|---|
| crt0 + main + main loop | 6 | 6 | 0 | 0 |
| Init sequence in `main` | 17 | 14 | 3 | 0 |
| Bump allocator | 6 | 6 | 0 | 0 |
| Scene system | 6 + 8 handlers | 6 | — | handlers: `../game/boot.md` |
| File pipeline (CD queues, TBL/CDP) | 23 | 23 | 0 | 0 |
| Per-frame helpers: DMA/display | 9 | 8 | 1 | 0 |
| Per-frame helpers: pads + USB joystick | 26 | 24 | 2 | 0 |
| Sound RPC client (SOUND.IRX) | 41 | 33 | 8 | 0 |
| Stream audio client (VAG.IRX) | 33 | 27 | 6 | 0 |
| Options/config | 12 | 7 | 5 | 0 |
| Save/HDD init reached from boot | 22 | 14 | 8 | 0 |
| **Game total** | **199** | **~168** | **~31** | **0** |
| SDK/libc entry points called by game code | 99 | 90 named + confirmed, 9 named hypothesis | — | 0 |

Counts per row are approximate where one function serves two groups. Every one of the 199 game functions has been read at least once in the decompiler.

## 1. crt0, main, main loop

| Address | Name | Status | Depends on |
|---|---|---|---|
| 0x00100008 | `entry` (crt0) | understood | syscalls SetupThread/SetupHeap, `_InitSys`, `FlushCache`, `main`, `Exit` |
| 0x001000B8 | `_exit` (H) | understood | `Exit` |
| 0x001000C0 | `_root` | understood | syscall ExitThread |
| 0x001001F0 | `main` (init + loop) | understood | everything below |
| 0x00288950 | `__main` | understood | `__do_global_ctors` (H) 0x002888A0, frame-info registration |
| 0x0026B2D0 | `_InitSys` (H) | partly (SDK) | not translated: the port's runtime init replaces it |

Main loop (0x00100290–0x001003BC), per iteration:
1. `SendDmaPacketList(1, VIF1 list, tte=1)` and `SendDmaPacketList(2, GIF list, 0)`: kick last frame's chains.
2. `ctx->cur_buf = SwapDoubleBuffer(&ctx->dbuf)`.
3. `UpdatePads()`, unless `ctx->flags & 0x40000`.
4. `FlushCache(0)`.
5. `UpdateSoundRpc()` and `UpdateVagStream()`.
6. `r = SceneStep()`.
7. `odd = WaitVSync()`; `RCNT0_COUNT = 0`; `UpdateDisplayFrame(odd)`.
8. Frame counters and flag bookkeeping (0x100/0x200/0x400/0x800/0x2000).
9. `UpdateCdQueue()`.
10. If `r != 0`, exit: `ShutdownPads`, `FreeWorkHeap`, `sceCdInit(5)`, `"Good Bye!"`. `SceneStep` always returns 0, so this exit path is unreachable by default. It is still translated (dead-code principle).

## 2. Init sequence in `main`

Order and roles (also in `../game/boot.md`):

| # | Address | Name | Status | Role / depends on |
|---|---|---|---|---|
| 1 | 0x00288950 | `__main` | understood | global ctors |
| 2 | 0x00100650 | `InitMainContext` | understood | zero `g_MainContext` 0x0029F380, `g_pMainContext`, `ResetDoubleBuffer`, `ResetPacketList` ×2 |
| 3 | 0x00100400 | `IopBoot` | understood | `sceSifInitRpc`, `sceCdInit`, `sceCdGetDiskType`, `sceCdMmode`, `sceFsReset` (H), `sceSifRebootIop("cdrom0:\IOPRP23.IMG;1")`, `sceSifSyncIop`, `sceSifInitIopHeap`, 9× `sceSifLoadModule` |
| 4 | 0x00100620 | `InitHardware` | understood | `sceGsResetPath`, `sceDmaReset(1)`, `sceVu0ResetVif0` (H, VIF0/VU0 reset), `sceCdInit(0)` |
| 5 | 0x00100F48 | `InitWorkHeap` | understood | `ProbeLargestMallocSize`, `malloc` |
| 6 | 0x001006C0 | `InitGameStateGlobals` (H) | partly | writes `g_GameState` 0x0029F350 block; field meaning open |
| 7 | 0x001053F8 | `InitOptionData` (H) | partly | 0x001053C8 → 0x001056D8 (defaults copy 0x002A0280, memset 0x10C, 0x8D4 table init, `func_0017BB58`), 0x00105728, 0x00105748; `func_00246FE0` |
| 8 | 0x00100D38 | `InitRandomTable` | understood | `srand`, `rand` |
| 9 | 0x001000D8 | `InitSubsystems` | understood | `InitCdFileSystem`, `BuildCdLsnTable`, `InitSaveSystem`, `sceSifInitRpc`, `InitSoundRpc` loop, `func_0021E2D8` (`InitVagStream` loop), `func_0021FC20`, `func_00217778`, `func_00219110`, `func_0021ADF8`, `func_0021A1C0`, `func_0021A680`, `InitMoviePlayback` (H) |
| 10 | 0x00105340 | `ApplyOptionSettings` (H) | partly | `ApplySoundOptions`, `ApplyDisplayOffset`, `ApplyBrightness`, `ApplyVibrationOptions` |
| 11 | — | `ctx->flags |= 0x800` | — | — |
| 12 | 0x001044A8 | `InitGraphics` | understood | `sceGsResetGraph(0,1,2,1)`, `BuildDrawEnvPackets` (`sceGsSetDefDispEnv`), `ApplyDisplayOffset` |
| 13 | 0x00100768 | `LoadBootData` | understood | `GetCdFileLsn(2)`, `GetTblBuffer(0)`, `QueueCdRead`, `GetCdDoneTicket`, `AllocAndLoadDataEntry(0)`, `IsDataLoadDone`, `LoadDataEntryAsync(0x166)`, sound-bank LSN table 0x0021F968/0x0021F908/0x0021F8A8/0x0021F8B8; every wait loops `UpdateCdQueue` + `sceGsSyncV(0)` |
| 14 | 0x00102050 | `PadSystemInit` | understood | `BindUsbJoyRpc`, `scePadInit`, `InitPadPort`, `scePadPortOpen` ×2, `ApplyVibrationOptions` |
| 15 | 0x00103C00 | `InitSprRingBuffer` | understood | writes DMAC `RBOR`=0x418000, `RBSR`=0x7FF0, `D_CTRL` |
| 16 | 0x001010A8 | `SetWorkHeapMark` | understood | allocator globals |
| 17 | — | loop `sceGsSyncV(0)` until it returns non-zero | — | waits for `CSR.FIELD` = 1 (`sceGsSyncV` returns CSR bit 13 when interlaced) |

**Boot data:**
- `LoadBootData` reads the whole `DATA.TBL` (file id 2) first. It then loads DATA entry 0, a count+offsets container, and stores its sub-block pointers in `ctx->data0_table`. Next it fills a 21-entry sound-bank table from file-table ids at 0x00354680. Finally it loads DATA entry 0x166 into 0x00354B40, a sound header container whose 21 sub-blocks are linked by 0x0021F8B8.
- The boot path blocks on the CD queue only at these points, and in `InitSubsystems` (VAG stream init). Each blocking wait still calls `sceGsSyncV(0)`, so frame pacing continues during loading.

## 3. Bump allocator

| Address | Name | Status | Notes |
|---|---|---|---|
| 0x00100EC8 | `ProbeLargestMallocSize` | understood | tries `malloc(0x1FFEFF0 - heap_end - k*0x100)` until it succeeds, then frees. **Port: the result depends on the libc heap layout; the port must give a fixed size** (see risks) |
| 0x00100F48 | `InitWorkHeap` | understood | base 0x00411788, size 0x00411784, offset 0x0041178C, mark 0x00411790 |
| 0x00101000 | `WorkHeapAlloc(size, align)` | understood | aligns the *absolute* address (`(base+off) % align`); returns 0 when exhausted (callers do not check) |
| 0x001010A8 | `SetWorkHeapMark` | understood | mark = offset |
| 0x001010F0 | `ResetWorkHeapToMark` | understood | offset = mark (`SceneStep`, request bit 4) |
| 0x00100FB0 | `FreeWorkHeap` | understood | exit path only |

## 4. Scene system

| Address | Name | Status | Notes |
|---|---|---|---|
| 0x00100B18 | `SceneStep` | understood | indirect call through 0x0029FDA0; request bits 1 (change scene), 2 (sub-state), 0x40 (return), 4 (`sceGsSyncPath`, reset packet lists, `ResetWorkHeapToMark`) |
| 0x00100AB0 | `RequestScene` | understood | |
| 0x00100AC8 | `RequestSubState` | understood | |
| 0x00100AF8 | `SetRequestFlags` | understood | |
| 0x00100B08 | `SetReturnScene` (H) | understood | dead (`dead-code.md`) |
| 0x001008B0 | `SceneBootLoader` (H) | understood | first scene; leads to scene 6 startup |
| 0x0029FDA0 table | 8 handlers | see `../game/boot.md` | |

## 5. File pipeline

Details: `../game/file-loading.md`, `../formats/CDP.md`.

| Address | Name | Status |
|---|---|---|
| 0x002145C8 | `InitCdFileSystem` | understood |
| 0x002146C0 | `WaitDiscAndReadToc` | understood |
| 0x00214508 | `BuildCdLsnTable` | understood |
| 0x002147F0 | `GetCdFileLsn` | understood |
| 0x00214738 | `FindCdFileLsnByName` | understood (dead) |
| 0x00214258 | `PushCdReadRequest` | understood |
| 0x00214870 | `QueueCdRead` | understood (chunk quirk documented) |
| 0x00214490 | `GetCdDoneTicket` | understood |
| 0x00214350 | `UpdateCdQueue` | understood |
| 0x00214A40 | `ProcessCdRequest` | understood |
| 0x002149D0 | `IssueCdRead` | understood |
| 0x00214990 | `PollCdSync` | understood |
| 0x00214438 | `FlushCdQueue` | understood (HDD error path) |
| 0x00215450 / 0x00215468 / 0x00215488 | `GetTblBuffer` / `GetTblEntrySector` / `GetTblEntrySize` | understood |
| 0x002154A8 / 0x002154E8 / 0x00215588 | `GetDataEntryAllocSize` / `LoadDataEntryAsync` / `IsDataLoadDone` | understood |
| 0x00102668 | `AllocAndLoadDataEntry` | understood |
| 0x00214FB0 | `InternHostPath` (H) | understood (dead) |
| 0x002597B8 / 0x002597C8 / 0x0025ACD0 | HDD context getter / HDD error getter / HDD read job | understood; HDD path unreachable (`../subsystems/save.md`) |

SDK under the pipeline: `sceCdInit`, `sceCdMmode`, `sceCdGetDiskType`, `sceCdDiskReady`, `sceCdGetToc`, `sceCdSearchFile`, `sceCdRead`, `sceCdReadIOPm` (H), `sceCdSync`, `sceCdBreak`, `sceCdSeek`, `FlushCache`.

## 6. Per-frame helpers: DMA, display, scratchpad

| Address | Name | Status | Depends on |
|---|---|---|---|
| 0x00104438 | `SendDmaPacketList` | understood | `sceDmaGetChan`, `sceDmaSend`; writes CHCR.TTE |
| 0x00103E90 | `SwapDoubleBuffer` | understood | — |
| 0x00103E38 | `ResetDoubleBuffer` | understood | — |
| 0x001043B0 | `ResetPacketList` | understood | — |
| 0x00104920 | `WaitVSync` | understood | `sceGsSyncPath`, `sceGsSyncV` |
| 0x00104950 | `UpdateDisplayFrame` | understood | `DrawDisplayTransfer` (H), `PutDispAndDrawEnv` 0x00104E28 |
| 0x001049B0 | `DrawDisplayTransfer` (H) | understood code / partly meaning | builds a GIF packet at 0x00420000, `FlushCache`, `sceDmaSend` ch2, `sceGsSyncPath`; `LerpRgba` (H, MMI + VU0 `vftoi12`) |
| 0x00103D80 / 0x00103D98 | `GetSprWorkBuffer` / `FlushSprWorkBuffer` | understood; **dead** | scratchpad 0x70000000, DMA ch8 + DMAC ring buffer (`D_RBOR`/`D_RBSR`, GIF `TADR`); only caller 0x00103CC8 has no reference (`../subsystems/rendering.md`) |
| 0x00105028 | `LerpRgba` (H) | understood (disassembly-level MMI) | `pextlb/psubh/pmulth/psraw/ppach/paddsw/pmaxw/pminh/ppacb`, `lqc2`/`vftoi12`/`qmfc2` |

## 7. Per-frame helpers: pads and USB joystick

| Address | Name | Status |
|---|---|---|
| 0x00102050 | `PadSystemInit` | understood |
| 0x00102220 | `UpdatePads` | understood |
| 0x00101298 | `UpdatePadConnection` | understood (Sony sample-style mode state machine) |
| 0x001011B0 | `ReadPadData` | understood |
| 0x001011F0 | `InitPadPort` | understood |
| 0x00101858 | `ConvertPadInput` | understood; button layout tables 0x0029FDC0/0x0029FE08/0x0029FE50/0x0029FE98: understood 2026-10-31 (`../subsystems/pad-input.md`) |
| 0x00101688 | `ScalePadAxis` | understood |
| 0x00101CF0 / 0x00101D80 / 0x00101E28 | `UpdatePadButtonEdges` / `BuildPadButtonMap` / `UpdateMappedPadButtons` | understood; masks 0x0029FEE0, maps 0x0029FF20/40/60 = partly |
| 0x00102010 / 0x00102488 / 0x001024C0 / 0x00102530 / 0x00102620 | vibration helpers | understood |
| 0x001021E8 | `ShutdownPads` | understood (exit path) |
| 0x001096D8 / 0x00109780 | `BindUsbJoyRpc` / `ReadUsbJoy` | understood (RPC 0x80000210, fno 2) |
| 0x00109CA0–0x00109F08 | 8 axis/byte scaling helpers for USB joystick values | understood |

SDK: `scePadInit/PortOpen/Read/GetState/GetReqState/InfoMode/SetMainMode/InfoAct/SetActAlign/SetActDirect/InfoPressMode/EnterPressMode/End`, `sceSifBindRpc/CallRpc/CheckStatRpc`.

## 8. Sound RPC client (SOUND.IRX, RPC 0x81000101)

All are EE game code and are translated. The IOP side is HLE (ADR-004 B, `../subsystems/sound.md`).

| Address | Name | Status |
|---|---|---|
| 0x00219030 | `InitSoundRpc` | understood |
| 0x002172C8 / 0x00217428 / 0x002174B0 | `BindSoundRpc` / `SendSoundCommands` / `IsSoundRpcIdle` | understood (2 × 0x2400 cmd buffers, 0x600 reply, fno 0 then fno 1 every frame) |
| 0x00218F18 | `UpdateSoundRpc` | understood (reply records 6 / 10 / 0xF) |
| 0x00217578 / 0x00217888 / 0x002178F0 / 0x00217960 | `QueueSoundVoiceCmd` / `QueueSoundCmd8/12/16` | understood |
| 0x00217A48 / 0x00217A88 / 0x00217BF8 / 0x00217F28 | voice table init / status update / `AllocSoundVoice` / request-id renumbering | understood |
| 0x002174D0, 0x002181E8, 0x002183E8, 0x002187B0, 0x00218620, 0x00218318, 0x00218008, 0x0021D138, 0x0021E058, 0x0021E240 | voice command builders (key on, set attr, key off, find by request id) | understood; 72-byte record layout matches `../subsystems/sound.md` |
| 0x00218868, 0x00218C20, 0x0021CD70 | sound-bank lookup (bank table 0x0033E190/0x0033E194/0x0033E214/0x0033E298), note→pitch adjust | partly (bank header format open) |
| 0x0021CE08 / 0x0021CF08 / 0x0021CF70 / 0x0021CFA0 | `SetSoundOutputMode` (0xE) / `SetSoundDataInputVolume` (0xD) / cmd 6 / cmd 7 | understood |
| 0x0021A1C0, 0x0021A328, 0x0021A680, 0x0021A800, 0x0021AD48, 0x0021ADF8, 0x0021CCF0, 0x00219110, 0x00219260 | volume groups (16 group volumes 0x0042CA50, channel map table in `0x0021A328`), pan law table 0x0042A4E8, defaults | partly (group meaning) |
| 0x00219850, 0x00219900 | 3D sound angle (VU0 matrix + `atan2f`), sin/cos pan | understood |
| 0x00218860, 0x00218EE8, 0x00218EF8, 0x002177C8–0x00217838, 0x00217778 | accessors | understood |

## 9. Stream audio client (VAG.IRX, RPC 0x81000102)

| Address | Name | Status |
|---|---|---|
| 0x0021E2D8 | `InitVagStream` caller loop | understood |
| 0x0021E370 | `InitVagStream` | understood |
| 0x0021E770 / 0x0021E850 / 0x0021E8F0 | `IsVagRpcIdle` / `QueueVagCmd` / `SendVagCommands` | understood |
| 0x0021FE60 | `UpdateVagStream` | understood |
| 0x0021F448 | per-slot streaming state machine (CD refill via `QueueCdRead` queue 0) | partly (state values 1/2/8/0x20/0x40/0x80) |
| 0x0021EAB8, 0x0021EC80, 0x0021ECE8, 0x0021ED60, 0x0021EDB8, 0x0021EEB0 | slot start / VAG begin, end, complete | understood |
| 0x0021EFD8, 0x0021F298, 0x0021F138, 0x0021F190, 0x0021F208 | slot volume and fade | understood |
| 0x0021F8A8–0x0021FB00 (12 accessors) | VPK/stream table lookups (0x00354B40, 0x00365B40, 0x003546DC) | partly (VPK header fields) |
| 0x0021FB58 / 0x0021FC20 / 0x0021FD40 (H) / 0x0021FDF8 (H) | queue reset / init / `RequestStreamPlay` / `RequestStreamStop` | understood |

## 10. Options, save and HDD init reached from boot

| Address | Name | Status |
|---|---|---|
| 0x00105340 | `ApplyOptionSettings` (H) | partly |
| 0x00105800 / 0x001058D8 / 0x00105778 | `ApplySoundOptions` / `ApplyDisplayOffset` / `ApplyVibrationOptions` | understood |
| 0x00105988 | `ApplyBrightness` | understood |
| 0x001053C8, 0x001056D8, 0x00105528, 0x00105568, 0x00105578, 0x00105670, 0x00105690, 0x00105728 | option/save-data defaults | understood code, partly meaning |
| 0x0025F298 / 0x0025F2A8 / 0x0025F320 / 0x0025E310 / 0x0025E330 | save system init (`sceMcInit`, 2 port blocks, dir `BASLUS-20152AC04`) | understood |
| 0x00259938 `InitHddModule` (H), 0x0025E218, 0x0025C090, 0x0025C058, 0x0025C198, 0x0025BFC0, 0x0025E1D0/1F0/248, 0x0025B0B8, 0x0025C240, 0x0025C2C0, 0x002598E8 | HDD module: job thread, power-off thread, "pfs%d:" paths | understood; **unreachable on any hardware** (the module init has no caller, `../subsystems/save.md`); translated, SDK calls stubbed (`dead-code.md`) |
| 0x00212110 | `InitMoviePlayback` (H) | understood |
| 0x00213C50 | movie audio state reset (SOUND cmd 0xD) | understood |

## 11. SDK surface needed by the skeleton (99 entry points)

The game-side closure calls these directly. Each one needs a PC implementation in `src/sce/`, with the same name and signature.

| Library | Functions | Notes |
|---|---|---|
| libgraph | `sceGsResetGraph`, `sceGsResetPath`, `sceGsSetDefDispEnv`, `sceGsPutDispEnv`, `sceGsSyncV`, `sceGsSyncPath` | `sceGsSyncV` = frame clock (ADR-002) |
| libdma | `sceDmaReset`, `sceDmaGetChan`, `sceDmaSend` | `sceDmaSend` feeds the renderer (ADR-003) |
| libvif1pk | `sceVif1PkInit/Reset/Terminate/Size/End` (H names) | packet builder |
| libvu0 | `sceVu0UnitMatrix/ApplyMatrix/InversMatrix/RotMatrixX/Y/Z`, `sceVu0ResetVif0` (H) | `vu0_math.h` |
| kernel | `Exit`, `CreateThread`, `StartThread`, `ChangeThreadPriority`, `GetThreadId`, `CreateSema`, `SignalSema`, `iSignalSema`, `WaitSema`, `FlushCache`, `_InitSys` (H) | threads are used only by the HDD module |
| libsif / RPC | `sceSifInitRpc`, `sceSifBindRpc`, `sceSifCallRpc`, `sceSifCheckStatRpc`, `sceSifInitIopHeap`, `sceSifLoadModule`, `sceSifSyncIop`, `sceSifRebootIop` | RPC servers 0x81000101 (SOUND), 0x81000102 (VAG), 0x80000210 (USBJOY), plus SDK servers |
| fileio | `sceFsInit` (H), `sceFsReset` (H), `sceOpen`, `sceClose`, `sceLseek`, `sceRead`, `sceWrite`, `sceDevctl` (H), `sceIoctl2` (H), `sceGetstat` (H), `sceDread` (H), `sceDclose` (H), `sceDopen` (H), `sceRemove` (H) | the fileio calls on the boot path are made only by HDD and debug code |
| libpad | 13 functions (§7) | |
| libmc | `sceMcInit` | |
| libcdvd | `sceCdInit`, `sceCdMmode`, `sceCdGetDiskType`, `sceCdDiskReady`, `sceCdGetToc`, `sceCdSearchFile`, `sceCdRead`, `sceCdReadIOPm` (H), `sceCdSync`, `sceCdSeek`, `sceCdBreak`, `sceCdPOffCallback` (H), `sceCdPowerOff` (H) | port reads from the user's ISO |
| libm | `sinf`, `cosf`, `atan2f`, `sqrtf` | must match EE float behaviour (`ee_math.h`) |
| libc | `malloc`, `free`, `memset`, `printf`, `sprintf`, `srand`, `rand`, `strcat`, `strcmp`, `strcpy`, `strlen` | `rand` must reproduce newlib's LCG (random table) |
| runtime | `__main`, `__register_frame_info`, `__deregister_frame_info` | `__main` runs the original `.ctors` list through `CallGuest` (the 7 game constructors are translated in `src/game/static_init.cpp`; the two C++ runtime entries are no-ops); frame-info registration is a no-op |

Hypothesis names (plate comments, from the fileio/cdvd RPC function numbers): `sceDevctl` 0x00269940 (fno 0x17), `sceIoctl2` 0x00268070 (0x1A), `sceGetstat` 0x00268CF0 (0x0C), `sceDread` 0x00268B98 (0x0B), `sceDclose` 0x00268A30 (0x0A), `sceDopen` 0x00268990 (9), `sceRemove` 0x002683F0 (6), `sceCdPOffCallback` 0x0026EFB8, `sceCdPowerOff` 0x00270730 (S-cmd 0x21). All of them are called only from the HDD module.

## 12. Hardware registers touched directly by game code on the skeleton path

| Register | Where | Port handling |
|---|---|---|
| `RCNT0_COUNT` 0x10000000, `RCNT0_MODE` 0x10000010 | main loop (reset every frame), `InitMoviePlayback` and `StartMenuMovie` (mode 0x83: CLKS = 3, H-blank count; bit 7 count enable) | emulate T0 as an H-blank counter at the NTSC line rate (`MovieFrameSync` waits for 0x118) |
| DMAC `D_CTRL`, `D_RBOR`, `D_RBSR` | `InitSprRingBuffer` (ring set, drain MFD cleared) | no effect needed |
| DMA ch8 `SPR_FROM` CHCR/SADR/QWC/MADR, ch2 GIF `TADR` | `FlushSprWorkBuffer` (dead) | none: the SPR → GIF ring is not used by live code |
| VIF0 `MARK`/`ERR`/`FBRST`/`FIFO`, VU0 `vi28` | `sceVu0ResetVif0` (H) | no-op |
| scratchpad 0x70000000 | 73 functions (`../game/memory.md`) | 16 KB guest memory region (ADR-001 B) |

## Boot-path facts the port must honour

1. **Heap size probe.** `ProbeLargestMallocSize` depends on where the ELF's `.bss` ends and how newlib's heap works. The work heap size decides when `WorkHeapAlloc` returns 0. The port must use the same size the PS2 gets: base 0x004CC760, size 0x01A318A4 (`../pcsx2/sessions.md` run 03). Mission 1 fills it to 98.4 %, so allocation sizes must match exactly.
2. **SPR→GIF ring buffer.** `InitSprRingBuffer` sets `D_RBOR`/`D_RBSR` at boot but clears the drain bits, and `FlushSprWorkBuffer`/`GetSprWorkBuffer` are dead. The renderer only needs `sceDmaSend` chains.
3. **Field parity and `sceGsSyncV`.** The boot waits for a non-zero `sceGsSyncV` return, and `UpdateDisplayFrame` uses the parity. `sceGsSyncV` 0x00261C20 returns GS `CSR.FIELD` (bit 13) in interlaced mode, and 1 otherwise (disassembly). The port's frame clock must alternate parity exactly like the PS2 (ADR-002 already says so).
4. **RPC timing.** `InitSoundRpc`/`InitVagStream` are step functions that loop until the IOP replies. The SOUND/VAG HLE must answer the first calls (fno 0) with the same reply layout, or boot hangs. Measured (`../pcsx2/sessions.md` run 03): SOUND fno 0 reply = count 2 then zeros; VAG fno 0 reply = IOP stream base 0x0008DA40, ring indices 1,1,1,1 at +0x24, 0x200 at +0x50 and +0x60.
5. **USB joystick RPC.** `UpdatePads` calls `ReadUsbJoy` every frame (blocking `sceSifCallRpc` fno 2). The HLE answers "no device": word 0 = 0, axes 0x80, button words 0xFF (`../pcsx2/sessions.md` run 03).
6. **`rand()` sequence.** `InitRandomTable` seeds 1 then 123456789. The port must reproduce the ELF's `rand` 0x00291BD8 (verified in disassembly: `x = x*0x41C64E6D + 0x3039`, returns `x & 0x7FFFFFFF`, state at impure+0x58), not the host CRT. This affects all random tables (`ee_math`).
7. **HDD paths.** `GetCdFileLsn(0)` returns −1 when the HDD-install flag is set. No live code sets it, so it stays 0. The code stays translated.
8. **Partly-known data.** Sound-bank headers, VPK headers, pad mapping tables and option defaults are read by translated code as-is. They only need to be understood for debugging, not for the skeleton.

## PCSX2 measurements behind these facts

Results in `../pcsx2/sessions.md` run 03. Breakpoints: 0x00100220 in `main` (return from `InitWorkHeap`: u32 size at 0x00411784, base at 0x00411788); 0x00100130 in `InitSubsystems` (SOUND reply buffer 0, 0x600 bytes at 0x00429800); 0x00100138 (VAG reply buffer 0, 0x100 bytes at 0x0042CC90); 0x001097F8 in `ReadUsbJoy` (USB joystick reply, 0x80 bytes at 0x004204C0).

`frame_count` is incremented only by the main loop, so it is 0 at the first `SceneStep`; the vsyncs spent in the boot waits are not counted.

## Phase 3 outcome (moved from `../STATUS.md` 2026-10-20)

### 5. Readiness for the skeleton

**Ready:**
- Boot path closure: 199 game functions (about 168 understood, 31 partly, all read) and the 99 SDK entry points they call (`skeleton-readiness.md`).
- Boot values measured in PCSX2: work heap base/size, SOUND/VAG first replies, USB joystick "no device" reply.
- Main loop order, scene dispatch, file pipeline (TBL/CDP), ISO verification data, save image and folder-card layout.
- Every game function classified; dead code catalogued; records validated (`validation.md`).
- Architecture decisions ADR-001 to ADR-006 accepted (no open sub-decisions); ADR-007 (translation strategy) accepted 2026-10-13.

**Started (phase 3):** milestones A (build, platform) → B (ISO, guest memory) → C (boot path, main loop) → D (debug overlay) → E (validation). Progress: section 6.

### 6. Skeleton progress (phase 3)

| Milestone | Status |
|---|---|
| A. Build system and platform | done (2026-10-11) |
| B. ISO and guest memory | done (2026-10-11), awaiting User review: ISO reader, verification with cache, guest memory/ps2ptr/stack/call table, ELF load checked against PCSX2 (`validation.md` B-1, B-2) |
| C. Boot path and main loop | done (2026-10-12), reviewed; hardware register writes moved to explicit `src/sce/ee_hw.h` calls: 245 game functions translated (`translated-functions.csv`), SDK layer `src/sce/`, IOP stand-ins `src/platform/iop/`, game thread at 59.94 Hz; boot reaches the scene 6 stub after ~33 frames (`validation.md` C-1) |
| D. Debug overlay (minimal) | done (2026-10-12), reviewed: Dear ImGui overlay with performance, scene (+ request log), memory viewer with symbols, session log (`port-features.md` §8.7) |
| E. Validation | done (2026-10-12), awaiting User review: 46 doctest cases; RAM at the first `SceneStep` compared with PCSX2: every difference classified (`ramdiff-expected.csv`, 0 unexplained), three fixes (malloc chunk sizes, TOC, reverb address); EE FPU switched to round-to-nearest as PCSX2 runs AC04 (User decision 2026-10-13; `validation.md` E-1, E-2) |

Command-line parameters: `command-line.md`.
