#include "platform/file_reader.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>

namespace ac04::platform {

namespace {

// The header keeps the handle as an opaque pointer (nullptr = closed). On POSIX it holds the file
// descriptor + 1, so descriptor 0 is still distinguishable from "closed".
void* EncodeFd(int fd) { return reinterpret_cast<void*>(static_cast<std::intptr_t>(fd) + 1); }
int DecodeFd(void* handle) { return static_cast<int>(reinterpret_cast<std::intptr_t>(handle) - 1); }

}  // namespace

FileReader::~FileReader() { Close(); }

bool FileReader::Open(const std::filesystem::path& path, std::string& error) {
    Close();
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        const int code = errno;
        error = code == ENOENT || code == ENOTDIR
                    ? "the file does not exist"
                    : std::string("the file cannot be opened (") + std::strerror(code) + ")";
        return false;
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        error = "cannot read the file size";
        ::close(fd);
        return false;
    }
#ifdef POSIX_FADV_RANDOM
    ::posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM);
#endif
    handle_ = EncodeFd(fd);
    size_ = static_cast<std::uint64_t>(st.st_size);
    mtime_ = static_cast<std::uint64_t>(st.st_mtim.tv_sec) * 1000000000ull +
             static_cast<std::uint64_t>(st.st_mtim.tv_nsec);
    return true;
}

void FileReader::Close() {
    if (handle_) ::close(DecodeFd(handle_));
    handle_ = nullptr;
    size_ = 0;
    mtime_ = 0;
}

bool FileReader::Read(std::uint64_t offset, void* dst, std::uint64_t bytes) const {
    if (!handle_ || offset + bytes > size_) return false;
    const int fd = DecodeFd(handle_);
    auto* out = static_cast<unsigned char*>(dst);
    while (bytes > 0) {
        // Positioned read (no shared file pointer), so worker threads can read concurrently.
        const size_t chunk = bytes > 0x40000000u ? 0x40000000u : static_cast<size_t>(bytes);
        const ssize_t got = ::pread(fd, out, chunk, static_cast<off_t>(offset));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return false;
        out += got;
        offset += static_cast<std::uint64_t>(got);
        bytes -= static_cast<std::uint64_t>(got);
    }
    return true;
}

}  // namespace ac04::platform
