#!/usr/bin/env python3
"""Generate ClassResource .tres files for default loadouts."""

from pathlib import Path

# Default TDM classes
default_classes = [
    ("assault", "Assault", "assault_rifle", "pistol", "knife_combat"),
    ("sniper", "Sniper", "sniper", "service_pistol", "knife_combat"),
    ("support", "Support", "smg", "pistol", "bat"),
    ("shotgunner", "Shotgunner", "shotgun", "pistol", "axe"),
    ("rusher", "Rusher", "smg_compact", "pistol", "dual_daggers"),
]

def create_class_tres(class_name, display_name, primary, secondary, melee):
    """Generate .tres resource for a class."""
    return f"""[gd_resource type="Resource" format=3]

[ext_resource type="Script" path="res://scripts/resources/class_resource.gd" id="1_script"]
[ext_resource type="Resource" path="res://resources/weapons/{primary}.tres" id="2_primary"]
[ext_resource type="Resource" path="res://resources/weapons/{secondary}.tres" id="3_secondary"]
[ext_resource type="Resource" path="res://resources/weapons/{melee}.tres" id="4_melee"]

[resource]
script = ExtResource("1_script")
class_name_str = "{display_name}"
primary_weapon = ExtResource("2_primary")
secondary_weapon = ExtResource("3_secondary")
melee_weapon = ExtResource("4_melee")
"""

def main():
    """Generate all class .tres files."""
    base_dir = Path("resources/classes")
    base_dir.mkdir(parents=True, exist_ok=True)

    for name, display, primary, secondary, melee in default_classes:
        filepath = base_dir / f"class_{name}.tres"
        content = create_class_tres(name, display, primary, secondary, melee)
        filepath.write_text(content)
        print(f"[OK] {filepath}")

if __name__ == "__main__":
    main()
