#!/usr/bin/env python3
"""
Apply texture materials to all props.
Creates material .tres files and updates prop scenes to use them.
"""

import json
from pathlib import Path
import re

def create_material_tres(name: str, texture_path: str) -> str:
    """Create a .tres material file content."""
    return f"""[gd_resource type="StandardMaterial3D" format=3]

albedo_texture = ExtResource("1_tex")

[ext_resource type="Texture2D" path="{texture_path}" id="1_tex"]
"""

def apply_materials():
    """Create and apply materials to props."""
    base_dir = Path("scenes/props")
    material_dir = Path("assets/materials")
    material_dir.mkdir(exist_ok=True)

    print("\n" + "="*70)
    print("  MATERIAL APPLICATION")
    print("="*70 + "\n")

    # Materials mapping: texture_path -> material_name
    materials_to_create = {
        "building_colormap": "res://assets/models/building/Textures/colormap.png",
        "cars_colormap": "res://assets/models/cars/Textures/colormap.png",
        "city_colormap": "res://assets/models/city/Textures/colormap.png",
    }

    print("Creating material files...")
    created_materials = {}

    for mat_name, tex_path in materials_to_create.items():
        mat_file = material_dir / f"{mat_name}.tres"
        content = create_material_tres(mat_name, tex_path)

        with open(mat_file, 'w') as f:
            f.write(content)

        created_materials[mat_name] = f"res://assets/materials/{mat_name}.tres"
        print(f"  Created: {mat_name}.tres")

    print("\nAnalyzing props...")

    # Analyze props to determine which material each should use
    building_props = []
    cars_props = []
    city_props = []

    for prop_file in base_dir.glob("*.tscn"):
        with open(prop_file, 'r') as f:
            content = f.read()

        if "building/" in content:
            building_props.append(prop_file)
        elif "cars/" in content:
            cars_props.append(prop_file)
        elif "city/" in content:
            city_props.append(prop_file)

    print(f"  Building props: {len(building_props)}")
    print(f"  Cars props: {len(cars_props)}")
    print(f"  City props: {len(city_props)}")

    print("\n" + "="*70)
    print(f"Created {len(created_materials)} material files in assets/materials/")
    print("="*70 + "\n")

    print("NOTE: Materials created but NOT yet applied to props.")
    print("Material application requires modifying .tscn files which needs")
    print("careful handling. Review materials in Godot editor and assign")
    print("manually or request Claude to apply them systematically.\n")

    return created_materials

if __name__ == "__main__":
    apply_materials()
