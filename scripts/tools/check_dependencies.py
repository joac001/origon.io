#!/usr/bin/env python3
"""
Check if all referenced props and resources exist.
Fast dependency validator for the project.
"""

import re
from pathlib import Path

def check_test_map():
    """Check if all props in test_map.tscn exist."""
    test_map = Path("scenes/maps/test_map.tscn")

    if not test_map.exists():
        print("ERROR: test_map.tscn not found")
        return False

    with open(test_map, 'r') as f:
        content = f.read()

    # Extract all ext_resource references
    ext_resources = re.findall(r'path="([^"]+)"', content)

    print("\n" + "="*70)
    print("  DEPENDENCY CHECK")
    print("="*70 + "\n")

    missing = []
    found = []

    for ref_path in sorted(set(ext_resources)):
        if ref_path.startswith("res://"):
            ref_path = ref_path.replace("res://", "")

        full_path = Path(ref_path)
        if full_path.exists():
            found.append(ref_path)
            print(f"  OK: {ref_path}")
        else:
            missing.append(ref_path)
            print(f"  XX: {ref_path} [MISSING]")

    print("\n" + "="*70)
    print(f"Found: {len(found)}")
    print(f"Missing: {len(missing)}")
    print("="*70 + "\n")

    if missing:
        print("MISSING FILES:")
        for m in missing:
            print(f"  - {m}")
        return False

    return True

if __name__ == "__main__":
    success = check_test_map()
    exit(0 if success else 1)
