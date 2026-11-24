#include "platform/log.h"
#include "platform/paths.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <set>
#include <system_error>

namespace ac04::log {
namespace {

constexpr size_t kRecentCapacity = 2000;
constexpr const char* kFileName = "session.log";

std::mutex g_mutex;
std::FILE* g_file = nullptr;
std::filesystem::path g_path;
std::deque<std::string> g_recent;       // also the buffer for lines written before Open()
std::vector<std::string> g_pending;     // lines waiting for the file
std::set<std::string> g_once_keys;
const auto g_start = std::chrono::steady_clock::now();

const char* LevelName(Level level) {
    switch (level) {
        case Level::Info: return "info";
        case Level::Warning: return "WARN";
        case Level::Error: return "ERROR";
    }
    return "?";
}

std::string FormatLine(Level level, const char* module, const char* fmt, std::va_list args) {
    char message[2048];
    std::vsnprintf(message, sizeof(message), fmt, args);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
    char line[2200];
    std::snprintf(line, sizeof(line), "[%9.3f] %-5s %-10s %s", seconds, LevelName(level),
                  module ? module : "-", message);
    return line;
}

void Append(std::string line) {
    // Caller holds g_mutex.
    if (g_file) {
        std::fputs(line.c_str(), g_file);
        std::fputc('\n', g_file);
    } else {
        g_pending.push_back(line);
    }
#if AC04_DEV_BUILD
    std::fputs(line.c_str(), stderr);
    std::fputc('\n', stderr);
#endif
    g_recent.push_back(std::move(line));
    if (g_recent.size() > kRecentCapacity) g_recent.pop_front();
}

}  // namespace

bool Open(const std::filesystem::path& logs_dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::error_code ec;
    std::filesystem::create_directories(logs_dir, ec);
    // Only the last session is kept: remove every older file in the folder.
    for (const auto& entry : std::filesystem::directory_iterator(logs_dir, ec)) {
        if (entry.is_regular_file(ec)) std::filesystem::remove(entry.path(), ec);
    }
    g_path = logs_dir / kFileName;
    g_file = platform::OpenFile(g_path, "wb");
    if (!g_file) return false;
    for (const auto& line : g_pending) {
        std::fputs(line.c_str(), g_file);
        std::fputc('\n', g_file);
    }
    g_pending.clear();
    std::fflush(g_file);
    return true;
}

void Close() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void WriteV(Level level, const char* module, const char* fmt, std::va_list args) {
    std::string line = FormatLine(level, module, fmt, args);
    std::lock_guard<std::mutex> lock(g_mutex);
    Append(std::move(line));
    // Errors are flushed at once so they survive a crash that follows.
    if (g_file && level == Level::Error) std::fflush(g_file);
}

void Write(Level level, const char* module, const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    WriteV(level, module, fmt, args);
    va_end(args);
}

void WriteOnce(const char* key, Level level, const char* module, const char* fmt, ...) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_once_keys.insert(key).second) return;
    }
    std::va_list args;
    va_start(args, fmt);
    WriteV(level, module, fmt, args);
    va_end(args);
}

void Flush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fflush(g_file);
}

std::vector<std::string> RecentLines(size_t max_lines) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const size_t n = g_recent.size() < max_lines ? g_recent.size() : max_lines;
    return std::vector<std::string>(g_recent.end() - static_cast<std::ptrdiff_t>(n), g_recent.end());
}

void WriteRawUnsafe(const char* text) {
    if (g_file) {
        std::fputs(text, g_file);
        std::fflush(g_file);
    }
}

std::filesystem::path FilePath() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_path;
}

}  // namespace ac04::log
