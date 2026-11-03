#pragma once

#include <filesystem>

namespace origon {

// Reads the default framebuffer and writes it as PNG. Call after rendering, before swapping.
bool saveScreenshot(const std::filesystem::path& path, int width, int height, unsigned framebuffer = 0);

} // namespace origon
