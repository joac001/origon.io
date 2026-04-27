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
  props/              # Props modulares reutilizables (instanciar en cualquier mapa)
    stairs.tscn       # Escalera stairs-open.glb con ramp CollisionShape3D calibrada
    car_sedan.tscn    # Sedan con colisión compuesta: chassis (hood/trunk) + cabin (roof)
    car_suv.tscn      # SUV con colisión compuesta: chassis + cabin más ancho

scripts/
  player/
    player.gd         # Movimiento FPS: WASD + mouse look + salto + sprint + air control

autoloads/            # GameManager, NetworkManager, etc. (a crear)
resources/            # .tres — recursos de armas, clases, etc. (a crear)

assets/
  models/
    building/         # Kenney Building Kit (79 GLBs) — piezas modulares de edificio
                      # En uso: column.glb, wall-low.glb, stairs-open.glb
                      # Disponibles para futuros mapas: wall.glb, wall-corner.glb, wall-doorway-*.glb,
                      #   stairs-*.glb, floor.glb, roof-*.glb, barricade-*.glb, door-*.glb, etc.
                      # Textures/colormap.png — textura compartida del kit
    characters/       # Kenney Blocky Characters (18 GLBs, character-a.glb a character-r.glb)
                      # Para uso futuro como skins de jugador
                      # Textures/texture-a.png a texture-r.png (una por personaje)
    cars/             # Kenney Car Kit: sedan.glb, suv.glb, police.glb
                      # En uso: sedan.glb (car_sedan.tscn), suv.glb (car_suv.tscn)
                      # Disponible: police.glb (prop scene pendiente)
                      # Textures/colormap.png
    city/             # Kenney City Kit Commercial: building-a/b/c.glb
                      # Para uso futuro como decoración de fondo en mapas
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
