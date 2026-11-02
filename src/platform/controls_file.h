// controls.ini (port baseline "Controls file", User request 2026-10-31; docs/port/port-features.md §1): the
// key, mouse and gamepad bindings in a file a player can edit with Notepad. INI layout: [Keyboard]
// names actions ("Throttle up = W, Mouse Left"), [Mouse] the mouse stick, [Gamepad] the PS2 buttons
// ("Cross = South"). Notes start with ';' or '#'. The port writes the file with its defaults only when
// it is missing and never rewrites it; an entry it cannot read is reported and keeps its default,
// the rest of the file still counts. Edits apply while the port runs. A file without the line
// "Version = 1" (kControlsVersion) is incompatible: deleted and replaced with the defaults.
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/input.h"

namespace ac04::platform {

// Layout version of the file ("Version = N" before the first section). Bump it when an action or
// entry is removed or renamed or a value changes meaning.
constexpr int kControlsVersion = 1;

// The file the port writes when there is none (comments, the AC7-style keyboard defaults, the mouse
// and the gamepad layout of earlier builds).
const std::string& DefaultControlsText();
// DefaultControlsText() read.
ControlBindings DefaultControls();
// Reads a controls file; `warnings` gets one line per entry that was not understood (that entry
// keeps its default). `compatible`: the file's version is kControlsVersion.
ControlBindings ParseControls(const std::string& text, std::vector<std::string>& warnings,
                              bool* compatible = nullptr);
// One host input in a player's spelling ("W", "Left Ctrl", "Mouse Left", "Mouse Wheel Up", "South",
// "Left Trigger", "Left Stick Up", "Joy Axis 7+", "Joy Button 4", or the settings syntax "key:W").
bool ParseFriendlyInput(const std::string& text, InputSource& source);

// The file on disk: loaded at start and again when it changes (checked at most once a second).
class ControlsFile {
public:
    explicit ControlsFile(std::filesystem::path path) : path_(std::move(path)) {}
    // Reads the file (writing the defaults first when it is missing); problems go to the log.
    ControlBindings Load();
    // True (once) when the file was changed since the last Load.
    bool Changed();
    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
    std::filesystem::file_time_type last_time_{};
    std::chrono::steady_clock::time_point last_check_{};
};

}  // namespace ac04::platform
