# Requisitos Funcionales — FPS Multijugador

> Juego inspirado en Call of Duty para jugar entre amigos (4-5 jugadores). Dos modos: Team Deathmatch y Gun Game. Servidor propio con Godot 4 + C#.

---

## Sección 1 — Configuración General del Juego

- **RF-01** El juego soporta entre 1 a 10 jugadores máximo por partida conectados a un servidor dedicado propio.
- **RF-02** Existen dos modos de juego: Team Deathmatch (TDM) y Gun Game. Cada modo tiene sus propias reglas y mecánicas de progresión.
- **RF-03** El estilo visual es Low Poly 3D en primera persona (FPS).
- **RF-04** Hay 2 mapas disponibles en la release inicial, ambos construidos con assets low poly gratuitos.
- **RF-05** La cámara está en primera persona con sonido direccional. No hay minimapa ni radar visual.

---

## Sección 2 — Sistema de Clases (Loadouts)

- **RF-06** El juego proporciona 5 clases default pre-configuradas. El jugador puede editar cualquiera de ellas o mantenerlas como están.
- **RF-07** Una clase está compuesta por: nombre, arma primaria, arma secundaria (pistola u otra corta) y un arma melee (cuchillo u otra).
- **RF-08** Las clases se configuran desde el menú pre-partida en el lobby.
- **RF-09** Cada arma en una clase tiene atributos específicos: daño, cadencia de fuego, alcance efectivo y capacidad de munición por cargador.
- **RF-10** Al respawnear en una partida, el jugador usa el loadout que eligió. Puede cambiar de clase antes de respawnear presionando un botón dentro del delay de respawn (RF-25).
- **RF-11** El sistema de clases **no aplica en Gun Game**. En ese modo el juego asigna automáticamente un arma única a cada jugador según la progresión.

---

## Sección 3 — Armas y Munición

- **RF-12** Cada arma tiene stats diferenciados: daño por bala, cadencia de fuego (RPM), alcance efectivo, recoil simplificado y spread (dispersión) de proyectiles.
- **RF-13** El daño de cada arma varía según el modo de juego. Por ejemplo: una pistola hace 20 de daño en TDM pero one-shot kill en Gun Game.
- **RF-14** La munición es limitada. Cada arma tiene una capacidad de cargador definida y el jugador tiene una cantidad máxima de munición en reserva según su clase.
- **RF-15** Los cargadores se recargan manualmente presionando un botón (ej: R). La recarga consume tiempo según el arma.
- **RF-16** Si un arma se queda sin balas pero hay cargadores en reserva, el arma recarga automáticamente cuando el jugador intenta disparar de nuevo (sin necesidad de presionar R).
- **RF-17** En los mapas hay cajas de munición fijas que el jugador puede acceder presionando un botón (ej: E). Al mantener presionado, se recargan todos los cargadores de todas las armas simultáneamente.
- **RF-18** El tiempo para recargar cargadores en la caja es proporcional a cuántos cargadores faltan. Si el jugador tiene capacidad máxima para X cargadores en total, tarda X segundos llenar completamente (1 segundo por cargador). Si solo le faltan 3 cargadores, tarda 3 segundos.
- **RF-19** Si el jugador suelta el botón antes de completar la recarga, se guarda el progreso. Por ejemplo, si mantuvo presionado 2 segundos de un total de 5 segundos necesarios, se recargan 2 cargadores y puede soltar. Cuando vuelva a presionar, continúa desde donde quedó.
- **RF-20** En Team Deathmatch, el jugador puede cambiar de arma por otra del mismo tipo recogida de un cadáver (primaria por primaria, secundaria por secundaria, melee por melee) presionando el botón de interacción (ej: E). El arma intercambiada se descarta.

---

## Sección 4 — Sistema de Daño y Salud

- **RF-21** Todo jugador empieza con 100 HP (vida).
- **RF-22** El daño recibido reduce los HP. Cuando los HP llegan a 0, el jugador muere.
- **RF-23** Los HP se restablecen al respawnear.
- **RF-24** No hay sistema de armadura o escudo. Está planificado para futuras iteraciones.
- **RF-25** El daño es calculado en el servidor para validar legitimidad. El cliente predice visualmente pero el servidor es fuente de verdad.

---

## Sección 5 — Sistema de Respawn

- **RF-26** Al morir, el jugador entra en un delay de respawn de 5 segundos.
- **RF-27** Durante el delay de respawn, la cámara sigue al jugador que lo mató (sin repetir la kill cam, solo ver su perspectiva).
- **RF-28** El jugador puede saltear el delay presionando un botón (ej: barra espaciadora).
- **RF-29** Al presionar saltear o cumplirse los 5 segundos, el jugador reaparece en uno de los spawn points del mapa.
- **RF-30** Si un jugador se desconecta durante una partida, su slot queda vacío pero disponible para reconexión. Si se reconecta, vuelve con la misma información de vida y posición que tenía al desconectarse.

---

## Sección 6 — Mecánica de Disparos y Recoil

- **RF-31** El recoil (retroceso) es simplificado: cada arma tiene un patrón de recoil predefinido que afecta la posición de la mira después de cada disparo.
- **RF-32** El spread (dispersión) es mínimo en armas de precisión (sniper) y mayor en armas de corto alcance (escopeta). Afecta el radio de error de cada bala.
- **RF-33** Los disparos son hitscan (instantáneos) sin proyectiles visibles, salvo granadas u otros items especiales que sí tienen trayectoria.

---

## Sección 7 — Crouch y Movimiento

- **RF-34** El jugador puede agacharse presionando un botón (ej: Ctrl). Esto reduce su altura visible y mejora la estabilidad de disparo (reduce recoil).
- **RF-35** Al agacharse, el sonido de pasos del jugador se reduce, haciéndolo más difícil de detectar por sonido direccional (RF-36).
- **RF-36** Los enemigos pueden escuchar pasos direccionales. El volumen y la dirección del sonido dependen de la distancia y si el jugador está corriendo o caminando lentamente.
- **RF-37** No hay sprint infinito. Correr consume una barra de stamina que se regenera al caminar.

---

## Sección 8 — Recogida de Armas y Cajas de Munición

- **RF-38** En Team Deathmatch, al matar a un enemigo, el cadáver deja caer un arma que puede ser recogida por otros jugadores durante 60 segundos. Después de ese tiempo, el arma desaparece.
- **RF-39** El jugador puede recoger armas del suelo presionando un botón (ej: E). El arma se agrega a su inventario si hay espacio.
- **RF-40** Si el jugador intercambia un arma que tiene por otra del suelo (mismo tipo), el arma que tenía se lanza al suelo y puede ser recogida por otros jugadores o por el mismo jugador posteriormente.
- **RF-41** La munición es compartida globalmente entre todas las armas. Cada arma (primary y secondary) tiene su propia cuenta de munición independiente, pero al cambiar de arma se mantiene la munición que ya tenía.
- **RF-42** Hay 2 cajas de munición fijas por mapa distribuidas estratégicamente. El jugador puede interactuar con ellas presionando E y mantener presionado para recargar cargadores de todas sus armas (como se especifica en RF-17 a RF-19).
- **RF-43** Las cajas de munición **no permiten cambiar de clase**. Para cambiar de clase durante una partida, el jugador debe usar el menú de clase que aparece durante el countdown de respawn de 5 segundos.
- **RF-44** Las cajas de cambio de clase han sido removidas de los mapas. El cambio de clase solo ocurre desde el menú del countdown de respawn.

---

## Sección 9 — Marcador y Kills

- **RF-45** Un kill se contabiliza instantáneamente cuando el enemigo muere por disparos del jugador.
- **RF-46** El marcador muestra: kills, muertes, assists (ayudas) y el score total (si se usa sistema de puntos).
- **RF-47** Un assist se registra si el jugador hace daño a un enemigo que luego es asesinado por otro jugador en menos de 5 segundos.

---

## Sección 10 — Team Deathmatch (TDM)

### Configuración y victoria

- **RF-48** En TDM, los jugadores se dividen en 2 equipos: Equipo A y Equipo B.
- **RF-49** La asignación de equipos es automática (equilibrada), pero el jugador puede cambiar manualmente de equipo antes de spawnear.
- **RF-50** La condición de victoria es customizable por el host de la partida. Opciones:
  - Llegar a un número específico de kills por equipo (ej: 20, 30, 50)
  - Límite de tiempo (ej: 10 minutos, 15 minutos)
  - O una combinación: lo que ocurra primero
- **RF-51** Si hay empate en kills al terminar el tiempo, gana el equipo que primero alcance un kill más de diferencia.

### Loadouts y armas en TDM

- **RF-52** En TDM, cada jugador respawnea con su clase (loadout) elegida.
- **RF-53** El jugador puede pickear armas del suelo (de cadáveres enemigos) si lo desea (RF-38).
- **RF-54** El daño de las armas es estándar en TDM (diferente al de Gun Game).

### Feedback en TDM

- **RF-55** Al morir, el jugador escucha un sonido de muerte y su cámara sigue al asesino durante los 5 segundos de delay de respawn.
- **RF-56** El marcador se actualiza en tiempo real y es visible para todos los jugadores.

---

## Sección 11 — Gun Game

### Progresión de armas

- **RF-57** En Gun Game, todos los jugadores compiten entre sí (Free For All, no hay equipos).
- **RF-58** Cada jugador empieza con la arma del nivel 1 de una lista predefinida de armas.
- **RF-59** Al conseguir una baja (matar a otro jugador), el asesino avanza al siguiente arma de la lista.
- **RF-60** La lista de armas es (20 niveles):
  1. Pistola
  2. Pistola de servicio
  3. SMG (subfusil)
  4. SMG Compacta
  5. Carabina (rifle corto)
  6. Rifle de asalto
  7. Rifle de asalto (variante)
  8. Rifle de batalla pesado
  9. Sniper (rifle de francotirador)
  10. Sniper de largo alcance
  11. Escopeta
  12. Escopeta táctica
  13. Lanzador de granadas
  14. Lanzacohetes
  15. Granada de mano (3 lanzamientos)
  16. Cuchillo de combate
  17. Hacha (arma melee)
  18. Bate (arma melee)
  19. Puñales duales (arma melee)
  20. Cuchillo (arma melee final - one-hit kill)
- **RF-61** Si el jugador se suicida, retrocede un nivel en la lista (vuelve al arma anterior).
- **RF-62** Para ganar, el jugador debe conseguir una baja usando el cuchillo final (nivel 20 de la lista).

### Daño en Gun Game

- **RF-63** En Gun Game, el daño de las armas es diferente al de TDM. Muchas armas hacen one-shot kill o tienen daño aumentado para acelerar el juego.
- **RF-64** El cuchillo en Gun Game es one-hit kill (un golpe mata).

### Feedback en Gun Game

- **RF-65** El marcador muestra la posición actual de cada jugador en la lista de armas (ej: "Jugador X en nivel 12/20").
- **RF-66** Al avanzar de arma, se emite un sonido y una notificación visual para el jugador.

---

## Sección 12 — Sistema de Armas Melee

- **RF-67** En Team Deathmatch, las armas melee (cuchillo, hacha, bate, etc.) son armas secundarias accesibles según la clase elegida. Hacen daño en un rango cercano (melee range).
- **RF-68** Cada arma melee tiene un daño específico diferenciado. Por ejemplo: cuchillo 50 HP, hacha 60 HP, bate 55 HP (valores a definir).
- **RF-69** En Gun Game, las armas melee aparecen en los niveles 16-20 con daño aumentado hasta one-hit kill en el cuchillo final (nivel 20).
- **RF-70** Las armas melee no tienen munición. Se usan presionando un botón (ej: clic derecho) y tienen un cooldown de 1 segundo entre ataques.

---

## Sección 13 — Interfaz y HUD

### Vista principal en-juego

- **RF-71** El HUD muestra en todo momento:
  - HP actual (barra de vida)
  - Arma actual con munición (cargador / reserva)
  - Miniatura del equipo actual (TDM) o posición en lista de armas (Gun Game)
  - Marcador del equipo o posiciones (actualizado en tiempo real)
  - Indicador de crouch (si está agachado)
  - Reticle/mira en el centro de la pantalla

- **RF-72** El HUD desaparece presionando TAB para ver el marcador completo en overlay.
- **RF-73** El marcador en overlay muestra: jugadores, kills, muertes, assists (si aplica) y puntuación total.

### Menú pre-partida (Lobby)

- **RF-74** Antes de entrar a una partida, el jugador ve:
  - Seleccionar modo (TDM o Gun Game)
  - Elegir una de sus 5 clases (solo en TDM)
  - Elegir equipo (solo en TDM, opcional)
  - Configurar límites de partida (host solamente): número de kills, tiempo límite, etc.

- **RF-75** El lobby muestra los jugadores conectados y sus nicknames.
- **RF-76** Solo el host (quien crea la partida) puede iniciar la partida. Los otros esperan confirmación.

### Menú de cambio de clase durante respawn

- **RF-77** Durante el countdown de respawn de 5 segundos, aparece un menú que permite al jugador cambiar a una de sus 5 clases guardadas. El cambio se aplica cuando el jugador spawneea.

---

## Sección 14 — Sincronización de Red

- **RF-78** La arquitectura es **cliente-servidor** con servidor authoritative (el servidor valida y es la fuente de verdad).
- **RF-79** La posición y rotación del jugador se sincronizan continuamente entre cliente y servidor. El cliente predice visualmente, pero el servidor corrige.
- **RF-80** Los disparos, cambios de arma y eventos de muerte se envían al servidor, que valida y broadcast a todos los clientes.
- **RF-81** La sincronización de munición, HP y progresión se valida en servidor.
- **RF-82** Si hay desconexión, el servidor guarda el estado del jugador (HP, posición, arma, progresión) durante 30 segundos. Si reconecta, recupera ese estado.

---

## Sección 15 — Customización de Personaje

- **RF-83** Cada jugador puede customizar su personaje con:
  - Selección de color (color del modelo 3D)
  - Selección de variante de cabeza/casco (2-3 opciones disponibles)

- **RF-84** La customización es visual solamente. No afecta stats ni jugabilidad.
- **RF-85** Cada color y variante disponible se guarda y es visible para todos los jugadores en la partida.

---

## Sección 16 — Reconexión y Persistencia de Sesión

- **RF-86** Si un jugador se desconecta accidentalmente, el servidor mantiene su slot disponible.
- **RF-87** Si el jugador reconecta, vuelve con:
  - Su HP actual
  - Su posición en el mapa
  - Su arma actual
  - Su munición
  - Su progresión (en Gun Game, el nivel en el que estaba)

---

## Sección 17 — Audio y Retroalimentación

- **RF-89** Los sonidos direccionales permiten al jugador localizar enemigos: pasos, disparos, recargas y movimiento hacen sonido.
- **RF-90** Al morir, el jugador escucha un sonido de muerte específico.
- **RF-91** Al recibir daño, hay un sonido o efecto visual de impacto.
- **RF-92** Al avanzar de arma en Gun Game, se emite un sonido de progresión.
- **RF-93** Las armas tienen sonidos diferenciados: disparo, recarga, cambio de arma.

---

## Sección 18 — Mapas

- **RF-94** Hay 2 mapas disponibles en la release inicial, ambos con estilo low poly.
- **RF-95** Cada mapa tiene:
  - Múltiples spawn points para cada equipo (TDM) o generales (Gun Game)
  - 2 cajas de munición fijas distribuidas estratégicamente
  - Geometría vertical y horizontal para fomentar tácticas diversas

- **RF-96** Los mapas pueden ser jugados en ambos modos (TDM y Gun Game).

---

## Sección 19 — Datos Guardados y Perfil de Jugador

- **RF-97** Cada jugador tiene un perfil guardado localmente que contiene:
  - Sus 5 clases personalizadas con nombre y configuración de armas
  - Color y variante de personaje elegidos
  - Apodo/nickname
  - Estadísticas básicas (partidas jugadas, kills totales, deaths totales, etc.)

- **RF-98** El perfil se sincroniza con el servidor al conectarse a una partida para validar clases.

---

## Sección 20 — Condiciones de Desempate y Fin de Partida

- **RF-99** En TDM, la partida termina cuando:
  - Un equipo llega al límite de kills configurado, O
  - Se agota el tiempo límite configurado
  - Lo que ocurra primero

- **RF-100** En caso de empate en kills al terminar el tiempo en TDM, se juega un overtime: gana el primer equipo que logre un kill de diferencia.
- **RF-101** En Gun Game, la partida termina cuando un jugador consigue una baja con el cuchillo.
- **RF-102** Al terminar la partida, se muestra una pantalla de resultados con:
  - Ganador(es)
  - Marcador final
  - Ranking de jugadores por kills (TDM) o posición final (Gun Game)
  - Opción para volver al lobby o salir

---

## Sección 21 — Bugs y Casos Excepcionales

- **RF-103** Si ocurre un error de red durante la partida, el cliente intenta reconectar automáticamente cada 2 segundos durante 30 segundos.
- **RF-104** Si el servidor se cae, todos los clientes reciben un mensaje de desconexión y vuelven al menú principal.
- **RF-105** Si hay lag extremo (ping > 500ms), se notifica al jugador pero no se interrumpe la partida.
