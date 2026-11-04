#!/usr/bin/env python3
"""Downloads the CC0 assets listed in tools/asset_manifest.json.

Textures (Poly Haven) go to assets/textures/<id>/: albedo.jpg (sRGB), normal.jpg (OpenGL
convention) and arm.jpg (R = ambient occlusion, G = roughness, B = metallic).
Leaf atlases (ambientCG) go to .cache/ambientcg/<id>/: color.png and opacity.png; they are
inputs for tools/textures/gen_foliage.py. Downloaded files are not committed.
"""
import io
import json
import pathlib
import sys
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
API = "https://api.polyhaven.com/files/{}"
ACG_ZIP = "https://ambientcg.com/get?file={}_{}.zip"
ACG_MAPS = {"color.png": "_Color.png", "opacity.png": "_Opacity.png"}
MAPS = {"albedo": "Diffuse", "normal": "nor_gl", "arm": "arm"}


def fetch_json(url):
    request = urllib.request.Request(url, headers={"User-Agent": "origon-asset-fetch"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def download(url, target):
    request = urllib.request.Request(url, headers={"User-Agent": "origon-asset-fetch"})
    with urllib.request.urlopen(request, timeout=120) as response:
        target.write_bytes(response.read())


def fetch_atlases(manifest):
    atlases = manifest.get("atlases")
    if not atlases:
        return
    cache = ROOT / ".cache" / "ambientcg"
    for entry in atlases["items"]:
        asset_id = entry["id"]
        folder = cache / asset_id
        if all((folder / name).exists() for name in ACG_MAPS):
            continue
        folder.mkdir(parents=True, exist_ok=True)
        url = ACG_ZIP.format(asset_id, atlases["resolution"])
        request = urllib.request.Request(url, headers={"User-Agent": "origon-asset-fetch"})
        with urllib.request.urlopen(request, timeout=300) as response:
            archive = zipfile.ZipFile(io.BytesIO(response.read()))
        # Read only the two maps we need, by name; never extract arbitrary archive paths.
        for name, suffix in ACG_MAPS.items():
            member = next((m for m in archive.namelist() if m.endswith(suffix) and "/" not in m), None)
            if member is None:
                print(f"  {asset_id}: no '{suffix}' map, skipped")
                continue
            (folder / name).write_bytes(archive.read(member))
        print(f"  .cache/ambientcg/{asset_id}")


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
    fetch_atlases(manifest)
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
