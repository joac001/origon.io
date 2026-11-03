#include "ui/Settings.h"

#include "core/FileSystem.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <system_error>

namespace origon {

GameSettings GameSettings::load(const std::filesystem::path& path) {
    GameSettings settings;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return settings;
    }
    const auto text = fs::readText(path);
    if (!text) {
        return settings;
    }
    try {
        const nlohmann::json j = nlohmann::json::parse(*text);
        RenderSettings& r = settings.render;
        r.shadowQuality = std::clamp(j.value("shadow_quality", r.shadowQuality), 0, 4);
        r.msaaSamples = j.value("msaa_samples", r.msaaSamples);
        r.renderScale = std::clamp(j.value("render_scale", r.renderScale), 0.5f, 1.0f);
        r.exposure = j.value("exposure", r.exposure);
        r.ssao = j.value("ssao", r.ssao);
        r.bloom = j.value("bloom", r.bloom);
        r.godRays = j.value("volumetric_light", r.godRays);
        r.ssr = j.value("ssr", r.ssr);
        settings.masterVolume = std::clamp(j.value("master_volume", settings.masterVolume), 0.0f, 1.0f);
        settings.ambience = j.value("ambience", settings.ambience);
        settings.showStats = j.value("show_stats", settings.showStats);
        settings.vegetationDensity = std::clamp(j.value("vegetation_density", settings.vegetationDensity), 0.2f, 1.0f);
        settings.vegetationDistance = std::clamp(j.value("vegetation_distance", settings.vegetationDistance), 30.0f, 150.0f);
        settings.vsync = j.value("vsync", settings.vsync);
        settings.maxFps = std::max(0, j.value("max_fps", settings.maxFps));
        settings.fov = std::clamp(j.value("fov", settings.fov), 60.0f, 110.0f);
        settings.mouseSensitivity = std::clamp(j.value("mouse_sensitivity", settings.mouseSensitivity), 0.01f, 1.0f);
        settings.adsSensitivityScale = j.value("ads_sensitivity_scale", settings.adsSensitivityScale);
        settings.invertY = j.value("invert_y", settings.invertY);
        settings.botCount = std::clamp(j.value("bot_count", settings.botCount), 0, 9);
    } catch (const std::exception& e) {
        log::warn("Settings parse error, using defaults: {}", e.what());
    }
    return settings;
}

bool GameSettings::save(const std::filesystem::path& path) const {
    const nlohmann::json j = {
        {"shadow_quality", render.shadowQuality},
        {"msaa_samples", render.msaaSamples},
        {"render_scale", render.renderScale},
        {"exposure", render.exposure},
        {"ssao", render.ssao},
        {"bloom", render.bloom},
        {"volumetric_light", render.godRays},
        {"ssr", render.ssr},
        {"master_volume", masterVolume},
        {"ambience", ambience},
        {"show_stats", showStats},
        {"vegetation_density", vegetationDensity},
        {"vegetation_distance", vegetationDistance},
        {"vsync", vsync},
        {"max_fps", maxFps},
        {"fov", fov},
        {"mouse_sensitivity", mouseSensitivity},
        {"ads_sensitivity_scale", adsSensitivityScale},
        {"invert_y", invertY},
        {"bot_count", botCount},
    };
    return fs::writeText(path, j.dump(2));
}

} // namespace origon
