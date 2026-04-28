#!/usr/bin/env python3
"""
Fix collision shapes to match 0.5x scaled meshes.
- Scales BoxShape3D.size by 0.5
- Scales CapsuleShape3D.radius and height by 0.5
- Scales CylinderShape3D.radius and height by 0.5
- Scales CollisionShape3D transform translation by 0.5
  (rotation stays unchanged — it controls shape orientation, not scale)
Skips cars (mesh at 1.0x, collision already correct).
"""

import re
from pathlib import Path

SKIP_FILES = {"car_sedan.tscn", "car_suv.tscn", "car_police.tscn"}
SCALE = 0.5


def scale_float(val: str) -> str:
    return f"{round(float(val) * SCALE, 6):g}"


def scale_vector3(line: str) -> str:
    """Scale Vector3(x, y, z) values by SCALE."""
    def replacer(m):
        x, y, z = m.group(1), m.group(2), m.group(3)
        return f"Vector3({scale_float(x)}, {scale_float(y)}, {scale_float(z)})"
    return re.sub(
        r'Vector3\(\s*(-?[\d.e+\-]+)\s*,\s*(-?[\d.e+\-]+)\s*,\s*(-?[\d.e+\-]+)\s*\)',
        replacer, line
    )


def scale_transform_translation(line: str) -> str:
    """
    Scale only the translation (last 3 values) of a Transform3D.
    Transform3D(r00,r01,r02, r10,r11,r12, r20,r21,r22, tx,ty,tz)
    Rotation matrix (first 9 values) is unchanged.
    """
    m = re.match(r'^(\s*transform\s*=\s*Transform3D\()(.+)(\)\s*)$', line)
    if not m:
        return line
    prefix, inner, suffix = m.group(1), m.group(2), m.group(3)
    vals = [v.strip() for v in inner.split(',')]
    if len(vals) != 12:
        return line
    # Scale only translation (indices 9, 10, 11)
    for i in (9, 10, 11):
        vals[i] = f"{round(float(vals[i]) * SCALE, 7):g}"
    return f"{prefix}{', '.join(vals)}{suffix}"


def needs_scaling(content: str) -> bool:
    """Return True if the file has a 0.5x mesh but un-scaled collision shapes."""
    has_scaled_mesh = 'Transform3D(0.5, 0, 0, 0, 0.5, 0, 0, 0, 0.5,' in content
    return has_scaled_mesh


def fix_file(path: Path) -> bool:
    content = path.read_text(encoding='utf-8')

    if not needs_scaling(content):
        return False  # Already fine or cars handled separately

    lines = content.splitlines(keepends=True)
    new_lines = []
    in_sub_resource = False
    current_sub_type = None

    for line in lines:
        stripped = line.strip()

        # Track which sub_resource block we're in
        if stripped.startswith('[sub_resource'):
            in_sub_resource = True
            current_sub_type = None
            m = re.search(r'type="(\w+)"', stripped)
            if m:
                current_sub_type = m.group(1)
            new_lines.append(line)
            continue

        if stripped.startswith('[') and not stripped.startswith('[sub_resource'):
            in_sub_resource = False
            current_sub_type = None

        if in_sub_resource and current_sub_type in ('BoxShape3D', 'ConvexPolygonShape3D'):
            if stripped.startswith('size =') and 'Vector3' in stripped:
                line = scale_vector3(line)

        elif in_sub_resource and current_sub_type in ('CapsuleShape3D', 'CylinderShape3D', 'SphereShape3D'):
            if stripped.startswith('radius ='):
                m = re.match(r'^(\s*radius\s*=\s*)(-?[\d.e+\-]+)', line)
                if m:
                    line = f"{m.group(1)}{scale_float(m.group(2))}\n"
            elif stripped.startswith('height ='):
                m = re.match(r'^(\s*height\s*=\s*)(-?[\d.e+\-]+)', line)
                if m:
                    line = f"{m.group(1)}{scale_float(m.group(2))}\n"

        # Scale CollisionShape3D transform translation (not mesh transform)
        elif not in_sub_resource and stripped.startswith('transform = Transform3D('):
            # Only CollisionShape3D nodes — skip mesh transforms (already have 0.5)
            if 'Transform3D(0.5, 0, 0,' not in stripped:
                line = scale_transform_translation(line)

        new_lines.append(line)

    new_content = ''.join(new_lines)
    if new_content != content:
        path.write_text(new_content, encoding='utf-8')
        return True
    return False


def main():
    props_dir = Path("scenes/props")
    fixed, skipped = 0, 0

    for prop_file in sorted(props_dir.glob("*.tscn")):
        if prop_file.name in SKIP_FILES:
            skipped += 1
            continue
        if fix_file(prop_file):
            print(f"[OK] Fixed {prop_file.name}")
            fixed += 1

    print(f"\nFixed: {fixed} props | Skipped (cars): {skipped}")


if __name__ == "__main__":
    main()
