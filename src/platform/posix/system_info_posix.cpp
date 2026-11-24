#include "platform/system_info.h"

#include <sys/utsname.h>

#include <fstream>
#include <string>

namespace ac04::platform {

std::string OperatingSystemDescription() {
    // Distribution name from os-release (e.g. "SteamOS"), then the kernel.
    std::string distro;
    std::ifstream os_release("/etc/os-release");
    for (std::string line; std::getline(os_release, line);) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            distro = line.substr(12);
            if (distro.size() >= 2 && (distro.front() == '"' || distro.front() == '\'')) {
                distro = distro.substr(1, distro.size() - 2);
            }
            break;
        }
    }
    struct utsname info {};
    std::string kernel = "unknown kernel";
    if (::uname(&info) == 0) kernel = std::string(info.sysname) + " " + info.release;
    return distro.empty() ? kernel : distro + " (" + kernel + ")";
}

}  // namespace ac04::platform
