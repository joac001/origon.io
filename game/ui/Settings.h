#pragma once

#include "render/Renderer.h"

#include <filesystem>
#include <string>

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
    bool adsDepthOfField = true;
    float vegetationDensity = 1.0f;
    float vegetationDistance = 90.0f;
    // Relay (servidor intermedio) for internet games: "host:port".
    std::string relayAddress = "127.0.0.1:27030";

    static GameSettings load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
};

} // namespace origon
