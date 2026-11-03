#include "ui/Hud.h"

#include "sim/Simulation.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace origon {
namespace {

constexpr ImU32 kWhite = IM_COL32(240, 240, 235, 255);
constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 170);
constexpr ImU32 kGold = IM_COL32(255, 205, 80, 255);
constexpr ImU32 kRed = IM_COL32(235, 80, 70, 255);
constexpr ImU32 kGreen = IM_COL32(120, 220, 120, 255);
constexpr ImU32 kGrey = IM_COL32(170, 170, 165, 255);

ImU32 withAlpha(ImU32 color, float alpha) {
    const auto a = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF));
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

void text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const std::string& s) {
    dl->AddText(font, size, ImVec2(pos.x + 2, pos.y + 2), withAlpha(kShadow, ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f), s.c_str());
    dl->AddText(font, size, pos, color, s.c_str());
}

ImVec2 measure(ImFont* font, float size, const std::string& s) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str());
}

void textCentered(ImDrawList* dl, ImFont* font, float size, ImVec2 center, ImU32 color, const std::string& s) {
    const ImVec2 extent = measure(font, size, s);
    text(dl, font, size, ImVec2(center.x - extent.x * 0.5f, center.y - extent.y * 0.5f), color, s);
}

std::string playerName(const Simulation& sim, int id) {
    const auto& players = sim.players();
    if (id < 0 || id >= static_cast<int>(players.size())) {
        return "El mundo";
    }
    return players[static_cast<size_t>(id)].name;
}

} // namespace

void Hud::reset() {
    m_feed.clear();
    m_banners.clear();
    m_hitMarker = 0.0f;
    m_damageFlash = 0.0f;
    m_lastKillerId = -1;
}

void Hud::pushBanner(const std::string& message, unsigned color, float duration) {
    m_banners.push_back({message, 0.0f, duration, color});
    while (m_banners.size() > 3) {
        m_banners.pop_front();
    }
}

void Hud::onEvents(const std::vector<GameEvent>& events, const Simulation& sim) {
    const int local = sim.localPlayerId();
    const int tiers = sim.gunGame().finalTier() + 1;
    for (const GameEvent& e : events) {
        switch (e.type) {
        case GameEvent::Type::Kill: {
            std::string line;
            if (e.a < 0 || e.a == e.b) {
                line = std::format("{} murió", playerName(sim, e.b));
            } else {
                line = std::format("{}  [{}]  {}", playerName(sim, e.a), e.weaponName, playerName(sim, e.b));
            }
            m_feed.push_back({line, 0.0f, e.a == local || e.b == local});
            if (m_feed.size() > 6) m_feed.pop_front();
            if (e.b == local) m_lastKillerId = e.a;
            if (e.a == local && e.b != local) {
                pushBanner(std::format("Eliminaste a {}", playerName(sim, e.b)), kWhite, 1.6f);
            }
            break;
        }
        case GameEvent::Type::Promoted:
            if (e.a == local) {
                pushBanner(std::format("¡Subiste de nivel! Arma: {}  ({}/{})", e.weaponName, e.tier + 1, tiers), kGreen);
            }
            break;
        case GameEvent::Type::Demoted:
            if (e.a == local) {
                if (e.cause == KillCause::Melee) {
                    pushBanner(std::format("¡Humillado! Bajaste al nivel {}", e.tier + 1), kRed, 3.0f);
                } else {
                    pushBanner(std::format("Suicidio: bajaste al nivel {}", e.tier + 1), kRed, 3.0f);
                }
            } else if (e.cause == KillCause::Melee) {
                m_feed.push_back({std::format("{} fue humillado (nivel {})", playerName(sim, e.a), e.tier + 1), 0.0f, false});
            }
            break;
        case GameEvent::Type::FinalTier:
            if (e.a == local) {
                pushBanner("¡Último nivel: cuchillo!", kGold, 3.0f);
            } else {
                pushBanner(std::format("¡{} llegó al cuchillo!", playerName(sim, e.a)), kGold, 2.5f);
            }
            break;
        case GameEvent::Type::Victory:
            break;
        case GameEvent::Type::TimeUp:
            pushBanner("Tiempo agotado", kGold, 3.0f);
            break;
        case GameEvent::Type::Hit:
            if (e.a == local) {
                m_hitMarker = 0.18f;
                m_hitMarkerHead = e.headshot;
            }
            if (e.b == local) {
                m_damageFlash = 0.35f;
            }
            break;
        default:
            break;
        }
    }
}

void Hud::draw(const Simulation& sim, float dt, bool showScoreboard, bool aimingScoped) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const Player& me = sim.players()[static_cast<size_t>(sim.localPlayerId())];
    const bool matchOver = sim.gunGame().phase() == GunGame::Phase::Finished;

    if (aimingScoped && me.alive) {
        const ImVec2 c(screen.x * 0.5f, screen.y * 0.5f);
        const float r = screen.y * 0.45f;
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(c.x - r, screen.y), IM_COL32(0, 0, 0, 255));
        dl->AddRectFilled(ImVec2(c.x + r, 0), screen, IM_COL32(0, 0, 0, 255));
        dl->AddCircle(c, r + screen.y * 0.15f, IM_COL32(0, 0, 0, 255), 96, screen.y * 0.3f);
        dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), IM_COL32(0, 0, 0, 230), 2.0f);
        dl->AddLine(ImVec2(c.x, c.y), ImVec2(c.x, c.y + r), IM_COL32(0, 0, 0, 230), 3.0f);
    }

    if (m_damageFlash > 0.0f) {
        dl->AddRectFilled(ImVec2(0, 0), screen, withAlpha(IM_COL32(160, 0, 0, 255), m_damageFlash * 0.6f));
        m_damageFlash = std::max(0.0f, m_damageFlash - dt);
    }

    if (me.alive && !matchOver) {
        if (!aimingScoped) {
            drawCrosshair(sim);
        }
        drawStatus(sim);
    }

    // Match clock and leader.
    const float timeLeft = sim.gunGame().timeLeft();
    const int minutes = static_cast<int>(timeLeft) / 60;
    const int seconds = static_cast<int>(timeLeft) % 60;
    textCentered(dl, m_bold, 30.0f, ImVec2(screen.x * 0.5f, 28.0f), timeLeft < 60.0f ? kRed : kWhite,
                 std::format("{:02}:{:02}", minutes, seconds));
    const auto order = GunGame::standings(sim.players());
    if (!order.empty()) {
        const Player* leader = order.front();
        textCentered(dl, m_regular, 18.0f, ImVec2(screen.x * 0.5f, 56.0f), kGrey,
                     std::format("Líder: {}  (nivel {})", leader->name, leader->tier + 1));
    }

    drawFeed(dt);
    drawBanners(dt);

    if (!me.alive && !matchOver) {
        drawDeathScreen(sim);
    }
    if (matchOver) {
        const int winner = sim.gunGame().winnerId();
        dl->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(0, 0, 0, 120));
        textCentered(dl, m_bold, 56.0f, ImVec2(screen.x * 0.5f, screen.y * 0.18f), kGold,
                     std::format("¡{} gana la partida!", playerName(sim, winner)));
        drawScoreboard(sim, true);
        textCentered(dl, m_regular, 22.0f, ImVec2(screen.x * 0.5f, screen.y * 0.88f), kWhite,
                     "Pulsa Enter para jugar otra vez");
    } else if (showScoreboard) {
        drawScoreboard(sim, false);
    }

    if (m_hitMarker > 0.0f) {
        m_hitMarker = std::max(0.0f, m_hitMarker - dt);
    }
}

void Hud::drawCrosshair(const Simulation& sim) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const ImVec2 c(std::floor(screen.x * 0.5f), std::floor(screen.y * 0.5f));
    const Player& me = sim.players()[static_cast<size_t>(sim.localPlayerId())];
    const WeaponDef& weapon = sim.weaponFor(me);

    if (weapon.isMelee()) {
        dl->AddCircleFilled(c, 3.0f, kWhite);
    } else if (!me.aiming) {
        const float speed = std::sqrt(me.velocity.x * me.velocity.x + me.velocity.z * me.velocity.z);
        const float gap = 6.0f + weapon.spreadHipDegrees * 4.0f + speed * 1.5f + me.viewKick * 300.0f;
        const float len = 9.0f;
        const ImU32 col = IM_COL32(255, 255, 255, 220);
        dl->AddLine(ImVec2(c.x - gap - len, c.y), ImVec2(c.x - gap, c.y), col, 2.0f);
        dl->AddLine(ImVec2(c.x + gap, c.y), ImVec2(c.x + gap + len, c.y), col, 2.0f);
        dl->AddLine(ImVec2(c.x, c.y - gap - len), ImVec2(c.x, c.y - gap), col, 2.0f);
        dl->AddLine(ImVec2(c.x, c.y + gap), ImVec2(c.x, c.y + gap + len), col, 2.0f);
    } else {
        dl->AddCircleFilled(c, 2.0f, IM_COL32(255, 255, 255, 200));
    }

    if (m_hitMarker > 0.0f) {
        const ImU32 col = m_hitMarkerHead ? kRed : kWhite;
        const float a = 6.0f;
        const float b = 13.0f;
        dl->AddLine(ImVec2(c.x - b, c.y - b), ImVec2(c.x - a, c.y - a), col, 2.5f);
        dl->AddLine(ImVec2(c.x + b, c.y - b), ImVec2(c.x + a, c.y - a), col, 2.5f);
        dl->AddLine(ImVec2(c.x - b, c.y + b), ImVec2(c.x - a, c.y + a), col, 2.5f);
        dl->AddLine(ImVec2(c.x + b, c.y + b), ImVec2(c.x + a, c.y + a), col, 2.5f);
    }
}

void Hud::drawStatus(const Simulation& sim) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const Player& me = sim.players()[static_cast<size_t>(sim.localPlayerId())];
    const WeaponDef& weapon = sim.weaponFor(me);
    const int tiers = sim.gunGame().finalTier() + 1;

    // Health (bottom left).
    const ImVec2 barPos(40.0f, screen.y - 60.0f);
    const ImVec2 barSize(260.0f, 14.0f);
    const float fraction = std::clamp(me.health / kMaxHealth, 0.0f, 1.0f);
    dl->AddRectFilled(barPos, ImVec2(barPos.x + barSize.x, barPos.y + barSize.y), IM_COL32(0, 0, 0, 140), 3.0f);
    dl->AddRectFilled(barPos, ImVec2(barPos.x + barSize.x * fraction, barPos.y + barSize.y),
                      fraction > 0.35f ? IM_COL32(230, 230, 220, 230) : kRed, 3.0f);
    text(dl, m_regular, 18.0f, ImVec2(barPos.x, barPos.y - 26.0f), kWhite,
         std::format("Salud {}", static_cast<int>(std::ceil(me.health))));
    if (me.spawnProtection > 0.0f) {
        text(dl, m_regular, 16.0f, ImVec2(barPos.x, barPos.y - 50.0f), kGreen, "Protección de aparición");
    }

    // Weapon and tier (bottom right).
    const float right = screen.x - 40.0f;
    auto rightText = [&](ImFont* font, float size, float y, ImU32 col, const std::string& s) {
        const ImVec2 extent = measure(font, size, s);
        text(dl, font, size, ImVec2(right - extent.x, y), col, s);
    };
    rightText(m_bold, 30.0f, screen.y - 120.0f, kWhite, weapon.name);
    if (!weapon.isMelee()) {
        const std::string ammo = me.reloadTimer > 0.0f ? "Recargando..." : std::format("{} / ∞", me.ammo);
        rightText(m_bold, 26.0f, screen.y - 84.0f, me.ammo == 0 ? kRed : kWhite, ammo);
    }
    rightText(m_regular, 18.0f, screen.y - 50.0f, kGold, std::format("Nivel {} / {}", me.tier + 1, tiers));
    if (me.tier + 1 < tiers) {
        rightText(m_regular, 16.0f, screen.y - 28.0f, kGrey,
                  std::format("Siguiente: {}", sim.gunGame().weaponForTier(me.tier + 1).name));
    }
}

void Hud::drawFeed(float dt) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    float y = 20.0f;
    for (FeedEntry& entry : m_feed) {
        entry.age += dt;
        const float alpha = std::clamp(6.0f - entry.age, 0.0f, 1.0f);
        const ImVec2 extent = measure(m_regular, 17.0f, entry.text);
        const ImVec2 pos(screen.x - 24.0f - extent.x, y);
        dl->AddRectFilled(ImVec2(pos.x - 8, pos.y - 3), ImVec2(pos.x + extent.x + 8, pos.y + extent.y + 3),
                          withAlpha(entry.involvesLocal ? IM_COL32(120, 90, 20, 255) : IM_COL32(0, 0, 0, 255), alpha * 0.45f), 3.0f);
        text(dl, m_regular, 17.0f, pos, withAlpha(kWhite, alpha), entry.text);
        y += extent.y + 10.0f;
    }
    while (!m_feed.empty() && m_feed.front().age > 6.0f) {
        m_feed.pop_front();
    }
}

void Hud::drawBanners(float dt) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    float y = screen.y * 0.30f;
    for (Banner& banner : m_banners) {
        banner.age += dt;
        const float alpha = std::clamp((banner.duration - banner.age) * 2.0f, 0.0f, 1.0f);
        textCentered(dl, m_bold, 30.0f, ImVec2(screen.x * 0.5f, y), withAlpha(banner.color, alpha), banner.text);
        y += 40.0f;
    }
    while (!m_banners.empty() && m_banners.front().age > m_banners.front().duration) {
        m_banners.pop_front();
    }
}

void Hud::drawDeathScreen(const Simulation& sim) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const Player& me = sim.players()[static_cast<size_t>(sim.localPlayerId())];
    dl->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(40, 0, 0, 90));
    textCentered(dl, m_bold, 48.0f, ImVec2(screen.x * 0.5f, screen.y * 0.42f), kRed, "Has muerto");
    if (m_lastKillerId >= 0 && m_lastKillerId != me.id) {
        textCentered(dl, m_regular, 22.0f, ImVec2(screen.x * 0.5f, screen.y * 0.49f), kWhite,
                     std::format("Eliminado por {}", playerName(sim, m_lastKillerId)));
    }
    textCentered(dl, m_regular, 22.0f, ImVec2(screen.x * 0.5f, screen.y * 0.54f), kGrey,
                 std::format("Reapareciendo en {:.1f} s", std::max(0.0f, me.respawnTimer)));
}

void Hud::drawScoreboard(const Simulation& sim, bool matchOver) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const auto order = GunGame::standings(sim.players());
    const float width = 760.0f;
    const float rowHeight = 32.0f;
    const float top = matchOver ? screen.y * 0.28f : screen.y * 0.2f;
    const float left = screen.x * 0.5f - width * 0.5f;
    const float height = rowHeight * static_cast<float>(order.size() + 1) + 20.0f;
    dl->AddRectFilled(ImVec2(left, top), ImVec2(left + width, top + height), IM_COL32(10, 12, 14, 210), 6.0f);

    const float columns[] = {20.0f, 70.0f, 300.0f, 390.0f, 610.0f, 690.0f};
    const char* headers[] = {"#", "Jugador", "Nivel", "Arma", "Bajas", "Muertes"};
    for (int i = 0; i < 6; ++i) {
        text(dl, m_bold, 18.0f, ImVec2(left + columns[i], top + 10.0f), kGrey, headers[i]);
    }
    float y = top + 10.0f + rowHeight;
    int rank = 1;
    for (const Player* p : order) {
        const bool local = p->id == sim.localPlayerId();
        if (local) {
            dl->AddRectFilled(ImVec2(left + 8, y - 4), ImVec2(left + width - 8, y + rowHeight - 8), IM_COL32(120, 90, 20, 120), 4.0f);
        }
        const ImU32 col = local ? kGold : kWhite;
        dl->AddRectFilled(ImVec2(left + 52, y + 4), ImVec2(left + 62, y + 14),
                          IM_COL32(static_cast<int>(p->color.r * 255), static_cast<int>(p->color.g * 255),
                                   static_cast<int>(p->color.b * 255), 255));
        text(dl, m_regular, 18.0f, ImVec2(left + columns[0], y), col, std::to_string(rank++));
        text(dl, m_regular, 18.0f, ImVec2(left + columns[1], y), col, p->name);
        text(dl, m_regular, 18.0f, ImVec2(left + columns[2], y), col, std::to_string(p->tier + 1));
        text(dl, m_regular, 18.0f, ImVec2(left + columns[3], y), col, sim.weaponFor(*p).name);
        text(dl, m_regular, 18.0f, ImVec2(left + columns[4], y), col, std::to_string(p->kills));
        text(dl, m_regular, 18.0f, ImVec2(left + columns[5], y), col, std::to_string(p->deaths));
        y += rowHeight;
    }
}

} // namespace origon
