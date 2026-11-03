// Headless tests for the Gun Game rules and the simulation loop (no window, no GL).
#include "core/FileSystem.h"
#include "physics/PhysicsWorld.h"
#include "sim/GunGame.h"
#include "sim/Simulation.h"
#include "sim/Weapons.h"
#include "world/MapData.h"
#include "world/World.h"

#include <cstdio>
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

} // namespace

int main() {
    WeaponDatabase weapons;
    check(weapons.load("data/weapons.json", "data/gungame_ladder.json"), "weapon data loads");
    check(weapons.tierCount() == 20, "ladder has 20 tiers");
    testRules(weapons);
    testFullMatch(weapons);
    std::printf("%s (%d failures)\n", g_failures == 0 ? "ALL PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
