# Asset Pipeline

One pipeline for the whole project. No manual modeling.

## 1. Meshes: headless Blender + Python

- Generators live in `tools/blender/` as Python scripts that run with
  `blender --background --python tools/blender/<script>.py -- <args>`.
- Each script builds its geometry from code (bmesh, modifiers, geometry nodes), unwraps UVs,
  generates LODs with the Decimate modifier, and exports one `.glb` per asset to
  `assets/models/<category>/<name>.glb`.
- LOD convention: mesh nodes named `<name>_LOD0`, `<name>_LOD1`, `<name>_LOD2`, plus
  `<name>_COL` for the collision mesh (convex or simplified). The engine loads collision only
  from `_COL` (competitive parity).
- Trees also export an impostor (billboard atlas) for the far LOD.
- Scripts are deterministic: same seed, same output. The seed is a script argument.
- Planned generators: `rock.py`, `tree_conifer.py`, `tree_redwood.py`, `fern.py`,
  `grass_cards.py`, `log.py`, weapon generators per category (`weapon_smg.py`, ...).

## 2. Textures: CC0 scanned PBR materials

- Source: Poly Haven and ambientCG (both CC0). Photoreal surfaces (wet bark, moss, porous rock,
  forest floor, worn cloth) need scanned data; code-generated textures do not reach that look.
- `tools/fetch_assets.py` downloads a pinned list (`tools/asset_manifest.json`: id, resolution,
  checksum) into `assets/textures/`. Downloaded files are not committed. The manifest is.
- Channel packing for the engine: albedo (sRGB), normal (OpenGL convention, +Y),
  ORM (R = ambient occlusion, G = roughness, B = metallic).
- HDRIs from Poly Haven for image-based lighting.

## 3. Requirements

- Blender 4.x or newer on PATH (not installed yet on the dev machine).
- Python 3 for `tools/fetch_assets.py`.

The graybox prototype needs neither: it uses procedural boxes defined in map JSON.
