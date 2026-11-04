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
    void setFonts(ImFont* regular, ImFont* bold) {
        m_regular = regular;
        m_bold = bold;
    }
    void reset();
    void onEvents(const std::vector<GameEvent>& events, const Simulation& sim);
    void draw(const Simulation& sim, float dt, bool showScoreboard, bool aimingScoped);

private:
    struct FeedEntry {
        std::string text;
        float age = 0.0f;
        bool involvesLocal = false;
    };
    struct Banner {
        std::string text;
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
    void pushBanner(const std::string& text, unsigned color, float duration = 2.5f);

    ImFont* m_regular = nullptr;
    ImFont* m_bold = nullptr;
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
};

} // namespace origon
