#pragma once

#include "sim/SimTypes.h"

#include <glm/vec3.hpp>

#include <deque>
#include <string>
#include <vector>

struct ImFont;

namespace origon {

class Simulation;

// In-game HUD. All player-facing text is Spanish.
class Hud {
public:
    void reset();
    void onEvents(const std::vector<GameEvent>& events, const Simulation& sim);
    void draw(const Simulation& sim, float dt, bool showScoreboard, bool aimingScoped);

private:
    struct FeedEntry {
        std::string killer;  // Empty: a death without a killer (falls, suicide) or a notice.
        std::string weapon;
        std::string victim;
        bool killerLocal = false, victimLocal = false;
        float age = 0.0f;
    };
    struct Banner {
        std::string title;  // Big tracked uppercase line.
        std::string detail; // Smaller line under it.
        float age = 0.0f;
        float duration = 2.5f;
        unsigned color = 0xFFFFFFFF;
    };

    void drawCrosshair(const Simulation& sim);
    void drawStatus(const Simulation& sim);
    void drawFeed(float dt);
    void drawBanners(float dt);
    void drawScoreboard(const Simulation& sim, bool matchOver);
    void drawDeathScreen(const Simulation& sim);
    void pushBanner(const std::string& title, const std::string& detail, unsigned color, float duration = 2.5f);
    void drawConfirmations(float dt);
    std::deque<FeedEntry> m_feed;
    std::deque<Banner> m_banners;
    float m_hitMarker = 0.0f;
    bool m_hitMarkerHead = false;
    float m_damageFlash = 0.0f;
    struct DamageMarker {
        glm::vec3 source;
        float age;
    };
    std::vector<DamageMarker> m_damageMarkers;
    void drawDamageMarkers(const Simulation& sim, float dt);
    int m_lastKillerId = -1;
    std::string m_lastKillerWeapon;
    struct Confirmation {
        std::string text;
        float age = 0.0f;
    };
    std::deque<Confirmation> m_confirmations; // "Eliminado X" notes under the crosshair.
};

} // namespace origon
