#include "editor/MapEditor.h"

#include "core/FileSystem.h"
#include "core/Input.h"
#include "core/Window.h"
#include "physics/PhysicsWorld.h"
#include "sim/Simulation.h"
#include "world/World.h"

#include <GLFW/glfw3.h>
#include <imgui.h> // Must precede ImGuizmo.h.
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace origon {
namespace {

constexpr float kMaxPitch = 1.55f;
constexpr size_t kMaxUndo = 64;

struct MaterialPreset {
    const char* name;
    glm::vec3 color; // sRGB
    float roughness;
    float metallic;
};

const MaterialPreset kMaterials[] = {
    {"Hormigón", {0.56f, 0.56f, 0.58f}, 0.9f, 0.0f},  {"Piedra", {0.45f, 0.43f, 0.4f}, 0.85f, 0.0f},
    {"Ladrillo", {0.55f, 0.28f, 0.2f}, 0.85f, 0.0f},  {"Madera", {0.52f, 0.35f, 0.2f}, 0.7f, 0.0f},
    {"Metal", {0.6f, 0.61f, 0.63f}, 0.35f, 1.0f},     {"Óxido", {0.5f, 0.3f, 0.18f}, 0.75f, 0.4f},
    {"Césped", {0.3f, 0.42f, 0.2f}, 0.95f, 0.0f},     {"Tierra", {0.4f, 0.31f, 0.22f}, 0.95f, 0.0f},
    {"Arena", {0.76f, 0.68f, 0.5f}, 0.95f, 0.0f},     {"Musgo", {0.25f, 0.33f, 0.16f}, 0.95f, 0.0f},
    {"Rojo", {0.7f, 0.2f, 0.18f}, 0.6f, 0.0f},        {"Azul", {0.2f, 0.35f, 0.7f}, 0.6f, 0.0f},
    {"Amarillo", {0.85f, 0.7f, 0.2f}, 0.6f, 0.0f},    {"Blanco", {0.85f, 0.85f, 0.83f}, 0.8f, 0.0f},
};
constexpr int kMaterialCount = static_cast<int>(std::size(kMaterials));

const char* kPrefabNames[] = {"Bloque", "Pared", "Losa", "Pilar", "Rampa", "Escalera", "Caja física", "Tablón físico",
                              "Spawn", "Luz"};
constexpr int kPrefabCount = static_cast<int>(std::size(kPrefabNames));
constexpr int kPrefabSpawn = 8;
constexpr int kPrefabLight = 9;

struct EnvironmentPreset {
    const char* name;
    EnvironmentData data;
};

EnvironmentPreset environmentPreset(int index) {
    EnvironmentData e;
    switch (index) {
    case 0: return {"Mediodía", e};
    case 1:
        e.sunDirection = {-0.6f, 0.22f, 0.4f};
        e.sunColor = {1.0f, 0.68f, 0.42f};
        e.sunIntensity = 3.6f;
        e.skyColor = {0.5f, 0.52f, 0.62f};
        e.groundColor = {0.3f, 0.22f, 0.17f};
        e.ambientIntensity = 0.45f;
        e.fogColor = {0.8f, 0.62f, 0.5f};
        e.fogDensity = 0.009f;
        return {"Atardecer", e};
    case 2:
        e.sunDirection = {0.3f, 0.85f, 0.2f};
        e.sunColor = {0.9f, 0.92f, 0.9f};
        e.sunIntensity = 1.6f;
        e.skyColor = {0.55f, 0.6f, 0.6f};
        e.groundColor = {0.22f, 0.25f, 0.2f};
        e.ambientIntensity = 0.85f;
        e.fogColor = {0.6f, 0.65f, 0.63f};
        e.fogDensity = 0.022f;
        return {"Bosque nublado", e};
    default:
        e.sunDirection = {0.7f, 0.18f, -0.5f};
        e.sunColor = {1.0f, 0.82f, 0.65f};
        e.sunIntensity = 2.8f;
        e.skyColor = {0.42f, 0.52f, 0.7f};
        e.groundColor = {0.2f, 0.22f, 0.24f};
        e.ambientIntensity = 0.5f;
        e.fogColor = {0.6f, 0.66f, 0.78f};
        e.fogDensity = 0.012f;
        return {"Amanecer frío", e};
    }
}

float rayObb(const glm::vec3& origin, const glm::vec3& dir, const BlockData& block) {
    const glm::quat inv = glm::inverse(block.orientation());
    const glm::vec3 o = inv * (origin - block.position);
    const glm::vec3 d = inv * dir;
    const glm::vec3 half = block.size * 0.5f;
    float tMin = 0.0f;
    float tMax = 1e9f;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(d[axis]) < 1e-6f) {
            if (o[axis] < -half[axis] || o[axis] > half[axis]) return -1.0f;
            continue;
        }
        float t1 = (-half[axis] - o[axis]) / d[axis];
        float t2 = (half[axis] - o[axis]) / d[axis];
        if (t1 > t2) std::swap(t1, t2);
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax) return -1.0f;
    }
    return tMin;
}

float raySphere(const glm::vec3& o, const glm::vec3& d, const glm::vec3& c, float r) {
    const glm::vec3 m = o - c;
    const float b = glm::dot(m, d);
    const float cc = glm::dot(m, m) - r * r;
    if (cc > 0.0f && b > 0.0f) return -1.0f;
    const float disc = b * b - cc;
    if (disc < 0.0f) return -1.0f;
    return std::max(0.0f, -b - std::sqrt(disc));
}

bool pointInBlock(const glm::vec3& p, const BlockData& block) {
    const glm::vec3 local = glm::inverse(block.orientation()) * (p - block.position);
    const glm::vec3 half = block.size * 0.5f;
    return std::abs(local.x) < half.x && std::abs(local.y) < half.y && std::abs(local.z) < half.z;
}

void boxLines(Renderer& renderer, const glm::mat4& model, const glm::vec4& color, bool overlay) {
    glm::vec3 c[8];
    for (int i = 0; i < 8; ++i) {
        c[i] = glm::vec3(model * glm::vec4((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.0f));
    }
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) {
        renderer.addLine(c[e[0]], c[e[1]], color, overlay);
    }
}

void circleLines(Renderer& renderer, const glm::vec3& center, float radius, const glm::vec4& color) {
    const int segments = 48;
    for (int i = 0; i < segments; ++i) {
        const float a0 = glm::two_pi<float>() * static_cast<float>(i) / segments;
        const float a1 = glm::two_pi<float>() * static_cast<float>(i + 1) / segments;
        renderer.addLine(center + glm::vec3(std::cos(a0), 0, std::sin(a0)) * radius,
                         center + glm::vec3(std::cos(a1), 0, std::sin(a1)) * radius, color);
    }
}

// Half extent of an oriented box along a direction.
float extentAlong(const glm::quat& rotation, const glm::vec3& half, const glm::vec3& n) {
    return std::abs(glm::dot(rotation * glm::vec3(1, 0, 0), n)) * half.x +
           std::abs(glm::dot(rotation * glm::vec3(0, 1, 0), n)) * half.y +
           std::abs(glm::dot(rotation * glm::vec3(0, 0, 1), n)) * half.z;
}

} // namespace

// --- Lifecycle ---------------------------------------------------------------------------------

void MapEditor::open(const MapData& map, const std::string& fileName, const glm::vec3& cameraPosition, float yaw,
                     float pitch, bool keepCamera) {
    m_map = map;
    m_fileName = fileName;
    if (!keepCamera || !m_cameraInitialized) {
        m_cameraPosition = cameraPosition;
        m_yaw = yaw;
        m_pitch = pitch;
        m_cameraInitialized = true;
    }
    m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(), [&](const ObjectRef& r) { return !validRef(r); }),
                      m_selection.end());
    m_playRequested = false;
    refreshMapList();
}

bool MapEditor::consumePlayRequest() {
    const bool requested = m_playRequested;
    m_playRequested = false;
    return requested;
}

void MapEditor::setStatus(const std::string& text) {
    m_status = text;
    m_statusTime = 4.0f;
}

// --- Camera ------------------------------------------------------------------------------------

glm::vec3 MapEditor::forward() const { return viewDirection(m_yaw, m_pitch); }

glm::mat4 MapEditor::viewMatrix() const {
    return glm::lookAt(m_cameraPosition, m_cameraPosition + forward(), glm::vec3(0, 1, 0));
}

glm::mat4 MapEditor::projectionMatrix() const {
    const float aspect = static_cast<float>(m_framebufferSize.x) / static_cast<float>(std::max(m_framebufferSize.y, 1));
    return glm::perspective(glm::radians(m_fov), aspect, 0.05f, 600.0f);
}

CameraView MapEditor::camera() const {
    CameraView view;
    view.position = m_cameraPosition;
    view.view = viewMatrix();
    view.fovYDegrees = m_fov;
    return view;
}

void MapEditor::mouseRay(glm::vec3& origin, glm::vec3& dir) const {
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 ndc(2.0f * io.MousePos.x / std::max(io.DisplaySize.x, 1.0f) - 1.0f,
                        1.0f - 2.0f * io.MousePos.y / std::max(io.DisplaySize.y, 1.0f));
    const glm::mat4 inv = glm::inverse(projectionMatrix() * viewMatrix());
    glm::vec4 nearPoint = inv * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farPoint = inv * glm::vec4(ndc, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    origin = glm::vec3(nearPoint);
    dir = glm::normalize(glm::vec3(farPoint - nearPoint));
}

void MapEditor::focusSelection() {
    if (m_selection.empty()) {
        return;
    }
    glm::vec3 minP(1e9f), maxP(-1e9f);
    for (const ObjectRef& ref : m_selection) {
        glm::vec3 half(0.6f);
        if (ref.type == ObjectType::Block) half = m_map.blocks[static_cast<size_t>(ref.index)].size * 0.5f;
        const glm::vec3 c = objectCenter(ref);
        minP = glm::min(minP, c - half);
        maxP = glm::max(maxP, c + half);
    }
    const glm::vec3 center = (minP + maxP) * 0.5f;
    const float radius = glm::length(maxP - minP) * 0.5f;
    m_orbitDistance = std::max(3.0f, radius * 2.2f + 2.0f);
    m_focusTarget = center - forward() * m_orbitDistance;
    m_focusTime = 0.25f;
}

void MapEditor::updateCamera(float dt, Input& input, Window& window, const PhysicsWorld& physics) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool overUi = io.WantCaptureMouse && !m_flying && !m_orbiting && !m_panning;
    const bool alt = input.keyDown(GLFW_KEY_LEFT_ALT) || input.keyDown(GLFW_KEY_RIGHT_ALT);
    const glm::vec2 delta = input.mouseDelta();

    // Start/stop modes.
    if (!overUi && input.mousePressed(GLFW_MOUSE_BUTTON_RIGHT)) m_flying = true;
    if (!input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT)) m_flying = false;
    if (!overUi && alt && input.mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
        m_orbiting = true;
        if (auto hit = physics.rayCast(m_cameraPosition, forward(), 300.0f, true)) {
            m_orbitDistance = std::max(1.0f, hit->distance);
        }
    }
    if (!input.mouseDown(GLFW_MOUSE_BUTTON_LEFT)) m_orbiting = false;
    if (!overUi && input.mousePressed(GLFW_MOUSE_BUTTON_MIDDLE)) m_panning = true;
    if (!input.mouseDown(GLFW_MOUSE_BUTTON_MIDDLE)) m_panning = false;

    if (m_flying != window.cursorCaptured()) {
        window.setCursorCaptured(m_flying);
    }

    const glm::vec3 right = glm::normalize(glm::cross(forward(), glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::cross(right, forward());

    if (m_flying) {
        m_yaw -= glm::radians(delta.x * 0.12f);
        m_pitch = std::clamp(m_pitch - glm::radians(delta.y * 0.12f), -kMaxPitch, kMaxPitch);
        if (input.scrollDelta() != 0.0f) {
            m_flySpeed = std::clamp(m_flySpeed * std::pow(1.2f, input.scrollDelta()), 1.0f, 200.0f);
            setStatus(std::format("Velocidad de vuelo: {:.1f} m/s", m_flySpeed));
        }
        glm::vec3 move(0.0f);
        if (input.keyDown(GLFW_KEY_W)) move += forward();
        if (input.keyDown(GLFW_KEY_S)) move -= forward();
        if (input.keyDown(GLFW_KEY_D)) move += right;
        if (input.keyDown(GLFW_KEY_A)) move -= right;
        if (input.keyDown(GLFW_KEY_E) || input.keyDown(GLFW_KEY_SPACE)) move.y += 1.0f;
        if (input.keyDown(GLFW_KEY_Q) || input.keyDown(GLFW_KEY_LEFT_CONTROL)) move.y -= 1.0f;
        if (glm::length(move) > 0.0f) {
            const float boost = input.keyDown(GLFW_KEY_LEFT_SHIFT) ? 3.0f : 1.0f;
            m_cameraPosition += glm::normalize(move) * m_flySpeed * boost * dt;
        }
        m_focusTime = 0.0f;
    } else if (m_orbiting) {
        const glm::vec3 pivot = m_cameraPosition + forward() * m_orbitDistance;
        m_yaw -= delta.x * 0.006f;
        m_pitch = std::clamp(m_pitch - delta.y * 0.006f, -kMaxPitch, kMaxPitch);
        m_cameraPosition = pivot - forward() * m_orbitDistance;
        m_focusTime = 0.0f;
    } else if (m_panning) {
        const float k = std::max(m_orbitDistance, 2.0f) * 0.0018f;
        m_cameraPosition += -right * delta.x * k + up * delta.y * k;
        m_focusTime = 0.0f;
    } else if (!overUi && input.scrollDelta() != 0.0f) {
        // Zoom toward the point under the cursor.
        glm::vec3 origin, dir;
        mouseRay(origin, dir);
        float distance = m_orbitDistance;
        if (auto hit = physics.rayCast(origin, dir, 400.0f, true)) {
            distance = hit->distance;
        }
        const float step = std::clamp(distance * 0.15f, 0.3f, 40.0f) * input.scrollDelta();
        m_cameraPosition += dir * step;
        m_orbitDistance = std::max(1.0f, m_orbitDistance - step);
        m_focusTime = 0.0f;
    }

    if (m_focusTime > 0.0f) {
        const float t = std::min(1.0f, dt / m_focusTime);
        m_cameraPosition = glm::mix(m_cameraPosition, m_focusTarget, t);
        m_focusTime = std::max(0.0f, m_focusTime - dt);
    }
}

// --- Selection ---------------------------------------------------------------------------------

bool MapEditor::validRef(const ObjectRef& ref) const {
    if (ref.index < 0) return false;
    const auto i = static_cast<size_t>(ref.index);
    switch (ref.type) {
    case ObjectType::Block: return i < m_map.blocks.size();
    case ObjectType::Spawn: return i < m_map.spawns.size();
    case ObjectType::Light: return i < m_map.lights.size();
    }
    return false;
}

bool MapEditor::isSelected(const ObjectRef& ref) const {
    return std::find(m_selection.begin(), m_selection.end(), ref) != m_selection.end();
}

void MapEditor::selectOnly(const ObjectRef& ref) { m_selection = {ref}; }

void MapEditor::toggleSelection(const ObjectRef& ref) {
    auto it = std::find(m_selection.begin(), m_selection.end(), ref);
    if (it != m_selection.end()) {
        m_selection.erase(it);
    } else {
        m_selection.push_back(ref);
    }
}

std::optional<MapEditor::ObjectRef> MapEditor::pick(const glm::vec3& origin, const glm::vec3& dir) const {
    float best = 1e9f;
    std::optional<ObjectRef> result;
    for (size_t i = 0; i < m_map.spawns.size(); ++i) {
        const float t = raySphere(origin, dir, m_map.spawns[i].position + glm::vec3(0, 0.9f, 0), 0.7f);
        if (t >= 0.0f && t < best) { best = t; result = ObjectRef{ObjectType::Spawn, static_cast<int>(i)}; }
    }
    for (size_t i = 0; i < m_map.lights.size(); ++i) {
        const float t = raySphere(origin, dir, m_map.lights[i].position, 0.5f);
        if (t >= 0.0f && t < best) { best = t; result = ObjectRef{ObjectType::Light, static_cast<int>(i)}; }
    }
    for (size_t i = 0; i < m_map.blocks.size(); ++i) {
        const float t = rayObb(origin, dir, m_map.blocks[i]);
        if (t >= 0.0f && t < best) { best = t; result = ObjectRef{ObjectType::Block, static_cast<int>(i)}; }
    }
    return result;
}

glm::mat4 MapEditor::objectMatrix(const ObjectRef& ref) const {
    const auto i = static_cast<size_t>(ref.index);
    switch (ref.type) {
    case ObjectType::Block: return World::blockTransform(m_map.blocks[i]);
    case ObjectType::Spawn:
        return glm::translate(glm::mat4(1.0f), m_map.spawns[i].position) *
               glm::rotate(glm::mat4(1.0f), glm::radians(m_map.spawns[i].yawDegrees), glm::vec3(0, 1, 0));
    case ObjectType::Light: return glm::translate(glm::mat4(1.0f), m_map.lights[i].position);
    }
    return glm::mat4(1.0f);
}

void MapEditor::setObjectMatrix(const ObjectRef& ref, const glm::mat4& matrix) {
    const auto i = static_cast<size_t>(ref.index);
    const glm::vec3 position(matrix[3]);
    switch (ref.type) {
    case ObjectType::Block: {
        BlockData& b = m_map.blocks[i];
        glm::vec3 scale(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])), glm::length(glm::vec3(matrix[2])));
        scale = glm::max(scale, glm::vec3(0.02f));
        glm::mat4 rotation(1.0f);
        rotation[0] = glm::vec4(glm::vec3(matrix[0]) / scale.x, 0.0f);
        rotation[1] = glm::vec4(glm::vec3(matrix[1]) / scale.y, 0.0f);
        rotation[2] = glm::vec4(glm::vec3(matrix[2]) / scale.z, 0.0f);
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        glm::extractEulerAngleYXZ(rotation, yaw, pitch, roll);
        b.position = position;
        b.size = glm::round(scale * 1000.0f) / 1000.0f;
        b.rotation = glm::round(glm::degrees(glm::vec3(pitch, yaw, roll)) * 100.0f) / 100.0f;
        break;
    }
    case ObjectType::Spawn: {
        SpawnData& s = m_map.spawns[i];
        const glm::vec3 fwd = -glm::vec3(matrix[2]);
        s.position = position;
        s.yawDegrees = glm::degrees(std::atan2(-fwd.x, -fwd.z));
        break;
    }
    case ObjectType::Light: m_map.lights[i].position = position; break;
    }
}

glm::vec3 MapEditor::objectCenter(const ObjectRef& ref) const {
    const auto i = static_cast<size_t>(ref.index);
    switch (ref.type) {
    case ObjectType::Block: return m_map.blocks[i].position;
    case ObjectType::Spawn: return m_map.spawns[i].position + glm::vec3(0, 0.9f, 0);
    case ObjectType::Light: return m_map.lights[i].position;
    }
    return glm::vec3(0.0f);
}

glm::vec3 MapEditor::selectionCenter() const {
    glm::vec3 sum(0.0f);
    for (const ObjectRef& ref : m_selection) sum += objectCenter(ref);
    return m_selection.empty() ? glm::vec3(0.0f) : sum / static_cast<float>(m_selection.size());
}

// --- Editing -----------------------------------------------------------------------------------

void MapEditor::pushUndo() {
    m_undo.push_back(m_map);
    if (m_undo.size() > kMaxUndo) m_undo.pop_front();
    m_redo.clear();
}

void MapEditor::undo() {
    if (m_undo.empty()) {
        setStatus("Nada que deshacer");
        return;
    }
    m_redo.push_back(m_map);
    m_map = m_undo.back();
    m_undo.pop_back();
    m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(), [&](const ObjectRef& r) { return !validRef(r); }),
                      m_selection.end());
    markDirty();
    setStatus("Deshacer");
}

void MapEditor::redo() {
    if (m_redo.empty()) {
        setStatus("Nada que rehacer");
        return;
    }
    m_undo.push_back(m_map);
    m_map = m_redo.back();
    m_redo.pop_back();
    m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(), [&](const ObjectRef& r) { return !validRef(r); }),
                      m_selection.end());
    markDirty();
    setStatus("Rehacer");
}

float MapEditor::snap(float value) const {
    if (!m_snapEnabled || m_gridStep <= 0.0f) return value;
    return std::round(value / m_gridStep) * m_gridStep;
}

glm::vec3 MapEditor::snapVec(const glm::vec3& v) const { return {snap(v.x), snap(v.y), snap(v.z)}; }

void MapEditor::deleteSelection() {
    if (m_selection.empty()) return;
    pushUndo();
    // Erase from the highest index down so earlier indices stay valid.
    std::vector<ObjectRef> sorted = m_selection;
    std::sort(sorted.begin(), sorted.end(), [](const ObjectRef& a, const ObjectRef& b) { return a.index > b.index; });
    for (const ObjectRef& ref : sorted) {
        if (!validRef(ref)) continue;
        switch (ref.type) {
        case ObjectType::Block: m_map.blocks.erase(m_map.blocks.begin() + ref.index); break;
        case ObjectType::Spawn: m_map.spawns.erase(m_map.spawns.begin() + ref.index); break;
        case ObjectType::Light: m_map.lights.erase(m_map.lights.begin() + ref.index); break;
        }
    }
    setStatus(std::format("Eliminados {} objetos", m_selection.size()));
    clearSelection();
    markDirty();
}

void MapEditor::copySelection() {
    m_clipBlocks.clear();
    m_clipSpawns.clear();
    m_clipLights.clear();
    for (const ObjectRef& ref : m_selection) {
        const auto i = static_cast<size_t>(ref.index);
        switch (ref.type) {
        case ObjectType::Block: m_clipBlocks.push_back(m_map.blocks[i]); break;
        case ObjectType::Spawn: m_clipSpawns.push_back(m_map.spawns[i]); break;
        case ObjectType::Light: m_clipLights.push_back(m_map.lights[i]); break;
        }
    }
    setStatus(std::format("Copiados {} objetos", m_selection.size()));
}

void MapEditor::paste(const glm::vec3& at) {
    if (m_clipBlocks.empty() && m_clipSpawns.empty() && m_clipLights.empty()) return;
    pushUndo();
    // Keep relative layout; the group's lowest-center point lands on the target.
    glm::vec3 center(0.0f);
    float minY = 1e9f;
    int count = 0;
    for (const auto& b : m_clipBlocks) { center += b.position; minY = std::min(minY, b.position.y - b.size.y * 0.5f); ++count; }
    for (const auto& s : m_clipSpawns) { center += s.position; minY = std::min(minY, s.position.y); ++count; }
    for (const auto& l : m_clipLights) { center += l.position; minY = std::min(minY, l.position.y); ++count; }
    center /= static_cast<float>(std::max(count, 1));
    const glm::vec3 offset = snapVec(glm::vec3(at.x - center.x, at.y - minY, at.z - center.z));
    m_selection.clear();
    for (BlockData b : m_clipBlocks) {
        b.position += offset;
        m_map.blocks.push_back(b);
        m_selection.push_back({ObjectType::Block, static_cast<int>(m_map.blocks.size()) - 1});
    }
    for (SpawnData s : m_clipSpawns) {
        s.position += offset;
        m_map.spawns.push_back(s);
        m_selection.push_back({ObjectType::Spawn, static_cast<int>(m_map.spawns.size()) - 1});
    }
    for (LightData l : m_clipLights) {
        l.position += offset;
        m_map.lights.push_back(l);
        m_selection.push_back({ObjectType::Light, static_cast<int>(m_map.lights.size()) - 1});
    }
    markDirty();
    setStatus("Pegado");
}

void MapEditor::duplicateSelection(const glm::vec3& offset) {
    if (m_selection.empty()) return;
    copySelection();
    glm::vec3 minPoint(1e9f);
    for (const ObjectRef& ref : m_selection) minPoint = glm::min(minPoint, objectCenter(ref));
    const glm::vec3 center = selectionCenter();
    float minY = 1e9f;
    for (const ObjectRef& ref : m_selection) {
        const auto i = static_cast<size_t>(ref.index);
        minY = std::min(minY, ref.type == ObjectType::Block ? m_map.blocks[i].position.y - m_map.blocks[i].size.y * 0.5f
                                                          : (ref.type == ObjectType::Spawn ? m_map.spawns[i].position.y
                                                                                          : m_map.lights[i].position.y));
    }
    paste(glm::vec3(center.x, minY, center.z) + offset);
    setStatus("Duplicado");
}

void MapEditor::nudgeSelection(const glm::vec3& delta) {
    if (m_selection.empty()) return;
    pushUndo();
    for (const ObjectRef& ref : m_selection) {
        const auto i = static_cast<size_t>(ref.index);
        switch (ref.type) {
        case ObjectType::Block: m_map.blocks[i].position += delta; break;
        case ObjectType::Spawn: m_map.spawns[i].position += delta; break;
        case ObjectType::Light: m_map.lights[i].position += delta; break;
        }
    }
    markDirty();
}

void MapEditor::applyMaterial(int material) {
    const MaterialPreset& m = kMaterials[std::clamp(material, 0, kMaterialCount - 1)];
    bool any = false;
    for (const ObjectRef& ref : m_selection) {
        if (ref.type != ObjectType::Block) continue;
        if (!any) pushUndo();
        any = true;
        BlockData& b = m_map.blocks[static_cast<size_t>(ref.index)];
        b.color = m.color;
        b.roughness = m.roughness;
        b.metallic = m.metallic;
    }
    if (any) {
        markDirty();
        setStatus(std::format("Material: {}", m.name));
    }
}

// --- Place tool ---------------------------------------------------------------------------------

std::vector<BlockData> MapEditor::prefabBlocks(int prefab, const glm::vec3& anchor, const glm::vec3& normal) const {
    const MaterialPreset& material = kMaterials[m_placeMaterial];
    auto make = [&](glm::vec3 size, glm::vec3 rotation) {
        BlockData b;
        b.size = size;
        b.rotation = rotation;
        b.color = material.color;
        b.roughness = material.roughness;
        b.metallic = material.metallic;
        return b;
    };
    std::vector<BlockData> blocks;
    const float yaw = m_placeYaw;
    switch (prefab) {
    case 0: blocks.push_back(make({2, 2, 2}, {0, yaw, 0})); break;
    case 1: blocks.push_back(make({4, 3, 0.3f}, {0, yaw, 0})); break;
    case 2: blocks.push_back(make({6, 0.3f, 6}, {0, yaw, 0})); break;
    case 3: blocks.push_back(make({0.6f, 4, 0.6f}, {0, yaw, 0})); break;
    case 4: blocks.push_back(make({3, 0.3f, 6}, {20.0f, yaw, 0})); break;
    case 5: {
        // Stairs: 8 steps of 0.25 m rising toward local -Z.
        const glm::quat q = glm::angleAxis(glm::radians(yaw), glm::vec3(0, 1, 0));
        for (int i = 0; i < 8; ++i) {
            const float height = 0.25f * static_cast<float>(i + 1);
            BlockData step = make({2.0f, height, 0.35f}, {0, yaw, 0});
            step.position = anchor + q * glm::vec3(0.0f, height * 0.5f, -0.35f * static_cast<float>(i));
            blocks.push_back(step);
        }
        return blocks;
    }
    case 6: {
        BlockData b = make({1, 1, 1}, {0, yaw, 0});
        b.color = kMaterials[3].color;
        b.roughness = kMaterials[3].roughness;
        b.dynamic = true;
        b.mass = 30.0f;
        blocks.push_back(b);
        break;
    }
    case 7: {
        BlockData b = make({2.4f, 0.1f, 0.4f}, {0, yaw, 0});
        b.color = kMaterials[3].color;
        b.dynamic = true;
        b.mass = 10.0f;
        blocks.push_back(b);
        break;
    }
    default: return blocks;
    }
    BlockData& b = blocks.front();
    const float extent = extentAlong(b.orientation(), b.size * 0.5f, normal);
    b.position = anchor + normal * extent;
    return blocks;
}

void MapEditor::updatePlacement(const PhysicsWorld& physics) {
    glm::vec3 origin, dir;
    mouseRay(origin, dir);
    m_ghostValid = false;
    if (auto hit = physics.rayCast(origin, dir, 400.0f, true)) {
        m_ghostAnchor = hit->position;
        m_ghostNormal = hit->normal;
        m_ghostValid = true;
    } else if (std::abs(dir.y) > 1e-4f) {
        const float t = -origin.y / dir.y;
        if (t > 0.0f && t < 400.0f) {
            m_ghostAnchor = origin + dir * t;
            m_ghostNormal = glm::vec3(0, 1, 0);
            m_ghostValid = true;
        }
    }
    if (m_ghostValid) {
        // Snap within the surface plane, keep the surface height exact.
        const glm::vec3 snapped = snapVec(m_ghostAnchor);
        const glm::vec3 n = glm::normalize(m_ghostNormal);
        m_ghostAnchor = snapped - n * glm::dot(snapped - m_ghostAnchor, n);
    }
}

void MapEditor::placePrefab() {
    if (!m_ghostValid) return;
    pushUndo();
    m_selection.clear();
    if (m_prefab == kPrefabSpawn) {
        m_map.spawns.push_back({m_ghostAnchor + glm::vec3(0, 0.05f, 0), m_placeYaw});
        m_selection.push_back({ObjectType::Spawn, static_cast<int>(m_map.spawns.size()) - 1});
    } else if (m_prefab == kPrefabLight) {
        LightData light;
        light.position = m_ghostAnchor + m_ghostNormal * 0.8f + glm::vec3(0, m_ghostNormal.y > 0.7f ? 1.7f : 0.0f, 0);
        light.color = {1.0f, 0.78f, 0.5f};
        m_map.lights.push_back(light);
        m_selection.push_back({ObjectType::Light, static_cast<int>(m_map.lights.size()) - 1});
    } else {
        for (const BlockData& b : prefabBlocks(m_prefab, m_ghostAnchor, glm::normalize(m_ghostNormal))) {
            m_map.blocks.push_back(b);
            m_selection.push_back({ObjectType::Block, static_cast<int>(m_map.blocks.size()) - 1});
        }
    }
    markDirty();
    setStatus(std::format("Colocado: {}", kPrefabNames[m_prefab]));
}

// --- Update ------------------------------------------------------------------------------------

bool MapEditor::update(float dt, Input& input, Window& window, const PhysicsWorld& physics, glm::ivec2 framebufferSize) {
    m_physics = &physics;
    m_framebufferSize = glm::max(framebufferSize, glm::ivec2(1));
    const ImGuiIO& io = ImGui::GetIO();
    updateCamera(dt, input, window, physics);

    const bool typing = io.WantTextInput;
    const bool ctrl = input.keyDown(GLFW_KEY_LEFT_CONTROL) || input.keyDown(GLFW_KEY_RIGHT_CONTROL);
    const bool shift = input.keyDown(GLFW_KEY_LEFT_SHIFT) || input.keyDown(GLFW_KEY_RIGHT_SHIFT);
    const bool alt = input.keyDown(GLFW_KEY_LEFT_ALT) || input.keyDown(GLFW_KEY_RIGHT_ALT);

    if (m_tool == Tool::Place) {
        updatePlacement(physics);
    }
    if (m_tool == Tool::Sculpt) {
        updateSculpt(dt, input, physics);
    }

    // Clicks in the viewport.
    const bool viewportClick = input.mousePressed(GLFW_MOUSE_BUTTON_LEFT) && !io.WantCaptureMouse && !alt && !m_flying;
    if (viewportClick) {
        if (m_tool == Tool::Sculpt) {
            // Handled by updateSculpt.
        } else if (m_tool == Tool::Place) {
            placePrefab();
            if (!shift && !m_keepPlacing) {
                m_tool = Tool::Move;
            }
        } else if (!m_gizmoHovered) {
            glm::vec3 origin, dir;
            mouseRay(origin, dir);
            if (auto ref = pick(origin, dir)) {
                if (ctrl || shift) toggleSelection(*ref);
                else selectOnly(*ref);
            } else if (!ctrl && !shift) {
                clearSelection();
            }
        }
    }

    // Keyboard shortcuts (not while flying or typing).
    if (!m_flying && !typing && !io.WantCaptureKeyboard) {
        if (ctrl && input.keyPressed(GLFW_KEY_Z)) {
            if (shift) redo(); else undo();
        } else if (ctrl && input.keyPressed(GLFW_KEY_Y)) {
            redo();
        } else if (ctrl && input.keyPressed(GLFW_KEY_C)) {
            copySelection();
        } else if (ctrl && input.keyPressed(GLFW_KEY_V)) {
            glm::vec3 origin, dir;
            mouseRay(origin, dir);
            glm::vec3 at = m_cameraPosition + forward() * 10.0f;
            if (auto hit = physics.rayCast(origin, dir, 400.0f, true)) at = hit->position;
            paste(at);
        } else if (ctrl && input.keyPressed(GLFW_KEY_D)) {
            duplicateSelection(glm::vec3(std::max(m_gridStep, 1.0f) * 2.0f, 0.0f, 0.0f));
        } else if (ctrl && input.keyPressed(GLFW_KEY_A)) {
            m_selection.clear();
            for (size_t i = 0; i < m_map.blocks.size(); ++i) m_selection.push_back({ObjectType::Block, static_cast<int>(i)});
        } else if (!ctrl) {
            if (input.keyPressed(GLFW_KEY_Q)) m_tool = Tool::Select;
            if (input.keyPressed(GLFW_KEY_W)) m_tool = Tool::Move;
            if (input.keyPressed(GLFW_KEY_E)) m_tool = Tool::Rotate;
            if (input.keyPressed(GLFW_KEY_R)) m_tool = Tool::Scale;
            if (input.keyPressed(GLFW_KEY_B) && m_map.terrain.enabled) m_tool = Tool::Sculpt;
            if (input.keyPressed(GLFW_KEY_F)) focusSelection();
            if (input.keyPressed(GLFW_KEY_G)) m_snapEnabled = !m_snapEnabled;
            if (input.keyPressed(GLFW_KEY_L)) m_localSpace = !m_localSpace;
            if (input.keyPressed(GLFW_KEY_T)) m_placeYaw = std::fmod(m_placeYaw + 90.0f, 360.0f);
            if (input.keyPressed(GLFW_KEY_DELETE) || input.keyPressed(GLFW_KEY_BACKSPACE)) deleteSelection();
            for (int k = 0; k < kPrefabCount; ++k) {
                const int key = k == 9 ? GLFW_KEY_0 : GLFW_KEY_1 + k;
                if (input.keyPressed(key)) {
                    m_prefab = k;
                    m_tool = Tool::Place;
                }
            }
            if (input.keyPressed(GLFW_KEY_ESCAPE)) {
                if (m_tool == Tool::Place || m_tool == Tool::Sculpt) m_tool = Tool::Move;
                else clearSelection();
            }
            const float step = m_snapEnabled ? m_gridStep : 0.1f;
            if (input.keyPressed(GLFW_KEY_LEFT)) nudgeSelection({-step, 0, 0});
            if (input.keyPressed(GLFW_KEY_RIGHT)) nudgeSelection({step, 0, 0});
            if (input.keyPressed(GLFW_KEY_UP)) nudgeSelection({0, 0, -step});
            if (input.keyPressed(GLFW_KEY_DOWN)) nudgeSelection({0, 0, step});
            if (input.keyPressed(GLFW_KEY_PAGE_UP)) nudgeSelection({0, step, 0});
            if (input.keyPressed(GLFW_KEY_PAGE_DOWN)) nudgeSelection({0, -step, 0});
        }
    }

    m_statusTime = std::max(0.0f, m_statusTime - dt);
    const bool changed = m_dirty;
    m_dirty = false;
    return changed;
}

// --- UI -----------------------------------------------------------------------------------------

void MapEditor::drawUi() {
    ImGuizmo::BeginFrame();
    drawGizmo();
    drawToolbar();
    drawToolsPanel();
    drawInspector();
    drawHierarchy();
    drawTerrainPanel();
    drawStatusBar();
    if (m_showValidation) {
        drawValidation();
    }
}

void MapEditor::drawGizmo() {
    m_gizmoHovered = false;
    if (m_selection.empty() || m_tool == Tool::Select || m_tool == Tool::Place || !validRef(m_selection.back())) {
        m_gizmoWasUsing = false;
        return;
    }
    const ObjectRef primary = m_selection.back();
    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    if (m_tool == Tool::Rotate) {
        if (primary.type == ObjectType::Light) return;
        operation = primary.type == ObjectType::Spawn ? ImGuizmo::ROTATE_Y : ImGuizmo::ROTATE;
    } else if (m_tool == Tool::Scale) {
        if (primary.type != ObjectType::Block) return;
        operation = ImGuizmo::SCALE;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);

    const glm::mat4 view = viewMatrix();
    const glm::mat4 proj = projectionMatrix();
    const glm::mat4 before = objectMatrix(primary);
    glm::mat4 matrix = before;
    float snapValues[3] = {m_gridStep, m_gridStep, m_gridStep};
    if (m_tool == Tool::Rotate) snapValues[0] = snapValues[1] = snapValues[2] = m_rotateStep;
    if (m_tool == Tool::Scale) snapValues[0] = snapValues[1] = snapValues[2] = 0.25f;
    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), operation,
                                              m_localSpace ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(matrix), nullptr,
                                              m_snapEnabled ? snapValues : nullptr);
    m_gizmoHovered = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    const bool using_ = ImGuizmo::IsUsing();
    if (using_ && !m_gizmoWasUsing) {
        pushUndo();
    }
    m_gizmoWasUsing = using_;
    if (!changed) {
        return;
    }
    const glm::mat4 delta = matrix * glm::inverse(before);
    if (m_tool != Tool::Scale) {
        for (const ObjectRef& ref : m_selection) {
            if (!(ref == primary) && validRef(ref)) {
                setObjectMatrix(ref, delta * objectMatrix(ref));
            }
        }
    }
    setObjectMatrix(primary, matrix);
    markDirty();
}

void MapEditor::drawToolbar() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 8.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::Begin("##toolbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
    auto toolButton = [&](const char* label, Tool tool) {
        const bool active = m_tool == tool;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) m_tool = tool;
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    toolButton("Seleccionar (Q)", Tool::Select);
    toolButton("Mover (W)", Tool::Move);
    toolButton("Rotar (E)", Tool::Rotate);
    toolButton("Escalar (R)", Tool::Scale);
    toolButton("Colocar (1-0)", Tool::Place);
    if (m_map.terrain.enabled) toolButton("Esculpir (B)", Tool::Sculpt);
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button(m_localSpace ? "Local (L)" : "Global (L)")) m_localSpace = !m_localSpace;
    ImGui::SameLine();
    ImGui::Checkbox("Ajuste (G)", &m_snapEnabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::DragFloat("##grid", &m_gridStep, 0.05f, 0.05f, 10.0f, "%.2f m");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::BeginDisabled(m_undo.empty());
    if (ImGui::Button("Deshacer")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_redo.empty());
    if (ImGui::Button("Rehacer")) redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.2f, 1.0f));
    if (ImGui::Button("Probar mapa (F1)")) m_playRequested = true;
    ImGui::PopStyleColor();
    ImGui::End();
}

void MapEditor::drawToolsPanel() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(8, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, io.DisplaySize.y - 110), ImGuiCond_FirstUseEver);
    ImGui::Begin("Herramientas");

    if (ImGui::CollapsingHeader("Archivo", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::InputText("Nombre", &m_map.name)) markDirty();
        ImGui::InputText("Archivo", &m_fileName);
        if (ImGui::Button("Guardar")) {
            std::string clean = m_fileName;
            std::erase_if(clean, [](char c) { return c == '/' || c == '\\' || c == '.' || c == ' '; });
            if (clean.empty()) clean = "mapa";
            m_fileName = clean;
            const bool ok = m_map.save(fs::resolve("maps") / (clean + ".json"));
            setStatus(ok ? std::format("Guardado: maps/{}.json", clean) : "Error al guardar el mapa");
            refreshMapList();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::BeginCombo("##load", "Cargar...")) {
            for (const std::string& file : m_mapFiles) {
                if (ImGui::Selectable(file.c_str())) {
                    if (auto loaded = MapData::load(fs::resolve("maps") / (file + ".json"))) {
                        pushUndo();
                        m_map = *loaded;
                        m_fileName = file;
                        clearSelection();
                        markDirty();
                        setStatus(std::format("Cargado: {}", file));
                    } else {
                        setStatus("Error al cargar el mapa");
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Nuevo")) {
            pushUndo();
            m_map = MapData::makeDefault();
            m_fileName = "nuevo_mapa";
            clearSelection();
            markDirty();
            setStatus("Mapa nuevo (Ctrl+Z para volver)");
        }
        if (ImGui::Button("Comprobar mapa", ImVec2(-1, 0))) m_showValidation = !m_showValidation;
    }

    if (ImGui::CollapsingHeader("Objetos", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Clic en el mundo para colocar. Shift: seguir colocando.");
        ImGui::TextDisabled("T: girar 90°.  Esc: cancelar.");
        for (int i = 0; i < kPrefabCount; ++i) {
            const bool active = m_tool == Tool::Place && m_prefab == i;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            const std::string label = std::format("{} {}", i == 9 ? 0 : i + 1, kPrefabNames[i]);
            if (ImGui::Button(label.c_str(), ImVec2(136, 0))) {
                m_prefab = i;
                m_tool = Tool::Place;
            }
            if (active) ImGui::PopStyleColor();
            if (i % 2 == 0) ImGui::SameLine();
        }
        ImGui::Checkbox("Colocar varios", &m_keepPlacing);
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Giro", &m_placeYaw, 0.0f, 359.0f, "%.0f°");
    }

    if (ImGui::CollapsingHeader("Materiales", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Clic: aplicar a la selección y usar al colocar.");
        for (int i = 0; i < kMaterialCount; ++i) {
            const MaterialPreset& m = kMaterials[i];
            ImGui::PushID(i);
            const ImVec4 color(m.color.r, m.color.g, m.color.b, 1.0f);
            if (ImGui::ColorButton("##swatch", color, ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18))) {
                m_placeMaterial = i;
                applyMaterial(i);
            }
            ImGui::SameLine();
            const bool current = m_placeMaterial == i;
            if (ImGui::Selectable(m.name, current, 0, ImVec2(100, 0))) {
                m_placeMaterial = i;
                applyMaterial(i);
            }
            ImGui::PopID();
            if (i % 2 == 0) ImGui::SameLine();
        }
    }

    if (ImGui::CollapsingHeader("Entorno")) {
        for (int i = 0; i < 4; ++i) {
            const EnvironmentPreset preset = environmentPreset(i);
            if (ImGui::Button(preset.name)) {
                pushUndo();
                m_map.environment = preset.data;
                markDirty();
            }
            if (i % 2 == 0) ImGui::SameLine();
        }
        EnvironmentData& e = m_map.environment;
        bool changed = false;
        changed |= ImGui::DragFloat3("Dirección del sol", &e.sunDirection.x, 0.01f, -1.0f, 1.0f);
        changed |= ImGui::ColorEdit3("Color del sol", &e.sunColor.x);
        changed |= ImGui::SliderFloat("Intensidad del sol", &e.sunIntensity, 0.0f, 10.0f);
        changed |= ImGui::ColorEdit3("Cielo", &e.skyColor.x);
        changed |= ImGui::ColorEdit3("Suelo (ambiente)", &e.groundColor.x);
        changed |= ImGui::SliderFloat("Luz ambiente", &e.ambientIntensity, 0.0f, 2.0f);
        changed |= ImGui::ColorEdit3("Niebla", &e.fogColor.x);
        changed |= ImGui::SliderFloat("Densidad de niebla", &e.fogDensity, 0.0f, 0.05f, "%.4f");
        if (changed) markDirty();
        ImGui::TextDisabled("La niebla es igual para todos los jugadores.");
    }
    ImGui::End();
}

void MapEditor::drawInspector() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 348, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 330), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");
    if (m_selection.empty()) {
        ImGui::TextDisabled("Nada seleccionado");
        ImGui::TextDisabled("Clic: seleccionar. Ctrl/Shift+clic: añadir.");
        ImGui::End();
        return;
    }
    // Snapshot for undo when any widget starts an edit.
    auto tracked = [&](bool changed) {
        if (ImGui::IsItemActivated()) pushUndo();
        if (changed) markDirty();
    };

    if (m_selection.size() > 1) {
        ImGui::Text("%d objetos seleccionados", static_cast<int>(m_selection.size()));
        bool anyBlock = false;
        bool allDynamic = true;
        for (const ObjectRef& ref : m_selection) {
            if (ref.type == ObjectType::Block) {
                anyBlock = true;
                allDynamic &= m_map.blocks[static_cast<size_t>(ref.index)].dynamic;
            }
        }
        if (anyBlock) {
            bool dynamic = allDynamic;
            if (ImGui::Checkbox("Física dinámica", &dynamic)) {
                pushUndo();
                for (const ObjectRef& ref : m_selection) {
                    if (ref.type == ObjectType::Block) m_map.blocks[static_cast<size_t>(ref.index)].dynamic = dynamic;
                }
                markDirty();
            }
        }
    } else {
        const ObjectRef ref = m_selection.back();
        const auto i = static_cast<size_t>(ref.index);
        const float dragStep = m_snapEnabled ? m_gridStep : 0.05f;
        if (ref.type == ObjectType::Block) {
            BlockData& b = m_map.blocks[i];
            ImGui::Text("Bloque #%d", ref.index);
            tracked(ImGui::DragFloat3("Posición", &b.position.x, dragStep));
            tracked(ImGui::DragFloat3("Tamaño", &b.size.x, dragStep, 0.05f, 500.0f));
            tracked(ImGui::DragFloat3("Rotación", &b.rotation.x, 1.0f, -180.0f, 180.0f, "%.1f°"));
            tracked(ImGui::ColorEdit3("Color", &b.color.x));
            tracked(ImGui::SliderFloat("Rugosidad", &b.roughness, 0.05f, 1.0f));
            tracked(ImGui::SliderFloat("Metálico", &b.metallic, 0.0f, 1.0f));
            tracked(ImGui::Checkbox("Física dinámica", &b.dynamic));
            if (b.dynamic) {
                tracked(ImGui::DragFloat("Masa", &b.mass, 1.0f, 1.0f, 2000.0f, "%.0f kg"));
                ImGui::TextDisabled("Los objetos dinámicos caen y se pueden empujar o disparar.");
            }
        } else if (ref.type == ObjectType::Spawn) {
            SpawnData& s = m_map.spawns[i];
            ImGui::Text("Punto de aparición #%d", ref.index);
            tracked(ImGui::DragFloat3("Posición", &s.position.x, dragStep));
            tracked(ImGui::SliderFloat("Orientación", &s.yawDegrees, -180.0f, 180.0f, "%.0f°"));
        } else {
            LightData& l = m_map.lights[i];
            ImGui::Text("Luz #%d", ref.index);
            tracked(ImGui::DragFloat3("Posición", &l.position.x, dragStep));
            tracked(ImGui::ColorEdit3("Color", &l.color.x));
            tracked(ImGui::SliderFloat("Intensidad", &l.intensity, 0.0f, 50.0f));
            tracked(ImGui::SliderFloat("Radio", &l.radius, 0.5f, 60.0f, "%.1f m"));
        }
    }
    ImGui::Separator();
    if (ImGui::Button("Duplicar (Ctrl+D)")) duplicateSelection(glm::vec3(std::max(m_gridStep, 1.0f) * 2.0f, 0.0f, 0.0f));
    ImGui::SameLine();
    if (ImGui::Button("Eliminar (Supr)")) deleteSelection();
    ImGui::SameLine();
    if (ImGui::Button("Enfocar (F)")) focusSelection();
    ImGui::End();
}

void MapEditor::drawHierarchy() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 348, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, io.DisplaySize.y - 450), ImGuiCond_FirstUseEver);
    ImGui::Begin("Jerarquía");
    ImGui::InputTextWithHint("##filter", "Filtrar...", &m_hierarchyFilter);
    ImGui::BeginChild("##list");
    const bool ctrl = io.KeyCtrl || io.KeyShift;
    auto entry = [&](ObjectType type, int index, const std::string& label) {
        if (!m_hierarchyFilter.empty() && label.find(m_hierarchyFilter) == std::string::npos) return;
        const ObjectRef ref{type, index};
        if (ImGui::Selectable(label.c_str(), isSelected(ref))) {
            if (ctrl) toggleSelection(ref);
            else selectOnly(ref);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            selectOnly(ref);
            focusSelection();
        }
    };
    if (ImGui::TreeNodeEx("Bloques", ImGuiTreeNodeFlags_DefaultOpen, "Bloques (%d)", static_cast<int>(m_map.blocks.size()))) {
        for (size_t i = 0; i < m_map.blocks.size(); ++i) {
            const BlockData& b = m_map.blocks[i];
            entry(ObjectType::Block, static_cast<int>(i),
                  std::format("{} {}  {:.1f}x{:.1f}x{:.1f}##b{}", b.dynamic ? "Físico" : "Bloque", i, b.size.x, b.size.y, b.size.z, i));
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Spawns", ImGuiTreeNodeFlags_DefaultOpen, "Spawns (%d)", static_cast<int>(m_map.spawns.size()))) {
        for (size_t i = 0; i < m_map.spawns.size(); ++i) entry(ObjectType::Spawn, static_cast<int>(i), std::format("Spawn {}##s{}", i, i));
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Luces", ImGuiTreeNodeFlags_DefaultOpen, "Luces (%d)", static_cast<int>(m_map.lights.size()))) {
        for (size_t i = 0; i < m_map.lights.size(); ++i) entry(ObjectType::Light, static_cast<int>(i), std::format("Luz {}##l{}", i, i));
        ImGui::TreePop();
    }
    ImGui::EndChild();
    ImGui::End();
}

void MapEditor::drawStatusBar() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 0));
    ImGui::Begin("##status", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    const char* tools[] = {"Seleccionar", "Mover", "Rotar", "Escalar", "Colocar", "Esculpir"};
    ImGui::Text("Herramienta: %s", tools[static_cast<int>(m_tool)]);
    if (m_tool == Tool::Place) {
        ImGui::SameLine();
        ImGui::Text("(%s, %s)", kPrefabNames[m_prefab], kMaterials[m_placeMaterial].name);
    }
    ImGui::SameLine(0, 24);
    ImGui::Text("Selección: %d", static_cast<int>(m_selection.size()));
    ImGui::SameLine(0, 24);
    ImGui::Text("Ajuste: %s %.2f m", m_snapEnabled ? "sí" : "no", m_gridStep);
    ImGui::SameLine(0, 24);
    ImGui::Text("Vuelo: %.0f m/s", m_flySpeed);
    ImGui::SameLine(0, 24);
    ImGui::TextDisabled("Clic der.+WASD: volar | Alt+clic: orbitar | Rueda: zoom | Clic central: desplazar | F: enfocar | Ctrl+Z/Y | Ctrl+C/V");
    if (m_statusTime > 0.0f) {
        ImGui::SameLine(0, 24);
        ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.55f, std::min(1.0f, m_statusTime)), "%s", m_status.c_str());
    }
    ImGui::End();
}

void MapEditor::drawValidation() {
    ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Comprobación del mapa", &m_showValidation)) {
        ImGui::End();
        return;
    }
    std::vector<std::string> issues;
    if (m_map.spawns.size() < 10) {
        issues.push_back(std::format("Faltan spawns: {}/10 para una partida de 10 jugadores", m_map.spawns.size()));
    }
    for (size_t i = 0; i < m_map.spawns.size(); ++i) {
        const SpawnData& s = m_map.spawns[i];
        for (const BlockData& b : m_map.blocks) {
            if (!b.dynamic && (pointInBlock(s.position + glm::vec3(0, 0.9f, 0), b) || pointInBlock(s.position + glm::vec3(0, 1.6f, 0), b))) {
                issues.push_back(std::format("Spawn {} está dentro de un bloque", i));
                break;
            }
        }
        if (m_physics && !m_physics->rayCast(s.position + glm::vec3(0, 0.5f, 0), glm::vec3(0, -1, 0), 3.0f)) {
            issues.push_back(std::format("Spawn {} no tiene suelo debajo", i));
        }
        for (size_t j = i + 1; j < m_map.spawns.size(); ++j) {
            if (glm::distance(s.position, m_map.spawns[j].position) < 2.5f) {
                issues.push_back(std::format("Spawns {} y {} están demasiado cerca", i, j));
            }
        }
    }
    if (m_map.blocks.empty()) issues.push_back("El mapa no tiene geometría");
    if (issues.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.95f, 0.5f, 1.0f), "Sin problemas. El mapa está listo para jugar.");
    }
    for (const std::string& issue : issues) {
        ImGui::BulletText("%s", issue.c_str());
    }
    ImGui::End();
}

void MapEditor::updateSculpt(float dt, Input& input, const PhysicsWorld& physics) {
    const ImGuiIO& io = ImGui::GetIO();
    glm::vec3 origin, dir;
    mouseRay(origin, dir);
    m_brushValid = false;
    if (auto hit = physics.rayCast(origin, dir, 500.0f)) {
        m_brushCenter = hit->position;
        m_brushValid = true;
    }
    const bool alt = input.keyDown(GLFW_KEY_LEFT_ALT) || input.keyDown(GLFW_KEY_RIGHT_ALT);
    const bool down = input.mouseDown(GLFW_MOUSE_BUTTON_LEFT) && !alt && !m_flying;
    if (input.mousePressed(GLFW_MOUSE_BUTTON_LEFT) && !io.WantCaptureMouse && !alt && m_brushValid) {
        pushUndo();
        m_sculpting = true;
        m_flattenHeight = m_brushCenter.y;
    }
    if (!down) {
        m_sculpting = false;
    }
    if (m_sculpting && m_brushValid) {
        const auto brush = static_cast<TerrainData::Brush>(m_brush);
        // Shift inverts raise/lower.
        TerrainData::Brush effective = brush;
        if (input.keyDown(GLFW_KEY_LEFT_SHIFT)) {
            if (brush == TerrainData::Brush::Raise) effective = TerrainData::Brush::Lower;
            else if (brush == TerrainData::Brush::Lower) effective = TerrainData::Brush::Raise;
            else if (brush == TerrainData::Brush::Paint) effective = TerrainData::Brush::Erase;
        }
        if (m_map.terrain.applyBrush(effective, m_brushCenter, m_brushRadius, m_brushStrength, dt, m_flattenHeight)) {
            markDirty();
        }
    }
    if (!m_flying && !io.WantCaptureMouse && input.keyDown(GLFW_KEY_LEFT_CONTROL) && input.scrollDelta() != 0.0f) {
        m_brushRadius = std::clamp(m_brushRadius * std::pow(1.15f, input.scrollDelta()), 0.5f, 60.0f);
    }
}

void MapEditor::drawTerrainPanel() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(320, io.DisplaySize.y - 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 250), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    ImGui::Begin("Terreno");
    TerrainData& t = m_map.terrain;
    bool enabled = t.enabled;
    if (ImGui::Checkbox("Usar terreno", &enabled)) {
        pushUndo();
        t.enabled = enabled;
        if (enabled && !t.valid()) t.generate();
        markDirty();
    }
    if (t.enabled) {
        ImGui::SeparatorText("Generador");
        ImGui::DragFloat("Tamaño", &t.size, 1.0f, 32.0f, 1024.0f, "%.0f m");
        const char* resolutions[] = {"128", "256", "512"};
        int resIndex = t.resolution >= 512 ? 2 : (t.resolution >= 256 ? 1 : 0);
        if (ImGui::Combo("Resolución", &resIndex, resolutions, 3)) t.resolution = 128 << resIndex;
        int seed = static_cast<int>(t.seed);
        if (ImGui::InputInt("Semilla", &seed)) t.seed = static_cast<uint32_t>(std::max(seed, 0));
        ImGui::SliderFloat("Amplitud", &t.amplitude, 0.0f, 60.0f, "%.1f m");
        ImGui::SliderFloat("Frecuencia", &t.frequency, 0.001f, 0.05f, "%.4f");
        if (ImGui::Button("Generar terreno", ImVec2(-1, 0))) {
            pushUndo();
            t.heights.clear();
            t.paint.clear();
            t.generate();
            markDirty();
        }
        ImGui::SeparatorText("Agua");
        if (ImGui::Checkbox("Agua", &t.waterEnabled)) markDirty();
        if (t.waterEnabled) {
            if (ImGui::IsItemActivated()) pushUndo();
            if (ImGui::DragFloat("Nivel del agua", &t.waterLevel, 0.05f, -100.0f, 100.0f, "%.2f m")) markDirty();
        }
        ImGui::SeparatorText("Pincel (B)");
        const char* brushes[] = {"Subir", "Bajar", "Suavizar", "Aplanar", "Pintar camino", "Borrar camino"};
        for (int i = 0; i < 6; ++i) {
            if (ImGui::RadioButton(brushes[i], m_brush == i)) {
                m_brush = i;
                m_tool = Tool::Sculpt;
            }
            if (i % 3 != 2) ImGui::SameLine();
        }
        ImGui::SliderFloat("Radio", &m_brushRadius, 0.5f, 60.0f, "%.1f m");
        ImGui::SliderFloat("Fuerza", &m_brushStrength, 0.05f, 5.0f, "%.2f");
        ImGui::TextDisabled("Shift: invertir. Ctrl+rueda: radio.");
    }
    ImGui::End();
}

void MapEditor::refreshMapList() {
    m_mapFiles.clear();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(fs::resolve("maps"), ec)) {
        if (entry.path().extension() == ".json") {
            m_mapFiles.push_back(entry.path().stem().string());
        }
    }
    std::sort(m_mapFiles.begin(), m_mapFiles.end());
}

// --- Scene overlay ----------------------------------------------------------------------------

void MapEditor::submit(Renderer& renderer) const {
    // Ground grid around the view target.
    const glm::vec3 target = m_cameraPosition + forward() * std::min(m_orbitDistance, 40.0f);
    const float extent = 60.0f;
    const float cx = std::round(target.x);
    const float cz = std::round(target.z);
    const float gridY = 0.01f;
    for (int i = -60; i <= 60; ++i) {
        const float x = cx + static_cast<float>(i);
        const float z = cz + static_cast<float>(i);
        const bool major = (static_cast<int>(x) % 5) == 0;
        const bool majorZ = (static_cast<int>(z) % 5) == 0;
        const glm::vec4 c1(0.0f, 0.0f, 0.0f, major ? 0.35f : 0.12f);
        const glm::vec4 c2(0.0f, 0.0f, 0.0f, majorZ ? 0.35f : 0.12f);
        renderer.addLine({x, gridY, cz - extent}, {x, gridY, cz + extent}, std::abs(x) < 0.01f ? glm::vec4(0.2f, 0.3f, 1.0f, 0.9f) : c1);
        renderer.addLine({cx - extent, gridY, z}, {cx + extent, gridY, z}, std::abs(z) < 0.01f ? glm::vec4(1.0f, 0.25f, 0.2f, 0.9f) : c2);
    }

    // Spawns and lights.
    for (size_t i = 0; i < m_map.spawns.size(); ++i) {
        const SpawnData& s = m_map.spawns[i];
        const bool selected = isSelected({ObjectType::Spawn, static_cast<int>(i)});
        const glm::vec3 color = selected ? glm::vec3(2.0f, 1.6f, 0.2f) : glm::vec3(0.2f, 1.2f, 0.3f);
        DrawItem body;
        body.mesh = &renderer.capsule();
        body.model = glm::translate(glm::mat4(1.0f), s.position + glm::vec3(0, 0.9f, 0));
        body.material.albedo = glm::vec3(0.0f);
        body.material.emissive = color * 0.5f;
        body.castShadow = false;
        renderer.submit(body);
        const float yaw = glm::radians(s.yawDegrees);
        const glm::vec3 fwd(-std::sin(yaw), 0.0f, -std::cos(yaw));
        const glm::vec3 base = s.position + glm::vec3(0, 1.5f, 0);
        renderer.addLine(base, base + fwd * 1.5f, glm::vec4(color, 1.0f), true);
        renderer.addLine(base + fwd * 1.5f, base + fwd * 1.1f + glm::vec3(fwd.z, 0, -fwd.x) * 0.3f, glm::vec4(color, 1.0f), true);
        renderer.addLine(base + fwd * 1.5f, base + fwd * 1.1f - glm::vec3(fwd.z, 0, -fwd.x) * 0.3f, glm::vec4(color, 1.0f), true);
    }
    for (size_t i = 0; i < m_map.lights.size(); ++i) {
        const LightData& l = m_map.lights[i];
        const bool selected = isSelected({ObjectType::Light, static_cast<int>(i)});
        DrawItem bulb;
        bulb.mesh = &renderer.sphere();
        bulb.model = glm::translate(glm::mat4(1.0f), l.position) * glm::scale(glm::mat4(1.0f), glm::vec3(selected ? 0.5f : 0.35f));
        bulb.material.albedo = glm::vec3(0.0f);
        bulb.material.emissive = selected ? glm::vec3(2.0f, 1.6f, 0.2f) : World::toLinear(l.color) * 3.0f;
        bulb.castShadow = false;
        renderer.submit(bulb);
        if (selected) {
            circleLines(renderer, l.position, l.radius, glm::vec4(1.0f, 0.85f, 0.3f, 0.6f));
        }
    }

    // Selection outlines.
    for (const ObjectRef& ref : m_selection) {
        if (ref.type == ObjectType::Block && validRef(ref)) {
            const BlockData& b = m_map.blocks[static_cast<size_t>(ref.index)];
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), b.position) * glm::mat4_cast(b.orientation()) *
                                    glm::scale(glm::mat4(1.0f), b.size + glm::vec3(0.02f));
            boxLines(renderer, model, glm::vec4(1.0f, 0.75f, 0.1f, 1.0f), true);
        }
    }
    // Dynamic blocks get a blue tint outline so they are easy to spot.
    for (const BlockData& b : m_map.blocks) {
        if (b.dynamic) {
            const glm::mat4 model = World::blockTransform(b);
            boxLines(renderer, model, glm::vec4(0.3f, 0.6f, 1.0f, 0.6f), false);
        }
    }

    // Sculpt brush outline following the ground.
    if (m_tool == Tool::Sculpt && m_brushValid) {
        const int segments = 64;
        for (int i = 0; i < segments; ++i) {
            const float a0 = glm::two_pi<float>() * static_cast<float>(i) / segments;
            const float a1 = glm::two_pi<float>() * static_cast<float>(i + 1) / segments;
            glm::vec3 p0 = m_brushCenter + glm::vec3(std::cos(a0), 0, std::sin(a0)) * m_brushRadius;
            glm::vec3 p1 = m_brushCenter + glm::vec3(std::cos(a1), 0, std::sin(a1)) * m_brushRadius;
            p0.y = m_map.terrain.heightAt(p0.x, p0.z) + 0.1f;
            p1.y = m_map.terrain.heightAt(p1.x, p1.z) + 0.1f;
            renderer.addLine(p0, p1, glm::vec4(1.0f, 0.9f, 0.3f, 1.0f), true);
        }
        renderer.addLine(m_brushCenter, m_brushCenter + glm::vec3(0, 1.5f, 0), glm::vec4(1.0f, 0.9f, 0.3f, 1.0f), true);
    }

    // Placement ghost.
    if (m_tool == Tool::Place && m_ghostValid) {
        const glm::vec4 ghost(0.3f, 1.0f, 0.4f, 1.0f);
        if (m_prefab == kPrefabSpawn) {
            DrawItem item;
            item.mesh = &renderer.capsule();
            item.model = glm::translate(glm::mat4(1.0f), m_ghostAnchor + glm::vec3(0, 0.95f, 0));
            item.material.albedo = glm::vec3(0.0f);
            item.material.emissive = glm::vec3(0.2f, 0.9f, 0.3f);
            item.castShadow = false;
            renderer.submit(item);
        } else if (m_prefab == kPrefabLight) {
            boxLines(renderer, glm::translate(glm::mat4(1.0f), m_ghostAnchor + glm::vec3(0, 1.7f, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.4f)),
                     ghost, true);
        } else {
            for (const BlockData& b : prefabBlocks(m_prefab, m_ghostAnchor, glm::normalize(m_ghostNormal))) {
                boxLines(renderer, World::blockTransform(b), ghost, true);
            }
        }
        renderer.addLine(m_ghostAnchor, m_ghostAnchor + m_ghostNormal * 0.6f, glm::vec4(1, 1, 1, 0.8f), true);
    }
}

} // namespace origon
