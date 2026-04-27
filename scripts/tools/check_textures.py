#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Check which GLBs reference textures and their texture paths.
Helps identify if materials need to be created in Godot.
"""

import json
import struct
from pathlib import Path
import sys
import io

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

def read_gltf_json(glb_path: str) -> dict:
    """Extract GLTF JSON from GLB file."""
    try:
        with open(glb_path, 'rb') as f:
            magic = f.read(4)
            if magic != b'glTF':
                return {}

            version = struct.unpack('<I', f.read(4))[0]
            length = struct.unpack('<I', f.read(4))[0]
            chunk_length = struct.unpack('<I', f.read(4))[0]
            chunk_type = f.read(4)

            if chunk_type != b'JSON':
                return {}

            return json.loads(f.read(chunk_length))
    except:
        return {}

def check_assets():
    """Check all GLBs for texture references."""
    base_dir = Path("assets/models")

    print("\n" + "="*70)
    print("  TEXTURE ANALYSIS - GLB Assets")
    print("="*70 + "\n")

    texture_map = {}

    for category_dir in ["building", "cars", "city", "characters"]:
        cat_path = base_dir / category_dir
        if not cat_path.exists():
            continue

        print(f"📦 {category_dir.upper()}")
        print("-" * 70)

        for glb_file in sorted(cat_path.glob("*.glb")):
            name = glb_file.stem
            gltf = read_gltf_json(str(glb_file))

            if not gltf:
                print(f"  ✗ {name:40} (failed to read)")
                continue

            images = gltf.get('images', [])
            materials = gltf.get('materials', [])

            if not images and not materials:
                print(f"  ℹ {name:40} (no materials/textures)")
                continue

            texture_info = []
            for img in images:
                uri = img.get('uri', 'unknown')
                texture_info.append(uri)

            texture_str = ", ".join(texture_info) if texture_info else "(embedded)"
            print(f"  ✓ {name:40} {texture_str}")

            texture_map[name] = {
                "textures": texture_info,
                "materials": len(materials),
                "has_materials": len(materials) > 0
            }

        print()

    print("="*70)
    print(f"Summary: Found {len(texture_map)} assets with material data")
    print("="*70 + "\n")

    # Save summary
    with open("texture_analysis.json", 'w') as f:
        json.dump(texture_map, f, indent=2)
    print("📄 Saved analysis to texture_analysis.json\n")

if __name__ == "__main__":
    check_assets()
