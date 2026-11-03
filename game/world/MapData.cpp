#include "world/MapData.h"

#include "core/FileSystem.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

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

constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = i + 1 < data.size() ? data[i + 1] : 0;
        const uint32_t b2 = i + 2 < data.size() ? data[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kBase64[(triple >> 18) & 63]);
        out.push_back(kBase64[(triple >> 12) & 63]);
        out.push_back(i + 1 < data.size() ? kBase64[(triple >> 6) & 63] : '=');
        out.push_back(i + 2 < data.size() ? kBase64[triple & 63] : '=');
    }
    return out;
}

std::vector<uint8_t> base64Decode(const std::string& text) {
    int table[256];
    std::fill(std::begin(table), std::end(table), -1);
    for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(kBase64[i])] = i;
    std::vector<uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        const int value = table[static_cast<unsigned char>(c)];
        if (value < 0) continue;
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

// Heights are stored as little-endian int16 centimeters.
std::string encodeHeights(const std::vector<float>& heights) {
    std::vector<uint8_t> bytes;
    bytes.reserve(heights.size() * 2);
    for (float h : heights) {
        const auto cm = static_cast<int16_t>(std::clamp(std::lround(h * 100.0f), -32768L, 32767L));
        const auto u = static_cast<uint16_t>(cm);
        bytes.push_back(static_cast<uint8_t>(u & 0xFF));
        bytes.push_back(static_cast<uint8_t>(u >> 8));
    }
    return base64Encode(bytes);
}

std::vector<float> decodeHeights(const std::string& text) {
    const std::vector<uint8_t> bytes = base64Decode(text);
    std::vector<float> heights(bytes.size() / 2);
    for (size_t i = 0; i < heights.size(); ++i) {
        const auto u = static_cast<uint16_t>(bytes[i * 2] | (bytes[i * 2 + 1] << 8));
        heights[i] = static_cast<float>(static_cast<int16_t>(u)) / 100.0f;
    }
    return heights;
}

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
    auto map = fromJson(*text);
    if (!map) {
        log::error("Map {} could not be parsed", path.string());
    }
    return map;
}

std::optional<MapData> MapData::fromJson(const std::string& text) {
    try {
        const json root = json::parse(text);
        const int version = root.value("version", 0);
        if (version > kVersion) {
            log::error("Map has version {}, this build supports {}", version, kVersion);
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
        if (root.contains("terrain")) {
            const json& t = root["terrain"];
            TerrainData& terrain = map.terrain;
            terrain.enabled = t.value("enabled", false);
            terrain.size = t.value("size", terrain.size);
            terrain.resolution = t.value("resolution", terrain.resolution);
            terrain.seed = t.value("seed", terrain.seed);
            terrain.amplitude = t.value("amplitude", terrain.amplitude);
            terrain.frequency = t.value("frequency", terrain.frequency);
            terrain.octaves = t.value("octaves", terrain.octaves);
            terrain.waterEnabled = t.value("water_enabled", terrain.waterEnabled);
            terrain.waterLevel = t.value("water_level", terrain.waterLevel);
            if (t.contains("heights")) terrain.heights = decodeHeights(t["heights"].get<std::string>());
            if (t.contains("paint")) terrain.paint = base64Decode(t["paint"].get<std::string>());
            const size_t count = static_cast<size_t>(terrain.resolution) * static_cast<size_t>(terrain.resolution);
            if (terrain.enabled && terrain.heights.size() != count) {
                terrain.generate(); // Missing or mismatched data: rebuild from the generator parameters.
            }
            terrain.ensureStorage();
        }
        for (const json& v : root.value("vegetation", json::array())) {
            VegetationLayer layer;
            layer.model = v.value("model", std::string());
            layer.density = v.value("density", layer.density);
            layer.minScale = v.value("min_scale", layer.minScale);
            layer.maxScale = v.value("max_scale", layer.maxScale);
            layer.minSlope = v.value("min_slope", layer.minSlope);
            layer.maxSlope = v.value("max_slope", layer.maxSlope);
            layer.waterMargin = v.value("water_margin", layer.waterMargin);
            layer.clumping = v.value("clumping", layer.clumping);
            layer.avoidPaths = v.value("avoid_paths", layer.avoidPaths);
            layer.sink = v.value("sink", layer.sink);
            layer.wind = v.value("wind", layer.wind);
            layer.collision = v.value("collision", layer.collision);
            layer.alignToGround = v.value("align_to_ground", layer.alignToGround);
            layer.groundCover = v.value("ground_cover", layer.groundCover);
            layer.seed = v.value("seed", layer.seed);
            if (!layer.model.empty()) map.vegetation.push_back(layer);
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
            block.texture = b.value("texture", std::string());
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
        log::error("Map parse error: {}", e.what());
        return std::nullopt;
    }
}

bool MapData::save(const std::filesystem::path& path) const {
    const bool ok = fs::writeText(path, toJson());
    if (ok) {
        log::info("Map saved: {}", path.string());
    }
    return ok;
}

std::string MapData::toJson() const {
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
    if (terrain.enabled) {
        root["terrain"] = {
            {"enabled", true},
            {"size", terrain.size},
            {"resolution", terrain.resolution},
            {"seed", terrain.seed},
            {"amplitude", terrain.amplitude},
            {"frequency", terrain.frequency},
            {"octaves", terrain.octaves},
            {"water_enabled", terrain.waterEnabled},
            {"water_level", terrain.waterLevel},
            {"heights", encodeHeights(terrain.heights)},
            {"paint", base64Encode(terrain.paint)},
        };
    }
    json vegetationArray = json::array();
    for (const VegetationLayer& v : vegetation) {
        vegetationArray.push_back({{"model", v.model}, {"density", v.density}, {"min_scale", v.minScale}, {"max_scale", v.maxScale},
                                   {"min_slope", v.minSlope}, {"max_slope", v.maxSlope}, {"water_margin", v.waterMargin},
                                   {"clumping", v.clumping}, {"avoid_paths", v.avoidPaths}, {"sink", v.sink}, {"wind", v.wind},
                                   {"collision", v.collision}, {"align_to_ground", v.alignToGround},
                                   {"ground_cover", v.groundCover}, {"seed", v.seed}});
    }
    if (!vegetation.empty()) root["vegetation"] = vegetationArray;
    json blockArray = json::array();
    for (const BlockData& b : blocks) {
        json block = {{"pos", writeVec3(b.position)},
                      {"size", writeVec3(b.size)},
                      {"rot", writeVec3(b.rotation)},
                      {"color", writeVec3(b.color)},
                      {"roughness", b.roughness},
                      {"metallic", b.metallic}};
        if (!b.texture.empty()) block["texture"] = b.texture;
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
    return root.dump(1);
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
