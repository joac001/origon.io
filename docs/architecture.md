# Origon — Engine Architecture

Origon is a custom C++20 engine and online FPS. No commercial engine.

## 1. Stack

| Area | Library | Notes |
|---|---|---|
| Window, input | GLFW 3.4 | |
| Graphics | OpenGL 4.5 core, DSA | Loader: glad. A thin render layer keeps a Vulkan port possible. |
| Math | glm | |
| Physics, collision | Jolt Physics | `CharacterVirtual` for the FPS controller, ray casts for hitscan |
| Networking | ENet (UDP) | Listen server, direct IP. Phase 2. |
| Tools UI | Dear ImGui (docking) | Map builder, debug panels, in-game menus |
| Audio | miniaudio | Phase 2 |
| Serialization | nlohmann/json | Maps, settings, weapon data |
| Build | CMake ≥ 3.24 + FetchContent | All dependencies pinned and fetched at configure time |

Targets: Linux and Windows. macOS is out of scope (OpenGL capped at 4.1).

## 2. Repository layout

```
CMakeLists.txt          root build, options
cmake/                  dependency fetching
engine/                 origon_engine static library (no game rules)
  core/                 window, input, logging, time, file IO
  render/               GL wrappers, renderer, shadows, camera, settings
  physics/              Jolt wrapper
game/                   origon executable
  sim/                  authoritative simulation: player, weapons, gun game, dummies
  world/                map data, JSON IO, world build (render + physics from map data)
  editor/               in-game map builder (ImGui)
  ui/                   HUD and menus (Spanish text)
assets/                 runtime assets: shaders, fonts, (later) .glb meshes and textures
data/                   gameplay data: weapons, gun game ladder
maps/                   map files (.json)
tools/blender/          headless Blender generators (asset pipeline)
docs/                   design and research
```

## 3. Simulation model (network-ready)

- Fixed tick: 60 Hz. Rendering runs at any frame rate and interpolates between ticks.
- Input is sampled into a `PlayerCommand` (move axes, view angles, buttons) once per tick.
- The simulation reads only `PlayerCommand`s. It never reads GLFW directly.
- Clients send `PlayerCommand`s to the host over ENet (unreliable). The host runs the
  simulation and sends 30 Hz snapshots (players, match state, dynamic props) plus reliable
  gameplay events. Clients predict their own movement with the same movement code and snap
  back when the error exceeds 1.2 m; remote players are interpolated between snapshots.
- Known limits: no lag compensation for hitscan yet (the host judges hits at its own time),
  and weapon fire feedback on clients waits for the server event.
- Game rules (Gun Game) live only in the simulation. The UI reads simulation state.

## 4. Rendering plan

Prototype (graybox):
- Forward renderer, HDR target, ACES tonemap, gamma.
- Directional sun with one shadow map and PCF soft shadows. Hemispheric ambient. Distance fog.
- Cook-Torrance PBR lighting with constant material values per block.
- Up to 32 point lights from the map.

Forest phase (target look: dense organic forest, see `docs/art-direction.md`):
- Cascaded shadow maps (CSM), PCSS soft shadows for vegetation.
- GPU-driven instanced foliage with compute culling and LOD selection, alpha-to-coverage.
- Heightmap terrain with clipmap LOD and splat materials (moss, dirt, rock).
- Volumetric light (froxel fog) for god rays. SSAO/GTAO. SSR and refractive water. Bloom, DOF.
- Texture streaming and BC7 compressed textures.

## 5. Graphics settings and competitive parity

Settings change only how things look, never what exists in the world:
- Collision always comes from the map data and the full-detail collision mesh. It never comes
  from a render LOD.
- Gameplay-relevant geometry (walls, rocks, trunks, cover) is never culled by quality settings.
  It only switches LOD.
- Grass and small ground foliage can be reduced on low settings. On all settings, the engine
  never renders vegetation that blocks a player's view in a way other settings would not.
  Rule: foliage tall enough to hide a player (> 0.6 m) belongs to the "gameplay" layer and
  renders at all settings, with LOD only.
- Fog and lighting values that affect visibility (fog density, darkness) are fixed per map.
  Settings only change their quality (resolution, sample counts).

Settings exposed (Spanish UI, `settings.json`): resolution scale, vsync, FOV, shadow quality
(off/low/medium/high/ultra = 0/1024/2048/4096 + PCF taps), volumetric light, SSAO, SSR,
bloom, depth of field, foliage density and distance, LOD bias, max FPS.

## 6. Maps

- JSON format (`maps/*.json`), versioned with a `"version"` field.
- Entities: blocks (box geometry with material), spawn points, lights, and the sun settings.
- The in-game builder (F1) edits the same data and saves to the same format. A saved map can
  be hosted directly.
