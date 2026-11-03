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

- Blender 4.5 LTS (portable build in `~/.local/share/origon-tools/` on the dev machine).
- Python 3 for `tools/fetch_assets.py`, plus Pillow for `tools/textures/gen_foliage.py`.

## 4. Current generators

| Script | Output |
|---|---|
| `tools/blender/gen_vegetation.py` | `assets/models/vegetation/*.glb`: pine_a, pine_b, fir_a, redwood, maple, rock_a, rock_b, boulder, fern, grass, bush, log, stump (LOD0-2, `_COL` collision) |
| `tools/blender/gen_weapons.py` | `assets/models/weapons/*.glb`: the 19 ladder weapons and the knife |
| `tools/textures/gen_foliage.py` | `assets/textures/foliage/*.png`: needles, fern, leaves, grass cards |
| `tools/fetch_assets.py` | `assets/textures/<id>/{albedo,normal,arm}.jpg` from Poly Haven (CC0) |
| `tools/mapgen` (`origon_mapgen`) | `maps/bosque.json` |

Material naming contract (models do not embed images): `bark_<id>`, `rock_<id>` and `pbr_<id>`
use `assets/textures/<id>/`; `foliage_<name>` uses `assets/textures/foliage/<name>.png` with
alpha testing, two-sided lighting and wind; other materials use their glTF color factors.
