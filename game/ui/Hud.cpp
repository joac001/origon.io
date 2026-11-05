#include "ui/Hud.h"

#include "sim/Simulation.h"
#include "ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace origon {
namespace {

using namespace ui::color;
using ui::withAlpha;

std::string playerName(const Simulation& sim, int id) {
    const auto& players = sim.players();
    if (id < 0 || id >= static_cast<int>(players.size())) {
        return "El mundo";
    }
    return players[static_cast<size_t>(id)].name;
}

ImU32 playerColor(const Player& p) {
    return IM_COL32(static_cast<int>(p.color.r * 255), static_cast<int>(p.color.g * 255), static_cast<int>(p.color.b * 255), 255);
}

// Thin horizontal progress bar.
void bar(ImDrawList* dl, ImVec2 min, float width, float height, float fraction, ImU32 fill, ImU32 track = IM_COL32(237, 232, 218, 40)) {
    dl->AddRectFilled(min, ImVec2(min.x + width, min.y + height), track);
    dl->AddRectFilled(min, ImVec2(min.x + width * std::clamp(fraction, 0.0f, 1.0f), min.y + height), fill);
}

} // namespace

void Hud::reset() {
    m_feed.clear();
    m_banners.clear();
    m_confirmations.clear();
    m_hitMarker = 0.0f;
    m_damageFlash = 0.0f;
    m_damageMarkers.clear();
    m_lastKillerId = -1;
    m_lastKillerWeapon.clear();
}

void Hud::pushBanner(const std::string& title, const std::string& detail, unsigned color, float duration) {
    m_banners.push_back({title, detail, 0.0f, duration, color});
    while (m_banners.size() > 2) {
        m_banners.pop_front();
    }
}

void Hud::onEvents(const std::vector<GameEvent>& events, const Simulation& sim) {
    const int local = sim.localPlayerId();
    const int tiers = sim.gunGame().finalTier() + 1;
    for (const GameEvent& e : events) {
        switch (e.type) {
        case GameEvent::Type::Kill: {
            FeedEntry entry;
            if (e.a >= 0 && e.a != e.b) {
                entry.killer = playerName(sim, e.a);
                entry.weapon = e.weaponName;
            } else {
                entry.weapon = e.cause == KillCause::Suicide ? "Suicidio" : "Caída";
            }
            entry.victim = playerName(sim, e.b);
            entry.killerLocal = e.a == local;
            entry.victimLocal = e.b == local;
            m_feed.push_back(entry);
            if (m_feed.size() > 6) m_feed.pop_front();
            if (e.b == local) {
                m_lastKillerId = e.a;
                m_lastKillerWeapon = e.weaponName;
            }
            if (e.a == local && e.b != local) {
                m_confirmations.push_back({ui::upper(playerName(sim, e.b)), 0.0f});
                if (m_confirmations.size() > 3) m_confirmations.pop_front();
            }
            break;
        }
        case GameEvent::Type::Promoted:
            if (e.a == local) {
                pushBanner(std::format("Nivel {} / {}", e.tier + 1, tiers), e.weaponName, kOlive);
            }
            break;
        case GameEvent::Type::Demoted:
            if (e.a == local) {
                pushBanner(e.cause == KillCause::Melee ? "Humillado" : "Suicidio", std::format("Bajaste al nivel {}", e.tier + 1),
                           kDanger, 3.0f);
            } else if (e.cause == KillCause::Melee) {
                FeedEntry entry;
                entry.weapon = std::format("Humillado · nivel {}", e.tier + 1);
                entry.victim = playerName(sim, e.a);
                m_feed.push_back(entry);
            }
            break;
        case GameEvent::Type::FinalTier:
            if (e.a == local) {
                pushBanner("Último nivel", "Cuchillo: una baja más para ganar", kBrass, 3.0f);
            } else {
                pushBanner(std::format("{} llegó al cuchillo", playerName(sim, e.a)), "Una baja más y gana", kBrass, 2.5f);
            }
            break;
        case GameEvent::Type::TimeUp:
            pushBanner("Tiempo agotado", "", kBrass, 3.0f);
            break;
        case GameEvent::Type::Hit:
            if (e.a == local) {
                m_hitMarker = 0.18f;
                m_hitMarkerHead = e.headshot;
            }
            if (e.b == local) {
                m_damageFlash = 0.35f;
                if (e.a >= 0 && e.a < static_cast<int>(sim.players().size())) {
                    m_damageMarkers.push_back({sim.players()[static_cast<size_t>(e.a)].position, 0.0f});
                }
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
    const float k = ui::scale();
    const ui::Fonts& f = ui::fonts();
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
        // Red at the screen edges only (the center stays readable).
        const float a = m_damageFlash * 0.9f;
        const ImU32 edge = withAlpha(IM_COL32(150, 10, 0, 255), a), clear = IM_COL32(150, 10, 0, 0);
        const float band = screen.y * 0.22f;
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(screen.x, band), edge, edge, clear, clear);
        dl->AddRectFilledMultiColor(ImVec2(0, screen.y - band), screen, clear, clear, edge, edge);
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(band, screen.y), edge, clear, clear, edge);
        dl->AddRectFilledMultiColor(ImVec2(screen.x - band, 0), screen, clear, edge, edge, clear);
        m_damageFlash = std::max(0.0f, m_damageFlash - dt);
    }

    if (me.alive && !matchOver) {
        if (!aimingScoped) drawCrosshair(sim);
        drawStatus(sim);
        drawDamageMarkers(sim, dt);
    }

    // Match clock and leader (top center).
    const float timeLeft = sim.gunGame().timeLeft();
    const int minutes = static_cast<int>(timeLeft) / 60;
    const int seconds = static_cast<int>(timeLeft) % 60;
    ui::textAligned(dl, f.heavy, 40.0f * k, ImVec2(screen.x * 0.5f, 20.0f * k), ImVec2(0.5f, 0.0f), timeLeft < 60.0f ? kDanger : kPaper,
                    std::format("{:02}:{:02}", minutes, seconds), 1.0f * k);
    const auto order = GunGame::standings(sim.players());
    if (!order.empty()) {
        const Player* leader = order.front();
        const int tiers = sim.gunGame().finalTier() + 1;
        ui::label(dl, ImVec2(screen.x * 0.5f, 68.0f * k), kMuted,
                  std::format("Líder  {}  ·  {}/{}", leader->id == me.id ? "Vos" : leader->name, leader->tier + 1, tiers), 13.0f,
                  ImVec2(0.5f, 0.0f));
    }

    drawFeed(dt);
    drawBanners(dt);
    drawConfirmations(dt);

    if (!me.alive && !matchOver && !showScoreboard) {
        drawDeathScreen(sim);
    }
    if (matchOver) {
        const int winner = sim.gunGame().winnerId();
        dl->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(6, 8, 6, 150));
        ui::label(dl, ImVec2(screen.x * 0.5f, screen.y * 0.12f), kBrass, winner == me.id ? "Victoria" : "Fin de la partida", 16.0f,
                  ImVec2(0.5f, 0.0f));
        ui::textAligned(dl, f.heavy, 64.0f * k, ImVec2(screen.x * 0.5f, screen.y * 0.12f + 26.0f * k), ImVec2(0.5f, 0.0f), kPaper,
                        ui::upper(winner == me.id ? "Ganaste la partida" : std::format("{} gana", playerName(sim, winner))),
                        2.0f * k);
        drawScoreboard(sim, true);
        ui::label(dl, ImVec2(screen.x * 0.5f, screen.y * 0.9f), kMuted, "Enter: jugar otra vez", 14.0f, ImVec2(0.5f, 0.0f));
    } else if (showScoreboard) {
        drawScoreboard(sim, false);
    }

    if (m_hitMarker > 0.0f) {
        m_hitMarker = std::max(0.0f, m_hitMarker - dt);
    }
}

void Hud::drawDamageMarkers(const Simulation& sim, float dt) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const Player& me = sim.players()[static_cast<size_t>(sim.localPlayerId())];
    const ImVec2 c(screen.x * 0.5f, screen.y * 0.5f);
    const float radius = screen.y * 0.15f;
    for (DamageMarker& marker : m_damageMarkers) {
        marker.age += dt;
        const float alpha = std::clamp(1.4f - marker.age, 0.0f, 1.0f);
        // Angle of the attacker relative to where we look (0 = straight ahead, screen up).
        const glm::vec3 to = marker.source - me.position;
        const float worldAngle = std::atan2(-to.x, -to.z);
        const float relative = worldAngle - me.yaw;
        const float screenAngle = -relative - 1.5707963f;
        for (int i = -7; i <= 7; ++i) {
            const float a0 = screenAngle + static_cast<float>(i) * 0.04f;
            const float a1 = a0 + 0.04f;
            const float w = 1.0f - std::abs(static_cast<float>(i)) / 8.0f;
            dl->AddLine(ImVec2(c.x + std::cos(a0) * radius, c.y + std::sin(a0) * radius),
                        ImVec2(c.x + std::cos(a1) * radius, c.y + std::sin(a1) * radius), withAlpha(kDanger, alpha * w),
                        4.0f * ui::scale());
        }
    }
    std::erase_if(m_damageMarkers, [](const DamageMarker& m) { return m.age > 1.4f; });
}

void Hud::drawCrosshair(const Simulation& sim) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const ImVec2 c(std::floor(screen.x * 0.5f) + 0.5f, std::floor(screen.y * 0.5f) + 0.5f);
    const Player& me = sim.players()[static_cast<size_t>(sim.localPlayerId())];
    const WeaponDef& weapon = sim.weaponFor(me);
    const float k = ui::scale();
    const ImU32 col = IM_COL32(245, 242, 232, 235);
    const ImU32 outline = IM_COL32(0, 0, 0, 120);
    // Lines with a 1 px dark outline: readable on sky and on snow-bright foliage alike.
    auto tick = [&](ImVec2 a, ImVec2 b) {
        dl->AddLine(a, b, outline, 4.0f);
        dl->AddLine(a, b, col, 2.0f);
    };

    if (weapon.isMelee()) {
        dl->AddCircleFilled(c, 3.5f, outline);
        dl->AddCircleFilled(c, 2.5f, col);
    } else if (!me.aiming) {
        const float speed = std::sqrt(me.velocity.x * me.velocity.x + me.velocity.z * me.velocity.z);
        const float gap = (6.0f + weapon.spreadHipDegrees * 4.0f + speed * 1.5f + me.viewKick * 300.0f) * k;
        const float len = 8.0f * k;
        tick(ImVec2(c.x - gap - len, c.y), ImVec2(c.x - gap, c.y));
        tick(ImVec2(c.x + gap, c.y), ImVec2(c.x + gap + len, c.y));
        tick(ImVec2(c.x, c.y + gap), ImVec2(c.x, c.y + gap + len));
        dl->AddCircleFilled(c, 1.6f, col);
    } else {
        dl->AddCircleFilled(c, 2.0f, IM_COL32(255, 255, 255, 200));
    }

    if (m_hitMarker > 0.0f) {
        const float t = m_hitMarker / 0.18f;
        const ImU32 hit = withAlpha(m_hitMarkerHead ? kBrass : kPaper, t);
        const float a = 7.0f * k, b = (13.0f + 3.0f * (1.0f - t)) * k;
        for (const ImVec2 d : {ImVec2(-1, -1), ImVec2(1, -1), ImVec2(-1, 1), ImVec2(1, 1)}) {
            dl->AddLine(ImVec2(c.x + d.x * b, c.y + d.y * b), ImVec2(c.x + d.x * a, c.y + d.y * a), withAlpha(outline, t), 4.5f);
            dl->AddLine(ImVec2(c.x + d.x * b, c.y + d.y * b), ImVec2(c.x + d.x * a, c.y + d.y * a), hit, 2.5f);
        }
    }
}

void Hud::drawStatus(const Simulation& sim) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float k = ui::scale();
    const ui::Fonts& f = ui::fonts();
    const Player& me = sim.players()[static_cast<size_t>(sim.localPlayerId())];
    const WeaponDef& weapon = sim.weaponFor(me);
    const int tiers = sim.gunGame().finalTier() + 1;
    const float margin = 44.0f * k;
    const float bottom = screen.y - margin;

    // --- Health (bottom left): big number, segmented bar, stance. ---
    const float health = std::clamp(me.health / kMaxHealth, 0.0f, 1.0f);
    const bool low = health <= 0.35f;
    const std::string number = std::to_string(static_cast<int>(std::ceil(me.health)));
    const float numberSize = 54.0f * k;
    const ImVec2 numberExtent = ui::measure(f.heavy, numberSize, number);
    ui::text(dl, f.heavy, numberSize, ImVec2(margin, bottom - numberExtent.y), low ? kDanger : kPaper, number);
    const float barX = margin + numberExtent.x + 16.0f * k;
    const float barWidth = 230.0f * k;
    const float barY = bottom - 30.0f * k;
    ui::label(dl, ImVec2(barX, barY - 22.0f * k), kMuted, "Salud");
    const int segments = 10;
    const float gap = 3.0f * k;
    const float segment = (barWidth - gap * (segments - 1)) / segments;
    for (int i = 0; i < segments; ++i) {
        const float x = barX + static_cast<float>(i) * (segment + gap);
        const float fill = std::clamp(health * segments - static_cast<float>(i), 0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(x, barY), ImVec2(x + segment, barY + 8.0f * k), IM_COL32(237, 232, 218, 36));
        if (fill > 0.0f) dl->AddRectFilled(ImVec2(x, barY), ImVec2(x + segment * fill, barY + 8.0f * k), low ? kDanger : kPaper);
    }
    std::string stance = me.prone ? "Cuerpo a tierra" : me.crouched ? "Agachado" : "";
    if (std::abs(me.lean) > 0.5f) stance = me.lean > 0.0f ? "Asomado · derecha" : "Asomado · izquierda";
    if (me.spawnProtection > 0.0f) {
        ui::label(dl, ImVec2(barX, barY + 16.0f * k), kOlive, std::format("Protegido  {:.1f}", me.spawnProtection));
    } else if (!stance.empty()) {
        ui::label(dl, ImVec2(barX, barY + 16.0f * k), kDim, stance);
    }

    // --- Weapon (bottom right): tier, name, ammo, magazine, ladder. ---
    const float right = screen.x - margin;
    const float ladderWidth = 260.0f * k;
    // Ladder: one mark per tier, the current one in brass.
    const float ladderY = bottom - 6.0f * k;
    const float step = ladderWidth / static_cast<float>(tiers);
    for (int i = 0; i < tiers; ++i) {
        const float x = right - ladderWidth + step * static_cast<float>(i);
        const ImU32 col = i == me.tier ? kBrass : i < me.tier ? kPaper : IM_COL32(237, 232, 218, 50);
        const float h = (i == me.tier ? 12.0f : 6.0f) * k;
        dl->AddRectFilled(ImVec2(x, ladderY - h), ImVec2(x + step - 3.0f * k, ladderY), col);
    }
    const bool last = me.tier + 1 >= tiers;
    ui::label(dl, ImVec2(right, ladderY - 34.0f * k), kMuted,
              last ? "Última arma" : std::format("Siguiente  {}", sim.gunGame().weaponForTier(me.tier + 1).name), 13.0f,
              ImVec2(1.0f, 0.0f));
    float y = ladderY - 46.0f * k;

    if (!weapon.isMelee()) {
        if (me.reloadTimer > 0.0f) {
            const float progress = weapon.reloadSeconds > 0.0f ? 1.0f - me.reloadTimer / weapon.reloadSeconds : 1.0f;
            bar(dl, ImVec2(right - 160.0f * k, y - 4.0f * k), 160.0f * k, 3.0f * k, progress, kBrass);
            ui::label(dl, ImVec2(right, y - 12.0f * k), kBrass, "Recargando", 15.0f, ImVec2(1.0f, 1.0f));
            y -= 52.0f * k;
        } else {
            // Magazine as pips (or a bar for big drums), then the round count.
            const int magazine = std::max(1, weapon.magazine);
            if (magazine <= 40) {
                const float pip = 4.0f * k, pipGap = 2.0f * k;
                for (int i = 0; i < magazine; ++i) {
                    const float x = right - static_cast<float>(magazine - i) * (pip + pipGap) + pipGap;
                    dl->AddRectFilled(ImVec2(x, y - 12.0f * k), ImVec2(x + pip, y), i < me.ammo ? kPaper : IM_COL32(237, 232, 218, 40));
                }
            } else {
                bar(dl, ImVec2(right - 160.0f * k, y - 6.0f * k), 160.0f * k, 4.0f * k, static_cast<float>(me.ammo) / magazine, kPaper);
            }
            y -= 18.0f * k;
            const std::string reserve = " / ∞";
            const ImVec2 reserveExtent = ui::measure(f.display, 26.0f * k, reserve);
            ui::text(dl, f.display, 26.0f * k, ImVec2(right - reserveExtent.x, y - reserveExtent.y - 4.0f * k), kDim, reserve);
            const std::string ammo = std::to_string(me.ammo);
            const ImVec2 ammoExtent = ui::measure(f.heavy, 54.0f * k, ammo);
            ui::text(dl, f.heavy, 54.0f * k, ImVec2(right - reserveExtent.x - ammoExtent.x, y - ammoExtent.y),
                     me.ammo == 0 ? kDanger : kPaper, ammo);
            y -= ammoExtent.y;
        }
    }
    const std::string name = ui::upper(weapon.name);
    ui::textAligned(dl, f.display, 30.0f * k, ImVec2(right, y - 2.0f * k), ImVec2(1.0f, 1.0f), kPaper, name, 1.5f * k);
    y -= 32.0f * k;
    ui::label(dl, ImVec2(right, y - 4.0f * k), kBrass, std::format("Nivel {} / {}", me.tier + 1, tiers), 14.0f, ImVec2(1.0f, 1.0f));
}

void Hud::drawFeed(float dt) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float k = ui::scale();
    const ui::Fonts& f = ui::fonts();
    const float size = 18.0f * k;
    const float right = screen.x - 30.0f * k;
    float y = 26.0f * k;
    for (FeedEntry& entry : m_feed) {
        entry.age += dt;
        const float alpha = std::clamp(6.0f - entry.age, 0.0f, 1.0f) * std::clamp(entry.age * 8.0f, 0.0f, 1.0f);
        const float labelSize = 13.5f * k;
        const std::string weapon = ui::upper(entry.weapon);
        const ImVec2 killerExtent = entry.killer.empty() ? ImVec2(0, 0) : ui::measure(f.medium, size, entry.killer);
        const ImVec2 weaponExtent = ui::measure(f.label, labelSize, weapon, labelSize * 0.14f);
        const ImVec2 victimExtent = ui::measure(f.medium, size, entry.victim);
        const float pad = 10.0f * k, spacing = 10.0f * k;
        const float width = (entry.killer.empty() ? 0.0f : killerExtent.x + spacing) + weaponExtent.x + spacing + victimExtent.x;
        const float height = 30.0f * k;
        const ImVec2 min(right - width - pad * 2.0f, y), max(right, y + height);
        const bool local = entry.killerLocal || entry.victimLocal;
        ui::panel(dl, min, max, 0.55f * alpha, false);
        if (local) dl->AddRectFilled(min, ImVec2(min.x + 3.0f * k, max.y), withAlpha(entry.victimLocal ? kDanger : kBrass, alpha));
        float x = min.x + pad;
        const float mid = y + height * 0.5f;
        if (!entry.killer.empty()) {
            ui::textAligned(dl, f.medium, size, ImVec2(x, mid), ImVec2(0.0f, 0.5f), withAlpha(entry.killerLocal ? kBrass : kPaper, alpha),
                            entry.killer, 0.0f, false);
            x += killerExtent.x + spacing;
        }
        ui::textAligned(dl, f.label, labelSize, ImVec2(x, mid), ImVec2(0.0f, 0.5f), withAlpha(kMuted, alpha), weapon, labelSize * 0.14f,
                        false);
        x += weaponExtent.x + spacing;
        ui::textAligned(dl, f.medium, size, ImVec2(x, mid), ImVec2(0.0f, 0.5f), withAlpha(entry.victimLocal ? kDanger : kPaper, alpha),
                        entry.victim, 0.0f, false);
        y += height + 6.0f * k;
    }
    while (!m_feed.empty() && m_feed.front().age > 6.0f) {
        m_feed.pop_front();
    }
}

void Hud::drawBanners(float dt) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float k = ui::scale();
    const ui::Fonts& f = ui::fonts();
    float y = screen.y * 0.24f;
    for (Banner& banner : m_banners) {
        banner.age += dt;
        const float alpha = std::clamp((banner.duration - banner.age) * 2.0f, 0.0f, 1.0f) * std::clamp(banner.age * 6.0f, 0.0f, 1.0f);
        // Accent rule that grows in, the title, then the detail.
        const float rule = 56.0f * k * std::clamp(banner.age * 4.0f, 0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(screen.x * 0.5f - rule * 0.5f, y), ImVec2(screen.x * 0.5f + rule * 0.5f, y + 3.0f * k),
                          withAlpha(banner.color, alpha));
        ui::textAligned(dl, f.heavy, 40.0f * k, ImVec2(screen.x * 0.5f, y + 10.0f * k), ImVec2(0.5f, 0.0f), withAlpha(kPaper, alpha),
                        ui::upper(banner.title), 2.0f * k);
        if (!banner.detail.empty()) {
            ui::textAligned(dl, f.medium, 20.0f * k, ImVec2(screen.x * 0.5f, y + 58.0f * k), ImVec2(0.5f, 0.0f),
                            withAlpha(banner.color, alpha), banner.detail);
        }
        y += 100.0f * k;
    }
    while (!m_banners.empty() && m_banners.front().age > m_banners.front().duration) {
        m_banners.pop_front();
    }
}

void Hud::drawConfirmations(float dt) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float k = ui::scale();
    float y = screen.y * 0.5f + 64.0f * k;
    for (Confirmation& c : m_confirmations) {
        c.age += dt;
        const float alpha = std::clamp((1.8f - c.age) * 3.0f, 0.0f, 1.0f) * std::clamp(c.age * 10.0f, 0.0f, 1.0f);
        const float rise = 6.0f * k * (1.0f - std::clamp(c.age * 6.0f, 0.0f, 1.0f));
        ui::label(dl, ImVec2(screen.x * 0.5f, y + rise), withAlpha(kPaper, alpha), "Eliminado  " + c.text, 15.0f, ImVec2(0.5f, 0.0f));
        y += 22.0f * k;
    }
    while (!m_confirmations.empty() && m_confirmations.front().age > 1.8f) {
        m_confirmations.pop_front();
    }
}

void Hud::drawDeathScreen(const Simulation& sim) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float k = ui::scale();
    const ui::Fonts& f = ui::fonts();
    const Player& me = sim.players()[static_cast<size_t>(sim.localPlayerId())];
    // Desaturating veil: dark at the edges, lighter in the middle.
    const ImU32 edge = IM_COL32(8, 4, 3, 190), center = IM_COL32(20, 6, 4, 70);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(screen.x, screen.y * 0.5f), edge, edge, center, center);
    dl->AddRectFilledMultiColor(ImVec2(0, screen.y * 0.5f), screen, center, center, edge, edge);

    const float cx = screen.x * 0.5f;
    float y = screen.y * 0.36f;
    const bool killed = m_lastKillerId >= 0 && m_lastKillerId != me.id;
    ui::label(dl, ImVec2(cx, y), kDanger, killed ? "Eliminado por" : "Caíste", 15.0f, ImVec2(0.5f, 0.0f));
    y += 24.0f * k;
    if (killed) {
        ui::textAligned(dl, f.heavy, 56.0f * k, ImVec2(cx, y), ImVec2(0.5f, 0.0f), kPaper, ui::upper(playerName(sim, m_lastKillerId)), 2.0f * k);
        y += 64.0f * k;
        if (!m_lastKillerWeapon.empty()) {
            ui::label(dl, ImVec2(cx, y), kMuted, m_lastKillerWeapon, 14.0f, ImVec2(0.5f, 0.0f));
            y += 22.0f * k;
        }
    }
    y += 28.0f * k;
    const float total = std::max(0.1f, sim.gunGame().rules().respawnDelaySeconds);
    const float width = 220.0f * k;
    bar(dl, ImVec2(cx - width * 0.5f, y), width, 3.0f * k, 1.0f - std::max(0.0f, me.respawnTimer) / total, kPaper);
    ui::label(dl, ImVec2(cx, y + 12.0f * k), kMuted, std::format("Reaparecés en {:.1f}", std::max(0.0f, me.respawnTimer)), 13.0f,
              ImVec2(0.5f, 0.0f));
}

void Hud::drawScoreboard(const Simulation& sim, bool matchOver) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float k = ui::scale();
    const ui::Fonts& f = ui::fonts();
    const auto order = GunGame::standings(sim.players());
    const int tiers = sim.gunGame().finalTier() + 1;
    const float width = 860.0f * k;
    const float rowHeight = 40.0f * k;
    const float headerHeight = 44.0f * k;
    const float top = matchOver ? screen.y * 0.28f : screen.y * 0.18f;
    const float left = std::floor(screen.x * 0.5f - width * 0.5f);
    const float height = headerHeight + rowHeight * static_cast<float>(order.size()) + 10.0f * k;
    ui::panel(dl, ImVec2(left, top), ImVec2(left + width, top + height), 0.82f);

    // Columns: rank, name, progress on the ladder, weapon, kills, deaths.
    const float colRank = 24.0f * k, colName = 70.0f * k, colLadder = 330.0f * k, colWeapon = 520.0f * k, colKills = 740.0f * k,
                colDeaths = 820.0f * k;
    const float headerY = top + headerHeight * 0.5f;
    ui::label(dl, ImVec2(left + colName, headerY), kDim, "Jugador", 13.5f, ImVec2(0.0f, 0.5f));
    ui::label(dl, ImVec2(left + colLadder, headerY), kDim, "Nivel", 13.5f, ImVec2(0.0f, 0.5f));
    ui::label(dl, ImVec2(left + colWeapon, headerY), kDim, "Arma", 13.5f, ImVec2(0.0f, 0.5f));
    ui::label(dl, ImVec2(left + colKills, headerY), kDim, "Bajas", 13.5f, ImVec2(1.0f, 0.5f));
    ui::label(dl, ImVec2(left + colDeaths, headerY), kDim, "Muertes", 13.5f, ImVec2(1.0f, 0.5f));
    dl->AddLine(ImVec2(left + 16.0f * k, top + headerHeight), ImVec2(left + width - 16.0f * k, top + headerHeight), kLine);

    float y = top + headerHeight;
    int rank = 1;
    for (const Player* p : order) {
        const bool local = p->id == sim.localPlayerId();
        const float mid = y + rowHeight * 0.5f;
        if (local) {
            dl->AddRectFilled(ImVec2(left + 1.0f, y + 2.0f * k), ImVec2(left + width - 1.0f, y + rowHeight - 2.0f * k), withAlpha(kBrass, 0.12f));
            dl->AddRectFilled(ImVec2(left + 1.0f, y + 2.0f * k), ImVec2(left + 4.0f * k, y + rowHeight - 2.0f * k), kBrass);
        }
        const ImU32 col = local ? kPaper : ui::mix(kPaper, kMuted, 0.25f);
        ui::textAligned(dl, f.display, 22.0f * k, ImVec2(left + colRank, mid), ImVec2(0.0f, 0.5f), rank == 1 ? kBrass : kDim,
                        std::to_string(rank), 0.0f, false);
        dl->AddCircleFilled(ImVec2(left + colName - 14.0f * k, mid), 4.0f * k, playerColor(*p));
        ui::textAligned(dl, f.medium, 19.0f * k, ImVec2(left + colName, mid), ImVec2(0.0f, 0.5f), col, p->name, 0.0f, false);
        // Ladder progress: a thin bar with the tier number.
        bar(dl, ImVec2(left + colLadder, mid - 1.5f * k), 120.0f * k, 3.0f * k, static_cast<float>(p->tier + 1) / tiers,
            local ? kBrass : kPaper);
        ui::textAligned(dl, f.display, 19.0f * k, ImVec2(left + colLadder + 132.0f * k, mid), ImVec2(0.0f, 0.5f), col,
                        std::format("{}", p->tier + 1), 0.0f, false);
        ui::textAligned(dl, f.body, 18.0f * k, ImVec2(left + colWeapon, mid), ImVec2(0.0f, 0.5f), col, sim.weaponFor(*p).name, 0.0f, false);
        ui::textAligned(dl, f.display, 21.0f * k, ImVec2(left + colKills, mid), ImVec2(1.0f, 0.5f), col, std::to_string(p->kills), 0.0f, false);
        ui::textAligned(dl, f.display, 21.0f * k, ImVec2(left + colDeaths, mid), ImVec2(1.0f, 0.5f), kDim, std::to_string(p->deaths), 0.0f,
                        false);
        y += rowHeight;
        ++rank;
    }
}

} // namespace origon
