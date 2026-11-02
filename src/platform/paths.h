// Locations of everything the port writes (ADR-005 §2). Only this module builds these paths.
#pragma once

#include <filesystem>
#include <string>

namespace ac04::platform {

struct Paths {
    std::filesystem::path user_data;      // %LOCALAPPDATA%\ProjectStonehenge\ (or --user-data)
    std::filesystem::path saves;          // user_data\saves\  (PCSX2 folder memory card root)
    std::filesystem::path logs;           // user_data\logs\   (last session only)
    std::filesystem::path settings_file;  // user_data\settings.json
    std::filesystem::path controls_file;  // user_data\controls.ini (key, mouse and gamepad bindings)
    std::filesystem::path cache;          // user_data\cache\  (renderer caches; deletable)
    std::filesystem::path pipeline_cache_file;  // cache\pipelines.bin (Vulkan pipeline cache data)
    std::filesystem::path exe_dir;        // folder of the executable (read only)
};

// Default per-user data folder for this OS (Windows: %LOCALAPPDATA%\ProjectStonehenge).
std::filesystem::path DefaultUserDataDir();
std::filesystem::path ExecutableDir();

// Fills `out` from `user_data_override` (empty = default) and creates the folders.
// Returns false with a human-readable reason if a folder cannot be created.
bool ResolvePaths(const std::filesystem::path& user_data_override, Paths& out, std::string& error);

// UTF-8 string <-> path (C++17: u8path / u8string).
std::filesystem::path PathFromUtf8(const std::string& utf8);
std::string PathToUtf8(const std::filesystem::path& path);

}  // namespace ac04::platform
