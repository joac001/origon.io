#!/usr/bin/env python3
"""
Update stairs-closed.tscn to have proper ramp collision.
Copies the collision geometry from stairs-open since they're the same mechanically.
"""

from pathlib import Path

def update_stairs_closed():
    """Update stairs-closed collision to match stairs-open."""

    stairs_open = Path("scenes/props/stairs-open.tscn")
    stairs_closed = Path("scenes/props/stairs-closed.tscn")

    print("\n" + "="*70)
    print("  STAIRS-CLOSED COLLISION UPDATE")
    print("="*70 + "\n")

    if not stairs_open.exists():
        print("ERROR: stairs-open.tscn not found")
        return False

    if not stairs_closed.exists():
        print("ERROR: stairs-closed.tscn not found")
        return False

    # Read stairs-open to get correct collision config
    with open(stairs_open, 'r') as f:
        open_content = f.read()

    # Extract the BoxShape3D definition
    import re

    # Find BoxShape3D resource
    shape_match = re.search(r'\[sub_resource type="BoxShape3D" id="[^"]*"\]\n([^\n]*\n)*?(?=\[)', open_content)
    if not shape_match:
        print("ERROR: Could not extract BoxShape3D from stairs-open")
        return False

    shape_def = shape_match.group(0)
    print(f"Found collision shape in stairs-open.tscn:")
    print(f"  {shape_def[:100]}...\n")

    # Read stairs-closed
    with open(stairs_closed, 'r') as f:
        closed_content = f.read()

    # Replace the BoxShape3D in stairs-closed
    updated_closed = re.sub(
        r'\[sub_resource type="BoxShape3D" id="[^"]*"\]\n[^\n]*\n[^\n]*\n(?=\[)',
        shape_def,
        closed_content
    )

    # Also ensure the CollisionShape3D transform matches
    # The transform should apply the ramp rotation
    correct_transform = "transform = Transform3D(1, 0, 0, 0, 0.848, 0.53, 0, -0.53, 0.848, 0, 1.123, 0)"
    updated_closed = re.sub(
        r'transform = Transform3D\([^\)]+\)\s*\nshape = SubResource\("CollisionShape"\)',
        f"{correct_transform}\nshape = SubResource(\"CollisionShape\")",
        updated_closed
    )

    # Write updated stairs-closed
    with open(stairs_closed, 'w') as f:
        f.write(updated_closed)

    print("SUCCESS: stairs-closed.tscn updated with proper ramp collision")
    print("  - Collision shape now matches stairs-open.tscn")
    print("  - Visual difference remains (closed vs open sides)")
    print("  - Both are now climbable\n")
    print("="*70 + "\n")

    return True

if __name__ == "__main__":
    success = update_stairs_closed()
    exit(0 if success else 1)
