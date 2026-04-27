#!/usr/bin/env python3
"""
Check if all props have valid internal references to GLB assets.
"""

import re
from pathlib import Path

def check_prop(prop_file):
    """Check if a prop references existing GLB."""
    if not prop_file.exists():
        return None

    with open(prop_file, 'r') as f:
        content = f.read()

    # Find asset path reference
    match = re.search(r'path="(res://assets/models/[^"]+)"', content)
    if not match:
        return "NO_GLB_REF"

    asset_path = match.group(1).replace("res://", "")
    if Path(asset_path).exists():
        return "OK"
    else:
        return f"MISSING: {asset_path}"

def main():
    props_dir = Path("scenes/props")

    print("\n" + "="*70)
    print("  PROP REFERENCE CHECK")
    print("="*70 + "\n")

    issues = []
    ok_count = 0

    for prop_file in sorted(props_dir.glob("*.tscn")):
        result = check_prop(prop_file)
        if result == "OK":
            ok_count += 1
        else:
            print(f"  ISSUE: {prop_file.name:40} -> {result}")
            issues.append((prop_file.name, result))

    print("\n" + "="*70)
    print(f"OK: {ok_count}")
    print(f"Issues: {len(issues)}")
    print("="*70 + "\n")

    if issues:
        print("FAILED PROPS:")
        for name, issue in issues:
            print(f"  - {name}: {issue}")
        return False
    return True

if __name__ == "__main__":
    success = main()
    exit(0 if success else 1)
