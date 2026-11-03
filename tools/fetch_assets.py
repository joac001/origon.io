#!/usr/bin/env python3
"""Downloads the CC0 textures listed in tools/asset_manifest.json into assets/textures/<id>/.

Each texture gets albedo.jpg (sRGB), normal.jpg (OpenGL convention) and arm.jpg
(R = ambient occlusion, G = roughness, B = metallic). Downloaded files are not committed.
"""
import json
import pathlib
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
API = "https://api.polyhaven.com/files/{}"
MAPS = {"albedo": "Diffuse", "normal": "nor_gl", "arm": "arm"}


def fetch_json(url):
    request = urllib.request.Request(url, headers={"User-Agent": "origon-asset-fetch"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def download(url, target):
    request = urllib.request.Request(url, headers={"User-Agent": "origon-asset-fetch"})
    with urllib.request.urlopen(request, timeout=120) as response:
        target.write_bytes(response.read())


def main():
    manifest = json.loads((ROOT / "tools" / "asset_manifest.json").read_text())
    resolution = manifest["resolution"]
    for entry in manifest["textures"]:
        asset_id = entry["id"]
        folder = ROOT / "assets" / "textures" / asset_id
        folder.mkdir(parents=True, exist_ok=True)
        files = None
        for name, key in MAPS.items():
            target = folder / f"{name}.jpg"
            if target.exists():
                continue
            if files is None:
                files = fetch_json(API.format(asset_id))
            if key not in files:
                print(f"  {asset_id}: no '{key}' map, skipped")
                continue
            url = files[key][resolution]["jpg"]["url"]
            download(url, target)
            print(f"  {asset_id}/{name}.jpg")
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
