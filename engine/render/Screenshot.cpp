#include "render/Screenshot.h"

#include "core/Log.h"

#include <glad/gl.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <vector>

namespace origon {

bool saveScreenshot(const std::filesystem::path& path, int width, int height, unsigned framebuffer) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(framebuffer == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    stbi_flip_vertically_on_write(1);
    const bool ok = stbi_write_png(path.string().c_str(), width, height, 3, pixels.data(), width * 3) != 0;
    if (ok) {
        log::info("Screenshot saved: {}", path.string());
    } else {
        log::error("Screenshot failed: {}", path.string());
    }
    return ok;
}

} // namespace origon
