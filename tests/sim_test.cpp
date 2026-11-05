// Headless tests for the Gun Game rules and the simulation loop (no window, no GL).
#include "core/FileSystem.h"
#include "physics/PhysicsWorld.h"
#include "sim/GunGame.h"
#include "sim/Simulation.h"
#include "sim/Weapons.h"
#include "world/MapData.h"
#include "world/MapGenerator.h"
#include "world/World.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <cstdlib>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "[ ok ]" : "[FAIL]", what);
    if (!condition) {
        ++g_failures;
    }
}

using namespace origon;

void testRules(const WeaponDatabase& weapons) {
    GunGame game(weapons);
    game.reset();
    std::vector<Player> players(2);
    players[0].id = 0;
    players[1].id = 1;
    std::vector<GameEvent> events;

    game.onKill(&players[0], players[1], KillCause::Weapon, events);
    check(players[0].tier == 1, "weapon kill promotes the killer");
    check(players[0].kills == 1, "weapon kill counts a kill");

    players[1].tier = 5;
    game.onKill(&players[0], players[1], KillCause::Melee, events);
    check(players[1].tier == 4, "melee kill demotes the victim (humiliation)");
    check(players[0].tier == 1, "melee kill does not promote the killer by default");

    players[1].tier = 0;
    game.onKill(&players[0], players[1], KillCause::Melee, events);
    check(players[1].tier == 0, "demotion is clamped at tier 1");

    players[0].tier = 3;
    game.onKill(nullptr, players[0], KillCause::Suicide, events);
    check(players[0].tier == 2, "suicide demotes the player");

    players[0].tier = 3;
    game.onKill(nullptr, players[0], KillCause::World, events);
    check(players[0].tier == 2, "world death (fall) demotes the player");

    players[0].tier = game.finalTier() - 1;
    game.onKill(&players[0], players[1], KillCause::Weapon, events);
    check(players[0].tier == game.finalTier(), "reaching the final tier");
    check(game.weaponFor(players[0]).isMelee(), "final tier weapon is the knife");
    check(game.phase() == GunGame::Phase::Playing, "reaching the final tier does not win yet");

    game.onKill(&players[0], players[1], KillCause::Melee, events);
    check(game.phase() == GunGame::Phase::Finished, "knife kill on the final tier wins");
    check(game.winnerId() == 0, "winner is the final tier killer");
}

void testFullMatch(const WeaponDatabase& weapons) {
    auto map = MapData::load(fs::resolve("maps/graybox.json"));
    check(map.has_value(), "graybox map loads");
    if (!map) {
        return;
    }
    PhysicsWorld physics;
    World world;
    world.build(*map, physics);

    Simulation sim(physics, weapons);
    sim.startMatch(*map, 9);
    check(sim.players().size() == 10, "match holds 10 players");
    sim.players()[0].isBot = true; // Let the AI drive every player.

    int kills = 0;
    int humiliations = 0;
    int promotions = 0;
    std::vector<PlayerCommand> commands;
    const int maxTicks = static_cast<int>((weapons.rules().timeLimitSeconds + 5.0f) * kTickRate);
    int tick = 0;
    for (; tick < maxTicks && sim.gunGame().phase() == GunGame::Phase::Playing; ++tick) {
        sim.computeBotCommands(commands);
        sim.tick(commands);
        for (const GameEvent& e : sim.takeEvents()) {
            if (e.type == GameEvent::Type::Kill) ++kills;
            if (e.type == GameEvent::Type::Promoted) ++promotions;
            if (e.type == GameEvent::Type::Demoted && e.cause == KillCause::Melee) ++humiliations;
        }
    }

    std::printf("       match ended after %.1f s: %d kills, %d promotions, %d humiliations\n",
                static_cast<float>(tick) / kTickRate, kills, promotions, humiliations);
    for (const Player* p : GunGame::standings(sim.players())) {
        std::printf("       %-12s tier %2d  kills %3d  deaths %3d  weapon %s\n", p->name.c_str(), p->tier + 1,
                    p->kills, p->deaths, sim.weaponFor(*p).name.c_str());
    }
    check(sim.gunGame().phase() == GunGame::Phase::Finished, "match finishes (victory or time limit)");
    check(sim.gunGame().winnerId() >= 0 && sim.gunGame().winnerId() < 10, "match has a valid winner");
    check(kills > 20, "bots fight (more than 20 kills)");
    bool positionsValid = true;
    for (const Player& p : sim.players()) {
        if (p.alive && (p.position.y < -1.0f || p.position.y > 20.0f)) {
            positionsValid = false;
        }
    }
    check(positionsValid, "living players stay inside the map");
}

// Leaning and lying down: eye offsets, stance toggles and getting up.
void testStances(const WeaponDatabase& weapons) {
    auto map = MapData::load(fs::resolve("maps/graybox.json"));
    if (!map) return;
    PhysicsWorld physics;
    World world;
    world.build(*map, physics);
    Simulation sim(physics, weapons);
    sim.startMatch(*map, 0);
    std::vector<PlayerCommand> commands(1);
    auto run = [&](uint32_t buttons, int ticks) {
        for (int i = 0; i < ticks; ++i) {
            commands[0].buttons = buttons;
            sim.tick(commands);
            sim.takeEvents();
        }
    };
    run(0, 90); // Spawn and settle on the ground.
    const Player& p = sim.players()[0];
    check(p.alive && p.onGround, "stance test player stands on the ground");
    const glm::vec3 right(std::cos(p.yaw), 0.0f, -std::sin(p.yaw));

    run(kButtonLeanRight, 40);
    check(glm::dot(p.eyeOffset, right) > 0.2f, "leaning right moves the eye to the right");
    run(kButtonLeanLeft, 60);
    check(glm::dot(p.eyeOffset, right) < -0.2f, "leaning left moves the eye to the left");
    run(0, 60);
    check(std::abs(glm::dot(p.eyeOffset, right)) < 0.02f, "releasing the lean centers the eye");

    run(kButtonProne, 2);
    run(0, 90);
    check(p.prone && p.eyeHeight < 0.45f, "prone toggles on and lowers the eye");
    run(kButtonLeanRight, 30);
    check(std::abs(glm::dot(p.eyeOffset, right)) < 0.02f, "no leaning while prone");
    run(kButtonJump, 2);
    run(0, 90);
    check(!p.prone && p.eyeHeight > 1.5f, "jump gets up from prone");
}

// Procedural maps: deterministic per seed, valid spawns on dry ground, playable headless.
void testProceduralMaps(const WeaponDatabase& weapons) {
    for (uint32_t seed : {1u, 7u, 42u, 1234u}) {
        const MapData a = generateProceduralMap(seed);
        const MapData b = generateProceduralMap(seed);
        check(a.toJson() == b.toJson(), "procedural map is deterministic");
        check(a.spawns.size() == 12, "procedural map has 12 spawns");
        check(a.blocks.size() > 20, "procedural map has structures");
        bool dry = true;
        for (const SpawnData& spawn : a.spawns) {
            if (a.terrain.waterEnabled && a.terrain.heightAt(spawn.position.x, spawn.position.z) < a.terrain.waterLevel + 0.3f) dry = false;
        }
        check(dry, "procedural spawns are above water");
    }
    check(generateProceduralMap(1).toJson() != generateProceduralMap(2).toJson(), "different seeds give different maps");

    const MapData map = generateProceduralMap(7);
    PhysicsWorld physics;
    ModelLibrary library(nullptr);
    World world;
    world.setHeadless(true);
    world.setModelLibrary(&library);
    world.build(map, physics);
    Simulation sim(physics, weapons);
    sim.startMatch(world.map(), 9);
    sim.players()[0].isBot = true;
    std::vector<PlayerCommand> commands;
    int kills = 0, tick = 0;
    const int maxTicks = static_cast<int>(weapons.rules().timeLimitSeconds * kTickRate) + 300;
    for (; tick < maxTicks && sim.gunGame().phase() == GunGame::Phase::Playing; ++tick) {
        sim.computeBotCommands(commands);
        sim.tick(commands);
        for (const GameEvent& e : sim.takeEvents()) {
            if (e.type == GameEvent::Type::Kill) ++kills;
        }
    }
    std::printf("       procedural #7 match: %.1f s, %d kills\n", static_cast<float>(tick) / kTickRate, kills);
    check(kills > 20, "bots fight on a procedural map");
}

// Full bot match on a generated outdoor map: terrain, vegetation collision and structures.
void testOutdoorMatch(const WeaponDatabase& weapons, const char* name) {
    auto map = MapData::load(fs::resolve("maps") / (std::string(name) + ".json"));
    check(map.has_value(), "outdoor map loads");
    if (!map) {
        return;
    }
    PhysicsWorld physics;
    ModelLibrary library(nullptr); // Geometry only: collision meshes without GL.
    World world;
    world.setHeadless(true);
    world.setModelLibrary(&library);
    world.build(*map, physics);
    check(world.vegetation().instanceCount() > 1000, "vegetation scattered headless");

    Simulation sim(physics, weapons);
    sim.startMatch(world.map(), 9);
    sim.players()[0].isBot = true;
    std::vector<PlayerCommand> commands;
    int kills = 0;
    int tick = 0;
    const int maxTicks = static_cast<int>(weapons.rules().timeLimitSeconds * kTickRate) + 300;
    for (; tick < maxTicks && sim.gunGame().phase() == GunGame::Phase::Playing; ++tick) {
        sim.computeBotCommands(commands);
        sim.tick(commands);
        for (const GameEvent& e : sim.takeEvents()) {
            if (e.type == GameEvent::Type::Kill) ++kills;
        }
    }
    std::printf("       %s match: %.1f s, %d kills, %zu vegetation instances\n", name, static_cast<float>(tick) / kTickRate,
                kills, world.vegetation().instanceCount());
    check(sim.gunGame().phase() == GunGame::Phase::Finished, "outdoor match finishes");
    check(kills > 20, "bots fight outdoors");
    bool aboveGround = true;
    for (const Player& p : sim.players()) {
        if (p.alive && p.position.y < world.terrainHeight(p.position.x, p.position.z) - 1.0f) aboveGround = false;
    }
    check(aboveGround, "living players stay on the terrain");
}

// City map: generated scenery (collision from the chunks' _COL meshes). Skipped when the chunks have not
// been generated (tools/blender/gen_plaza.py; they are not committed).
void testCityMatch(const WeaponDatabase& weapons) {
    auto map = MapData::load(fs::resolve("maps/plaza_de_mayo.json"));
    if (!map || map->scenery.empty() || !std::filesystem::exists(fs::resolve("assets/models/" + map->scenery.front().model + ".glb"))) {
        std::printf("       plaza_de_mayo: scenery not generated, skipped\n");
        return;
    }
    PhysicsWorld physics;
    ModelLibrary library(nullptr);
    World world;
    world.setHeadless(true);
    world.setModelLibrary(&library);
    world.build(*map, physics);
    Simulation sim(physics, weapons);
    sim.startMatch(world.map(), 9);
    sim.players()[0].isBot = true;
    std::vector<PlayerCommand> commands;
    int kills = 0;
    int tick = 0;
    bool grounded = true, inside = true;
    const int maxTicks = static_cast<int>(weapons.rules().timeLimitSeconds * kTickRate) + 300;
    for (; tick < maxTicks && sim.gunGame().phase() == GunGame::Phase::Playing; ++tick) {
        sim.computeBotCommands(commands);
        sim.tick(commands);
        for (const GameEvent& e : sim.takeEvents()) {
            if (e.type == GameEvent::Type::Kill) ++kills;
        }
        for (const Player& p : sim.players()) {
            if (!p.alive) continue;
            if (p.position.y < -1.0f) grounded = false;
            if (std::abs(p.position.x) > 300.0f || std::abs(p.position.z) > 260.0f) inside = false;
        }
    }
    std::printf("       plaza_de_mayo match: %.1f s, %d kills\n", static_cast<float>(tick) / kTickRate, kills);
    check(sim.gunGame().phase() == GunGame::Phase::Finished, "city match finishes");
    check(kills > 20, "bots fight in the city");
    check(grounded, "nobody falls through the city ground");
    check(inside, "the barriers keep everyone inside the map");
}

} // namespace

int main() {
    WeaponDatabase weapons;
    check(weapons.load("data/weapons.json", "data/gungame_ladder.json"), "weapon data loads");
    check(weapons.tierCount() == 20, "ladder has 20 tiers");
    testRules(weapons);
    testStances(weapons);
    testFullMatch(weapons);
    testOutdoorMatch(weapons, "bosque");
    testOutdoorMatch(weapons, "aserradero");
    testProceduralMaps(weapons);
    testCityMatch(weapons);
    std::printf("%s (%d failures)\n", g_failures == 0 ? "ALL PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
