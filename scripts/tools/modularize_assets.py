#!/usr/bin/env python3
"""
Modularize all GLB assets into reusable .tscn scenes with collision shapes.
Automatically generates scene files with StaticBody3D + MeshInstance3D + CollisionShape3D.
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
    def center_x(self) -> float:
        return (self.min_x + self.max_x) / 2

    @property
    def center_y(self) -> float:
        return (self.min_y + self.max_y) / 2

    @property
    def center_z(self) -> float:
        return (self.min_z + self.max_z) / 2


def extract_bounds_from_glb(glb_path: str) -> MeshBounds | None:
    """Extract bounding box from a GLB file by reading GLTF JSON."""
    try:
        with open(glb_path, 'rb') as f:
            magic = f.read(4)
            if magic != b'glTF':
                print(f"  ✗ Not a valid GLB file: {glb_path}")
                return None

            version = struct.unpack('<I', f.read(4))[0]
            length = struct.unpack('<I', f.read(4))[0]

            chunk_length = struct.unpack('<I', f.read(4))[0]
            chunk_type = f.read(4)

            if chunk_type != b'JSON':
                print(f"  ✗ First chunk is not JSON: {glb_path}")
                return None

            json_data = json.loads(f.read(chunk_length))

            meshes = json_data.get('meshes', [])
            if not meshes:
                print(f"  ⚠ No meshes found in {glb_path}")
                return None

            accessors = json_data.get('accessors', [])
            buffers = json_data.get('bufferViews', [])

            overall_min = None
            overall_max = None

            for mesh in meshes:
                for primitive in mesh.get('primitives', []):
                    if 'POSITION' in primitive['attributes']:
                        pos_accessor_idx = primitive['attributes']['POSITION']
                        accessor = accessors[pos_accessor_idx]

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
                return MeshBounds(
                    overall_min[0], overall_min[1], overall_min[2],
                    overall_max[0], overall_max[1], overall_max[2]
                )

            return None
    except Exception as e:
        print(f"  ✗ Error reading {glb_path}: {e}")
        return None


def suggest_collision_shape(bounds: MeshBounds, name: str) -> tuple[str, str]:
    """Suggest collision shape type based on dimensions."""
    sx, sy, sz = bounds.size_x, bounds.size_y, bounds.size_z

    # Thin vertical cylinders: columns
    if sy > max(sx, sz) * 1.5 and sx < 0.5 and sz < 0.5:
        radius = max(sx, sz) / 2
        return "cylinder", f'radius = {radius:.3f}\nheight = {sy:.3f}'

    # Thin vertical boxes: walls, doors
    if sy > 1.0 and (sx < 0.3 or sz < 0.3):
        return "box", f'size = Vector3({sx:.3f}, {sy:.3f}, {sz:.3f})'

    # Low horizontal boxes: floors, low walls
    if sy < 0.5 and sx > 1.0 and sz > 1.0:
        return "box", f'size = Vector3({sx:.3f}, {sy:.3f}, {sz:.3f})'

    # Default: box
    return "box", f'size = Vector3({sx:.3f}, {sy:.3f}, {sz:.3f})'


def generate_tscn_template(glb_path: str, asset_name: str, bounds: MeshBounds,
                          collision_type: str, collision_props: str) -> str:
    """Generate .tscn scene template."""

    rel_path = f"res://assets/models/{glb_path}"
    node_name = asset_name.replace('-', '_').title().replace('_', '')

    cx, cy, cz = bounds.center_x, bounds.center_y, bounds.center_z
    offset_y = cy

    shape_id = f"Shape3D_{asset_name}"

    tscn = f"""[gd_scene format=3]

[ext_resource type="PackedScene" path="{rel_path}" id="1_glb"]

[sub_resource type="{collision_type.title()}Shape3D" id="{shape_id}"]
{collision_props}

[node name="{node_name}" type="StaticBody3D"]

[node name="Mesh" parent="." instance=ExtResource("1_glb")]

[node name="CollisionShape3D" type="CollisionShape3D" parent="."]
transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, {offset_y:.3f}, 0)
shape = SubResource("{shape_id}")
"""
    return tscn


def modularize_assets():
    """Main function: scan all GLBs and generate .tscn files."""

    base_dir = Path("assets/models")
    scenes_dir = Path("scenes/props")

    print("\n=== Asset Modularization ===\n")

    asset_map = {}

    for category_dir in ["building", "cars", "city", "characters"]:
        cat_path = base_dir / category_dir
        if not cat_path.exists():
            print(f"⚠ Skipping {category_dir} (not found)")
            continue

        print(f"\n📁 {category_dir.upper()}")
        print("-" * 50)

        for glb_file in sorted(cat_path.glob("*.glb")):
            asset_name = glb_file.stem

            bounds = extract_bounds_from_glb(str(glb_file))
            if bounds is None:
                continue

            collision_type, collision_props = suggest_collision_shape(bounds, asset_name)

            tscn_name = f"{asset_name}.tscn"
            tscn_path = scenes_dir / tscn_name

            tscn_content = generate_tscn_template(
                f"{category_dir}/{glb_file.name}",
                asset_name,
                bounds,
                collision_type,
                collision_props
            )

            with open(tscn_path, 'w') as f:
                f.write(tscn_content)

            asset_map[asset_name] = {
                "path": str(tscn_path),
                "bounds": {
                    "size": (bounds.size_x, bounds.size_y, bounds.size_z),
                    "center": (bounds.center_x, bounds.center_y, bounds.center_z)
                },
                "collision": {
                    "type": collision_type,
                    "props": collision_props
                }
            }

            dims = f"({bounds.size_x:.2f} × {bounds.size_y:.2f} × {bounds.size_z:.2f})"
            print(f"  ✓ {asset_name:40} {dims:25} [{collision_type}]")

    print("\n" + "=" * 50)
    print(f"✓ Generated {len(asset_map)} scene files in {scenes_dir}")

    # Save map for reference
    with open("asset_modularization_map.json", 'w') as f:
        json.dump(asset_map, f, indent=2)
    print(f"✓ Saved asset map to asset_modularization_map.json")

    return asset_map


if __name__ == "__main__":
    modularize_assets()
