#include "platform/paths.h"

#include <unistd.h>

#include <cstdlib>

namespace ac04::platform {

// ADR-005 §2: $XDG_DATA_HOME/projectstonehenge/, falling back to ~/.local/share/projectstonehenge/.
std::filesystem::path DefaultUserDataDir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] == '/') return std::filesystem::path(xdg) / "projectstonehenge";
    const char* home = std::getenv("HOME");
    if (home && home[0] == '/') {
        return std::filesystem::path(home) / ".local" / "share" / "projectstonehenge";
    }
    return {};
}

std::filesystem::path ExecutableDir() {
    char buffer[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (n <= 0) return {};
    buffer[n] = 0;
    return std::filesystem::path(buffer).parent_path();
}

}  // namespace ac04::platform
