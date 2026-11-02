#include "platform/paths.h"

#include <system_error>

namespace ac04::platform {

std::filesystem::path PathFromUtf8(const std::string& utf8) {
    return std::filesystem::u8path(utf8);
}

std::string PathToUtf8(const std::filesystem::path& path) {
    return path.u8string();
}

bool ResolvePaths(const std::filesystem::path& user_data_override, Paths& out, std::string& error) {
    out.user_data = user_data_override.empty() ? DefaultUserDataDir() : user_data_override;
    if (out.user_data.empty()) {
        error = "The per-user data folder could not be determined.";
        return false;
    }
    out.saves = out.user_data / "saves";
    out.logs = out.user_data / "logs";
    out.settings_file = out.user_data / "settings.json";
    out.controls_file = out.user_data / "controls.ini";
    out.cache = out.user_data / "cache";
    out.pipeline_cache_file = out.cache / "pipelines.bin";
    out.exe_dir = ExecutableDir();
    for (const auto& dir : {out.user_data, out.saves, out.logs, out.cache}) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec || !std::filesystem::is_directory(dir)) {
            error = "Cannot create the folder " + PathToUtf8(dir) + ": " + ec.message();
            return false;
        }
    }
    return true;
}

}  // namespace ac04::platform
