#include "render/Texture.h"

#include "core/FileSystem.h"
#include "core/Log.h"

#include <glad/gl.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>

namespace origon {

TextureCache::~TextureCache() { clear(); }

void TextureCache::clear() {
    for (auto& [path, info] : m_textures) {
        if (info.id) glDeleteTextures(1, &info.id);
    }
    m_textures.clear();
}

const TextureInfo& TextureCache::load(const std::filesystem::path& path, bool srgb) {
    const std::string key = path.string() + (srgb ? "#srgb" : "#linear");
    if (auto it = m_textures.find(key); it != m_textures.end()) {
        return it->second;
    }
    TextureInfo info;
    int width = 0, height = 0, channels = 0;
    const std::string resolved = fs::resolve(path).string();
    stbi_uc* pixels = stbi_load(resolved.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        log::warn("Texture not found: {}", resolved);
        return m_textures.emplace(key, info).first->second;
    }
    info.width = width;
    info.height = height;
    info.hasAlpha = channels == 4;
    const int levels = 1 + static_cast<int>(std::floor(std::log2(std::max(width, height))));
    glCreateTextures(GL_TEXTURE_2D, 1, &info.id);
    glTextureStorage2D(info.id, levels, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height);
    glTextureSubImage2D(info.id, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateTextureMipmap(info.id);
    glTextureParameteri(info.id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(info.id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(info.id, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(info.id, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameterf(info.id, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    stbi_image_free(pixels);
    return m_textures.emplace(key, info).first->second;
}

} // namespace origon
