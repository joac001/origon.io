#!/usr/bin/env python3
"""
Fix collision shapes for different stair types.
Stairs-closed needs proper ramp collision like stairs-open.
"""

import json
import struct
from pathlib import Path

def extract_bounds_from_glb(glb_path: str) -> tuple:
    """Extract bounding box dimensions from GLB."""
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
                sx = overall_max[0] - overall_min[0]
                sy = overall_max[1] - overall_min[1]
                sz = overall_max[2] - overall_min[2]
                cy = (overall_min[1] + overall_max[1]) / 2
                return (sx, sy, sz, cy)

        return None
    except:
        return None

def suggest_stairs_collision(glb_name: str) -> dict:
    """Suggest collision shape for stair types."""

    # These measurements are based on Kenney Building Kit
    suggestions = {
        "stairs-open": {
            "type": "BoxShape3D",
            "size": "Vector3(1.3, 0.3, 4.72)",
            "offset_y": 1.123,
            "rotation": "Transform3D(1, 0, 0, 0, 0.848, 0.53, 0, -0.53, 0.848, 0, 1.123, 0)",
            "note": "Ramp collision tilted 32 degrees. Both ends flush with floor surfaces."
        },
        "stairs-open-short": {
            "type": "BoxShape3D",
            "size": "Vector3(1.3, 0.3, 2.36)",
            "offset_y": 0.562,
            "rotation": "Transform3D(1, 0, 0, 0, 0.848, 0.53, 0, -0.53, 0.848, 0, 0.562, 0)",
            "note": "Short staircase version. Same angle, half length."
        },
        "stairs-closed": {
            "type": "BoxShape3D",
            "size": "Vector3(1.3, 0.3, 4.72)",
            "offset_y": 1.123,
            "rotation": "Transform3D(1, 0, 0, 0, 0.848, 0.53, 0, -0.53, 0.848, 0, 1.123, 0)",
            "note": "Closed stairs use same ramp as open. Collision shape identical."
        },
        "stairs-closed-short": {
            "type": "BoxShape3D",
            "size": "Vector3(1.3, 0.3, 2.36)",
            "offset_y": 0.562,
            "rotation": "Transform3D(1, 0, 0, 0, 0.848, 0.53, 0, -0.53, 0.848, 0, 0.562, 0)",
            "note": "Short closed staircase. Same collision as short-open."
        },
        "stairs-center": {
            "type": "BoxShape3D",
            "size": "Vector3(1.3, 0.3, 4.72)",
            "offset_y": 1.123,
            "rotation": "Transform3D(1, 0, 0, 0, 0.848, 0.53, 0, -0.53, 0.848, 0, 1.123, 0)",
            "note": "Center-supported stairs. Same collision geometry."
        },
        "stairs-sides": {
            "type": "BoxShape3D",
            "size": "Vector3(1.3, 0.3, 4.72)",
            "offset_y": 1.123,
            "rotation": "Transform3D(1, 0, 0, 0, 0.848, 0.53, 0, -0.53, 0.848, 0, 1.123, 0)",
            "note": "Side-supported stairs. Same collision geometry."
        },
    }

    return suggestions.get(glb_name, None)

def main():
    print("\n" + "="*70)
    print("  STAIRS COLLISION ANALYZER")
    print("="*70 + "\n")

    stair_types = [
        "stairs-open",
        "stairs-open-short",
        "stairs-closed",
        "stairs-closed-short",
        "stairs-center",
        "stairs-center-short",
        "stairs-sides",
        "stairs-sides-short",
    ]

    print("Stair types and recommended collisions:\n")

    for stair_type in stair_types:
        suggestion = suggest_stairs_collision(stair_type)
        glb_path = f"assets/models/building/{stair_type}.glb"

        exists = Path(glb_path).exists()
        status = "EXISTS" if exists else "MISSING"

        if suggestion:
            print(f"{stair_type:25} [{status}]")
            print(f"  Type: {suggestion['type']}")
            print(f"  Size: {suggestion['size']}")
            print(f"  Note: {suggestion['note']}\n")
        else:
            print(f"{stair_type:25} [{status}] - No suggestion yet\n")

    print("="*70)
    print("ACTION: All closed stairs should use SAME collision as open stairs.")
    print("The difference is visual only (closed vs open sides).")
    print("="*70 + "\n")

if __name__ == "__main__":
    main()
