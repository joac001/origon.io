#pragma once

#include "render/Renderer.h"
#include "world/MapData.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace origon {

class Input;
class PhysicsWorld;
class Window;

// In-game map builder. Edits MapData directly; the App rebuilds the world when the map changes.
// All panel text is Spanish.
//
// Camera: right mouse + WASD/QE flies (wheel changes speed), Alt + left mouse orbits,
// middle mouse pans, wheel zooms toward the cursor, F frames the selection.
class MapEditor {
public:
    enum class ObjectType { Block, Spawn, Light };
    enum class Tool { Select, Move, Rotate, Scale, Place };

    struct ObjectRef {
        ObjectType type;
        int index;
        bool operator==(const ObjectRef&) const = default;
    };

    void open(const MapData& map, const std::string& fileName, const glm::vec3& cameraPosition, float yaw, float pitch,
              bool keepCamera);
    // Returns true when the map changed this frame.
    bool update(float dt, Input& input, Window& window, const PhysicsWorld& physics, glm::ivec2 framebufferSize);
    void drawUi();
    void submit(Renderer& renderer) const;

    MapData& map() { return m_map; }
    const std::string& fileName() const { return m_fileName; }
    void setFov(float fov) { m_fov = fov; }
    CameraView camera() const;
    bool consumePlayRequest();

private:
    struct Prefab {
        const char* name;
        const char* key;
    };

    // Camera.
    glm::vec3 forward() const;
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix() const;
    void mouseRay(glm::vec3& origin, glm::vec3& dir) const;
    void updateCamera(float dt, Input& input, Window& window, const PhysicsWorld& physics);
    void focusSelection();

    // Selection and picking.
    std::optional<ObjectRef> pick(const glm::vec3& origin, const glm::vec3& dir) const;
    bool isSelected(const ObjectRef& ref) const;
    void selectOnly(const ObjectRef& ref);
    void toggleSelection(const ObjectRef& ref);
    void clearSelection() { m_selection.clear(); }
    bool validRef(const ObjectRef& ref) const;
    glm::mat4 objectMatrix(const ObjectRef& ref) const;
    void setObjectMatrix(const ObjectRef& ref, const glm::mat4& matrix);
    glm::vec3 objectCenter(const ObjectRef& ref) const;
    glm::vec3 selectionCenter() const;

    // Editing.
    void pushUndo();
    void undo();
    void redo();
    void markDirty() { m_dirty = true; }
    void deleteSelection();
    void duplicateSelection(const glm::vec3& offset);
    void copySelection();
    void paste(const glm::vec3& at);
    void nudgeSelection(const glm::vec3& delta);
    void applyMaterial(int material);
    float snap(float value) const;
    glm::vec3 snapVec(const glm::vec3& v) const;

    // Place tool.
    void updatePlacement(const PhysicsWorld& physics);
    std::vector<BlockData> prefabBlocks(int prefab, const glm::vec3& anchor, const glm::vec3& normal) const;
    void placePrefab();

    // UI.
    void drawToolbar();
    void drawToolsPanel();
    void drawInspector();
    void drawHierarchy();
    void drawStatusBar();
    void drawGizmo();
    void drawValidation();
    void refreshMapList();
    void setStatus(const std::string& text);

    const PhysicsWorld* m_physics = nullptr;
    MapData m_map;
    std::string m_fileName = "graybox";
    bool m_cameraInitialized = false;

    // Camera state.
    glm::vec3 m_cameraPosition{0.0f, 12.0f, 25.0f};
    float m_yaw = 0.0f;
    float m_pitch = -0.45f;
    float m_orbitDistance = 15.0f;
    float m_flySpeed = 12.0f;
    float m_fov = 75.0f;
    glm::ivec2 m_framebufferSize{1};
    bool m_flying = false;
    bool m_orbiting = false;
    bool m_panning = false;
    glm::vec3 m_focusTarget{0.0f};
    float m_focusTime = 0.0f;

    // Selection, tools, gizmo.
    std::vector<ObjectRef> m_selection;
    Tool m_tool = Tool::Move;
    bool m_localSpace = false;
    bool m_snapEnabled = true;
    float m_gridStep = 0.5f;
    float m_rotateStep = 15.0f;
    bool m_gizmoWasUsing = false;
    bool m_gizmoHovered = false;

    // Place tool.
    int m_prefab = 0;
    int m_placeMaterial = 0;
    float m_placeYaw = 0.0f;
    bool m_ghostValid = false;
    glm::vec3 m_ghostAnchor{0.0f};
    glm::vec3 m_ghostNormal{0.0f, 1.0f, 0.0f};
    bool m_placeRequested = false;
    bool m_keepPlacing = false;

    // History and clipboard.
    std::deque<MapData> m_undo;
    std::deque<MapData> m_redo;
    std::vector<BlockData> m_clipBlocks;
    std::vector<SpawnData> m_clipSpawns;
    std::vector<LightData> m_clipLights;

    bool m_dirty = false;
    bool m_playRequested = false;
    bool m_showValidation = false;
    std::string m_hierarchyFilter;
    std::vector<std::string> m_mapFiles;
    std::string m_status;
    float m_statusTime = 0.0f;
};

} // namespace origon
