#!/usr/bin/env python3
"""Generate WeaponResource .tres files for Gun Game and TDM modes."""

import os
from pathlib import Path

# Gun Game weapon progression (20 levels)
gun_game_weapons = [
    ("pistol", "Pistola", "SECONDARY", 15, 25, 0.12, 1.5, 15, 120, 1.2, 0.15),
    ("service_pistol", "Pistola de Servicio", "SECONDARY", 18, 28, 0.10, 1.8, 12, 96, 1.0, 0.12),
    ("smg", "SMG", "PRIMARY", 16, 30, 0.05, 1.2, 25, 150, 1.5, 0.20),
    ("smg_compact", "SMG Compacta", "PRIMARY", 14, 28, 0.06, 1.0, 20, 120, 1.3, 0.18),
    ("carbine", "Carabina", "PRIMARY", 28, 45, 0.10, 2.0, 20, 80, 1.1, 0.08),
    ("assault_rifle", "Rifle de Asalto", "PRIMARY", 25, 50, 0.08, 2.2, 30, 120, 1.4, 0.12),
    ("assault_rifle_b", "Rifle de Asalto (Variante)", "PRIMARY", 26, 52, 0.08, 2.1, 30, 120, 1.35, 0.11),
    ("battle_rifle", "Rifle de Batalla Pesado", "PRIMARY", 35, 65, 0.15, 2.5, 25, 100, 1.8, 0.10),
    ("sniper", "Sniper", "PRIMARY", 70, 100, 0.50, 3.0, 10, 30, 0.5, 0.02),
    ("sniper_long", "Sniper de Largo Alcance", "PRIMARY", 80, 100, 0.60, 3.5, 8, 24, 0.4, 0.01),
    ("shotgun", "Escopeta", "PRIMARY", 60, 90, 0.80, 2.5, 8, 24, 2.0, 0.30),
    ("shotgun_tactical", "Escopeta Táctica", "PRIMARY", 55, 85, 0.70, 2.2, 10, 30, 1.9, 0.28),
    ("grenade_launcher", "Lanzador de Granadas", "PRIMARY", 40, 70, 1.20, 3.0, 6, 18, 0.8, 0.05),
    ("rocket_launcher", "Lanzacohetes", "PRIMARY", 80, 100, 2.00, 4.0, 4, 12, 0.3, 0.03),
    ("grenade_hand", "Granada de Mano", "SECONDARY", 45, 75, 2.50, 3.5, 3, 9, 0.0, 0.05),
    ("knife_combat", "Cuchillo de Combate", "MELEE", 50, 50, 0.0, 1.0, 1, 1, 0.0, 0.0),
    ("axe", "Hacha", "MELEE", 60, 60, 0.0, 1.2, 1, 1, 0.0, 0.0),
    ("bat", "Bate", "MELEE", 55, 55, 0.0, 1.1, 1, 1, 0.0, 0.0),
    ("dual_daggers", "Puñales Duales", "MELEE", 45, 45, 0.0, 0.8, 1, 1, 0.0, 0.0),
    ("knife_final", "Cuchillo Final", "MELEE", 100, 100, 0.0, 1.0, 1, 1, 0.0, 0.0),
]

def create_weapon_tres(name, display_name, weapon_type, dmg_tdm, dmg_gg, fire_rate, reload, mag, reserve, recoil, spread):
    """Generate .tres resource for a weapon."""
    is_melee = weapon_type == "MELEE"
    weapon_type_map = {'SECONDARY': 1, 'PRIMARY': 0, 'MELEE': 2}
    wtype_val = weapon_type_map[weapon_type]
    return f"""[gd_resource type="Resource" format=3]

[ext_resource type="Script" path="res://scripts/resources/weapon_resource.gd" id="1_script"]

[resource]
script = ExtResource("1_script")
weapon_name = "{display_name}"
weapon_type = {wtype_val}
damage_tdm = {dmg_tdm}
damage_gungame = {dmg_gg}
fire_rate = {fire_rate}
reload_time = {reload}
mag_capacity = {mag}
ammo_reserve = {reserve}
recoil = {recoil}
spread = {spread}
is_melee = {str(is_melee).lower()}
"""

def main():
    """Generate all weapon .tres files."""
    base_dir = Path("resources/weapons")
    base_dir.mkdir(parents=True, exist_ok=True)

    for name, display, weapon_type, dmg_tdm, dmg_gg, fire, reload, mag, reserve, rec, spr in gun_game_weapons:
        filepath = base_dir / f"{name}.tres"
        content = create_weapon_tres(name, display, weapon_type, dmg_tdm, dmg_gg, fire, reload, mag, reserve, rec, spr)
        filepath.write_text(content)
        print(f"[OK] {filepath}")

if __name__ == "__main__":
    main()
