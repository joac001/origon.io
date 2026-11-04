#include "App.h"

#include "world/MapGenerator.h"

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
#include <imgui_stdlib.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <map>
#include <fstream>
#include <array>
#include <chrono>
#include <cstdlib>
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
    m_autoPresetPending = !std::filesystem::exists(settingsPath()) && options.benchSeconds <= 0.0f;
    m_settings = GameSettings::load(settingsPath());
    if (m_autoPresetPending) applyGraphicsPreset(2);
    m_window = std::make_unique<Window>(WindowDesc{"Origon", 1600, 900, m_settings.vsync && options.benchSeconds <= 0.0f});
    m_input.attach(m_window->handle());
    initImGui();

    if (!m_renderer.init()) {
        throw std::runtime_error("Renderer initialization failed");
    }
    if (!m_weapons.load("data/weapons.json", "data/gungame_ladder.json")) {
        throw std::runtime_error("Weapon data could not be loaded");
    }
    m_audioReady = m_audio.init();
    // Sound propagation: three rays (direct, above, beside) against static geometry; the blocked
    // fraction muffles and quiets the sound.
    m_audio.setOcclusionQuery([this](const glm::vec3& listener, const glm::vec3& source) {
        const glm::vec3 delta = source - listener;
        const float distance = glm::length(delta);
        if (distance < 1.0f) return 0.0f;
        const glm::vec3 side = glm::normalize(glm::cross(delta, glm::vec3(0, 1, 0)) + glm::vec3(1e-4f, 0, 0)) * 0.8f;
        const glm::vec3 offsets[3] = {glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), side};
        int blocked = 0;
        for (const glm::vec3& o : offsets) {
            const glm::vec3 from = listener + o;
            const glm::vec3 to = source + o * 0.5f;
            const glm::vec3 path = to - from;
            const float length = glm::length(path);
            if (length < 0.5f) continue;
            // Stop short of the source so its own surroundings do not count as a wall.
            if (m_physics.rayCast(from, path / length, std::max(length - 0.6f, 0.1f), false)) ++blocked;
        }
        return static_cast<float>(blocked) / 3.0f;
    });
    // Footsteps on structures (bridges, floors, docks) sound like wood, on the ground like dirt.
    m_audio.setSurfaceQuery([this](const glm::vec3& feet) {
        return m_world.map().terrain.valid() ? feet.y > m_world.terrainHeight(feet.x, feet.z) + 0.25f : true;
    });
    if (m_audioReady && !options.dumpSounds.empty()) {
        m_audio.dumpWav(options.dumpSounds);
    }
    if (m_audioReady) {
        m_audio.setVolume(m_settings.masterVolume);
        m_audio.setAmbienceEnabled(m_settings.ambience);
    }
    m_soldierReady = m_soldiers[0].load("assets/models/characters/soldier_m.glb", &m_textures);
    m_soldierFemale = m_soldierReady && m_soldiers[1].load("assets/models/characters/soldier_f.glb", &m_textures);
    // Skin textures are picked per look at draw time: load them now so a new face never stalls a frame.
    for (const bool female : {false, true}) {
        const LookCatalog skins = lookOptions(CharacterLook::SkinTone, female);
        for (size_t i = 0; i < skins.count; ++i) {
            m_textures.load(std::string("assets/textures/characters/skin_") + skins.options[i].id +
                                (female ? "_female.jpg" : "_male.jpg"),
                            true);
        }
    }
    m_effects.setSkinnedCorpses(m_soldierReady);
    m_world.setModelLibrary(&m_models);
    m_world.setTextureCache(&m_textures);
    m_sim = std::make_unique<Simulation>(m_physics, m_weapons);
    m_sim->setLocalLook(m_settings.look);
    m_hud.setFonts(m_fontRegular, m_fontBold);
    m_mapFiles = listMaps();
    loadMap(options.map);
    m_window->setCursorCaptured(!options.autoplay);
    // The title screen appears on a normal launch (not for captures, hosting or joining).
    m_titleScreen = (options.screenshotPath.empty() || options.forceTitle) && !options.autoplay && options.hostPort == 0 &&
                    options.connectAddress.empty() && options.joinCode.empty() &&
                    !options.startInEditor;
    if (m_titleScreen) {
        m_window->setCursorCaptured(false);
    }
    m_playerName = options.playerName;
    if (!options.relayAddress.empty()) m_settings.relayAddress = options.relayAddress;
    if (options.hostPort > 0) {
        startHosting(options.hostPort, options.publish);
    } else if (!options.connectAddress.empty()) {
        joinServer(options.connectAddress, options.connectPort);
    } else if (!options.joinCode.empty()) {
        joinRelayRoom(options.joinCode);
    }
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
    std::optional<MapData> map;
    if (fileName.rfind("procedural:", 0) == 0) {
        // Generated in place from the seed; joining players receive it like any other map.
        const auto seed = static_cast<uint32_t>(std::strtoul(fileName.c_str() + 11, nullptr, 10));
        map = generateProceduralMap(seed);
        m_proceduralSeed = static_cast<int>(seed);
        log::info("Map: procedural seed {}", seed);
    } else {
        map = MapData::load(fs::resolve("maps") / (fileName + ".json"));
    }
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
    m_sim->startMatch(m_world.map(), m_options.botCount >= 0 ? m_options.botCount : m_settings.botCount);
    if (m_options.autoplay) {
        m_sim->players()[0].isBot = true;
    }
    m_hud.reset();
    m_tickAccumulator = 0.0f;
    m_rematchTimer = -1.0f;
    m_walkPhase.assign(m_sim->players().size(), 0.0f);
    processEvents(m_sim->takeEvents());
    m_net.serverRematch(*m_sim, m_world.map());
}

// --- Networking ------------------------------------------------------------------------------

void App::startHosting(int port, bool publish) {
    m_titleScreen = false;
    m_sim->players()[0].isBot = false;
    leaveNetwork();
    if (m_net.host(static_cast<uint16_t>(port))) {
        m_hostPort = port;
        restartMatch();
        std::string relayHost;
        uint16_t relayPort = 0;
        if (publish && net::parseHostPort(m_settings.relayAddress, relayHost, relayPort, net::kRelayDefaultPort)) {
            m_net.publishToRelay(relayHost, relayPort);
        }
    }
}

void App::joinRelayRoom(const std::string& code) {
    std::string relayHost;
    uint16_t relayPort = 0;
    if (!net::parseHostPort(m_settings.relayAddress, relayHost, relayPort, net::kRelayDefaultPort)) {
        log::warn("Invalid relay address '{}'", m_settings.relayAddress);
        return;
    }
    m_titleScreen = false;
    leaveNetwork();
    m_clientReady = false;
    m_net.setLocalLook(m_settings.look);
    m_net.connectViaRelay(relayHost, relayPort, code, m_playerName);
}

void App::applyLook(const CharacterLook& look) {
    m_settings.look = look;
    m_settings.look.sanitize();
    m_settings.save(settingsPath());
    m_sim->setLocalLook(m_settings.look);
    if (m_net.isServer()) m_net.broadcastPlayerInfo(*m_sim);
    m_net.sendLook(m_settings.look);
}

void App::drawCharacterTab() {
    m_characterPreview = true;
    CharacterLook look = m_settings.look;
    bool changed = false;
    ImGui::TextDisabled("Solo cambia el aspecto: todos los personajes tienen el mismo tamaño y las mismas zonas de impacto.");
    auto field = [&](CharacterLook::Field f) {
        const LookCatalog options = lookOptions(f, look.female());
        size_t colorCount = 0;
        const LookColor* colors = lookColors(f, colorCount);
        const size_t count = colors ? colorCount : options.count;
        const size_t current = look[f];
        const char* preview = colors ? colors[current].label : options.options[current].label;
        ImGui::PushID(static_cast<int>(f));
        if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) {
            look[f] = static_cast<uint8_t>((current + count - 1) % count);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##next", ImGuiDir_Right)) {
            look[f] = static_cast<uint8_t>((current + 1) % count);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo(lookFieldLabel(f), preview)) {
            for (size_t i = 0; i < count; ++i) {
                if (colors) {
                    const glm::vec3 c = colors[i].srgb;
                    ImGui::ColorButton("##swatch", ImVec4(c.r, c.g, c.b, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
                    ImGui::SameLine();
                }
                const char* label = colors ? colors[i].label : options.options[i].label;
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(label, i == current)) {
                    look[f] = static_cast<uint8_t>(i);
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    };
    ImGui::SeparatorText("Cuerpo");
    field(CharacterLook::Body);
    field(CharacterLook::SkinTone);
    field(CharacterLook::Hair);
    field(CharacterLook::HairColor);
    if (!look.female()) field(CharacterLook::FacialHair);
    ImGui::SeparatorText("Accesorios");
    field(CharacterLook::Headwear);
    field(CharacterLook::Eyewear);
    ImGui::SeparatorText("Ropa");
    field(CharacterLook::Outfit);
    field(CharacterLook::OutfitColor);
    field(CharacterLook::Shoes);
    ImGui::Spacing();
    if (ImGui::Button("Aleatorio", ImVec2(180, 0))) {
        look = CharacterLook::random(static_cast<uint32_t>(m_time * 1000.0));
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restablecer", ImVec2(180, 0))) {
        look = CharacterLook{};
        changed = true;
    }
    if (changed) applyLook(look);
}

void App::drawInternetSection() {
    ImGui::SeparatorText("Por internet (servidor intermedio)");
    if (ImGui::InputText("Servidor intermedio", &m_settings.relayAddress)) {
        m_nextRelayRefresh = 0.0;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) m_settings.save(settingsPath());
    std::string relayHost;
    uint16_t relayPort = 0;
    const bool relayValid = net::parseHostPort(m_settings.relayAddress, relayHost, relayPort, net::kRelayDefaultPort);
    if (m_net.isServer()) {
        if (m_net.isPublished()) {
            if (!m_net.relayCode().empty()) {
                ImGui::Text("Código de la partida:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", m_net.relayCode().c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Copiar")) ImGui::SetClipboardText(m_net.relayCode().c_str());
            }
            ImGui::TextDisabled("%s", m_net.relayStatus().c_str());
            if (ImGui::Button("Dejar de publicar", ImVec2(-1, 0))) m_net.unpublishFromRelay();
        } else {
            if (!m_net.relayStatus().empty()) ImGui::TextDisabled("%s", m_net.relayStatus().c_str());
            ImGui::BeginDisabled(!relayValid);
            if (ImGui::Button("Publicar esta partida por internet", ImVec2(-1, 0))) m_net.publishToRelay(relayHost, relayPort);
            ImGui::EndDisabled();
        }
    } else if (relayValid) {
        if (m_time >= m_nextRelayRefresh) {
            m_relayBrowser.refresh(relayHost, relayPort);
            m_nextRelayRefresh = m_time + 3.0;
        }
        m_relayBrowser.poll();
        if (!m_relayBrowser.status().empty()) ImGui::TextDisabled("%s", m_relayBrowser.status().c_str());
        for (const net::RelayBrowser::Room& room : m_relayBrowser.rooms()) {
            ImGui::PushID(room.code.c_str());
            ImGui::Text("%s  ·  %s  ·  %d/%d  [%s]", room.name.c_str(), room.map.c_str(), room.players, room.maxPlayers, room.code.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Unirse")) {
                joinRelayRoom(room.code);
                setMenuOpen(false);
            }
            ImGui::PopID();
        }
    }
    ImGui::InputText("Código", &m_joinCode, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    ImGui::BeginDisabled(!relayValid || m_joinCode.size() != static_cast<size_t>(net::kRelayCodeLength));
    if (ImGui::Button("Unirse con código", ImVec2(-1, 0))) {
        joinRelayRoom(m_joinCode);
        setMenuOpen(false);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("El anfitrión publica la partida y comparte el código. No hace falta abrir puertos.");
}

void App::joinServer(const std::string& address, int port) {
    m_titleScreen = false;
    leaveNetwork();
    m_clientReady = false;
    m_net.setLocalLook(m_settings.look);
    m_net.connect(address, static_cast<uint16_t>(port), m_playerName);
}

void App::leaveNetwork() {
    const bool wasClient = m_net.isClient();
    m_net.shutdown();
    m_clientReady = false;
    if (wasClient) {
        loadMap(m_mapFile); // Back to a local match.
    }
}

net::Snapshot App::makeSnapshot() const {
    net::Snapshot s;
    s.serverTick = m_netTick;
    s.timeLeft = m_sim->gunGame().timeLeft();
    s.phase = m_sim->gunGame().phase() == GunGame::Phase::Finished ? 1 : 0;
    s.winner = static_cast<int8_t>(m_sim->gunGame().winnerId());
    for (const Player& p : m_sim->players()) {
        net::PlayerState state;
        state.id = static_cast<uint8_t>(p.id);
        state.flags = static_cast<uint8_t>((p.alive ? 1 : 0) | (p.isBot ? 2 : 0) | (p.crouched ? 4 : 0) | (p.onGround ? 8 : 0) |
                                           (p.aiming ? 16 : 0));
        state.position = p.position;
        state.velocity = p.velocity;
        state.yaw = p.yaw;
        state.pitch = p.pitch;
        state.eyeHeight = p.eyeHeight;
        state.health = p.health;
        state.tier = static_cast<uint8_t>(p.tier);
        state.kills = static_cast<uint16_t>(p.kills);
        state.deaths = static_cast<uint16_t>(p.deaths);
        state.ammo = static_cast<uint8_t>(std::clamp(p.ammo, 0, 255));
        state.reloadTimer = p.reloadTimer;
        state.respawnTimer = p.respawnTimer;
        state.spawnProtection = p.spawnProtection;
        state.viewKick = p.viewKick;
        state.meleeAnimation = p.meleeAnimation;
        s.players.push_back(state);
    }
    for (BodyHandle body : m_world.dynamicBodies()) {
        const BodyTransform t = m_physics.transform(body);
        s.props.push_back({t.position, t.rotation});
    }
    return s;
}

void App::applySnapshot(const net::Snapshot& snapshot) {
    int count = 0;
    for (const net::PlayerState& state : snapshot.players) count = std::max(count, static_cast<int>(state.id) + 1);
    m_sim->ensurePlayerCount(count);
    m_sim->setMatchState(snapshot.timeLeft, snapshot.phase, snapshot.winner);
    auto& players = m_sim->mutablePlayers();
    const int local = m_sim->localPlayerId();
    for (const net::PlayerState& state : snapshot.players) {
        Player& p = players[state.id];
        const bool alive = (state.flags & 1) != 0;
        const bool respawned = alive && !p.alive;
        if (p.id == local) {
            // Prediction owns the local position; correct it only when it drifts or on respawn.
            const float error = glm::distance(p.position, state.position);
            if (respawned || !alive || error > 1.2f) {
                p.character->teleport(state.position);
                p.position = p.previousPosition = state.position;
                p.velocity = state.velocity;
            }
            if (respawned) {
                p.yaw = state.yaw;
                p.pitch = state.pitch;
                p.viewReset = true;
            }
        } else {
            p.previousPosition = respawned ? state.position : p.position;
            p.position = state.position;
            p.velocity = state.velocity;
            p.yaw = state.yaw;
            p.pitch = state.pitch;
            p.onGround = (state.flags & 8) != 0;
            p.character->teleport(alive ? state.position : glm::vec3(0.0f, -500.0f - 5.0f * static_cast<float>(p.id), 0.0f));
        }
        p.alive = alive;
        p.isBot = (state.flags & 2) != 0;
        p.crouched = (state.flags & 4) != 0;
        p.aiming = (state.flags & 16) != 0;
        p.eyeHeight = state.eyeHeight;
        p.health = state.health;
        p.tier = state.tier;
        p.kills = state.kills;
        p.deaths = state.deaths;
        p.ammo = state.ammo;
        p.reloadTimer = state.reloadTimer;
        p.respawnTimer = state.respawnTimer;
        p.spawnProtection = state.spawnProtection;
        p.viewKick = state.viewKick;
        p.meleeAnimation = state.meleeAnimation;
    }
    m_replicatedProps = snapshot.props;
    m_snapshotAge = 0.0f;
}

void App::predictLocalFire(const PlayerCommand& command) {
    Player& me = m_sim->mutablePlayers()[static_cast<size_t>(m_sim->localPlayerId())];
    const WeaponDef& weapon = m_sim->weaponFor(me);
    m_predictedFireCooldown = std::max(0.0f, m_predictedFireCooldown - kTickDt);
    const uint32_t pressed = command.buttons & ~m_previousClientButtons;
    m_previousClientButtons = command.buttons;
    if (!me.alive || weapon.isMelee() || me.reloadTimer > 0.0f || me.ammo <= 0 || m_predictedFireCooldown > 0.0f) {
        return;
    }
    const bool wantsFire = weapon.automatic ? (command.buttons & kButtonFire) != 0 : (pressed & kButtonFire) != 0;
    if (!wantsFire) {
        return;
    }
    m_predictedFireCooldown = weapon.fireInterval();
    me.viewKick += glm::radians(weapon.recoilDegrees) * (me.aiming ? 0.6f : 1.0f);
    // Local feedback now; the server's copy of this event is skipped when it arrives.
    const std::vector<GameEvent> fired = {GameEvent{.type = GameEvent::Type::Fired, .a = me.id, .start = me.eyePosition(), .category = weapon.category}};
    m_effects.onEvents(fired, *m_sim, m_physics, localMuzzle());
    if (m_audioReady) m_audio.onEvents(fired, *m_sim);
}

void App::updateNetworkClient() {
    net::NetSession::ClientUpdate update = m_net.clientPoll();
    if (update.welcomed) {
        if (auto map = MapData::fromJson(update.mapJson)) {
            buildWorld(*map);
            m_sim->startClientMatch(m_world.map(), update.localId);
            m_hud.reset();
            m_clientReady = true;
            m_netTick = 0;
            m_walkPhase.assign(m_sim->players().size(), 0.0f);
            log::info("Joined as player {} on map '{}'", update.localId, map->name);
        }
    }
    if (!m_clientReady) {
        if (update.disconnected) leaveNetwork();
        return;
    }
    if (!update.playerInfo.empty()) {
        int count = 0;
        for (const auto& info : update.playerInfo) count = std::max(count, info.id + 1);
        m_sim->ensurePlayerCount(count);
        for (const auto& info : update.playerInfo) {
            Player& p = m_sim->mutablePlayers()[static_cast<size_t>(info.id)];
            p.name = info.name;
            p.isBot = info.isBot;
            p.look = info.look;
        }
    }
    if (update.snapshot) applySnapshot(*update.snapshot);
    if (!update.events.empty()) {
        // Our own shots were already played locally by predictLocalFire.
        const int local = m_sim->localPlayerId();
        std::erase_if(update.events, [local](const GameEvent& e) { return e.type == GameEvent::Type::Fired && e.a == local; });
        processEvents(update.events);
    }
    if (update.disconnected) leaveNetwork();
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
        m_window->setCursorCaptured(!open && !m_options.autoplay && !m_titleScreen);
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
        if (!m_options.recordFolder.empty()) dt = 1.0f / 60.0f; // Recording: real-time playback at 60 fps.
        last = frameStart;
        dt = std::min(dt, 0.1f);
        m_time += dt;
        if (dt > 0.0f) {
            m_fpsSmoothed = glm::mix(m_fpsSmoothed, 1.0f / dt, 0.05f);
            m_frameMs = glm::mix(m_frameMs, dt * 1000.0f, 0.05f);
        }
        ++m_frameCount;
        updateAutoPreset();
        if (m_titleScreen && m_options.pressPlayFrame > 0 && m_frameCount == m_options.pressPlayFrame) playFromTitle();

        m_input.beginFrame();
        m_window->pollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        if (m_shotFbo) {
            // Photo mode: the UI is laid out and rendered at the capture size.
            ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(m_options.shotSize.x), static_cast<float>(m_options.shotSize.y));
            ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        }
        ImGui::NewFrame();

        if (m_input.keyPressed(GLFW_KEY_F1) && !m_net.isClient()) {
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
        if (m_mode == Mode::Playing && m_titleScreen) {
            // Bots keep playing behind the title screen.
            const bool wasAutoplay = m_options.autoplay;
            m_options.autoplay = true;
            m_sim->players()[0].isBot = true;
            updatePlaying(dt);
            m_options.autoplay = wasAutoplay;
            m_effects.update(dt, m_physics);
        } else if (m_mode == Mode::Playing) {
            if (m_input.keyPressed(GLFW_KEY_ESCAPE)) setMenuOpen(!m_menuOpen);
            updatePlaying(dt);
            m_effects.update(dt, m_physics);
        } else {
            m_editor.setFov(m_settings.fov);
            if (m_editor.update(dt, m_input, *m_window, m_physics, framebuffer)) {
                m_world.setVegetationEnabled(!m_editor.isSculpting());
                buildWorld(m_editor.map());
                m_world.setVegetationEnabled(true);
            }
            if (m_editor.consumePlayRequest()) leaveEditor();
        }

        // --- Render -------------------------------------------------------------------
        RenderSettings frameSettings = m_settings.render;
        // Aim blur belongs to the first-person view only (not spectator, photo or title cameras).
        const bool firstPerson = !m_options.fixedCamera && m_options.followPlayer < 0 && !m_titleScreen;
        if (m_mode == Mode::Playing && m_settings.adsDepthOfField && firstPerson) {
            const Player& me = m_sim->players()[static_cast<size_t>(m_sim->localPlayerId())];
            const bool scoped = m_sim->weaponFor(me).scoped;
            frameSettings.depthOfField = scoped ? 0.0f : 0.85f * m_adsBlend;
            const glm::vec3 eye = me.eyePosition();
            const auto hit = m_physics.rayCast(eye, viewDirection(m_viewYaw, m_viewPitch), 300.0f, true);
            frameSettings.focusDistance = hit ? std::max(hit->distance, 2.0f) : 60.0f;
        }
        const auto tScene = Clock::now();
        m_renderer.beginFrame(frameSettings, framebuffer);
        CameraView camera;
        if (m_mode == Mode::Playing) {
            const float alpha = std::clamp(m_tickAccumulator / kTickDt, 0.0f, 1.0f);
            updateCameraFeel(dt);
            camera = playingCamera(alpha);
            m_lastCamera = camera;
            if (m_options.followPlayer >= 0 && m_options.followPlayer < static_cast<int>(m_sim->players().size())) {
                const Player& target = m_sim->players()[static_cast<size_t>(m_options.followPlayer)];
                const glm::vec3 feet = glm::mix(target.previousPosition, target.position, alpha);
                const glm::vec3 pivot = feet + glm::vec3(0.0f, 1.7f, 0.0f);
                glm::vec3 offset = -viewDirection(target.yaw + 0.5f, 0.0f) * 3.2f + glm::vec3(0.0f, 0.3f, 0.0f);
                // Keep the spectator camera out of trees and walls.
                for (float side : {0.5f, -0.5f, 1.4f, -1.4f}) {
                    const glm::vec3 candidate = -viewDirection(target.yaw + side, 0.0f) * 3.2f + glm::vec3(0.0f, 0.3f, 0.0f);
                    auto hit = m_physics.rayCast(pivot, glm::normalize(candidate), glm::length(candidate), true);
                    offset = hit ? glm::normalize(candidate) * std::max(hit->distance - 0.3f, 1.6f) : candidate;
                    if (!hit || hit->distance > 2.0f) break;
                }
                camera.position = pivot + offset;
                camera.view = glm::lookAt(camera.position, feet + glm::vec3(0.0f, 1.2f, 0.0f), glm::vec3(0, 1, 0));
                camera.fovYDegrees = 65.0f;
            }
            if (m_titleScreen) {
                // Very slow orbit above the map center, with a gentle height drift.
                const float angle = m_time * 0.015f;
                const float x = std::sin(angle) * 38.0f;
                const float z = std::cos(angle) * 38.0f;
                const glm::vec3 position(x, 9.0f + 0.6f * std::sin(m_time * 0.05f) + m_world.terrainHeight(x, z), z);
                camera.position = position;
                camera.view = glm::lookAt(position, glm::vec3(0.0f, m_world.terrainHeight(0.0f, 0.0f) + 2.0f, 0.0f), glm::vec3(0, 1, 0));
                camera.fovYDegrees = 70.0f;
            }
            if (m_options.fixedCamera) {
                camera.position = m_options.cameraPosition;
                camera.view = glm::lookAt(camera.position,
                                          camera.position + viewDirection(glm::radians(m_options.cameraYaw), glm::radians(m_options.cameraPitch)),
                                          glm::vec3(0, 1, 0));
                camera.fovYDegrees = m_settings.fov;
            }
            // Character tab: look at a preview soldier standing in front of the camera, right of the menu.
            const bool preview = m_characterPreviewShown && m_menuOpen && !m_titleScreen;
            Player previewSoldier;
            if (preview) {
                const Player& me = m_sim->players()[static_cast<size_t>(m_sim->localPlayerId())];
                const glm::vec3 forward = viewDirection(me.yaw, 0.0f);
                const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
                glm::vec3 feet = me.position + forward * 2.6f + right * 0.85f;
                if (auto ground = m_physics.rayCast(feet + glm::vec3(0, 2.0f, 0), glm::vec3(0, -1, 0), 6.0f)) feet.y = ground->position.y;
                m_previewTurn += dt * 0.5f;
                previewSoldier.id = me.id;
                previewSoldier.tier = me.tier;
                previewSoldier.color = me.color;
                previewSoldier.alive = true;
                previewSoldier.position = previewSoldier.previousPosition = feet;
                previewSoldier.velocity = glm::vec3(0.0f);
                previewSoldier.onGround = true;
                previewSoldier.eyeHeight = kEyeHeight;
                previewSoldier.yaw = me.yaw + glm::pi<float>() + 0.5f * std::sin(m_previewTurn);
                previewSoldier.pitch = 0.0f;
                previewSoldier.reloadTimer = previewSoldier.meleeAnimation = previewSoldier.spawnProtection = 0.0f;
                previewSoldier.look = m_settings.look;
                camera.position = me.position + glm::vec3(0.0f, 1.45f, 0.0f);
                camera.view = glm::lookAt(camera.position, feet + glm::vec3(0.0f, 1.0f, 0.0f) - right * 0.55f, glm::vec3(0, 1, 0));
                camera.fovYDegrees = 50.0f;
            }
            m_lodView = camera.position;
            m_lodFov = camera.fovYDegrees;
            m_world.submit(m_renderer, m_physics);
            submitPlayers(m_net.isClient() ? std::clamp(m_snapshotAge / (2.0f * kTickDt), 0.0f, 1.0f) : alpha,
                          m_options.fixedCamera || m_options.followPlayer >= 0);
            if (preview) submitSoldier(previewSoldier, 1.0f);
            m_effects.submit(m_renderer, m_physics);
            if (m_options.poseGallery && m_options.fixedCamera) {
                submitPoseGallery(m_options.cameraPosition, glm::radians(m_options.cameraYaw));
            }
            if (m_options.weaponGallery && m_options.fixedCamera) {
                // Debug: every ladder weapon in profile, two rows in front of the camera.
                const float yaw = glm::radians(m_options.cameraYaw);
                const glm::vec3 forward = viewDirection(yaw, 0.0f);
                const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
                const int count = m_weapons.tierCount();
                for (int i = 0; i < count; ++i) {
                    const Model* gun = m_models.getWeapon(m_sim->gunGame().weaponForTier(i).id);
                    if (!gun) continue;
                    const int row = i / 5, col = i % 5;
                    const glm::vec3 at = m_options.cameraPosition + forward * 1.7f + right * ((static_cast<float>(col) - 2.0f) * 0.62f) +
                                         glm::vec3(0.0f, 0.42f - static_cast<float>(row) * 0.27f, 0.0f);
                    // Muzzle (model -Z) to the right of the screen.
                    submitModel(*gun, glm::translate(glm::mat4(1.0f), at) *
                                          glm::rotate(glm::mat4(1.0f), yaw - glm::half_pi<float>(), glm::vec3(0, 1, 0)) *
                                          glm::scale(glm::mat4(1.0f), glm::vec3(0.5f)));
                }
            }
            if (m_options.lookGallery >= 0 && m_options.fixedCamera) {
                submitLookGallery(m_options.cameraPosition, glm::radians(m_options.cameraYaw), m_options.lookGallery);
            }
            if (!m_titleScreen && !preview) submitViewModel(dt);
            if (m_audioReady) {
                const glm::mat4 invView = glm::inverse(camera.view);
                m_audio.update(dt, *m_sim, camera.position, -glm::vec3(invView[2]));
                // Distance to the nearest water: sparse terrain samples around the listener.
                const TerrainData& terrain = m_world.map().terrain;
                float nearest = 1e9f;
                if (terrain.valid() && terrain.waterEnabled) {
                    for (int z = -10; z <= 10; ++z) {
                        for (int x = -10; x <= 10; ++x) {
                            const glm::vec2 p(camera.position.x + static_cast<float>(x) * 3.0f, camera.position.z + static_cast<float>(z) * 3.0f);
                            if (terrain.heightAt(p.x, p.y) < terrain.waterLevel) {
                                nearest = std::min(nearest, glm::length(p - glm::vec2(camera.position.x, camera.position.z)));
                            }
                        }
                    }
                }
                m_audio.setWaterProximity(1.0f - std::min(nearest / 30.0f, 1.0f));
            }
        } else {
            camera = m_editor.camera();
            m_world.submit(m_renderer, m_physics);
            m_editor.submit(m_renderer);
        }
        Environment environment = m_world.environment();
        environment.time = m_time;
        VegetationSettings vegetation;
        vegetation.density = m_settings.vegetationDensity;
        vegetation.distance = m_settings.vegetationDistance;
        m_world.submitVegetation(m_renderer, m_renderer.projection(camera) * camera.view, camera.position, vegetation);
        const auto tRender = Clock::now();
        m_renderer.render(camera, environment);
        const auto tUi = Clock::now();

        // --- UI -----------------------------------------------------------------------
        if (!m_options.hideUi) {
            if (m_mode == Mode::Playing && m_titleScreen) {
                drawTitleScreen();
                if (m_menuOpen) drawMenu();
            } else if (m_mode == Mode::Playing) {
                const Player& me = m_sim->players()[static_cast<size_t>(m_sim->localPlayerId())];
                const bool scoped = m_sim->weaponFor(me).scoped && m_adsBlend > 0.9f;
                m_hud.draw(*m_sim, dt, m_input.keyDown(GLFW_KEY_TAB) && !m_menuOpen, scoped);
                if (m_menuOpen) drawMenu();
            } else {
                m_editor.drawUi();
            }
            if (m_settings.showStats) drawStats(dt);
            if (m_net.isClient() && !m_clientReady) {
                ImDrawList* dl = ImGui::GetForegroundDrawList();
                const ImVec2 screen = ImGui::GetIO().DisplaySize;
                dl->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(0, 0, 0, 170));
                const std::string text = m_net.status() + "   (Esc: menú)";
                const ImVec2 size = m_fontBold->CalcTextSizeA(26.0f, FLT_MAX, 0.0f, text.c_str());
                dl->AddText(m_fontBold, 26.0f, ImVec2((screen.x - size.x) * 0.5f, screen.y * 0.45f), IM_COL32(240, 230, 200, 255), text.c_str());
            }
        }

        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, m_shotFbo);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // --record: every frame after warm-up to numbered PNGs (turn into a video with ffmpeg).
        if (!m_options.recordFolder.empty() && m_frameCount > 60) {
            const int index = m_frameCount - 60;
            if (index <= m_options.recordFrames) {
                std::filesystem::create_directories(m_options.recordFolder);
                saveScreenshot(std::filesystem::path(m_options.recordFolder) / std::format("frame_{:05d}.png", index), framebuffer.x,
                               framebuffer.y, m_shotFbo);
            } else {
                m_window->requestClose();
            }
        }
        if (!m_pendingScreenshot.empty()) {
            saveScreenshot(m_pendingScreenshot, framebuffer.x, framebuffer.y, m_shotFbo);
            log::info("Perf: {:.2f} ms/frame ({:.0f} FPS), {} draw calls, {} vegetation instances", m_frameMs, m_fpsSmoothed,
                      m_renderer.stats().drawCalls, m_world.vegetation().instanceCount());
            if (m_pendingScreenshot == m_options.screenshotPath) {
                m_window->requestClose();
            }
            m_pendingScreenshot.clear();
        }
        if (m_shotFbo) {
            // Photo mode renders offscreen at a fixed size: mirror it into the window (letterboxed).
            const glm::ivec2 window = m_window->framebufferSize();
            const float scale = std::min(static_cast<float>(window.x) / static_cast<float>(framebuffer.x),
                                         static_cast<float>(window.y) / static_cast<float>(framebuffer.y));
            const int w = static_cast<int>(static_cast<float>(framebuffer.x) * scale);
            const int h = static_cast<int>(static_cast<float>(framebuffer.y) * scale);
            const int x = (window.x - w) / 2;
            const int y = (window.y - h) / 2;
            glClearNamedFramebufferfv(0, GL_COLOR, 0, std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}.data());
            glBlitNamedFramebuffer(m_shotFbo, 0, 0, 0, framebuffer.x, framebuffer.y, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT,
                                   GL_LINEAR);
        }
        const auto tSwap = Clock::now();
        m_window->swapBuffers();
        const auto tEnd = Clock::now();
        {
            auto ms = [](auto a, auto b) { return std::chrono::duration<float, std::milli>(b - a).count(); };
            const std::array<float, kCpuSections> sample{ms(frameStart, tScene), ms(tScene, tRender), ms(tRender, tUi),
                                                         ms(tUi, tSwap), ms(tSwap, tEnd)};
            for (size_t i = 0; i < sample.size(); ++i) m_cpuMs[i] = glm::mix(m_cpuMs[i], sample[i], 0.05f);
            recordBench(dt, sample);
        }

        if (!m_settings.vsync && m_settings.maxFps > 0) {
            std::this_thread::sleep_until(frameStart + std::chrono::duration<double>(1.0 / m_settings.maxFps));
        }
    }
    return 0;
}

// --- Playing -------------------------------------------------------------------------------

void App::updatePlaying(float frameDt) {
    if (m_net.isServer()) {
        m_net.serverPoll(*m_sim, m_world.map());
        int humans = 0;
        for (const Player& p : m_sim->players()) humans += p.isBot ? 0 : 1;
        m_lan.announce(m_time, static_cast<uint16_t>(m_hostPort), m_playerName, m_world.map().name, humans,
                       m_weapons.rules().maxPlayers);
        m_net.updateRelayInfo(m_playerName, m_world.map().name, humans, m_weapons.rules().maxPlayers);
    }
    if (m_net.isClient()) {
        updateNetworkClient();
        m_snapshotAge += frameDt;
        if (!m_clientReady) return;
    }
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
    if (!m_net.isClient()) {
        // Online matches restart by themselves; local ones on Enter.
        if (matchOver && m_net.isServer()) {
            if (m_rematchTimer < 0.0f) m_rematchTimer = 10.0f;
            m_rematchTimer -= frameDt;
        }
        const bool enter = m_input.keyPressed(GLFW_KEY_ENTER) || m_input.keyPressed(GLFW_KEY_KP_ENTER);
        if (matchOver && !m_menuOpen && (enter || m_options.autoplay || (m_net.isServer() && m_rematchTimer <= 0.0f))) {
            restartMatch();
            return;
        }
        if (!m_menuOpen && m_input.keyPressed(GLFW_KEY_K)) m_sim->killSelf(m_sim->localPlayerId());
    }

    m_tickAccumulator += frameDt;
    int ticks = 0;
    std::vector<PlayerCommand> commands;
    while (m_tickAccumulator >= kTickDt && ticks < kMaxTicksPerFrame) {
        if (m_net.isClient()) {
            // Client: send the command, predict our own movement, keep props where the server says.
            const PlayerCommand command = buildLocalCommand();
            m_net.sendCommand(++m_netTick, command);
            m_physics.update(kTickDt);
            const auto bodies = m_world.dynamicBodies();
            for (size_t i = 0; i < bodies.size() && i < m_replicatedProps.size(); ++i) {
                m_physics.setTransform(bodies[i], m_replicatedProps[i].position, m_replicatedProps[i].rotation);
            }
            m_sim->predictMovement(m_sim->localPlayerId(), command);
            predictLocalFire(command);
        } else {
            m_sim->computeBotCommands(commands);
            if (!m_options.autoplay) {
                commands[static_cast<size_t>(m_sim->localPlayerId())] = buildLocalCommand();
            }
            if (m_net.isServer()) {
                for (const Player& p : m_sim->players()) {
                    if (const PlayerCommand* remote = m_net.remoteCommand(p.id); remote && !p.isBot) {
                        commands[static_cast<size_t>(p.id)] = *remote;
                        // Half the round trip plus the client's interpolation delay (two ticks).
                        const float rewindSeconds = m_net.playerRttMs(p.id) * 0.0005f + 2.0f * kTickDt;
                        m_sim->setRewindTicks(p.id, static_cast<int>(std::round(rewindSeconds / kTickDt)));
                    }
                }
            }
            m_sim->tick(commands);
            const std::vector<GameEvent> events = m_sim->takeEvents();
            m_net.broadcastEvents(events);
            processEvents(events);
            if (m_net.isServer() && (++m_netTick % 2) == 0) {
                m_net.broadcastSnapshot(makeSnapshot());
            }
        }
        m_tickAccumulator -= kTickDt;
        ++ticks;
    }
    if (ticks == kMaxTicksPerFrame) m_tickAccumulator = std::min(m_tickAccumulator, kTickDt);
    if (m_options.autoplay) {
        m_viewYaw = me.yaw;
        m_viewPitch = me.pitch;
    }

    const float adsTarget = me.alive && (me.aiming || m_options.forceAds) ? 1.0f : 0.0f;
    m_adsBlend += (adsTarget - m_adsBlend) * std::min(1.0f, frameDt * 14.0f);

    // Locomotion state for third-person models.
    m_walkPhase.resize(m_sim->players().size(), 0.0f);
    m_locomotion.resize(m_sim->players().size());
    for (const Player& p : m_sim->players()) {
        const glm::vec2 velocity(p.velocity.x, p.velocity.z);
        const float speed = glm::length(velocity);
        m_walkPhase[static_cast<size_t>(p.id)] += speed * frameDt * 2.3f; // Cube-model fallback.
        updateLocomotion(m_locomotion[static_cast<size_t>(p.id)], p, frameDt);
    }
}

void App::updateLocomotion(Locomotion& l, const Player& p, float dt) {
    const glm::vec2 velocity(p.velocity.x, p.velocity.z);
    const float speed = glm::length(velocity);
    const float rate = 1.0f - std::exp(-dt * 10.0f);
    l.speed += (speed - l.speed) * rate;
    const bool moving = p.onGround && l.speed > 0.35f;

    // Legs turn toward the direction of travel (up to 70 degrees), the torso keeps aiming;
    // moving backwards plays the cycle in reverse.
    float targetYaw = 0.0f;
    if (speed > 0.35f) {
        const glm::vec2 forward(-std::sin(p.yaw), -std::cos(p.yaw));
        const glm::vec2 right(std::cos(p.yaw), -std::sin(p.yaw));
        const float angle = std::atan2(glm::dot(velocity, right), glm::dot(velocity, forward));
        const float limit = glm::radians(70.0f);
        if (std::abs(angle) <= glm::radians(110.0f)) {
            l.direction = 1.0f;
            targetYaw = std::clamp(angle, -limit, limit);
        } else {
            l.direction = -1.0f;
            float back = angle - std::copysign(glm::pi<float>(), angle);
            targetYaw = std::clamp(back, -limit, limit);
        }
    }
    l.legYaw += (targetYaw - l.legYaw) * (1.0f - std::exp(-dt * 8.0f));

    // Stride per cycle (two steps) grows with speed, so feet do not slide.
    const float crouch = std::clamp((kEyeHeight - p.eyeHeight) / (kEyeHeight - kCrouchEyeHeight), 0.0f, 1.0f);
    const float standingStride = l.speed < 4.6f ? glm::mix(1.5f, 3.3f, glm::smoothstep(1.5f, 4.6f, l.speed))
                                                 : glm::mix(3.3f, 3.9f, glm::smoothstep(4.6f, 6.8f, l.speed));
    const float stride = glm::mix(standingStride, 1.3f, crouch);
    if (moving) l.phase = std::fmod(l.phase + l.direction * l.speed * dt / stride + 1.0f, 1.0f);

    const float approach = 1.0f - std::exp(-dt * 9.0f);
    l.moveWeight += ((moving ? glm::smoothstep(0.35f, 1.4f, l.speed) : 0.0f) - l.moveWeight) * approach;
    l.runWeight += (glm::smoothstep(2.6f, 4.2f, l.speed) - l.runWeight) * approach;
    l.airWeight += ((p.onGround ? 0.0f : 1.0f) - l.airWeight) * (1.0f - std::exp(-dt * 12.0f));
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
    glm::vec3 offset = glm::mix(hipOffset(body), adsOffset(body), m_adsBlend) + glm::vec3(0.0f, 0.0f, -body.z * 0.5f - 0.05f);
    if (const Model* model = const_cast<ModelLibrary&>(m_models).getWeapon(m_sim->weaponFor(me).id)) {
        const glm::vec3 hip(0.14f, -0.13f, -0.26f);
        const glm::vec3 ads(0.0f, -model->boundsMax().y + 0.004f, -0.2f);
        offset = glm::mix(hip, ads, m_adsBlend) + glm::vec3(0.0f, 0.035f, model->boundsMin().z - 0.03f);
    }
    return glm::vec3(glm::inverse(m_lastCamera.view) * glm::vec4(offset, 1.0f));
}

namespace {

struct SoldierRig {
    int idle = -1, walk = -1, run = -1, crouch = -1, crouchWalk = -1, jump = -1, reload = -1, knife = -1, death = -1,
        deathForward = -1;
    int hips = -1, spine = -1, chest = -1, hand = -1;
    std::vector<float> upperBody;

    explicit SoldierRig(const SkinnedModel& m)
        : idle(m.findClip("idle")), walk(m.findClip("walk")), run(m.findClip("run")), crouch(m.findClip("crouch")),
          crouchWalk(m.findClip("crouch_walk")), jump(m.findClip("jump")), reload(m.findClip("reload")),
          knife(m.findClip("knife")), death(m.findClip("death")), deathForward(m.findClip("death_forward")),
          hips(m.findJoint("hips")), spine(m.findJoint("spine")), chest(m.findJoint("chest")), hand(m.findJoint("hand.R")),
          upperBody(m.jointMask(m.findJoint("spine"))) {}
};

// Weight envelope for one-shot upper-body clips: fades in and out over `edge` of the progress.
float clipEnvelope(float progress, float edge) {
    return std::clamp(std::min(progress, 1.0f - progress) / edge, 0.0f, 1.0f);
}

} // namespace

void App::submitSoldier(const Player& p, float alpha) {
    const SkinnedModel& sk = soldierModel(p.look);
    static const SoldierRig rig(m_soldiers[0]);
    const glm::vec3 feet = glm::mix(p.previousPosition, p.position, alpha);
    const float speed = glm::length(glm::vec2(p.velocity.x, p.velocity.z));
    const float crouch = std::clamp((kEyeHeight - p.eyeHeight) / (kEyeHeight - kCrouchEyeHeight), 0.0f, 1.0f);
    const WeaponDef& weapon = m_sim->weaponFor(p);
    Locomotion fallback;
    const Locomotion& l = static_cast<size_t>(p.id) < m_locomotion.size() ? m_locomotion[static_cast<size_t>(p.id)] : fallback;
    (void)speed;

    auto clipAt = [&](int clip, float phase) {
        SkinnedModel::Pose out = sk.restPose();
        sk.sample(clip, phase * sk.clipDuration(clip) * 0.999f, out);
        return out;
    };
    // Standing: idle -> walk -> run (all cycles share the phase: 0 = right heel strike).
    SkinnedModel::Pose pose = sk.restPose();
    sk.sample(rig.idle, m_time + static_cast<float>(p.id) * 0.37f, pose);
    if (l.moveWeight > 0.01f) {
        SkinnedModel::Pose move = clipAt(rig.walk >= 0 ? rig.walk : rig.run, l.phase);
        if (rig.walk >= 0 && l.runWeight > 0.01f) SkinnedModel::blend(move, clipAt(rig.run, l.phase), l.runWeight);
        SkinnedModel::blend(pose, move, l.moveWeight);
    }
    // Crouched: crouch pose -> crouch walk.
    if (crouch > 0.01f) {
        SkinnedModel::Pose crouched = sk.restPose();
        sk.sample(rig.crouch, 0.0f, crouched);
        if (rig.crouchWalk >= 0 && l.moveWeight > 0.01f) {
            SkinnedModel::blend(crouched, clipAt(rig.crouchWalk, l.phase), l.moveWeight);
        }
        SkinnedModel::blend(pose, crouched, crouch);
    }
    // In the air: tucked legs.
    if (rig.jump >= 0 && l.airWeight > 0.01f) SkinnedModel::blend(pose, clipAt(rig.jump, 0.0f), l.airWeight * 0.9f);
    // Upper-body actions layered over locomotion.
    if (p.reloadTimer > 0.0f && weapon.reloadSeconds > 0.0f && rig.reload >= 0) {
        const float progress = std::clamp(1.0f - p.reloadTimer / weapon.reloadSeconds, 0.0f, 0.999f);
        SkinnedModel::Pose reload = sk.restPose();
        sk.sample(rig.reload, progress * sk.clipDuration(rig.reload), reload);
        SkinnedModel::blend(pose, reload, clipEnvelope(progress, 0.08f), rig.upperBody);
    }
    if (p.meleeAnimation > 0.0f && rig.knife >= 0) {
        const float progress = std::clamp(1.0f - p.meleeAnimation / 0.3f, 0.0f, 0.999f);
        SkinnedModel::Pose knife = sk.restPose();
        sk.sample(rig.knife, progress * sk.clipDuration(rig.knife), knife);
        SkinnedModel::blend(pose, knife, clipEnvelope(progress, 0.1f), rig.upperBody);
    }
    // Legs face the direction of travel; spine and chest turn back so the upper body keeps aiming.
    if (std::abs(l.legYaw) > 1e-3f) {
        sk.rotateJoint(pose, rig.hips, glm::vec3(0, 1, 0), -l.legYaw);
        sk.rotateJoint(pose, rig.spine, glm::vec3(0, 1, 0), l.legYaw * 0.5f);
        sk.rotateJoint(pose, rig.chest, glm::vec3(0, 1, 0), l.legYaw * 0.5f);
    }
    // Aim: bend the spine and chest toward the view pitch (positive pitch leans back = looks up).
    sk.rotateJoint(pose, rig.spine, glm::vec3(1, 0, 0), p.pitch * 0.45f);
    sk.rotateJoint(pose, rig.chest, glm::vec3(1, 0, 0), p.pitch * 0.45f);

    const glm::mat4 model = glm::translate(glm::mat4(1.0f), feet) * glm::rotate(glm::mat4(1.0f), p.yaw, glm::vec3(0, 1, 0));
    const glm::vec3 tint = World::toLinear(p.color);
    const std::vector<glm::mat4>& globals = drawSoldierPose(pose, model, p.look, tint, p.spawnProtection > 0.0f);
    // Weapon in the right hand, aligned with the aim direction.
    if (const Model* gun = m_models.getWeapon(weapon.id); gun && rig.hand >= 0) {
        const glm::vec3 handPosition(globals[static_cast<size_t>(rig.hand)][3]);
        const glm::mat4 gunMatrix = model * glm::translate(glm::mat4(1.0f), handPosition) *
                                    glm::rotate(glm::mat4(1.0f), p.pitch, glm::vec3(1, 0, 0)) *
                                    glm::translate(glm::mat4(1.0f), glm::vec3(-0.02f, -0.02f, 0.03f));
        submitModel(*gun, gunMatrix);
    }
}

const std::vector<glm::mat4>& App::drawSoldierPose(const SkinnedModel::Pose& pose, const glm::mat4& model,
                                                   const CharacterLook& look, const glm::vec3& tint, bool highlight) {
    m_skinBuffers.emplace_back();
    std::vector<glm::mat4>& skin = m_skinBuffers.back();
    const size_t gender = look.female() && m_soldierFemale ? 1 : 0;
    const SkinnedModel& sk = m_soldiers[gender];
    // Level of detail from the on-screen size: distance scaled by the zoom (scopes keep detail).
    const float distance = glm::length(glm::vec3(model[3]) - m_lodView) * (m_lodFov / 80.0f);
    const int lod = m_options.forceLod >= 0 ? m_options.forceLod : distance > 30.0f ? 2 : distance > 10.0f ? 1 : 0;
    std::vector<SoldierPiece>& pieces = m_soldierPieces[gender];
    sk.evaluate(pose, m_soldierGlobals, skin);
    if (pieces.size() != sk.parts().size()) {
        // Which garments hide each skin part, written next to the model by gen_human.py.
        std::map<std::string, std::vector<std::string>> hiddenBy;
        try {
            std::ifstream file(fs::resolve(gender ? "assets/models/characters/soldier_f.pieces.json"
                                                  : "assets/models/characters/soldier_m.pieces.json"));
            if (file) {
                const nlohmann::json root = nlohmann::json::parse(file);
                for (const auto& [name, items] : root.at("hidden_by").items()) {
                    hiddenBy[name] = items.get<std::vector<std::string>>();
                }
            }
        } catch (const std::exception& e) {
            log::warn("Soldier pieces file: {}", e.what());
        }
        pieces.clear();
        for (const SkinnedModel::Part& part : sk.parts()) {
            SoldierPiece piece = parseSoldierPiece(part.name);
            // LOD pieces ("x_skin_03_LOD1") share the garment list of their LOD0 part.
            const std::string base = part.name.substr(0, part.name.rfind("_LOD") == std::string::npos ? part.name.size()
                                                                                                  : part.name.rfind("_LOD"));
            if (auto it = hiddenBy.find(base); it != hiddenBy.end()) piece.hiddenBy = it->second;
            pieces.push_back(std::move(piece));
        }
    }
    for (size_t i = 0; i < sk.parts().size(); ++i) {
        const SkinnedModel::Part& part = sk.parts()[i];
        if (!soldierPieceVisible(pieces[i], look, lod)) continue;
        DrawItem item;
        item.mesh = &part.mesh;
        item.model = model;
        item.material = part.material;
        applyLookMaterial(item.material, part.materialName, look, tint, m_textures);
        if (highlight) item.material.emissive = item.material.albedo * 0.35f;
        item.bones = skin.data();
        item.boneCount = static_cast<int>(skin.size());
        item.shadowCascades = 2; // Characters are small: skip the far cascades.
        m_renderer.submit(item);
    }
    return m_soldierGlobals;
}

void App::submitModel(const Model& model, const glm::mat4& transform) {
    for (const ModelPart& part : model.lod(0)) {
        DrawItem item;
        item.mesh = &part.mesh;
        item.material = part.material;
        item.model = transform;
        item.shadowCascades = 2;
        m_renderer.submit(item);
    }
}

void App::submitCorpses() {
    if (!m_soldierReady) return;
    static const SoldierRig rig(m_soldiers[0]);
    for (const Effects::Corpse& c : m_effects.corpses()) {
        const SkinnedModel& sk = soldierModel(c.look);
        const int clip = c.forward ? rig.deathForward : rig.death;
        const float duration = sk.clipDuration(clip);
        SkinnedModel::Pose pose = sk.restPose();
        sk.sample(clip, std::min(c.age, duration * 0.999f), pose);
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), c.feet) * glm::rotate(glm::mat4(1.0f), c.yaw, glm::vec3(0, 1, 0));
        drawSoldierPose(pose, model, c.look, World::toLinear(c.color) * 0.8f, false);
        // The weapon drops beside the body, lying on its side.
        const Model* gun = m_models.getWeapon(m_sim->gunGame().weaponForTier(c.weaponTier).id);
        if (gun && c.age > 0.35f) {
            const glm::vec3 side = c.forward ? glm::vec3(0.45f, 0.0f, -0.6f) : glm::vec3(0.5f, 0.0f, 0.3f);
            const float lift = std::max(std::abs(gun->boundsMin().x), std::abs(gun->boundsMax().x));
            const glm::mat4 gunMatrix = model * glm::translate(glm::mat4(1.0f), side + glm::vec3(0.0f, lift, 0.0f)) *
                                        glm::rotate(glm::mat4(1.0f), 1.1f, glm::vec3(0, 1, 0)) *
                                        glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0, 0, 1));
            submitModel(*gun, gunMatrix);
        }
    }
}

void App::submitPoseGallery(const glm::vec3& eye, float yaw) {
    if (!m_soldierReady) return;
    struct Entry {
        const char* clip;
        float progress; // 0..1 of the clip.
    };
    static const Entry entries[] = {{"walk", 0.0f},  {"walk", 0.125f}, {"walk", 0.25f},  {"walk", 0.375f},
                                    {"walk", 0.5f},  {"run", 0.0f},    {"run", 0.125f},  {"run", 0.25f},
                                    {"run", 0.375f}, {"crouch_walk", 0.0f}, {"crouch_walk", 0.25f}, {"jump", 0.0f}};
    const glm::vec3 forward = viewDirection(yaw, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    const int count = static_cast<int>(std::size(entries));
    for (int i = 0; i < count; ++i) {
        const Entry& e = entries[i];
        const CharacterLook look = CharacterLook::random(static_cast<uint32_t>(i));
        const SkinnedModel& sk = soldierModel(look);
        const int clip = sk.findClip(e.clip);
        SkinnedModel::Pose pose = sk.restPose();
        sk.sample(clip, e.progress * sk.clipDuration(clip), pose);
        glm::vec3 feet = eye + forward * 7.0f + right * ((static_cast<float>(i) - (count - 1) * 0.5f) * 1.05f);
        feet.y = m_world.terrainHeight(feet.x, feet.z);
        // Profile view (facing right on screen) so gait poses read.
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), feet) *
                                glm::rotate(glm::mat4(1.0f), yaw - glm::half_pi<float>(), glm::vec3(0, 1, 0));
        drawSoldierPose(pose, model, look, glm::vec3(0.6f, 0.1f, 0.08f), false);
    }
}

void App::submitLookGallery(const glm::vec3& eye, float yaw, int seed) {
    if (!m_soldierReady) return;
    const glm::vec3 forward = viewDirection(yaw, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    for (int i = 0; i < 5; ++i) {
        const CharacterLook look = CharacterLook::random(static_cast<uint32_t>(seed * 5 + i));
        const SkinnedModel& sk = soldierModel(look);
        const int idle = sk.findClip("idle");
        SkinnedModel::Pose pose = sk.restPose();
        sk.sample(idle, 0.0f, pose);
        glm::vec3 feet = eye + forward * 2.8f + right * ((static_cast<float>(i) - 2.0f) * 0.8f);
        feet.y = m_world.terrainHeight(feet.x, feet.z);
        const float turn = glm::pi<float>() + (static_cast<float>(i) - 2.0f) * 0.25f;
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), feet) * glm::rotate(glm::mat4(1.0f), yaw + turn, glm::vec3(0, 1, 0));
        drawSoldierPose(pose, model, look, glm::vec3(0.6f, 0.1f, 0.08f), false);
    }
}

void App::submitPlayers(float alpha, bool includeLocal) {
    m_skinBuffers.clear();
    m_skinBuffers.reserve(m_sim->players().size() + m_effects.corpses().size());
    submitCorpses();
    for (const Player& p : m_sim->players()) {
        if ((p.id == m_sim->localPlayerId() && !includeLocal) || !p.alive) continue;
        if (m_soldierReady) {
            submitSoldier(p, alpha);
            continue;
        }
        PlayerPose pose;
        pose.feet = glm::mix(p.previousPosition, p.position, alpha);
        pose.rotation = glm::angleAxis(p.yaw, glm::vec3(0, 1, 0));
        pose.pitch = p.pitch;
        pose.walkPhase = m_walkPhase[static_cast<size_t>(p.id)];
        pose.walkAmount = p.onGround ? std::min(glm::length(glm::vec2(p.velocity.x, p.velocity.z)) / 5.0f, 1.0f) : 0.3f;
        pose.eyeHeight = p.eyeHeight;
        pose.color = World::toLinear(p.color);
        pose.weapon = &m_sim->weaponFor(p);
        pose.weaponModel = m_models.getWeapon(pose.weapon->id);
        pose.emissiveHighlight = p.spawnProtection > 0.0f;
        submitPlayerModel(m_renderer, pose);
    }
}

bool App::submitViewArms(const glm::mat4& gunBase, const Model& gun, const WeaponDef& weapon) {
    const Player& me = m_sim->players()[static_cast<size_t>(m_sim->localPlayerId())];
    const SkinnedModel& sk = soldierModel(me.look);
    struct Joints {
        int upper[2], fore[2], hand[2], head;
        explicit Joints(const SkinnedModel& m)
            : upper{m.findJoint("upperarm.R"), m.findJoint("upperarm.L")}, fore{m.findJoint("forearm.R"), m.findJoint("forearm.L")},
              hand{m.findJoint("hand.R"), m.findJoint("hand.L")}, head(m.findJoint("head")) {}
    };
    static const Joints j(m_soldiers[0]);
    if (j.head < 0 || j.upper[0] < 0 || j.hand[1] < 0) return false;

    SkinnedModel::Pose pose = sk.restPose();
    std::vector<glm::mat4> globals, skin;
    sk.evaluate(pose, globals, skin);
    auto position = [&](int joint) { return glm::vec3(globals[static_cast<size_t>(joint)][3]); };
    // Body where it belongs: its eyes at the camera (model faces -Z like the view).
    const glm::vec3 eye = position(j.head) + glm::vec3(0.0f, 0.1f, -0.08f);
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.02f, 0.02f) - eye);
    const glm::mat4 toModel = glm::inverse(model);

    // How the rest pose holds a rifle: the weapon sits at the right wrist with the model's axes.
    // Keep each hand's placement and orientation relative to the weapon, and solve the arms (IK)
    // so the hands follow the weapon wherever the view puts it.
    const glm::vec3 gripOffset(-0.02f, -0.02f, 0.03f);
    const glm::mat4 restGun = glm::translate(glm::mat4(1.0f), position(j.hand[0]) + gripOffset);
    const glm::mat4 gunModel = toModel * gunBase;
    const glm::quat gunRotation = glm::quat_cast(glm::mat3(gunModel));
    glm::vec3 handInGun[2];
    glm::quat handRest[2];
    for (int side = 0; side < 2; ++side) {
        handInGun[side] = glm::vec3(glm::inverse(restGun) * glm::vec4(position(j.hand[side]), 1.0f));
        handRest[side] = glm::quat_cast(glm::mat3(globals[static_cast<size_t>(j.hand[side])]));
    }
    // The support hand slides along the barrel to this weapon's forend (pistols: cups the grip hand).
    const float front = gun.boundsMin().z;
    const bool oneHanded = weapon.isMelee() || weapon.category == WeaponCategory::Pistol;
    if (oneHanded) handInGun[1] = handInGun[0] + glm::vec3(-0.05f, -0.03f, 0.02f);
    else handInGun[1] = glm::vec3(-0.01f, -0.045f, front * 0.42f); // Under the barrel.
    const glm::vec3 poles[2] = {glm::vec3(0.8f, -1.0f, 0.3f), glm::vec3(-0.6f, -1.0f, 0.1f)};
    for (int side = 0; side < 2; ++side) {
        const glm::vec3 target(gunModel * glm::vec4(handInGun[side], 1.0f));
        sk.evaluate(pose, globals, skin);
        const glm::vec3 s = position(j.upper[side]);
        const glm::vec3 e = position(j.fore[side]);
        const glm::vec3 w = position(j.hand[side]);
        const float a = glm::length(e - s), b = glm::length(w - e);
        glm::vec3 d = target - s;
        const float c = std::clamp(glm::length(d), 0.05f, a + b - 0.002f);
        d = glm::normalize(d);
        const float cosA = std::clamp((a * a + c * c - b * b) / (2.0f * a * c), -1.0f, 1.0f);
        glm::vec3 pole = poles[side] - d * glm::dot(poles[side], d);
        pole = glm::length(pole) > 1e-4f ? glm::normalize(pole) : glm::vec3(0, -1, 0);
        const glm::vec3 elbow = s + d * (a * cosA) + pole * (a * std::sqrt(1.0f - cosA * cosA));
        sk.aimJoint(pose, j.upper[side], j.fore[side], elbow);
        sk.aimJoint(pose, j.fore[side], j.hand[side], s + d * c);
        // Fingers along the weapon: the hand bone runs along its local +Y (wrist to knuckles).
        sk.evaluate(pose, globals, skin);
        const glm::vec3 forward = glm::normalize(glm::vec3(gunModel * glm::vec4(0, 0, -1, 0)));
        const glm::vec3 right = glm::normalize(glm::vec3(gunModel * glm::vec4(1, 0, 0, 0)));
        const glm::vec3 up = glm::normalize(glm::vec3(gunModel * glm::vec4(0, 1, 0, 0)));
        const glm::vec3 fingers = side == 0 ? glm::normalize(forward * 0.55f - up * 0.65f - right * 0.2f)
                                            : glm::normalize(forward * 0.7f + right * 0.6f);
        const glm::vec3 current = glm::normalize(glm::mat3(globals[static_cast<size_t>(j.hand[side])]) * glm::vec3(0, 1, 0));
        const glm::vec3 axis = glm::cross(current, fingers);
        if (glm::length(axis) > 1e-4f) {
            sk.rotateJoint(pose, j.hand[side], glm::normalize(axis), std::atan2(glm::length(axis), glm::dot(current, fingers)));
        }
        (void)handRest;
        (void)gunRotation;
    }
    sk.evaluate(pose, globals, skin);
    // Only arm-weighted geometry is drawn (the shader discards the rest), cut cleanly at the shoulders.
    unsigned armMask = 0;
    for (int side = 0; side < 2; ++side) {
        for (int joint : {j.upper[side], j.fore[side], j.hand[side]}) {
            if (joint >= 0 && joint < 32) armMask |= 1u << static_cast<unsigned>(joint);
        }
    }
    m_skinBuffers.push_back(skin);
    const std::vector<glm::mat4>& palette = m_skinBuffers.back();

    const size_t gender = me.look.female() && m_soldierFemale ? 1 : 0;
    std::vector<SoldierPiece>& pieces = m_soldierPieces[gender];
    if (pieces.size() != sk.parts().size()) return false; // Built on the first third-person draw.
    for (size_t i = 0; i < sk.parts().size(); ++i) {
        const SoldierPiece& piece = pieces[i];
        if (piece.slot == "eyes" || piece.slot == "eyebrows" || piece.slot == "eyelashes" || piece.slot == "hair" ||
            piece.slot == "headwear" || piece.slot == "eyewear" || piece.slot == "face" || piece.slot == "shoes") {
            continue;
        }
        if (!soldierPieceVisible(piece, me.look, 0)) continue;
        const SkinnedModel::Part& part = sk.parts()[i];
        DrawItem item;
        item.mesh = &part.mesh;
        item.model = model;
        item.material = part.material;
        applyLookMaterial(item.material, part.materialName, me.look, World::toLinear(me.color), m_textures);
        item.bones = palette.data();
        item.boneCount = static_cast<int>(palette.size());
        item.jointMask = armMask;
        item.castShadow = false;
        m_renderer.submitViewModel(item);
    }
    return true;
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

    const Model* weaponModel = m_models.getWeapon(weapon.id);
    glm::vec3 hip = hipOffset(body);
    glm::vec3 ads = adsOffset(body);
    if (weaponModel) {
        // Held at arm's length, low on the right: the weapon reads as held, not glued to the face.
        hip = glm::vec3(0.15f, -0.17f, -0.34f);
        ads = glm::vec3(0.0f, -weaponModel->boundsMax().y + 0.004f, -0.24f);
    }
    glm::vec3 position = glm::mix(hip, ads, m_adsBlend) + bob;
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
    // Slight toe-in toward the crosshair at the hip.
    const float toeIn = 0.05f * (1.0f - m_adsBlend);
    const glm::mat4 base = glm::translate(glm::mat4(1.0f), position) * glm::rotate(glm::mat4(1.0f), toeIn, glm::vec3(0, 1, 0)) *
                           glm::rotate(glm::mat4(1.0f), pitch, glm::vec3(1, 0, 0)) * glm::rotate(glm::mat4(1.0f), roll, glm::vec3(0, 0, 1));

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
    const glm::vec3 sleeve(0.05f, 0.06f, 0.03f); // Olive drab jacket.
    if (weaponModel) {
        for (const ModelPart& modelPart : weaponModel->lod(0)) {
            DrawItem item;
            item.mesh = &modelPart.mesh;
            item.material = modelPart.material;
            item.model = base;
            item.castShadow = false;
            m_renderer.submitViewModel(item);
        }
        if (m_soldierReady && submitViewArms(base, *weaponModel, weapon)) return;
        // Fallback: box hands. Right hand on the grip, left hand under the barrel (except pistols and the knife).
        part({0.0f, -0.05f, 0.015f}, {0.05f, 0.06f, 0.07f}, skin, 0.0f, 0.7f);
        part({0.03f, -0.1f, 0.2f}, {0.08f, 0.08f, 0.32f}, sleeve, 0.0f, 0.9f);
        const float length = weaponModel->boundsMax().z - weaponModel->boundsMin().z;
        if (!weapon.isMelee() && weapon.category != WeaponCategory::Pistol) {
            const float z = weaponModel->boundsMin().z * 0.45f;
            part({-0.005f, -0.015f, z}, {0.05f, 0.045f, 0.08f}, skin, 0.0f, 0.7f);
            part({-0.11f, -0.08f, z + 0.18f}, {0.08f, 0.08f, 0.4f}, sleeve, 0.0f, 0.9f);
        }
        (void)length;
        return;
    }
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
    m_characterPreviewShown = m_characterPreview;
    m_characterPreview = false;
    // With the character preview the menu moves left so the soldier is visible on the right.
    const float menuX = m_characterPreviewShown && !m_titleScreen ? std::min(screen.x * 0.5f, 40.0f + 290.0f) : screen.x * 0.5f;
    ImGui::SetNextWindowPos(ImVec2(menuX, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
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

    if (m_titleScreen && ImGui::Button("Volver al título", ImVec2(-1, 0))) setMenuOpen(false);
    if (ImGui::BeginTabBar("menu_tabs")) {
        const int forcedTab = m_menuTab;
        m_menuTab = -1;
        if (ImGui::BeginTabItem("Gráficos", nullptr, forcedTab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            RenderSettings& r = m_settings.render;
            ImGui::TextDisabled("Preajustes:");
            ImGui::SameLine();
            const char* presets[] = {"Batata", "Media", "Alta", "Ultra"};
            for (int i = 0; i < 4; ++i) {
                if (i > 0) ImGui::SameLine();
                if (ImGui::SmallButton(presets[i])) applyGraphicsPreset(i);
            }
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
            ImGui::Checkbox("Luces volumétricas (niebla con sombras)", &r.godRays);
            ImGui::Checkbox("Reflejos en el agua (SSR)", &r.ssr);
            ImGui::Checkbox("Sombras de contacto", &r.contactShadows);
            ImGui::Checkbox("Profundidad de campo al apuntar", &m_settings.adsDepthOfField);
            if (ImGui::Checkbox("Sincronización vertical (VSync)", &m_settings.vsync)) m_window->setVsync(m_settings.vsync);
            ImGui::BeginDisabled(m_settings.vsync);
            ImGui::SliderInt("FPS máximos", &m_settings.maxFps, 0, 360, m_settings.maxFps == 0 ? "Sin límite" : "%d");
            ImGui::EndDisabled();
            ImGui::SliderFloat("Exposición", &r.exposure, 0.5f, 2.0f, "%.2f");
            int density = static_cast<int>(std::round(m_settings.vegetationDensity * 100.0f));
            if (ImGui::SliderInt("Densidad de hierba", &density, 20, 100, "%d%%")) m_settings.vegetationDensity = static_cast<float>(density) / 100.0f;
            ImGui::SliderFloat("Distancia de hierba", &m_settings.vegetationDistance, 30.0f, 150.0f, "%.0f m");
            ImGui::TextDisabled("Árboles, rocas, arbustos y helechos se ven igual en todas las calidades.");
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
        if (ImGui::BeginTabItem("Personaje")) {
            drawCharacterTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Multijugador", nullptr, forcedTab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            ImGui::InputText("Nombre", &m_playerName);
            ImGui::Separator();
            ImGui::TextUnformatted("Estado:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s",
                               m_net.role() == net::NetSession::Role::None ? "Partida local" : m_net.status().c_str());
            if (m_net.isClient()) ImGui::Text("Ping: %.0f ms", m_net.pingMs());
            ImGui::Separator();
            ImGui::SeparatorText("Crear partida (servidor)");
            ImGui::InputInt("Puerto", &m_hostPort);
            ImGui::TextDisabled("Usa el mapa actual. Los jugadores ocupan el lugar de los bots.");
            if (ImGui::Button("Crear partida", ImVec2(-1, 0))) {
                startHosting(std::clamp(m_hostPort, 1024, 65535));
                setMenuOpen(false);
            }
            if (ImGui::Button("Crear partida y publicarla por internet", ImVec2(-1, 0))) {
                startHosting(std::clamp(m_hostPort, 1024, 65535), true);
            }
            ImGui::SeparatorText("Partidas en la red local");
            m_lan.listen(m_time);
            if (m_lan.servers().empty()) {
                ImGui::TextDisabled("Buscando partidas... (los servidores se anuncian cada segundo)");
            }
            for (const net::LanDiscovery::Server& server : m_lan.servers()) {
                ImGui::PushID((server.address + ":" + std::to_string(server.port)).c_str());
                ImGui::Text("%s  ·  %s  ·  %d/%d  (%s:%d)", server.name.c_str(), server.map.c_str(), server.players, server.maxPlayers,
                            server.address.c_str(), server.port);
                ImGui::SameLine();
                if (ImGui::SmallButton("Unirse")) {
                    joinServer(server.address, server.port);
                    setMenuOpen(false);
                }
                ImGui::PopID();
            }
            ImGui::SeparatorText("Unirse por IP");
            ImGui::InputText("Dirección IP", &m_joinAddress);
            ImGui::InputInt("Puerto##join", &m_joinPort);
            if (ImGui::Button("Unirse", ImVec2(-1, 0))) {
                joinServer(m_joinAddress, std::clamp(m_joinPort, 1, 65535));
                setMenuOpen(false);
            }
            if (m_net.role() != net::NetSession::Role::None && ImGui::Button("Desconectar", ImVec2(-1, 0))) {
                leaveNetwork();
            }
            ImGui::TextDisabled("Conexión directa por IP (abre el puerto UDP en tu router o usa una VPN LAN).");
            drawInternetSection();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Partida")) {
            ImGui::TextUnformatted("Modo: Gun Game (todos contra todos)");
            ImGui::SliderInt("Bots", &m_settings.botCount, 0, m_weapons.rules().maxPlayers - 1);
            ImGui::TextDisabled("Los bots se aplican al reiniciar la partida.");
            if (ImGui::BeginCombo("Mapa", mapLabel(m_mapFile).c_str())) {
                for (const std::string& file : m_mapFiles) {
                    if (ImGui::Selectable(file.c_str(), file == m_mapFile)) {
                        loadMap(file);
                        setMenuOpen(false);
                    }
                }
                if (ImGui::Selectable("Procedural (nuevo al azar)")) {
                    loadMap(randomProceduralMap());
                    setMenuOpen(false);
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("Semilla", &m_proceduralSeed);
            ImGui::SameLine();
            if (ImGui::Button("Generar mapa procedural")) {
                loadMap("procedural:" + std::to_string(std::max(0, m_proceduralSeed)));
                setMenuOpen(false);
            }
            ImGui::TextDisabled("Cada semilla genera un mapa distinto: terreno, agua, estructuras y bosque.");
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

void App::applyGraphicsPreset(int level) {
    RenderSettings& r = m_settings.render;
    switch (level) {
    case 0: // Batata: integrated GPUs.
        r.shadowQuality = 1; r.msaaSamples = 0; r.renderScale = 0.7f; r.ssao = false; r.bloom = false; r.godRays = false;
        r.ssr = false; r.contactShadows = false; m_settings.adsDepthOfField = false;
        m_settings.vegetationDensity = 0.3f; m_settings.vegetationDistance = 40.0f;
        break;
    case 1: // Media.
        r.shadowQuality = 2; r.msaaSamples = 2; r.renderScale = 0.9f; r.ssao = true; r.bloom = true; r.godRays = false;
        r.ssr = false; r.contactShadows = false; m_settings.adsDepthOfField = true;
        m_settings.vegetationDensity = 0.6f; m_settings.vegetationDistance = 70.0f;
        break;
    case 2: // Alta.
        r.shadowQuality = 3; r.msaaSamples = 4; r.renderScale = 1.0f; r.ssao = true; r.bloom = true; r.godRays = true;
        r.ssr = true; r.contactShadows = true; m_settings.adsDepthOfField = true;
        m_settings.vegetationDensity = 1.0f; m_settings.vegetationDistance = 90.0f;
        break;
    default: // Ultra.
        r.shadowQuality = 4; r.msaaSamples = 8; r.renderScale = 1.0f; r.ssao = true; r.bloom = true; r.godRays = true;
        r.ssr = true; r.contactShadows = true; m_settings.adsDepthOfField = true;
        m_settings.vegetationDensity = 1.0f; m_settings.vegetationDistance = 150.0f;
        break;
    }
}

void App::updateAutoPreset() {
    // First launch only: run "Alta" for a few seconds, then step down (or up) from the GPU time.
    if (!m_autoPresetPending || m_frameCount < 120) return;
    m_autoPresetSamples.push_back(m_renderer.stats().gpuTotalMs);
    if (m_autoPresetSamples.size() < 180) return;
    std::sort(m_autoPresetSamples.begin(), m_autoPresetSamples.end());
    const float gpu = m_autoPresetSamples[m_autoPresetSamples.size() / 2];
    const int level = gpu > 14.0f ? 0 : gpu > 8.0f ? 1 : gpu > 3.5f ? 2 : 3;
    const char* names[] = {"Batata", "Media", "Alta", "Ultra"};
    log::info("Graphics: first launch, GPU {:.1f} ms on Alta -> preset {}", gpu, names[level]);
    if (level != 2) applyGraphicsPreset(level);
    m_autoPresetPending = false;
    m_settings.save(settingsPath());
}

std::string App::mapLabel(const std::string& file) {
    if (file.rfind("procedural:", 0) == 0) return "Procedural #" + file.substr(11);
    return file;
}

std::string App::randomProceduralMap() {
    const auto seed = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000);
    return "procedural:" + std::to_string(seed);
}

void App::playFromTitle() {
    m_titleScreen = false;
    m_sim->players()[0].isBot = false;
    restartMatch();
    m_window->setCursorCaptured(true);
}

void App::drawTitleScreen() {
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(screen.x * 0.45f, screen.y), IM_COL32(0, 0, 0, 200), IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 200));
    dl->AddText(m_fontBold, 96.0f, ImVec2(80.0f, screen.y * 0.18f), IM_COL32(235, 225, 200, 255), "ORIGON");
    dl->AddText(m_fontRegular, 22.0f, ImVec2(86.0f, screen.y * 0.18f + 104.0f), IM_COL32(190, 200, 170, 230),
                "Gun Game  ·  Bosque  ·  Hasta 10 jugadores");
    if (m_menuOpen) return;
    ImGui::SetNextWindowPos(ImVec2(80.0f, screen.y * 0.42f));
    ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##title", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(m_fontBold, 22.0f);
    const ImVec2 size(-1, 46);
    if (ImGui::Button("Jugar contra bots", size)) playFromTitle();
    if (ImGui::Button("Multijugador", size)) {
        m_menuTab = 1;
        setMenuOpen(true);
    }
    if (ImGui::Button("Editor de mapas", size)) {
        m_titleScreen = false;
        m_sim->players()[0].isBot = false;
        enterEditor();
    }
    if (ImGui::Button("Configuración", size)) {
        m_menuTab = 0;
        setMenuOpen(true);
    }
    if (ImGui::Button("Salir", size)) m_window->requestClose();
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##map", ("Mapa: " + mapLabel(m_mapFile)).c_str())) {
        for (const std::string& file : m_mapFiles) {
            if (ImGui::Selectable(file.c_str(), file == m_mapFile)) loadMap(file);
        }
        if (ImGui::Selectable("Procedural (nuevo al azar)")) loadMap(randomProceduralMap());
        ImGui::EndCombo();
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void App::drawStats(float) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const RenderStats& stats = m_renderer.stats();
    std::string text = std::format("{:.0f} FPS  {:.2f} ms\nDraw calls: {}  Cascadas: {}\n\nCPU (ms)\n", m_fpsSmoothed, m_frameMs,
                                   stats.drawCalls, stats.shadowCascades);
    for (int i = 0; i < kCpuSections; ++i) text += std::format("  {:<10} {:5.2f}\n", kCpuSectionNames[i], m_cpuMs[static_cast<size_t>(i)]);
    text += std::format("\nGPU {:.2f} ms\n", stats.gpuTotalMs);
    for (size_t i = 0; i < stats.gpuMs.size(); ++i) text += std::format("  {:<10} {:5.2f}\n", kGpuSectionNames[i], stats.gpuMs[i]);
    dl->AddRectFilled(ImVec2(8, 8), ImVec2(230, 330), IM_COL32(0, 0, 0, 160), 4.0f);
    dl->AddText(m_fontRegular, 15.0f, ImVec2(14, 12), IM_COL32(220, 230, 210, 255), text.c_str());
}

void App::recordBench(float dt, const std::array<float, kCpuSections>& cpu) {
    if (m_options.benchSeconds <= 0.0f) return;
    m_benchClock += dt;
    if (m_benchClock < 2.0f) return; // Warm-up: shader compiles, first texture uploads.
    m_benchFrames.push_back(dt * 1000.0f);
    for (int i = 0; i < kCpuSections; ++i) m_benchCpu[static_cast<size_t>(i)] += cpu[static_cast<size_t>(i)];
    const RenderStats& stats = m_renderer.stats();
    for (size_t i = 0; i < stats.gpuMs.size(); ++i) m_benchGpu[i] += stats.gpuMs[i];
    m_benchGpuTotal += stats.gpuTotalMs;
    if (m_benchClock < 2.0f + m_options.benchSeconds) return;

    std::vector<float> sorted = m_benchFrames;
    std::sort(sorted.begin(), sorted.end());
    const float n = static_cast<float>(sorted.size());
    float sum = 0.0f;
    for (float f : sorted) sum += f;
    const float p99 = sorted[static_cast<size_t>(n * 0.99f)];
    log::info("Bench: {} frames, avg {:.2f} ms ({:.0f} FPS), 1% worst {:.2f} ms, max {:.2f} ms, draw calls {}",
              sorted.size(), sum / n, 1000.0f * n / sum, p99, sorted.back(), stats.drawCalls);
    std::string cpuText, gpuText;
    for (int i = 0; i < kCpuSections; ++i) cpuText += std::format(" {} {:.2f}", kCpuSectionNames[i], m_benchCpu[static_cast<size_t>(i)] / n);
    for (size_t i = 0; i < stats.gpuMs.size(); ++i) gpuText += std::format(" {} {:.2f}", kGpuSectionNames[i], m_benchGpu[i] / n);
    log::info("Bench CPU ms:{}", cpuText);
    log::info("Bench GPU ms: total {:.2f} |{}", m_benchGpuTotal / n, gpuText);
    m_window->requestClose();
}

} // namespace origon
