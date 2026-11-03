#include "core/FileSystem.h"

#include "core/Log.h"

#include <fstream>
#include <sstream>

#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace origon::fs {
namespace {

std::filesystem::path executableDir() {
#if defined(__linux__)
    char buffer[4096];
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0';
        return std::filesystem::path(buffer).parent_path();
    }
#elif defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0) {
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

bool hasContent(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::is_directory(dir / "assets", ec) && std::filesystem::is_directory(dir / "data", ec);
}

std::filesystem::path findContentRoot() {
    const std::filesystem::path candidates[] = {
        executableDir(),
        std::filesystem::current_path(),
        std::filesystem::path(ORIGON_SOURCE_DIR),
    };
    for (const auto& candidate : candidates) {
        if (hasContent(candidate)) {
            log::info("Content root: {}", candidate.string());
            return candidate;
        }
    }
    log::warn("Content root not found, using current directory");
    return std::filesystem::current_path();
}

} // namespace

const std::filesystem::path& contentRoot() {
    static const std::filesystem::path root = findContentRoot();
    return root;
}

std::filesystem::path resolve(const std::filesystem::path& relative) {
    if (relative.is_absolute()) {
        return relative;
    }
    return contentRoot() / relative;
}

std::optional<std::string> readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        log::error("Cannot open file: {}", path.string());
        return std::nullopt;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool writeText(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        log::error("Cannot write file: {}", path.string());
        return false;
    }
    file << text;
    return static_cast<bool>(file);
}

} // namespace origon::fs
