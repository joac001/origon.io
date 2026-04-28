# origon.io — FPS Multijugador

## Qué es este proyecto

FPS multijugador low poly 3D estilo Call of Duty para 4-5 amigos. Modos: Team Deathmatch y Gun Game. Arquitectura cliente-servidor autoritativo.

Los requisitos funcionales completos están en [`especificaciones_funcionales.md`](./especificaciones_funcionales.md). Consultarlo siempre antes de implementar cualquier mecánica de juego.

## Stack técnico

- **Engine**: Godot 4.6
- **Lenguaje**: GDScript (archivos `.gd`). El proyecto tiene .NET configurado pero se usa GDScript por defecto. Usar C# solo si la tarea lo justifica explícitamente.
- **Física**: Jolt Physics (3D)
- **Renderer**: Forward Plus / D3D12
- **Red**: Godot High-Level Multiplayer (ENet), arquitectura servidor-autoritativo

## Estructura de archivos

- `project.godot` — configuración del proyecto
- `especificaciones_funcionales.md` — requisitos funcionales (fuente de verdad del diseño)

Estructura actual de carpetas:
```
scenes/
  maps/
	test_map.tscn     # Mapa de prueba 30x30: piso, paredes, segundo piso, escaleras, autos, columnas, cover walls
  player/
	player.tscn       # CharacterBody3D + CapsuleCollision + ShadowBody + Head + Camera3D
  props/              # Props modulares reutilizables (89 escenas auto-generadas de GLBs)
					  # BUILDING KIT (78 props):
					  #   - Columnas: column.tscn, column-thin.tscn, column-wide.tscn
					  #   - Paredes: wall.tscn, wall-low.tscn, wall-corner.tscn, wall-corner-diagonal.tscn, wall-corner-round.tscn, wall-half.tscn
					  #   - Paredes con ventanas: wall-window-*.tscn (4 tipos)
					  #   - Paredes con puertas: wall-doorway-*.tscn (4 tipos)
					  #   - Puertas rotativas: door-rotate-*.tscn (8 tipos: square/round a/b/c/d)
					  #   - Escaleras: stairs-open.tscn, stairs-closed.tscn, stairs-center.tscn, stairs-sides.tscn (+ short variants)
					  #   - Techos: roof-flat-*.tscn (7 tipos)
					  #   - Barricadas: barricade-*.tscn (6 tipos)
					  #   - Bordes: border.tscn, border-corner.tscn, border-high.tscn (+ variants)
					  #   - Pisos: floor.tscn, floor-corner-*.tscn, floor-half.tscn, floor-quarter.tscn
					  #   - Detalles: detail-pipe.tscn, plating.tscn, gutter-vertical.tscn
					  # CARS (3 props):
					  #   - car-sedan.tscn, car-suv.tscn, car-police.tscn (colisiones compuestas)
					  # CITY (3 props):
					  #   - building-a.tscn, building-b.tscn, building-c.tscn (decoración de fondo)

scenes/
  ui/
	hud.tscn          # HUD en-juego: health bar, stamina bar, ammo counter, crouch indicator, reticle (CanvasLayer)

scripts/
  player/
	player.gd         # Movimiento FPS: WASD + mouse look + salto + sprint + crouch + stamina + air control
  ui/
	hud.gd            # Lógica del HUD: update_health(), update_stamina(), update_ammo(), update_crouch()
  resources/
	weapon_resource.gd    # Clase Resource para armas (damage_tdm, damage_gungame, fire_rate, reload_time, etc.)
	class_resource.gd     # Clase Resource para clases (nombre, primary/secondary/melee weapons)
  autoloads/
	game_manager.gd   # Singleton: estado de partida (GameState enum: MENU/LOADING/IN_GAME/PAUSED/GAME_OVER, GameMode enum: TDM/GUN_GAME)
  tools/              # Scripts de validación y utilidades (Python)
	check_dependencies.py    # Verifica que test_map.tscn tenga todas sus refs
	check_prop_refs.py       # Verifica que props tengan GLBs válidos
	check_textures.py        # Analiza qué texturas usa cada GLB
	generate_weapons.py      # Genera 20 WeaponResource .tres de Gun Game + TDM (ejecutar: python scripts/tools/generate_weapons.py)
	generate_classes.py      # Genera 5 ClassResource .tres de loadouts default (ejecutar: python scripts/tools/generate_classes.py)

resources/
  weapons/            # 20 WeaponResource .tres (Gun Game progression: pistol → knife_final, con damage_tdm/damage_gungame)
  classes/            # 5 ClassResource .tres de TDM default: class_assault, class_sniper, class_support, class_shotgunner, class_rusher

assets/
  models/
	building/         # Kenney Building Kit (79 GLBs) — TODOS están modularizados en scenes/props/
					  # Textures/colormap.png — textura compartida del kit
	characters/       # Kenney Blocky Characters (18 GLBs, character-a.glb a character-r.glb)
					  # Para uso futuro como skins de jugador
					  # Textures/texture-a.png a texture-r.png (una por personaje)
	cars/             # Kenney Car Kit (3 GLBs) — TODOS modularizados en scenes/props/
					  # sedan.glb → car-sedan.tscn, suv.glb → car-suv.tscn, police.glb → car-police.tscn
					  # Todas con colisiones compuestas: chassis (hood/trunk) + cabin (roof)
					  # Textures/colormap.png
	city/             # Kenney City Kit Commercial (3 GLBs) — modularizados en scenes/props/
					  # building-a/b/c.glb → building-a/b/c.tscn (decoración de fondo en mapas)
					  # Textures/colormap.png
```

Otros archivos en raíz:
- `icon.svg` — ícono del proyecto Godot
- `especificaciones_funcionales.md` — fuente de verdad del diseño (ver sección arriba)

## Convenciones de código

- **Nombres**: `snake_case` para variables y funciones, `PascalCase` para clases/nodos, `UPPER_CASE` para constantes
- **Señales**: declarar con `signal` al tope del script, conectar por código (no por editor)
- **Autoloads**: para managers globales (GameManager, NetworkManager, etc.)
- **Resources**: usar `.tres` para datos de armas, clases y configuración (tipado, recargable en caliente)

## Reglas de implementación

- El servidor es siempre la fuente de verdad (posición, daño, kills, progresión)
- El cliente predice localmente para responsividad, el servidor corrige
- No implementar features no listados en `especificaciones_funcionales.md` sin consultar
- Los modos TDM y Gun Game tienen lógica de daño separada (ver RF-13, RF-63)
- El cambio de clase solo ocurre en el countdown de respawn, no en cajas de munición (RF-43, RF-44)

## Catálogo de Props Modularizados

**89 escenas .tscn listos para usar** en cualquier mapa. Cada prop incluye:
- Malla (mesh) del GLB correspondiente
- CollisionShape3D calibrada automáticamente
- StaticBody3D como raíz (pronto para ser instanciado)

**Para agregar un prop a un mapa** (ej: columna):
1. En el editor: clic derecho en el mapa → Instancia Scene
2. Seleccionar `scenes/props/column.tscn`
3. Posicionar y rotar el prop en el mapa
4. Las colisiones se aplican automáticamente

**Todos los props usan texturas del Kenney Kit** (colormap.png compartido por kit).

## Sistema de Datos (Resources)

**WeaponResource** (`scripts/resources/weapon_resource.gd`):
- Campos: `weapon_name`, `weapon_type` (enum PRIMARY/SECONDARY/MELEE), `damage_tdm`, `damage_gungame`, `fire_rate`, `reload_time`, `mag_capacity`, `ammo_reserve`, `recoil`, `spread`, `is_melee`
- Uso: Cargar con `preload("res://resources/weapons/pistol.tres")` y acceder a `.damage_tdm` o `.damage_gungame`

**ClassResource** (`scripts/resources/class_resource.gd`):
- Campos: `class_name_str`, `primary_weapon` (WeaponResource), `secondary_weapon` (WeaponResource), `melee_weapon` (WeaponResource)
- Uso: Cargar con `preload("res://resources/classes/class_assault.tres")` y acceder a `.primary_weapon.damage_tdm`

**GameManager** (autoload `scripts/autoloads/game_manager.gd`):
- Estados: enum GameState { MENU, LOADING, IN_GAME, PAUSED, GAME_OVER }
- Modos: enum GameMode { TDM, GUN_GAME }
- Señales: `game_state_changed(new_state)`, `game_mode_changed(new_mode)`
- Accesible globalmente como `GameManager.current_state` o `GameManager.set_game_mode(GameMode.TDM)`

## Sistema de Movimiento Expandido

**Player.gd** ahora incluye:
- **Crouch** (Ctrl): Alterna altura de colisión (1.2 → 0.7), velocidad 2.5, emite señal `crouching_changed(bool)`
- **Stamina** (100 max): Se drena al sprintar (30/seg), se regenera al caminar (20/seg), emite señal `stamina_changed(float)`
- Señales: `health_changed(int)`, `ammo_changed(int, int)`, `stamina_changed(float)`, `crouching_changed(bool)`

## HUD Básico

**scenes/ui/hud.tscn** (CanvasLayer):
- Health bar (ProgressBar 0-100)
- Stamina bar (ProgressBar 0-100, amarillo)
- Ammo counter (Label "30 / 120")
- Crouch indicator (Label "[CROUCH]" cuando está agachado)
- Reticle (Cruz blanca centrada)

Métodos en `hud.gd`:
- `update_health(value: int)`
- `update_stamina(value: float)`
- `update_ammo(current: int, reserve: int)`
- `update_crouch(is_crouching: bool)`

**Integración**: HUD instanciado en `test_map.tscn`. Para conectar señales del Player, agregar en el script del Player o en el mapa:
```gdscript
var player = $Player
var hud = $HUD
player.health_changed.connect(hud.update_health)
player.stamina_changed.connect(hud.update_stamina)
player.ammo_changed.connect(hud.update_ammo)
player.crouching_changed.connect(hud.update_crouch)
```

## Scripts de Herramientas (Python)

Los scripts de validación y utilidades se guardan en `scripts/tools/` y se ejecutan desde la raíz del proyecto:

```bash
# Validar que test_map.tscn tiene todas sus referencias
python scripts/tools/check_dependencies.py

# Validar que cada prop tiene GLBs válidos
python scripts/tools/check_prop_refs.py

# Analizar qué texturas usa cada GLB
python scripts/tools/check_textures.py
```

**Convención**: Todos los scripts de herramientas (análisis, validación, generación) van en `scripts/tools/` con nombres descriptivos. No dejar scripts sueltos en la raíz del proyecto.

## Workflow con Claude

El usuario quiere tocar Godot lo mínimo posible. Claude se encarga de:
- Escribir y editar todos los scripts `.gd`
- Crear y modificar archivos `.tscn` como texto
- Crear recursos `.tres`
- Modificar `project.godot` (autoloads, input maps, configuración)

El usuario se encarga de:
- Verificar visualmente el layout de escenas
- Probar el juego corriendo desde el editor
- Ajustar posiciones y transforms visuales cuando sea necesario

Nota: Claude puede extraer assets de los ZIPs en `Asset library/` usando Python directamente (sin necesidad de que el usuario los importe manualmente). Los GLBs van en `assets/models/<kit>/` y las texturas en `assets/models/<kit>/Textures/`.
