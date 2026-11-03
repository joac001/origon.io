#pragma once

#include "audio/GameAudio.h"
#include "core/Input.h"
#include "core/Window.h"
#include "editor/MapEditor.h"
#include "fx/Effects.h"
#include "physics/PhysicsWorld.h"
#include "render/Renderer.h"
#include "render/Texture.h"
#include "world/Vegetation.h"
#include "sim/Simulation.h"
#include "sim/Weapons.h"
#include "ui/Hud.h"
#include "ui/Settings.h"
#include "world/World.h"

#include <memory>
#include <string>
#include <vector>

struct ImFont;

namespace origon {

// Command line options (mostly for automated captures and testing).
struct LaunchOptions {
    std::string map = "graybox";
    std::string screenshotPath; // When set, save a screenshot at screenshotFrame and exit.
    int screenshotFrame = 300;
    bool startInEditor = false;
    bool autoplay = false; // The local player is driven by the bot AI (spectator/demo).
    bool hideUi = false;
    // Photo mode: fixed camera (position, yaw/pitch degrees) and offscreen capture size.
    bool fixedCamera = false;
    glm::vec3 cameraPosition{0.0f};
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;
    glm::ivec2 shotSize{0};
};

class App {
public:
    explicit App(const LaunchOptions& options);
    ~App();
    int run();

private:
    enum class Mode { Playing, Editor };

    void initImGui();
    void shutdownImGui();
    void loadMap(const std::string& fileName);
    void buildWorld(const MapData& map);
    void restartMatch();
    void enterEditor();
    void leaveEditor();
    void setMenuOpen(bool open);

    void updatePlaying(float frameDt);
    PlayerCommand buildLocalCommand() const;
    void processEvents(const std::vector<GameEvent>& events);

    CameraView playingCamera(float alpha) const;
    glm::vec3 localMuzzle() const;
    void updateCameraFeel(float frameDt);
    void submitPlayers(float alpha, bool includeLocal);
    void submitViewModel(float frameDt);
    void drawMenu();
    void drawStats(float frameDt);
    void takeScreenshot(const std::string& path);

    LaunchOptions m_options;
    std::unique_ptr<Window> m_window;
    Input m_input;
    Renderer m_renderer;
    TextureCache m_textures;
    ModelLibrary m_models{&m_textures};
    PhysicsWorld m_physics;
    WeaponDatabase m_weapons;
    std::unique_ptr<Simulation> m_sim;
    World m_world;
    Hud m_hud;
    MapEditor m_editor;
    GameSettings m_settings;
    GameAudio m_audio;
    Effects m_effects;
    bool m_audioReady = false;

    Mode m_mode = Mode::Playing;
    bool m_menuOpen = false;
    std::string m_mapFile = "graybox";
    std::vector<std::string> m_mapFiles;

    float m_viewYaw = 0.0f;
    float m_viewPitch = 0.0f;
    float m_tickAccumulator = 0.0f;
    float m_adsBlend = 0.0f;
    float m_bobPhase = 0.0f;
    float m_time = 0.0f;
    float m_landDip = 0.0f;
    float m_landVelocity = 0.0f;
    float m_roll = 0.0f;
    glm::vec2 m_sway{0.0f};
    std::vector<float> m_walkPhase;
    CameraView m_lastCamera;

    ImFont* m_fontRegular = nullptr;
    ImFont* m_fontBold = nullptr;
    float m_fpsSmoothed = 60.0f;
    float m_frameMs = 16.0f;
    long m_frameCount = 0;
    std::string m_pendingScreenshot;
    unsigned m_shotFbo = 0;
    unsigned m_shotColor = 0;
};

} // namespace origon
