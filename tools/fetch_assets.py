#!/usr/bin/env python3
"""Downloads the CC0 assets listed in tools/asset_manifest.json.

Textures (Poly Haven) go to assets/textures/<id>/: albedo.jpg (sRGB), normal.jpg (OpenGL
convention) and arm.jpg (R = ambient occlusion, G = roughness, B = metallic).
Leaf atlases (ambientCG) go to .cache/ambientcg/<id>/: color.png and opacity.png; they are
inputs for tools/textures/gen_foliage.py. MakeHuman CC0 data (mesh, targets, rig, system asset pack)
goes to .cache/makehuman/ for tools/blender/gen_human.py. Downloaded files are not committed.
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


def safe_extract(archive, member, target_root, strip_prefix):
    """Writes one archive member under target_root, refusing absolute paths and '..'."""
    relative = member[len(strip_prefix):]
    path = pathlib.PurePosixPath(relative)
    if not relative or path.is_absolute() or ".." in path.parts or member.endswith("/"):
        return False
    target = target_root.joinpath(*path.parts)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(archive.read(member))
    return True


def fetch_makehuman(manifest):
    """MakeHuman CC0 data only: mesh, targets and rig files plus the system asset pack."""
    config = manifest.get("makehuman")
    if not config:
        return
    cache = ROOT / ".cache" / "makehuman"
    data = cache / "mpfb_data"
    if not (data / "3dobjs" / "base.obj").exists():
        request = urllib.request.Request(config["mpfb_zip"], headers={"User-Agent": "origon-asset-fetch"})
        with urllib.request.urlopen(request, timeout=600) as response:
            archive = zipfile.ZipFile(io.BytesIO(response.read()))
        count = 0
        for member in archive.namelist():
            marker = "/src/mpfb/data/"
            if marker not in member:
                continue
            prefix = member[: member.index(marker) + len(marker)]
            if any(member[len(prefix):].startswith(d) for d in config["mpfb_data"]):
                count += safe_extract(archive, member, data, prefix)
        print(f"  .cache/makehuman/mpfb_data ({count} files)")
    assets = cache / "system_assets"
    if not (assets / "clothes").exists():
        for url in config["packs"]:
            request = urllib.request.Request(url, headers={"User-Agent": "origon-asset-fetch"})
            with urllib.request.urlopen(request, timeout=900) as response:
                archive = zipfile.ZipFile(io.BytesIO(response.read()))
            count = sum(safe_extract(archive, member, assets, "") for member in archive.namelist())
            print(f"  .cache/makehuman/system_assets ({count} files)")


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
    fetch_makehuman(manifest)
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
