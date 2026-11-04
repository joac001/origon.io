# Origon

Online FPS on a custom C++20 engine (no commercial engine). First mode: Gun Game, free-for-all,
up to 10 players. Target look: dense photoreal forest (see `docs/art-direction.md`).

## Features

- **Renderer (OpenGL 4.5):** depth prepass, cascaded shadow maps with PCF, SSAO, bloom,
  screen-space light shafts, MSAA with alpha-to-coverage foliage, HDR + ACES, render scale,
  terrain splatting with CC0 scanned textures, water with refraction and depth absorption,
  instanced vegetation with wind, LOD and frustum culling, tangent-free normal mapping.
- **Physics (Jolt):** heightfield terrain, static mesh collision for trees/rocks, dynamic props
  (pushable, shootable), cosmetic debris (casings, corpses), character controller with crouch.
- **Characters:** realistic male and female soldiers built from MakeHuman CC0 data with GPU
  skinning; cosmetic looks (skin, hair and color, facial hair, helmets and hats, eyewear, outfit
  and color, shoes) chosen in the *Personaje* menu tab; idle, run, crouch, reload, knife and death
  clips; everyone shares the same hit capsules.
- **Gameplay:** 20-tier Gun Game with WWII weapons, knife humiliation, suicide/fall demotion,
  spawn protection, bots, hitscan with spread/falloff/headshots, recoil, ADS and scopes.
- **Networking (ENet):** listen server, direct IP; custom maps are sent to joining players;
  client-side prediction; 30 Hz snapshots; automatic rematch.
- **Audio (miniaudio):** 3D positional sound. Recorded CC0 sounds from Freesound (gunshots per
  weapon class with variants, distant shots, bolt cycles, reloads, brass casings, footsteps on dirt
  and wood, knife, hits, forest/wind/creek ambience loops), built by `tools/sounds/build_sounds.py`;
  procedurally synthesized fallbacks for anything missing, plus UI cues.
- **Map builder (F1):** gizmos, fly/orbit/pan/zoom camera, prefabs, materials, terrain sculpting
  and path painting, water level, vegetation layers, undo/redo, copy/paste, validation.
- **Graphics presets:** Batata / Media / Alta / Ultra. Settings never change collision or the
  visibility of gameplay-relevant objects; only low grass follows the density setting.

Measured at 1920x1080 on the `bosque` map (23k vegetation instances):

| GPU | Preset | Frame time |
|---|---|---|
| RTX 5060 Ti | Ultra (MSAA 8x, 4 cascades, SSAO, bloom, light shafts) | 5.8 ms (176 FPS) |
| RTX 5060 Ti | Batata | 0.8 ms |
| Radeon 610M iGPU (2 CUs) | Batata | 16.4 ms (61 FPS) |

## Build

Requirements: CMake ≥ 3.24, a C++20 compiler (GCC 13+, Clang 17+, MSVC 2022), Python 3 with
`jinja2` (glad loader generation), and a GPU with OpenGL 4.5. Linux also needs the X11/Wayland
development packages that GLFW requires.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
python3 tools/fetch_assets.py      # CC0 textures, leaf atlases, MakeHuman data (once, ~400 MB download)
$BLENDER --background --factory-startup --python tools/blender/gen_human.py   # character textures
./build/game/origon --map bosque
```

All C/C++ dependencies (GLFW, glad, glm, nlohmann/json, Jolt, Dear ImGui, ImGuizmo, ENet,
miniaudio, cgltf, stb) are fetched and pinned by `cmake/Dependencies.cmake`.

Tests (headless): `ctest --test-dir build --output-on-failure`

## Playing online

- Host: menu (Esc) → **Multijugador** → *Crear partida*, or `origon --host 27015`.
- Join: menu → **Multijugador**: LAN games appear automatically (UDP broadcast on port 27020);
  or type an IP and *Unirse*, or `origon --connect 192.168.1.10:27015 --name Ana`.
- Internet without opening ports: run the relay (`origon_relay`, UDP 27030) on any machine with a
  public IP, e.g. a small VPS. Set *Servidor intermedio* to `host:27030` in **Multijugador**.
  The host clicks *Crear partida y publicarla por internet* and shares the five-character code;
  players pick the room from the list or type the code. Both sides only connect out, so NAT
  is not a problem. Every packet goes through the relay, which adds the relay's round trip.
  Command line: `origon --host --publish --relay HOST:27030` and
  `origon --join-code ABCDE --relay HOST:27030`.
- Direct IP still works: open UDP port 27015 on the host's router, or use a LAN VPN.
- Joining players replace bots. The host's current map (including custom maps) is sent to them.

## Controls

| Key | Action |
|---|---|
| WASD | Move |
| Shift | Sprint |
| Space | Jump |
| Ctrl / C | Crouch |
| Left mouse | Fire |
| Right mouse | Aim down sights |
| R | Reload |
| V | Knife |
| Tab | Scoreboard |
| Esc | Menu (graphics, audio, controls, multiplayer, match) |
| F1 | Map builder |
| F3 | Performance overlay |
| F12 | Screenshot (`screenshots/`) |

Map builder: right mouse + WASD/QE to fly (wheel: speed), Alt + left mouse to orbit, middle mouse
to pan, wheel to zoom toward the cursor, F to frame the selection. Q/W/E/R select, move, rotate,
scale; 1-0 place prefabs (T rotates); B sculpts terrain. Ctrl+Z/Y, Ctrl+C/V, Ctrl+D, Delete.

## Asset pipeline

Everything is generated by scripts, no manual modeling:

```sh
BLENDER=~/.local/share/origon-tools/blender-4.5.15-linux-x64/blender
$BLENDER --background --factory-startup --python tools/blender/gen_vegetation.py
$BLENDER --background --factory-startup --python tools/blender/gen_weapons.py
$BLENDER --background --factory-startup --python tools/blender/gen_human.py
python3 tools/textures/gen_foliage.py
./build/tools/mapgen/origon_mapgen bosque
./build/tools/mapgen/origon_mapgen aserradero
```

See `docs/asset-pipeline.md`.

## Command line

`origon [--map NAME] [--editor] [--host [PORT]] [--connect HOST[:PORT]] [--name NAME]`
`[--relay HOST[:PORT]] [--publish] [--join-code CODE]`
`[--autoplay] [--hide-ui] [--title] [--follow PLAYER] [--pose-gallery] [--look-gallery SEED] [--camera X Y Z YAW PITCH] [--shot-size WxH] [--screenshot FILE] [--screenshot-frame N]`

`origon_relay [--port 27030] [--max-rooms 256]`: the relay server (headless).

## Layout

```
engine/   window, input, renderer, models, textures, physics, audio  -> origon_engine
game/     sim/ (authoritative simulation, Gun Game, bots), world/ (maps, terrain, vegetation) -> origon_sim
          editor/, ui/, fx/, audio/, net/, App -> origon executable
tools/    blender/ (mesh generators), textures/, mapgen/, fetch_assets.py
assets/   shaders, fonts, models (generated), textures (foliage generated, scans fetched)
data/     weapons.json, gungame_ladder.json
maps/     graybox.json, bosque.json (creek forest), aserradero.json (lakeside sawmill)
docs/     architecture, asset pipeline, art direction, research
tests/    headless simulation tests
```

Code, file names, comments and commits are in English. All player-facing text is in Spanish.
