#!/usr/bin/env python3
"""Scale all prop scenes to match player size (0.5x)."""

from pathlib import Path
import re

def scale_mesh_node(content: str, scale_factor: float) -> str:
    """Add scale transform to Mesh instance nodes that don't have transform."""
    # Find all Mesh nodes that are instances and don't have explicit transform
    pattern = r'(\[node name="Mesh" parent="\." instance=ExtResource.*?\])\n'

    def replacer(match):
        node_def = match.group(1)
        scale_str = f"transform = Transform3D({scale_factor}, 0, 0, 0, {scale_factor}, 0, 0, 0, {scale_factor}, 0, 0, 0)"
        return f"{node_def}\n{scale_str}\n"

    return re.sub(pattern, replacer, content)

def main():
    """Scale all props in scenes/props/"""
    props_dir = Path("scenes/props")
    scale = 0.5
    scaled_count = 0

    for prop_file in sorted(props_dir.glob("*.tscn")):
        if "car_" in prop_file.name or "column" in prop_file.name or "wall" in prop_file.name or "stairs" in prop_file.name or "door" in prop_file.name or "roof" in prop_file.name or "barricade" in prop_file.name or "border" in prop_file.name or "floor" in prop_file.name or "building" in prop_file.name:
            content = prop_file.read_text()

            # Skip if already scaled
            if "transform = Transform3D(0.5" in content:
                continue

            new_content = scale_mesh_node(content, scale)

            if new_content != content:
                prop_file.write_text(new_content)
                print(f"[OK] Scaled {prop_file.name}")
                scaled_count += 1

    print(f"\nTotal scaled: {scaled_count} props")

if __name__ == "__main__":
    main()
