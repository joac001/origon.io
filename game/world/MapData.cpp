#include "world/MapData.h"

#include "core/FileSystem.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

namespace origon {
namespace {

using json = nlohmann::json;

glm::vec3 readVec3(const json& node, const char* key, const glm::vec3& fallback) {
    if (!node.contains(key) || !node[key].is_array() || node[key].size() != 3) {
        return fallback;
    }
    const json& a = node[key];
    return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
}

json writeVec3(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

} // namespace

glm::quat BlockData::orientation() const {
    const glm::vec3 r = glm::radians(rotation);
    return glm::angleAxis(r.y, glm::vec3(0, 1, 0)) * glm::angleAxis(r.x, glm::vec3(1, 0, 0)) *
           glm::angleAxis(r.z, glm::vec3(0, 0, 1));
}

std::optional<MapData> MapData::load(const std::filesystem::path& path) {
    const auto text = fs::readText(path);
    if (!text) {
        return std::nullopt;
    }
    try {
        const json root = json::parse(*text);
        const int version = root.value("version", 0);
        if (version > kVersion) {
            log::error("Map {} has version {}, this build supports {}", path.string(), version, kVersion);
            return std::nullopt;
        }
        MapData map;
        map.name = root.value("name", std::string("Untitled"));
        if (root.contains("environment")) {
            const json& e = root["environment"];
            EnvironmentData& env = map.environment;
            env.sunDirection = readVec3(e, "sun_direction", env.sunDirection);
            env.sunColor = readVec3(e, "sun_color", env.sunColor);
            env.sunIntensity = e.value("sun_intensity", env.sunIntensity);
            env.skyColor = readVec3(e, "sky_color", env.skyColor);
            env.groundColor = readVec3(e, "ground_color", env.groundColor);
            env.ambientIntensity = e.value("ambient_intensity", env.ambientIntensity);
            env.fogColor = readVec3(e, "fog_color", env.fogColor);
            env.fogDensity = e.value("fog_density", env.fogDensity);
        }
        for (const json& b : root.value("blocks", json::array())) {
            BlockData block;
            block.position = readVec3(b, "pos", block.position);
            block.size = readVec3(b, "size", block.size);
            block.rotation = readVec3(b, "rot", block.rotation);
            block.color = readVec3(b, "color", block.color);
            block.roughness = b.value("roughness", block.roughness);
            block.metallic = b.value("metallic", block.metallic);
            block.dynamic = b.value("dynamic", block.dynamic);
            block.mass = b.value("mass", block.mass);
            map.blocks.push_back(block);
        }
        for (const json& s : root.value("spawns", json::array())) {
            SpawnData spawn;
            spawn.position = readVec3(s, "pos", spawn.position);
            spawn.yawDegrees = s.value("yaw", 0.0f);
            map.spawns.push_back(spawn);
        }
        for (const json& l : root.value("lights", json::array())) {
            LightData light;
            light.position = readVec3(l, "pos", light.position);
            light.color = readVec3(l, "color", light.color);
            light.intensity = l.value("intensity", light.intensity);
            light.radius = l.value("radius", light.radius);
            map.lights.push_back(light);
        }
        log::info("Map loaded: {} ({} blocks, {} spawns, {} lights)", map.name, map.blocks.size(),
                  map.spawns.size(), map.lights.size());
        return map;
    } catch (const std::exception& e) {
        log::error("Map {} parse error: {}", path.string(), e.what());
        return std::nullopt;
    }
}

bool MapData::save(const std::filesystem::path& path) const {
    json root;
    root["version"] = kVersion;
    root["name"] = name;
    root["environment"] = {
        {"sun_direction", writeVec3(environment.sunDirection)},
        {"sun_color", writeVec3(environment.sunColor)},
        {"sun_intensity", environment.sunIntensity},
        {"sky_color", writeVec3(environment.skyColor)},
        {"ground_color", writeVec3(environment.groundColor)},
        {"ambient_intensity", environment.ambientIntensity},
        {"fog_color", writeVec3(environment.fogColor)},
        {"fog_density", environment.fogDensity},
    };
    json blockArray = json::array();
    for (const BlockData& b : blocks) {
        json block = {{"pos", writeVec3(b.position)},
                      {"size", writeVec3(b.size)},
                      {"rot", writeVec3(b.rotation)},
                      {"color", writeVec3(b.color)},
                      {"roughness", b.roughness},
                      {"metallic", b.metallic}};
        if (b.dynamic) {
            block["dynamic"] = true;
            block["mass"] = b.mass;
        }
        blockArray.push_back(block);
    }
    root["blocks"] = blockArray;
    json spawnArray = json::array();
    for (const SpawnData& s : spawns) {
        spawnArray.push_back({{"pos", writeVec3(s.position)}, {"yaw", s.yawDegrees}});
    }
    root["spawns"] = spawnArray;
    json lightArray = json::array();
    for (const LightData& l : lights) {
        lightArray.push_back({{"pos", writeVec3(l.position)},
                              {"color", writeVec3(l.color)},
                              {"intensity", l.intensity},
                              {"radius", l.radius}});
    }
    root["lights"] = lightArray;
    const bool ok = fs::writeText(path, root.dump(1));
    if (ok) {
        log::info("Map saved: {}", path.string());
    }
    return ok;
}

MapData MapData::makeDefault() {
    MapData map;
    map.name = "Nuevo mapa";
    map.blocks.push_back({{0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f}, {0.0f, 0.0f, 0.0f}, {0.4f, 0.42f, 0.38f}});
    map.spawns.push_back({{0.0f, 0.1f, -8.0f}, 0.0f});
    map.spawns.push_back({{0.0f, 0.1f, 8.0f}, 180.0f});
    return map;
}

} // namespace origon
