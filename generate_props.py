#!/usr/bin/env python3
"""
Generate .tscn prop scenes from all GLB assets automatically.
Run this standalone: python generate_props.py
"""

import os
import json
import struct
from pathlib import Path
from dataclasses import dataclass

@dataclass
class MeshBounds:
    min_x: float
    min_y: float
    min_z: float
    max_x: float
    max_y: float
    max_z: float

    @property
    def size_x(self) -> float:
        return self.max_x - self.min_x

    @property
    def size_y(self) -> float:
        return self.max_y - self.min_y

    @property
    def size_z(self) -> float:
        return self.max_z - self.min_z

    @property
    def center_y(self) -> float:
        return (self.min_y + self.max_y) / 2


def extract_bounds_from_glb(glb_path: str) -> MeshBounds | None:
    """Extract bounding box from GLB by reading GLTF JSON chunk."""
    try:
        with open(glb_path, 'rb') as f:
            magic = f.read(4)
            if magic != b'glTF':
                return None

            version = struct.unpack('<I', f.read(4))[0]
            length = struct.unpack('<I', f.read(4))[0]
            chunk_length = struct.unpack('<I', f.read(4))[0]
            chunk_type = f.read(4)

            if chunk_type != b'JSON':
                return None

            json_data = json.loads(f.read(chunk_length))
            accessors = json_data.get('accessors', [])
            meshes = json_data.get('meshes', [])

            if not meshes:
                return None

            overall_min = None
            overall_max = None

            for mesh in meshes:
                for primitive in mesh.get('primitives', []):
                    if 'POSITION' in primitive['attributes']:
                        accessor = accessors[primitive['attributes']['POSITION']]
                        if 'min' in accessor and 'max' in accessor:
                            acc_min = accessor['min']
                            acc_max = accessor['max']

                            if overall_min is None:
                                overall_min = acc_min[:]
                                overall_max = acc_max[:]
                            else:
                                for i in range(3):
                                    overall_min[i] = min(overall_min[i], acc_min[i])
                                    overall_max[i] = max(overall_max[i], acc_max[i])

            if overall_min and overall_max:
                return MeshBounds(*overall_min, *overall_max)
        return None
    except Exception as e:
        return None


def suggest_collision(bounds: MeshBounds) -> tuple[str, str]:
    """Suggest collision shape and properties."""
    sx, sy, sz = bounds.size_x, bounds.size_y, bounds.size_z

    # Thin cylinders: columns
    if sy > max(sx, sz) * 1.5 and sx < 0.5 and sz < 0.5:
        r = max(sx, sz) / 2
        return "CylinderShape3D", f"radius = {r:.3f}\nheight = {sy:.3f}"

    # Default: box
    return "BoxShape3D", f"size = Vector3({sx:.3f}, {sy:.3f}, {sz:.3f})"


def generate_tscn(category: str, glb_name: str, bounds: MeshBounds,
                 shape_type: str, shape_props: str) -> str:
    """Generate .tscn content."""

    node_name = glb_name.replace('-', '_').title().replace('_', '')
    rel_path = f"res://assets/models/{category}/{glb_name}.glb"
    offset_y = bounds.center_y

    return f"""[gd_scene format=3]

[ext_resource type="PackedScene" path="{rel_path}" id="1_glb"]

[sub_resource type="{shape_type}" id="CollisionShape"]
{shape_props}

[node name="{node_name}" type="StaticBody3D"]

[node name="Mesh" parent="." instance=ExtResource("1_glb")]

[node name="CollisionShape3D" type="CollisionShape3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, {offset_y:.3f}, 0)
shape = SubResource("CollisionShape")
"""


def main():
    base_dir = Path("assets/models")
    scenes_dir = Path("scenes/props")
    scenes_dir.mkdir(exist_ok=True)

    print("\n" + "="*60)
    print("  ASSET MODULARIZATION - Generating .tscn files")
    print("="*60 + "\n")

    total = 0
    created = {}

    for category in ["building", "cars", "city"]:
        cat_path = base_dir / category
        if not cat_path.exists():
            continue

        print(f"📦 {category.upper()}")
        print("-" * 60)

        for glb_file in sorted(cat_path.glob("*.glb")):
            name = glb_file.stem
            bounds = extract_bounds_from_glb(str(glb_file))

            if bounds is None:
                print(f"  ✗ {name:45} (failed to read)")
                continue

            shape_type, shape_props = suggest_collision(bounds)
            tscn_content = generate_tscn(category, name, bounds, shape_type, shape_props)

            tscn_path = scenes_dir / f"{name}.tscn"
            with open(tscn_path, 'w') as f:
                f.write(tscn_content)

            size_str = f"({bounds.size_x:.2f}×{bounds.size_y:.2f}×{bounds.size_z:.2f})"
            shape_str = shape_type.replace("Shape3D", "")
            print(f"  ✓ {name:40} {size_str:20} {shape_str}")

            created[name] = {
                "path": str(tscn_path),
                "type": shape_type,
                "size": (bounds.size_x, bounds.size_y, bounds.size_z)
            }
            total += 1

        print()

    print("="*60)
    print(f"✅ Generated {total} scene files in scenes/props/")
    print("="*60 + "\n")

    # Save index
    with open("props_index.json", 'w') as f:
        json.dump(created, f, indent=2)
    print("📄 Saved props index to props_index.json\n")


if __name__ == "__main__":
    main()
