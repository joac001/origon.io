#!/usr/bin/env python3
"""Scale all prop scenes to 0.5x for consistency. Skips cars (handled separately)."""

from pathlib import Path
import re

SKIP_FILES = {"car_sedan.tscn", "car_suv.tscn", "car_police.tscn"}

def scale_mesh_node(content: str, scale_factor: float) -> str:
    """Add scale transform to Mesh instance nodes that don't have transform yet."""
    # Match Mesh node with optional unique_id, that uses ExtResource as instance
    pattern = re.compile(
        r'(\[node name="Mesh"[^\]]*instance=ExtResource\([^\)]+\)\])\n(?!transform =)',
        re.MULTILINE
    )

    scale_str = f"transform = Transform3D({scale_factor}, 0, 0, 0, {scale_factor}, 0, 0, 0, {scale_factor}, 0, 0, 0)"
    return pattern.sub(r'\1\n' + scale_str + '\n', content)

def main():
    """Scale all props in scenes/props/"""
    props_dir = Path("scenes/props")
    scale = 0.5
    scaled_count = 0
    skipped_count = 0

    for prop_file in sorted(props_dir.glob("*.tscn")):
        if prop_file.name in SKIP_FILES:
            print(f"[SKIP] {prop_file.name} (cars at 1.0x)")
            skipped_count += 1
            continue

        content = prop_file.read_text()
        new_content = scale_mesh_node(content, scale)

        if new_content != content:
            prop_file.write_text(new_content)
            print(f"[OK] Scaled {prop_file.name}")
            scaled_count += 1

    print(f"\nTotal scaled: {scaled_count} props")
    print(f"Total skipped: {skipped_count} props")

if __name__ == "__main__":
    main()
