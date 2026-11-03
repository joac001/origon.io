#include "sim/Weapons.h"

#include "core/FileSystem.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <glm/common.hpp>

namespace origon {
namespace {

WeaponCategory parseCategory(const std::string& text) {
    if (text == "pistol") return WeaponCategory::Pistol;
    if (text == "smg") return WeaponCategory::Smg;
    if (text == "assault_rifle") return WeaponCategory::AssaultRifle;
    if (text == "lmg") return WeaponCategory::Lmg;
    if (text == "semi_auto_rifle") return WeaponCategory::SemiAutoRifle;
    if (text == "bolt_rifle") return WeaponCategory::BoltRifle;
    if (text == "sniper") return WeaponCategory::Sniper;
    if (text == "shotgun") return WeaponCategory::Shotgun;
    if (text == "melee") return WeaponCategory::Melee;
    log::warn("Unknown weapon category '{}'", text);
    return WeaponCategory::Smg;
}

} // namespace

float WeaponDef::damageAt(float distance) const {
    if (distance <= rangeFull || rangeMinDamage <= rangeFull) {
        return damage;
    }
    const float t = glm::clamp((distance - rangeFull) / (rangeMinDamage - rangeFull), 0.0f, 1.0f);
    return glm::mix(damage, damageMin, t);
}

bool WeaponDatabase::load(const std::filesystem::path& weaponsFile, const std::filesystem::path& ladderFile) {
    using json = nlohmann::json;
    const auto weaponsText = fs::readText(fs::resolve(weaponsFile));
    const auto ladderText = fs::readText(fs::resolve(ladderFile));
    if (!weaponsText || !ladderText) {
        return false;
    }
    try {
        m_weapons.clear();
        // Keep the parsed document alive for the whole loop (range-for does not extend it).
        const json weaponsRoot = json::parse(*weaponsText);
        for (const json& w : weaponsRoot.at("weapons")) {
            WeaponDef def;
            def.id = w.at("id").get<std::string>();
            def.name = w.value("name", def.id);
            def.category = parseCategory(w.value("category", std::string("smg")));
            def.damage = w.value("damage", def.damage);
            def.damageMin = w.value("damage_min", def.damageMin);
            def.headshotMultiplier = w.value("headshot_multiplier", def.headshotMultiplier);
            def.rpm = std::max(1.0f, w.value("rpm", def.rpm));
            def.automatic = w.value("automatic", def.automatic);
            def.magazine = w.value("magazine", def.magazine);
            def.reloadSeconds = w.value("reload_s", def.reloadSeconds);
            def.rangeFull = w.value("range_full_m", def.rangeFull);
            def.rangeMinDamage = w.value("range_min_damage_m", def.rangeMinDamage);
            def.spreadHipDegrees = w.value("spread_hip_deg", def.spreadHipDegrees);
            def.spreadAdsDegrees = w.value("spread_ads_deg", def.spreadAdsDegrees);
            def.adsFov = w.value("ads_fov", def.adsFov);
            def.moveSpeedMultiplier = w.value("move_speed_multiplier", def.moveSpeedMultiplier);
            def.recoilDegrees = w.value("recoil_deg", def.recoilDegrees);
            def.pellets = std::max(1, w.value("pellets", def.pellets));
            def.scoped = w.value("scoped", def.scoped);
            m_weapons.push_back(def);
        }

        const json ladder = json::parse(*ladderText);
        if (ladder.contains("rules")) {
            const json& r = ladder["rules"];
            m_rules.maxPlayers = r.value("max_players", m_rules.maxPlayers);
            m_rules.timeLimitSeconds = r.value("time_limit_s", m_rules.timeLimitSeconds);
            m_rules.respawnDelaySeconds = r.value("respawn_delay_s", m_rules.respawnDelaySeconds);
            m_rules.spawnProtectionSeconds = r.value("spawn_protection_s", m_rules.spawnProtectionSeconds);
            m_rules.meleeDemotesVictim = r.value("melee_demotes_victim", m_rules.meleeDemotesVictim);
            m_rules.meleePromotesKiller = r.value("melee_promotes_killer", m_rules.meleePromotesKiller);
            m_rules.suicideDemotes = r.value("suicide_demotes", m_rules.suicideDemotes);
        }

        m_knife = find("knife");
        if (!m_knife) {
            log::error("weapons.json must define a 'knife' weapon");
            return false;
        }
        m_ladder.clear();
        for (const json& id : ladder.at("tiers")) {
            const WeaponDef* def = find(id.get<std::string>());
            if (!def) {
                log::error("Ladder references unknown weapon '{}'", id.get<std::string>());
                return false;
            }
            m_ladder.push_back(def);
        }
        if (m_ladder.empty()) {
            log::error("Gun Game ladder is empty");
            return false;
        }
    } catch (const std::exception& e) {
        log::error("Weapon data parse error: {}", e.what());
        return false;
    }
    log::info("Weapons loaded: {} definitions, {} ladder tiers", m_weapons.size(), m_ladder.size());
    return true;
}

const WeaponDef* WeaponDatabase::find(const std::string& id) const {
    for (const WeaponDef& def : m_weapons) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

} // namespace origon
