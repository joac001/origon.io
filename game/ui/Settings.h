#pragma once

#include "render/Renderer.h"

#include <filesystem>

namespace origon {

struct GameSettings {
    RenderSettings render;
    bool vsync = true;
    int maxFps = 0; // 0 = unlimited (only applies with vsync off).
    float fov = 80.0f;
    float mouseSensitivity = 0.10f; // Degrees per pixel.
    float adsSensitivityScale = 0.7f;
    bool invertY = false;
    int botCount = 5;
    float masterVolume = 0.8f;
    bool ambience = true;
    bool showStats = false;
    float vegetationDensity = 1.0f;
    float vegetationDistance = 90.0f;

    static GameSettings load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
};

} // namespace origon
