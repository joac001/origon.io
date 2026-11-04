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

const TextureInfo& TextureCache::loadArray(const std::vector<std::filesystem::path>& paths, bool srgb) {
    std::string key = srgb ? "array#srgb" : "array#linear";
    for (const auto& path : paths) key += "|" + path.string();
    if (auto it = m_textures.find(key); it != m_textures.end()) {
        return it->second;
    }
    TextureInfo info;
    std::vector<stbi_uc*> layers;
    int width = 0, height = 0;
    bool ok = !paths.empty();
    for (const auto& path : paths) {
        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels = stbi_load(fs::resolve(path).string().c_str(), &w, &h, &channels, 4);
        if (!pixels || (width && (w != width || h != height))) {
            log::warn("Texture array layer missing or wrong size: {}", path.string());
            if (pixels) stbi_image_free(pixels);
            ok = false;
            break;
        }
        width = w;
        height = h;
        layers.push_back(pixels);
    }
    if (ok) {
        const int levels = 1 + static_cast<int>(std::floor(std::log2(std::max(width, height))));
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &info.id);
        glTextureStorage3D(info.id, levels, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height, static_cast<int>(layers.size()));
        for (size_t i = 0; i < layers.size(); ++i) {
            glTextureSubImage3D(info.id, 0, 0, 0, static_cast<int>(i), width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, layers[i]);
        }
        glGenerateTextureMipmap(info.id);
        glTextureParameteri(info.id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(info.id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(info.id, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(info.id, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTextureParameterf(info.id, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
        info.width = width;
        info.height = height;
    }
    for (stbi_uc* pixels : layers) stbi_image_free(pixels);
    return m_textures.emplace(key, info).first->second;
}

} // namespace origon
