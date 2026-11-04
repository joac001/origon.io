# Weapon Research — Classic WWII Arsenal (Call of Duty 2 era)

Scope: the multiplayer arsenal of Call of Duty 2 (2005). It is the reference for Origon's
weapon catalog. Every weapon below is a real historical firearm. Origon uses the real firearm
names. It does not use any game's models, sounds, stats or branding: all meshes come from our
own Blender pipeline (see `docs/asset-pipeline.md`), and all stats are our own.

## 1. Call of Duty 2 multiplayer catalog by faction

In CoD2 multiplayer, each faction has its own primary weapons. Any player can pick up a dropped
weapon from any faction. The Trench Gun is the only weapon that all factions share.

| Category | United States | United Kingdom | Soviet Union | Germany |
|---|---|---|---|---|
| Pistol (sidearm) | Colt M1911A1 | Webley Mk VI | Tokarev TT-33 | Luger P08 |
| Submachine gun | Thompson M1A1, M3 "Grease Gun" | Sten Mk II, Thompson M1A1 | PPSh-41, PPS-42 | MP40 |
| Bolt-action rifle | — | Lee-Enfield No.4 | Mosin-Nagant 91/30 | Karabiner 98k |
| Semi-automatic rifle | M1 Garand, M1A1 Carbine | M1 Garand | SVT-40 | Gewehr 43 |
| Assault rifle | — | — | — | StG 44 (MP44) |
| Light machine gun / automatic rifle | BAR M1918 | Bren Mk II | — | — |
| Scoped rifle (sniper) | M1903A4 Springfield | Lee-Enfield No.4 (T) scoped | Mosin-Nagant scoped | Kar98k scoped |
| Shotgun | Winchester M1897 Trench Gun (all factions) | ← | ← | ← |
| Frag grenade | Mk 2 | No. 36M Mills bomb | RGD-33 | Stielhandgranate 24 |
| Smoke grenade | yes | yes | yes | yes |
| Melee | Weapon bash (all) | ← | ← | ← |

Not carried in multiplayer: mounted MG42 and M1919 .30 cal (map-placed turrets), Panzerschreck
and other launchers (single-player only).

Verification notes:
- Confirmed by sources: faction-specific loadouts with weapon pickup; Trench Gun shared and
  multiplayer-only; Grease Gun US-only; German list (MP40, G43, MP44, Kar98k, scoped Kar98k,
  Luger); M1911A1 as US sidearm; British list (Sten, M1 Garand, Lee-Enfield, scoped
  Lee-Enfield, Thompson, Bren, Webley).
- Not fully confirmed: PPS-42 in the Soviet multiplayer list (one source lists "PPSh-40",
  likely a typo). Our ladder does not depend on this detail.

## 2. Origon weapon categories

| Category (code id) | UI name (Spanish) | Role |
|---|---|---|
| `pistol` | Pistola | Sidearm, fast draw, low damage |
| `smg` | Subfusil | High fire rate, short range |
| `assault_rifle` | Fusil de asalto | Automatic, mid range |
| `lmg` | Ametralladora ligera | Automatic, heavy, slow handling |
| `semi_auto_rifle` | Fusil semiautomático | One shot per click, mid-long range |
| `bolt_rifle` | Fusil de cerrojo | High damage, slow cycle |
| `sniper` | Fusil de francotirador | Scoped bolt-action |
| `shotgun` | Escopeta | Pump action, pellets |
| `melee` | Cuerpo a cuerpo | Knife |

## 3. Gun Game progression ladder (Origon default)

Design rule: easy weapons first, hard weapons last. The leader gets harder weapons, so trailing
players can catch up. The final tier is the knife.

| Tier | Weapon | Category |
|---|---|---|
| 1 | MP40 | smg |
| 2 | Thompson M1A1 | smg |
| 3 | PPSh-41 | smg |
| 4 | StG 44 | assault_rifle |
| 5 | BAR M1918 | lmg |
| 6 | Bren Mk II | lmg |
| 7 | Sten Mk II | smg |
| 8 | M3 Grease Gun | smg |
| 9 | PPS-42 | smg |
| 10 | M1897 Trench Gun | shotgun |
| 11 | M1A1 Carbine | semi_auto_rifle |
| 12 | M1 Garand | semi_auto_rifle |
| 13 | Gewehr 43 | semi_auto_rifle |
| 14 | SVT-40 | semi_auto_rifle |
| 15 | Lee-Enfield No.4 | bolt_rifle |
| 16 | Mosin-Nagant 91/30 | bolt_rifle |
| 17 | Kar98k (scoped) | sniper |
| 18 | Webley Mk VI | pistol |
| 19 | Colt M1911A1 | pistol |
| 20 | Combat knife | melee |

The ladder and all weapon stats are data, not code: `data/weapons.json` and
`data/gungame_ladder.json`. Changing balance or order needs no rebuild.
