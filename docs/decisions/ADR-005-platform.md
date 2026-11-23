# ADR-005: Platform, saves and logging

Status: **Accepted** (User decision 2026-10-10; storage revised by the User 2026-10-11: everything under `%LOCALAPPDATA%\ProjectStonehenge\`). No open sub-points.

## Context

- The port is a native PC program. It needs a target platform order, a save location that interoperates with PCSX2, and a session log for support and debugging.
- The User decided all three. This ADR records the decisions and their consequences.

## Decision

### 1. Platform
- **Windows first.** Linux support is deferred until the port is complete. macOS (MoltenVK) stays best effort.
- Do not choose libraries or APIs that would clearly be hard to make work on Linux later. Examples: Win32-only audio or input APIs in place of SDL3, Direct3D in place of Vulkan, MSVC-only language extensions in portable code.
- OS-specific code is isolated in `src/platform/`, behind interfaces. `src/game/` and `src/sce/` contain no OS calls.
- **File names are case-sensitive.** ISO file names are matched exactly as stored on the disc (uppercase, e.g. `DATA.CDP`, `SLUS_201.52`). The port never relies on case-insensitive lookup, not even on Windows.

### 1a. Amendment: Linux build now (**Proposed**, issue #13)
- Status: **Proposed**, waiting for the owner's decision. Sections 1-5 stay Accepted as they are.
- Proposal: build and ship Linux x86-64 now instead of after the port is complete, as a native build of the same code (no game-code change), packaged as an AppImage for the Steam Deck and desktop distributions.
- Scope: five POSIX files in `src/platform/posix/` behind the existing interfaces, compiler flags that give GCC/Clang the MSVC build's floating-point and integer behaviour, a Linux CMake preset, a `package_appimage` target and an Ubuntu 22.04 build container. Details and validation: `../port/linux.md`.
- User data on Linux: `$XDG_DATA_HOME/projectstonehenge/` (fallback `~/.local/share/projectstonehenge/`), as §2 already specified.
- Cost: one more build to keep compiling (the CI or the maintainer builds both). Windows stays the primary platform.

### 2. User data location
- All files the port writes go under one per-user folder: **`%LOCALAPPDATA%\ProjectStonehenge\`** on Windows.
  - Linux later: `$XDG_DATA_HOME/projectstonehenge/` (fallback `~/.local/share/projectstonehenge/`); macOS later: `~/Library/Application Support/ProjectStonehenge/`.
  - The path is resolved only in `src/platform/` (a paths interface: `UserDataDir()`, `SavesDir()`, `SettingsFile()`, `LogsDir()`), so another OS is one new implementation.
  - Nothing is written next to the executable. This replaces the earlier "executable directory" rule and removes the non-writable-directory question.
- Layout:

```
%LOCALAPPDATA%\ProjectStonehenge\
  saves\           PCSX2 Folder Memory Card root (slot 1)
  settings.json    all settings
  logs\            log of the last session only
  cache\           renderer caches: pipelines.bin (Vulkan pipeline cache; added 2026-10-25, User approval)
```
- `cache\` holds only data the port can rebuild: deleting it costs one slower start, never a setting or a save. A file made for another GPU or driver, or a damaged one, is deleted and rebuilt (`../port/port-features.md` §6).

### 3. Saves
- `saves\` is a complete **PCSX2 Folder Memory Card**: a `_pcsx2_superblock` file in the root and one host folder per PS2 directory (AC04: `BASLUS-20152AC04\` with `icon.sys`, `icon1.ico`, `BASLUS-20152AC04` and `_pcsx2_index`).
  - The whole `saves\` folder can be used as a PCSX2 folder card, and single save folders can be copied between the port and PCSX2 in both directions.
  - Each save file holds the raw bytes exactly as the PS2 writes them to the card.
  - Reference layout: the User's example card (`CLAUDE.local.md`, "Memory Card Folder example"), described in `../formats/folder-memcard.md` §9. Layout and PCSX2's metadata files were researched from the PCSX2 source for compatibility only, no code copied.
- Memory card port 2 reports **empty** (no card); only port 1 maps to `saves\` (User decision 2026-10-11).
- The game's own save logic (`sceMc*` calls, file names, file contents) is reproduced exactly. Save layers: `../subsystems/save.md`; file contents: `../formats/SAVE.md`. `src/sce/` maps `sceMc*` onto the folder card.

### 4. Settings
- `settings.json` holds every port setting (presentation, frame cap, v-sync, window mode, filter, debug flags, key bindings when approved).
- Code: one `Settings` struct with defaults, JSON load/save, and a change notification so a settings menu can be added later without rework (ADR-006). Missing or invalid values fall back to defaults and are logged. Unknown keys are kept on save.
- Game code never reads settings: they only affect the platform layer (presentation, input mapping, audio output).

### 4b. Audio output
- The port opens the **system default audio device**. Device selection is a later baseline candidate. On focus loss audio **keeps playing** by default (setting `audio.on_focus_loss`, approved baseline 2026-10-11).

### 5. Logging
- `logs\` holds **only the log of the last session**. Older logs are deleted at start.
- The log records:
  - build version, OS and GPU information, ISO verification result, the settings in effect;
  - errors and problems in the main modules: startup, ISO check, renderer, audio, file I/O, saves, IOP stand-ins (SOUND/VAG/USBJOY services);
  - unimplemented `sce*` calls, **once each** (including the stubs for SDK functions reached only from dead game code);
  - crashes, through a crash handler in `src/platform/` (exception code, address, guest PC/scene when known, a short host stack trace).
- It is **not** written per call or per frame.
- **No game data in logs** (no memory dumps, no file contents, no text extracted from the disc).

## Consequences

- `src/platform/` provides:
  - paths: the user data folder, `saves\`, `settings.json`, `logs\`, `cache\` (per OS);
  - case-exact ISO lookup;
  - the log sink and the crash handler;
  - the folder-card storage behind `sceMc*`;
  - settings load/save and change notification.
- Portable code uses only C++17 and the approved dependencies (decided at the skeleton phase).
- Tests can compare save folders byte for byte with folders written by PCSX2.
