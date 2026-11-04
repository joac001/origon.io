#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace origon {

struct TextureInfo {
    unsigned id = 0;
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
};

// Loads image files into immutable GL textures with mipmaps. Each path is loaded once.
class TextureCache {
public:
    ~TextureCache();
    // srgb: color textures (albedo). Linear for normal and ARM maps. Returns id 0 on failure.
    const TextureInfo& load(const std::filesystem::path& path, bool srgb);
    // 2D array texture from same-size images (one layer per path). Cached by the joined paths.
    const TextureInfo& loadArray(const std::vector<std::filesystem::path>& paths, bool srgb);
    void clear();

private:
    std::unordered_map<std::string, TextureInfo> m_textures;
    TextureInfo m_missing;
};

} // namespace origon
