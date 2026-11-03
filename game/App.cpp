#include "App.h"

#include "core/FileSystem.h"
#include "core/Log.h"
#include "fx/PlayerModel.h"
#include "render/Screenshot.h"

#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <stdexcept>
#include <thread>

namespace origon {
namespace {

constexpr int kMaxTicksPerFrame = 5;

std::filesystem::path settingsPath() { return fs::resolve("settings.json"); }

std::vector<std::string> listMaps() {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(fs::resolve("maps"), ec)) {
        if (entry.path().extension() == ".json") {
            files.push_back(entry.path().stem().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// First-person weapon placement in view space.
glm::vec3 hipOffset(const glm::vec3& body) { return {0.24f, -0.23f, -0.34f - body.z * 0.5f}; }
glm::vec3 adsOffset(const glm::vec3& body) { return {0.0f, -0.085f - body.y * 0.5f, -0.22f - body.z * 0.5f}; }

} // namespace

App::App(const LaunchOptions& options) : m_options(options) {
    m_settings = GameSettings::load(settingsPath());
    m_window = std::make_unique<Window>(WindowDesc{"Origon", 1600, 900, m_settings.vsync});
    m_input.attach(m_window->handle());
    initImGui();

    if (!m_renderer.init()) {
        throw std::runtime_error("Renderer initialization failed");
    }
    if (!m_weapons.load("data/weapons.json", "data/gungame_ladder.json")) {
        throw std::runtime_error("Weapon data could not be loaded");
    }
    m_audioReady = m_audio.init();
    if (m_audioReady) {
        m_audio.setVolume(m_settings.masterVolume);
        m_audio.setAmbienceEnabled(m_settings.ambience);
    }
    m_sim = std::make_unique<Simulation>(m_physics, m_weapons);
    m_hud.setFonts(m_fontRegular, m_fontBold);
    m_mapFiles = listMaps();
    loadMap(options.map);
    m_window->setCursorCaptured(!options.autoplay);
    if (options.startInEditor) {
        enterEditor();
    }
    if (options.shotSize.x > 0 && options.shotSize.y > 0) {
        glCreateTextures(GL_TEXTURE_2D, 1, &m_shotColor);
        glTextureStorage2D(m_shotColor, 1, GL_RGBA8, options.shotSize.x, options.shotSize.y);
        glCreateFramebuffers(1, &m_shotFbo);
        glNamedFramebufferTexture(m_shotFbo, GL_COLOR_ATTACHMENT0, m_shotColor, 0);
        m_renderer.setOutputFramebuffer(m_shotFbo);
    }
}

App::~App() {
    m_settings.save(settingsPath());
    shutdownImGui();
}

void App::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    const std::string regular = fs::resolve("assets/fonts/DejaVuSans.ttf").string();
    const std::string bold = fs::resolve("assets/fonts/DejaVuSans-Bold.ttf").string();
    m_fontRegular = io.Fonts->AddFontFromFileTTF(regular.c_str(), 16.0f);
    m_fontBold = io.Fonts->AddFontFromFileTTF(bold.c_str(), 16.0f);
    if (!m_fontRegular) m_fontRegular = io.Fonts->AddFontDefault();
    if (!m_fontBold) m_fontBold = m_fontRegular;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowPadding = ImVec2(12, 10);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.08f, 0.94f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.3f, 0.36f, 0.24f, 0.6f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.38f, 0.46f, 0.3f, 0.8f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.25f, 0.3f, 0.22f, 0.8f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.42f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.48f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.18f, 0.15f, 0.9f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.22f, 0.27f, 0.18f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.9f, 0.78f, 0.35f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.8f, 0.68f, 0.3f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(m_window->handle(), true);
    ImGui_ImplOpenGL3_Init("#version 450");
}

void App::shutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void App::buildWorld(const MapData& map) {
    m_effects.clear(); // Debris bodies die with the physics rebuild.
    m_world.build(map, m_physics);
}

void App::loadMap(const std::string& fileName) {
    auto map = MapData::load(fs::resolve("maps") / (fileName + ".json"));
    if (!map) {
        log::warn("Map '{}' not found, using the default map", fileName);
        map = MapData::makeDefault();
    }
    m_mapFile = fileName;
    buildWorld(*map);
    restartMatch();
}

void App::restartMatch() {
    buildWorld(m_world.map()); // Props back to their map positions.
    m_sim->startMatch(m_world.map(), m_settings.botCount);
    if (m_options.autoplay) {
        m_sim->players()[0].isBot = true;
    }
    m_hud.reset();
    m_tickAccumulator = 0.0f;
    m_walkPhase.assign(m_sim->players().size(), 0.0f);
    processEvents(m_sim->takeEvents());
}

void App::enterEditor() {
    m_mode = Mode::Editor;
    setMenuOpen(false);
    m_window->setCursorCaptured(false);
    const Player& me = m_sim->players()[static_cast<size_t>(m_sim->localPlayerId())];
    m_editor.open(m_world.map(), m_mapFile, me.eyePosition() + glm::vec3(0.0f, 3.0f, 0.0f), m_viewYaw, -0.35f, true);
    buildWorld(m_world.map());
}

void App::leaveEditor() {
    m_mode = Mode::Playing;
    m_mapFile = m_editor.fileName();
    buildWorld(m_editor.map());
    restartMatch();
    m_window->setCursorCaptured(!m_options.autoplay);
}

void App::setMenuOpen(bool open) {
    if (m_menuOpen && !open) {
        m_settings.save(settingsPath());
    }
    m_menuOpen = open;
    if (m_mode == Mode::Playing) {
        m_window->setCursorCaptured(!open && !m_options.autoplay);
    }
    if (open) {
        m_mapFiles = listMaps();
    }
}

void App::takeScreenshot(const std::string& path) { m_pendingScreenshot = path; }

int App::run() {
    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();

    while (!m_window->shouldClose()) {
        const auto frameStart = Clock::now();
        float dt = std::chrono::duration<float>(frameStart - last).count();
        last = frameStart;
        dt = std::min(dt, 0.1f);
        m_time += dt;
        if (dt > 0.0f) {
            m_fpsSmoothed = glm::mix(m_fpsSmoothed, 1.0f / dt, 0.05f);
            m_frameMs = glm::mix(m_frameMs, dt * 1000.0f, 0.05f);
        }
        ++m_frameCount;

        m_input.beginFrame();
        m_window->pollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (m_input.keyPressed(GLFW_KEY_F1)) {
            if (m_mode == Mode::Playing) enterEditor();
            else leaveEditor();
        }
        if (m_input.keyPressed(GLFW_KEY_F3)) m_settings.showStats = !m_settings.showStats;
        if (m_input.keyPressed(GLFW_KEY_F12)) {
            const std::time_t now = std::time(nullptr);
            char stamp[32];
            std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&now));
            takeScreenshot(fs::resolve("screenshots").string() + "/origon_" + stamp + ".png");
        }
        if (!m_options.screenshotPath.empty() && m_frameCount == m_options.screenshotFrame) {
            takeScreenshot(m_options.screenshotPath);
        }

        const glm::ivec2 framebuffer = m_shotFbo ? m_options.shotSize : m_window->framebufferSize();
        if (m_mode == Mode::Playing) {
            if (m_input.keyPressed(GLFW_KEY_ESCAPE)) setMenuOpen(!m_menuOpen);
            updatePlaying(dt);
            m_effects.update(dt, m_physics);
        } else {
            m_editor.setFov(m_settings.fov);
            if (m_editor.update(dt, m_input, *m_window, m_physics, framebuffer)) {
                buildWorld(m_editor.map());
            }
            if (m_editor.consumePlayRequest()) leaveEditor();
        }

        // --- Render -------------------------------------------------------------------
        m_renderer.beginFrame(m_settings.render, framebuffer);
        CameraView camera;
        if (m_mode == Mode::Playing) {
            const float alpha = std::clamp(m_tickAccumulator / kTickDt, 0.0f, 1.0f);
            updateCameraFeel(dt);
            camera = playingCamera(alpha);
            m_lastCamera = camera;
            if (m_options.fixedCamera) {
                camera.position = m_options.cameraPosition;
                camera.view = glm::lookAt(camera.position,
                                          camera.position + viewDirection(glm::radians(m_options.cameraYaw), glm::radians(m_options.cameraPitch)),
                                          glm::vec3(0, 1, 0));
                camera.fovYDegrees = m_settings.fov;
            }
            m_world.submit(m_renderer, m_physics);
            submitPlayers(alpha, m_options.fixedCamera);
            m_effects.submit(m_renderer, m_physics);
            submitViewModel(dt);
            if (m_audioReady) {
                const glm::mat4 invView = glm::inverse(camera.view);
                m_audio.update(dt, *m_sim, camera.position, -glm::vec3(invView[2]));
            }
        } else {
            camera = m_editor.camera();
            m_world.submit(m_renderer, m_physics);
            m_editor.submit(m_renderer);
        }
        Environment environment = m_world.environment();
        environment.time = m_time;
        m_renderer.render(camera, environment);

        // --- UI -----------------------------------------------------------------------
        if (!m_options.hideUi) {
            if (m_mode == Mode::Playing) {
                const Player& me = m_sim->players()[static_cast<size_t>(m_sim->localPlayerId())];
                const bool scoped = m_sim->weaponFor(me).scoped && m_adsBlend > 0.9f;
                m_hud.draw(*m_sim, dt, m_input.keyDown(GLFW_KEY_TAB) && !m_menuOpen, scoped);
                if (m_menuOpen) drawMenu();
            } else {
                m_editor.drawUi();
            }
            if (m_settings.showStats) drawStats(dt);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!m_pendingScreenshot.empty()) {
            saveScreenshot(m_pendingScreenshot, framebuffer.x, framebuffer.y, m_shotFbo);
            if (m_pendingScreenshot == m_options.screenshotPath) {
                m_window->requestClose();
            }
            m_pendingScreenshot.clear();
        }
        m_window->swapBuffers();

        if (!m_settings.vsync && m_settings.maxFps > 0) {
            std::this_thread::sleep_until(frameStart + std::chrono::duration<double>(1.0 / m_settings.maxFps));
        }
    }
    return 0;
}

// --- Playing -------------------------------------------------------------------------------

void App::updatePlaying(float frameDt) {
    Player& me = m_sim->players()[static_cast<size_t>(m_sim->localPlayerId())];
    if (me.viewReset) {
        m_viewYaw = me.yaw;
        m_viewPitch = me.pitch;
        me.viewReset = false;
    }

    const WeaponDef& weapon = m_sim->weaponFor(me);
    glm::vec2 mouse(0.0f);
    if (!m_menuOpen && m_window->cursorCaptured()) {
        float sensitivity = m_settings.mouseSensitivity;
        if (me.aiming) sensitivity *= m_settings.adsSensitivityScale * (weapon.adsFov / m_settings.fov);
        mouse = m_input.mouseDelta();
        m_viewYaw -= glm::radians(mouse.x * sensitivity);
        m_viewPitch -= glm::radians(mouse.y * sensitivity) * (m_settings.invertY ? -1.0f : 1.0f);
        m_viewPitch = std::clamp(m_viewPitch, -1.53f, 1.53f);
        m_viewYaw = std::remainder(m_viewYaw, glm::two_pi<float>());
    }
    m_sway = glm::mix(m_sway, glm::clamp(-mouse * 0.0006f, glm::vec2(-0.03f), glm::vec2(0.03f)), std::min(1.0f, frameDt * 10.0f));

    const bool matchOver = m_sim->gunGame().phase() == GunGame::Phase::Finished;
    if (matchOver && !m_menuOpen && (m_input.keyPressed(GLFW_KEY_ENTER) || m_input.keyPressed(GLFW_KEY_KP_ENTER) || m_options.autoplay)) {
        restartMatch();
        return;
    }
    if (!m_menuOpen && m_input.keyPressed(GLFW_KEY_K)) m_sim->killSelf(m_sim->localPlayerId());

    m_tickAccumulator += frameDt;
    int ticks = 0;
    std::vector<PlayerCommand> commands;
    while (m_tickAccumulator >= kTickDt && ticks < kMaxTicksPerFrame) {
        m_sim->computeBotCommands(commands);
        if (!m_options.autoplay) {
            commands[static_cast<size_t>(m_sim->localPlayerId())] = buildLocalCommand();
        }
        m_sim->tick(commands);
        processEvents(m_sim->takeEvents());
        m_tickAccumulator -= kTickDt;
        ++ticks;
    }
    if (ticks == kMaxTicksPerFrame) m_tickAccumulator = std::min(m_tickAccumulator, kTickDt);
    if (m_options.autoplay) {
        m_viewYaw = me.yaw;
        m_viewPitch = me.pitch;
    }

    const float adsTarget = me.alive && me.aiming ? 1.0f : 0.0f;
    m_adsBlend += (adsTarget - m_adsBlend) * std::min(1.0f, frameDt * 14.0f);

    // Walk animation phases for third-person models.
    m_walkPhase.resize(m_sim->players().size(), 0.0f);
    for (const Player& p : m_sim->players()) {
        const float speed = glm::length(glm::vec2(p.velocity.x, p.velocity.z));
        m_walkPhase[static_cast<size_t>(p.id)] += speed * frameDt * 2.3f;
    }
}

PlayerCommand App::buildLocalCommand() const {
    PlayerCommand command;
    command.yaw = m_viewYaw;
    command.pitch = m_viewPitch;
    if (m_menuOpen || !m_window->cursorCaptured()) return command;
    if (m_input.keyDown(GLFW_KEY_W)) command.forward += 1.0f;
    if (m_input.keyDown(GLFW_KEY_S)) command.forward -= 1.0f;
    if (m_input.keyDown(GLFW_KEY_D)) command.right += 1.0f;
    if (m_input.keyDown(GLFW_KEY_A)) command.right -= 1.0f;
    if (m_input.mouseDown(GLFW_MOUSE_BUTTON_LEFT)) command.buttons |= kButtonFire;
    if (m_input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT)) command.buttons |= kButtonAds;
    if (m_input.keyDown(GLFW_KEY_SPACE)) command.buttons |= kButtonJump;
    if (m_input.keyDown(GLFW_KEY_LEFT_SHIFT)) command.buttons |= kButtonSprint;
    if (m_input.keyDown(GLFW_KEY_R)) command.buttons |= kButtonReload;
    if (m_input.keyDown(GLFW_KEY_V)) command.buttons |= kButtonMelee;
    if (m_input.keyDown(GLFW_KEY_LEFT_CONTROL) || m_input.keyDown(GLFW_KEY_C)) command.buttons |= kButtonCrouch;
    return command;
}

void App::processEvents(const std::vector<GameEvent>& events) {
    const int local = m_sim->localPlayerId();
    for (const GameEvent& e : events) {
        if (e.type == GameEvent::Type::Landed && e.a == local) {
            m_landVelocity -= std::min(e.magnitude * 0.035f, 0.5f);
        }
    }
    m_hud.onEvents(events, *m_sim);
    m_effects.onEvents(events, *m_sim, m_physics, localMuzzle());
    if (m_audioReady) m_audio.onEvents(events, *m_sim);
}

void App::updateCameraFeel(float frameDt) {
    const Player& me = m_sim->players()[static_cast<size_t>(m_sim->localPlayerId())];
    // Landing dip: damped spring.
    m_landVelocity += (-m_landDip * 140.0f - m_landVelocity * 16.0f) * frameDt;
    m_landDip += m_landVelocity * frameDt;
    // Strafe roll.
    const glm::vec3 right(std::cos(m_viewYaw), 0.0f, -std::sin(m_viewYaw));
    const float strafe = glm::dot(me.velocity, right) / 7.0f;
    m_roll = glm::mix(m_roll, -strafe * 0.02f * (1.0f - m_adsBlend), std::min(1.0f, frameDt * 8.0f));
    const float speed = glm::length(glm::vec2(me.velocity.x, me.velocity.z));
    if (me.onGround) m_bobPhase += frameDt * speed * 1.7f;
}

CameraView App::playingCamera(float alpha) const {
    const Player& me = m_sim->players()[static_cast<size_t>(m_sim->localPlayerId())];
    const WeaponDef& weapon = m_sim->weaponFor(me);
    CameraView camera;
    glm::vec3 eye = glm::mix(me.previousPosition, me.position, alpha) + glm::vec3(0.0f, me.eyeHeight, 0.0f);
    float pitch = m_viewPitch + me.viewKick;
    float roll = m_roll;
    if (me.alive) {
        const float speed = glm::length(glm::vec2(me.velocity.x, me.velocity.z));
        const float bob = me.onGround ? std::min(speed / 6.0f, 1.0f) * (1.0f - m_adsBlend * 0.85f) : 0.0f;
        eye.y += std::abs(std::sin(m_bobPhase)) * 0.035f * bob + m_landDip;
        roll += std::sin(m_bobPhase) * 0.004f * bob;
    } else {
        eye.y = me.position.y + 0.4f; // Death camera close to the ground.
        pitch = -0.2f;
        roll = 0.35f;
    }
    camera.position = eye;
    const glm::mat4 look = glm::lookAt(eye, eye + viewDirection(m_viewYaw, pitch), glm::vec3(0, 1, 0));
    camera.view = glm::rotate(glm::mat4(1.0f), roll, glm::vec3(0, 0, 1)) * look;
    const float adsFov = weapon.scoped ? weapon.adsFov : weapon.adsFov * m_settings.fov / 80.0f;
    camera.fovYDegrees = glm::mix(m_settings.fov, adsFov, m_adsBlend);
    return camera;
}

glm::vec3 App::localMuzzle() const {
    const Player& me = m_sim->players()[static_cast<size_t>(m_sim->localPlayerId())];
    const glm::vec3 body = weaponBodySize(&m_sim->weaponFor(me));
    const glm::vec3 offset = glm::mix(hipOffset(body), adsOffset(body), m_adsBlend) + glm::vec3(0.0f, 0.0f, -body.z * 0.5f - 0.05f);
    return glm::vec3(glm::inverse(m_lastCamera.view) * glm::vec4(offset, 1.0f));
}

void App::submitPlayers(float alpha, bool includeLocal) {
    for (const Player& p : m_sim->players()) {
        if ((p.id == m_sim->localPlayerId() && !includeLocal) || !p.alive) continue;
        PlayerPose pose;
        pose.feet = glm::mix(p.previousPosition, p.position, alpha);
        pose.rotation = glm::angleAxis(p.yaw, glm::vec3(0, 1, 0));
        pose.pitch = p.pitch;
        pose.walkPhase = m_walkPhase[static_cast<size_t>(p.id)];
        pose.walkAmount = p.onGround ? std::min(glm::length(glm::vec2(p.velocity.x, p.velocity.z)) / 5.0f, 1.0f) : 0.3f;
        pose.eyeHeight = p.eyeHeight;
        pose.color = World::toLinear(p.color);
        pose.weapon = &m_sim->weaponFor(p);
        pose.emissiveHighlight = p.spawnProtection > 0.0f;
        submitPlayerModel(m_renderer, pose);
    }
}

void App::submitViewModel(float) {
    const Player& me = m_sim->players()[static_cast<size_t>(m_sim->localPlayerId())];
    if (!me.alive || m_options.autoplay) return;
    const WeaponDef& weapon = m_sim->weaponFor(me);
    if (weapon.scoped && m_adsBlend > 0.9f) return;
    const glm::vec3 body = weaponBodySize(&weapon);

    const float speed = glm::length(glm::vec2(me.velocity.x, me.velocity.z));
    const float bobAmount = me.onGround ? std::min(speed / 6.0f, 1.0f) * (1.0f - m_adsBlend * 0.8f) : 0.0f;
    const glm::vec3 bob(std::sin(m_bobPhase) * 0.012f * bobAmount, -std::abs(std::cos(m_bobPhase)) * 0.012f * bobAmount, 0.0f);

    glm::vec3 position = glm::mix(hipOffset(body), adsOffset(body), m_adsBlend) + bob;
    position += glm::vec3(m_sway.x, m_sway.y, 0.0f) * (1.0f - m_adsBlend * 0.7f);
    position.y += m_landDip * 0.4f;
    position.z += me.viewKick * 0.8f;
    float pitch = me.viewKick * 1.5f;
    float roll = 0.0f;
    const bool sprinting = speed > 6.0f && me.onGround && !me.aiming;
    if (sprinting) {
        pitch -= 0.35f;
        roll = 0.4f;
        position += glm::vec3(-0.04f, -0.04f, 0.04f);
    }
    if (me.reloadTimer > 0.0f && weapon.reloadSeconds > 0.0f) {
        const float t = 1.0f - me.reloadTimer / weapon.reloadSeconds;
        pitch -= std::sin(t * glm::pi<float>()) * 0.7f;
        position.y -= std::sin(t * glm::pi<float>()) * 0.08f;
    }
    if (me.meleeAnimation > 0.0f) {
        const float t = 1.0f - me.meleeAnimation / 0.3f;
        const float thrust = std::sin(t * glm::pi<float>());
        position += glm::vec3(-0.12f * thrust, 0.04f * thrust, -0.28f * thrust);
    }
    const glm::mat4 base = glm::translate(glm::mat4(1.0f), position) * glm::rotate(glm::mat4(1.0f), pitch, glm::vec3(1, 0, 0)) *
                           glm::rotate(glm::mat4(1.0f), roll, glm::vec3(0, 0, 1));

    auto part = [&](const glm::vec3& offset, const glm::vec3& size, const glm::vec3& color, float metallic, float roughness) {
        DrawItem item;
        item.mesh = &m_renderer.cube();
        item.model = base * glm::translate(glm::mat4(1.0f), offset) * glm::scale(glm::mat4(1.0f), size);
        item.material.albedo = color;
        item.material.metallic = metallic;
        item.material.roughness = roughness;
        item.castShadow = false;
        m_renderer.submitViewModel(item);
    };

    const glm::vec3 metal(0.05f, 0.05f, 0.055f);
    const glm::vec3 wood(0.2f, 0.08f, 0.03f);
    const glm::vec3 skin(0.55f, 0.32f, 0.2f);
    const glm::vec3 sleeve = World::toLinear(me.color) * 0.7f;
    if (weapon.isMelee()) {
        part({0.0f, 0.0f, 0.0f}, body, glm::vec3(0.75f), 1.0f, 0.2f);
        part({0.0f, -0.01f, body.z * 0.5f + 0.06f}, {0.03f, 0.045f, 0.12f}, glm::vec3(0.03f), 0.0f, 0.8f);
        part({0.0f, -0.03f, body.z * 0.5f + 0.1f}, {0.07f, 0.08f, 0.09f}, skin, 0.0f, 0.7f);
        part({0.02f, -0.06f, body.z * 0.5f + 0.3f}, {0.09f, 0.09f, 0.35f}, sleeve, 0.0f, 0.9f);
        return;
    }
    const bool longGun = body.z > 0.6f;
    part({0.0f, 0.0f, 0.0f}, body, metal, 0.85f, 0.35f);
    if (weapon.category != WeaponCategory::Pistol) {
        part({0.0f, -0.035f, body.z * 0.5f + 0.1f}, {body.x * 0.9f, body.y * 1.4f, 0.24f}, wood, 0.0f, 0.6f);
        if (longGun) part({0.0f, -0.03f, -body.z * 0.1f}, {body.x * 1.1f, body.y * 0.9f, body.z * 0.55f}, wood, 0.0f, 0.6f);
    }
    if (weapon.category == WeaponCategory::Smg || weapon.category == WeaponCategory::AssaultRifle || weapon.category == WeaponCategory::Lmg) {
        part({0.0f, -body.y * 0.5f - 0.07f, -body.z * 0.12f}, {0.03f, 0.14f, 0.05f}, metal, 0.7f, 0.5f);
    }
    if (weapon.scoped) {
        part({0.0f, body.y * 0.5f + 0.04f, 0.0f}, {0.045f, 0.045f, 0.32f}, glm::vec3(0.02f), 0.6f, 0.3f);
    }
    if (weapon.category == WeaponCategory::Pistol) {
        part({0.0f, -0.09f, 0.06f}, {0.04f, 0.1f, 0.05f}, glm::vec3(0.12f, 0.06f, 0.03f), 0.0f, 0.7f);
    }
    // Hands and sleeves.
    part({0.0f, -0.07f, body.z * 0.5f - 0.02f}, {0.08f, 0.08f, 0.1f}, skin, 0.0f, 0.7f);
    part({0.03f, -0.1f, body.z * 0.5f + 0.2f}, {0.1f, 0.1f, 0.35f}, sleeve, 0.0f, 0.9f);
    if (weapon.category != WeaponCategory::Pistol) {
        part({-0.01f, -0.07f, -body.z * 0.25f}, {0.08f, 0.07f, 0.1f}, skin, 0.0f, 0.7f);
        part({-0.12f, -0.12f, -body.z * 0.05f}, {0.1f, 0.1f, 0.4f}, sleeve, 0.0f, 0.9f);
    }
}

// --- Menus ---------------------------------------------------------------------------------

void App::drawMenu() {
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(0, 0, 0, 120));
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(580, 580), ImGuiCond_Always);
    ImGui::Begin("Pausa", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (ImGui::Button("Reanudar", ImVec2(130, 0))) setMenuOpen(false);
    ImGui::SameLine();
    if (ImGui::Button("Reiniciar", ImVec2(130, 0))) {
        restartMatch();
        setMenuOpen(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Editor (F1)", ImVec2(130, 0))) enterEditor();
    ImGui::SameLine();
    if (ImGui::Button("Salir", ImVec2(-1, 0))) m_window->requestClose();
    ImGui::Separator();

    if (ImGui::BeginTabBar("menu_tabs")) {
        if (ImGui::BeginTabItem("Gráficos")) {
            RenderSettings& r = m_settings.render;
            ImGui::TextDisabled("Preajustes:");
            ImGui::SameLine();
            if (ImGui::SmallButton("Batata")) { r.shadowQuality = 1; r.msaaSamples = 0; r.renderScale = 0.7f; r.ssao = false; r.bloom = false; r.godRays = false; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Media")) { r.shadowQuality = 2; r.msaaSamples = 2; r.renderScale = 0.9f; r.ssao = true; r.bloom = true; r.godRays = false; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Alta")) { r.shadowQuality = 3; r.msaaSamples = 4; r.renderScale = 1.0f; r.ssao = true; r.bloom = true; r.godRays = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Ultra")) { r.shadowQuality = 4; r.msaaSamples = 8; r.renderScale = 1.0f; r.ssao = true; r.bloom = true; r.godRays = true; }
            ImGui::Separator();
            const char* shadowLabels[] = {"Desactivadas", "Bajas", "Medias", "Altas", "Ultra"};
            ImGui::Combo("Calidad de sombras", &r.shadowQuality, shadowLabels, 5);
            const char* msaaLabels[] = {"Desactivado", "MSAA 2x", "MSAA 4x", "MSAA 8x"};
            int msaaIndex = r.msaaSamples >= 8 ? 3 : r.msaaSamples >= 4 ? 2 : r.msaaSamples >= 2 ? 1 : 0;
            if (ImGui::Combo("Antialiasing", &msaaIndex, msaaLabels, 4)) {
                const int values[] = {0, 2, 4, 8};
                r.msaaSamples = values[msaaIndex];
            }
            int scale = static_cast<int>(std::round(r.renderScale * 100.0f));
            if (ImGui::SliderInt("Escala de resolución", &scale, 50, 100, "%d%%")) r.renderScale = static_cast<float>(scale) / 100.0f;
            ImGui::Checkbox("Oclusión ambiental (SSAO)", &r.ssao);
            ImGui::Checkbox("Bloom", &r.bloom);
            ImGui::Checkbox("Luces volumétricas (rayos de sol)", &r.godRays);
            if (ImGui::Checkbox("Sincronización vertical (VSync)", &m_settings.vsync)) m_window->setVsync(m_settings.vsync);
            ImGui::BeginDisabled(m_settings.vsync);
            ImGui::SliderInt("FPS máximos", &m_settings.maxFps, 0, 360, m_settings.maxFps == 0 ? "Sin límite" : "%d");
            ImGui::EndDisabled();
            ImGui::SliderFloat("Exposición", &r.exposure, 0.5f, 2.0f, "%.2f");
            ImGui::Checkbox("Mostrar rendimiento (F3)", &m_settings.showStats);
            ImGui::Spacing();
            ImGui::TextDisabled("Los ajustes nunca cambian colisiones ni visibilidad del mapa.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio")) {
            if (ImGui::SliderFloat("Volumen general", &m_settings.masterVolume, 0.0f, 1.0f, "%.2f") && m_audioReady) {
                m_audio.setVolume(m_settings.masterVolume);
            }
            if (ImGui::Checkbox("Sonido ambiente (viento, pájaros)", &m_settings.ambience) && m_audioReady) {
                m_audio.setAmbienceEnabled(m_settings.ambience);
            }
            if (!m_audioReady) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "No se encontró dispositivo de audio.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controles")) {
            ImGui::SliderFloat("Campo de visión", &m_settings.fov, 60.0f, 110.0f, "%.0f°");
            ImGui::SliderFloat("Sensibilidad del ratón", &m_settings.mouseSensitivity, 0.01f, 0.5f, "%.3f");
            ImGui::SliderFloat("Sensibilidad al apuntar", &m_settings.adsSensitivityScale, 0.2f, 1.5f, "%.2f");
            ImGui::Checkbox("Invertir eje Y", &m_settings.invertY);
            ImGui::Separator();
            ImGui::TextUnformatted("WASD: moverse          Shift: correr        Espacio: saltar");
            ImGui::TextUnformatted("Ctrl / C: agacharse     Clic izq.: disparar  Clic der.: apuntar");
            ImGui::TextUnformatted("R: recargar             V: cuchillo          Tab: tabla");
            ImGui::TextUnformatted("Esc: menú               F1: editor           F3: rendimiento");
            ImGui::TextUnformatted("F12: captura            K: suicidio (prueba)");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Partida")) {
            ImGui::TextUnformatted("Modo: Gun Game (todos contra todos)");
            ImGui::SliderInt("Bots", &m_settings.botCount, 0, m_weapons.rules().maxPlayers - 1);
            ImGui::TextDisabled("Los bots se aplican al reiniciar la partida.");
            if (ImGui::BeginCombo("Mapa", m_mapFile.c_str())) {
                for (const std::string& file : m_mapFiles) {
                    if (ImGui::Selectable(file.c_str(), file == m_mapFile)) {
                        loadMap(file);
                        setMenuOpen(false);
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Reiniciar con estos ajustes", ImVec2(-1, 0))) {
                restartMatch();
                setMenuOpen(false);
            }
            ImGui::Separator();
            const GunGameRules& rules = m_weapons.rules();
            ImGui::Text("Niveles: %d   Límite de tiempo: %d min", m_weapons.tierCount(), static_cast<int>(rules.timeLimitSeconds / 60.0f));
            ImGui::TextWrapped("Cada baja sube un nivel de arma. Morir por cuchillo te baja un nivel (humillación). "
                               "El suicidio y las caídas también bajan un nivel. Gana quien consiga una baja con el "
                               "cuchillo en el último nivel.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void App::drawStats(float) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const RenderStats& stats = m_renderer.stats();
    const std::string text = std::format("{:.0f} FPS  {:.2f} ms\nDraw calls: {}\nCascadas: {}", m_fpsSmoothed, m_frameMs,
                                         stats.drawCalls, stats.shadowCascades);
    dl->AddRectFilled(ImVec2(8, 8), ImVec2(190, 72), IM_COL32(0, 0, 0, 150), 4.0f);
    dl->AddText(m_fontRegular, 15.0f, ImVec2(14, 12), IM_COL32(220, 230, 210, 255), text.c_str());
}

} // namespace origon
