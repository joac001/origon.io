#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace origon::fs {

// Root directory that contains assets/, data/ and maps/.
// Search order: next to the executable, current directory, source tree (dev builds).
const std::filesystem::path& contentRoot();

// Resolves a path relative to the content root.
std::filesystem::path resolve(const std::filesystem::path& relative);

std::optional<std::string> readText(const std::filesystem::path& path);
bool writeText(const std::filesystem::path& path, const std::string& text);

} // namespace origon::fs
