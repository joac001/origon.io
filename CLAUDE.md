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
- `test.tscn` — escena de prueba inicial (jugador cilindro básico)
- `character_body_3d.gd` — script de movimiento básico del jugador (prototipo)

Estructura actual de carpetas:
```
scenes/
  player/
	player.tscn       # CharacterBody3D + CapsuleCollision + Head + Camera3D
  maps/
	test_map.tscn     # Escena de prueba: piso, paredes, 2 cajas de cover, jugador
scripts/
  player/
	player.gd         # Movimiento FPS: WASD + mouse look + salto
autoloads/      # GameManager, NetworkManager, etc. (a crear)
  weapons/
  network/
  game_modes/
resources/      # .tres — recursos de armas, clases, etc. (a crear)
assets/
  models/
	building/   # Kenney Building Kit (79 GLBs) — piezas modulares de edificio
	characters/ # Kenney Blocky Characters (18 GLBs, character-a.glb a character-r.glb)
  textures/
	characters/ # Texturas para characters (texture-a.png a texture-r.png)
```

Archivos legacy en raíz (no borrar hasta confirmar que no se necesitan):
- `test.tscn` / `character_body_3d.gd` — escena de prueba original

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

## Workflow con Claude

El usuario quiere tocar Godot lo mínimo posible. Claude se encarga de:
- Escribir y editar todos los scripts `.gd`
- Crear y modificar archivos `.tscn` como texto
- Crear recursos `.tres`
- Modificar `project.godot` (autoloads, input maps, configuración)

El usuario se encarga de:
- Importar assets 3D (modelos, texturas, sonidos) desde el editor
- Verificar visualmente el layout de escenas
- Probar el juego corriendo desde el editor
- Ajustar posiciones y transforms visuales cuando sea necesario
