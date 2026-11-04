#pragma once

#include "audio/GameAudio.h"
#include "core/Input.h"
#include "core/Window.h"
#include "editor/MapEditor.h"
#include "fx/Effects.h"
#include "net/LanDiscovery.h"
#include "net/NetSession.h"
#include "physics/PhysicsWorld.h"
#include "render/Renderer.h"
#include "render/SkinnedModel.h"
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
    std::string map = "bosque";
    std::string screenshotPath; // When set, save a screenshot at screenshotFrame and exit.
    int screenshotFrame = 300;
    bool startInEditor = false;
    bool autoplay = false; // The local player is driven by the bot AI (spectator/demo).
    bool hideUi = false;
    bool forceTitle = false; // Show the title screen even in capture mode.
    int followPlayer = -1;   // Third-person spectator camera behind this player.
    bool poseGallery = false; // Debug: a row of soldiers in front of the fixed camera, one per clip.
    std::string dumpSounds;  // Write the synthesized sound bank as WAV files here.
    // Photo mode: fixed camera (position, yaw/pitch degrees) and offscreen capture size.
    bool fixedCamera = false;
    glm::vec3 cameraPosition{0.0f};
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;
    glm::ivec2 shotSize{0};
    // Networking.
    int hostPort = 0;              // > 0: start a listen server on this port.
    std::string connectAddress;    // Non-empty: join this server.
    int connectPort = net::kDefaultPort;
    std::string playerName = "Jugador";
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
    void drawTitleScreen();
    void drawStats(float frameDt);
    void takeScreenshot(const std::string& path);
    net::Snapshot makeSnapshot() const;
    void applySnapshot(const net::Snapshot& snapshot);
    void updateNetworkClient();
    void startHosting(int port);
    void joinServer(const std::string& address, int port);
    void leaveNetwork();

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
    bool m_titleScreen = false;
    int m_menuTab = -1; // Tab to open when the pause menu appears (-1: keep).
    std::string m_mapFile = "bosque";
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
    SkinnedModel m_soldier;
    bool m_soldierReady = false;
    std::vector<glm::mat4> m_soldierGlobals; // Scratch: joint globals of the last drawn soldier.
    std::vector<std::vector<glm::mat4>> m_skinBuffers;
    void submitSoldier(const Player& p, float alpha);
    const std::vector<glm::mat4>& drawSoldierPose(const SkinnedModel::Pose& pose, const glm::mat4& model, const glm::vec3& tint,
                                                    bool highlight);
    void submitModel(const Model& model, const glm::mat4& transform);
    void submitCorpses();
    void submitPoseGallery(const glm::vec3& eye, float yaw);
    CameraView m_lastCamera;

    ImFont* m_fontRegular = nullptr;
    ImFont* m_fontBold = nullptr;
    float m_fpsSmoothed = 60.0f;
    float m_frameMs = 16.0f;
    long m_frameCount = 0;
    std::string m_pendingScreenshot;
    net::NetSession m_net;
    net::LanDiscovery m_lan;
    bool m_browserOpen = false;
    uint32_t m_netTick = 0;
    bool m_clientReady = false;
    float m_snapshotAge = 0.0f;
    float m_predictedFireCooldown = 0.0f;
    uint32_t m_previousClientButtons = 0;
    void predictLocalFire(const PlayerCommand& command);
    float m_rematchTimer = -1.0f;
    std::vector<net::PropState> m_replicatedProps;
    std::string m_joinAddress = "127.0.0.1";
    int m_joinPort = net::kDefaultPort;
    int m_hostPort = net::kDefaultPort;
    std::string m_playerName = "Jugador";
    unsigned m_shotFbo = 0;
    unsigned m_shotColor = 0;
};

} // namespace origon
