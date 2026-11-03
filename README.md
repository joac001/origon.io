# Origon

Online FPS on a custom C++20 engine (no commercial engine). First mode: Gun Game, free-for-all,
up to 10 players. Target look: dense photoreal forest (see `docs/art-direction.md`).

Current state: graybox prototype. It covers the renderer, the FPS controller, local Gun Game
against bots, and the in-game map builder. Networking (ENet listen server) is the next phase.

## Build

Requirements: CMake ≥ 3.24, a C++20 compiler (GCC 13+, Clang 17+, MSVC 2022), Python 3 with
`jinja2` (the glad loader is generated at configure time), and a GPU with OpenGL 4.5.
Linux also needs the X11/Wayland development packages that GLFW requires.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/game/origon
```

All other dependencies (GLFW, glad, glm, nlohmann/json, Jolt Physics, Dear ImGui) are fetched
and pinned by `cmake/Dependencies.cmake`.

Tests (headless, no window):

```sh
ctest --test-dir build --output-on-failure
```

## Controls

| Key | Action |
|---|---|
| WASD | Move |
| Shift | Sprint |
| Space | Jump |
| Left mouse | Fire |
| Right mouse | Aim down sights |
| R | Reload |
| V | Knife |
| Tab | Scoreboard |
| Esc | Menu (graphics, controls, match) |
| F1 | Map builder on/off |
| K | Suicide (testing) |

Map builder: hold right mouse + WASD/QE to fly, left click to select, Delete to remove,
Ctrl+D to duplicate, arrows / PageUp / PageDown to move, G to toggle grid snap. Maps are saved
to `maps/<name>.json`.

## Layout

```
engine/   window, input, renderer (GL 4.5), physics (Jolt)  -> origon_engine
game/     sim/ (authoritative simulation, Gun Game, bots), world/ (map data)  -> origon_sim
          editor/, ui/, App  -> origon executable
assets/   shaders, fonts
data/     weapons.json, gungame_ladder.json (balance without rebuilding)
maps/     map files (JSON)
docs/     architecture, asset pipeline, art direction, research
tests/    headless simulation tests
```

## Conventions

Code, file names, comments and commits are in English. All player-facing text (HUD, menus,
editor panels) is in Spanish.
