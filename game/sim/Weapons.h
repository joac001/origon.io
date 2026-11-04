#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace origon {

enum class WeaponCategory { Pistol, Smg, AssaultRifle, Lmg, SemiAutoRifle, BoltRifle, Sniper, Shotgun, Melee };

struct WeaponDef {
    std::string id;
    std::string name; // Player-facing name.
    WeaponCategory category = WeaponCategory::Smg;
    float damage = 30.0f;
    float damageMin = 20.0f;
    float headshotMultiplier = 2.0f;
    float rpm = 600.0f;
    bool automatic = true;
    int magazine = 30;
    float reloadSeconds = 2.5f;
    float rangeFull = 15.0f;     // Full damage up to this distance (m).
    float rangeMinDamage = 40.0f; // damageMin from this distance on (m).
    float spreadHipDegrees = 3.0f;
    float spreadAdsDegrees = 1.0f;
    float adsFov = 60.0f;
    float moveSpeedMultiplier = 1.0f;
    float recoilDegrees = 1.0f;
    int pellets = 1;
    bool scoped = false;

    bool isMelee() const { return category == WeaponCategory::Melee; }
    float fireInterval() const { return 60.0f / rpm; }
    float damageAt(float distance) const;
};

struct GunGameRules {
    int maxPlayers = 10;
    float timeLimitSeconds = 600.0f;
    float respawnDelaySeconds = 3.0f;
    float spawnProtectionSeconds = 1.5f;
    bool meleeDemotesVictim = true;
    bool meleePromotesKiller = false;
    bool suicideDemotes = true;
};

// Weapon definitions and the Gun Game ladder, loaded from data/*.json.
class WeaponDatabase {
public:
    bool load(const std::filesystem::path& weaponsFile, const std::filesystem::path& ladderFile);

    const WeaponDef* find(const std::string& id) const;
    const WeaponDef& knife() const { return *m_knife; }
    const std::vector<const WeaponDef*>& ladder() const { return m_ladder; }
    const GunGameRules& rules() const { return m_rules; }
    int tierCount() const { return static_cast<int>(m_ladder.size()); }

private:
    std::vector<WeaponDef> m_weapons;
    std::vector<const WeaponDef*> m_ladder;
    const WeaponDef* m_knife = nullptr;
    GunGameRules m_rules;
};

} // namespace origon
