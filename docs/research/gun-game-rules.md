# Gun Game Rules — Research and Origon Specification

## 1. Reference: Call of Duty: Black Ops (2010), "Wager Match" Gun Game

Confirmed by sources:
- Free-for-all. Every player starts on the same first weapon (Python revolver in Black Ops).
- 20 weapon tiers. Each kill moves the killer to the next weapon.
- Being killed by melee (knife) is a "humiliation": the victim drops back one tier.
- Suicide also drops the player back one tier.
- Last two tiers are the explosive crossbow and the ballistic knife. The first player to get a
  kill with the final weapon wins.
- The mode is played with a small lobby (series wiki: 6 players).

Not confirmed by sources (treat as design choices, not facts):
- Exact time limit (commonly cited as 10 minutes).
- Whether a knife kill also promotes the killer. Counter-Strike's "Arms Race" mode does this
  (the knife "steals" a level). Black Ops only demotes the victim.

## 2. Origon Gun Game specification

| Rule | Value | Config key |
|---|---|---|
| Mode | Free-for-all, no teams | — |
| Max players | 10 | `max_players` |
| Tiers | 20 (see `docs/research/weapons.md`) | `data/gungame_ladder.json` |
| Promotion | +1 tier per kill with the current tier weapon | — |
| Melee kill (any tier) | Victim −1 tier (minimum tier 1). Killer stays. | `melee_demotes_victim`, `melee_promotes_killer` (default `false`) |
| Melee kill on final tier | The knife is the final weapon. A knife kill on tier 20 wins. | — |
| Suicide / fall / world death | Player −1 tier (minimum tier 1) | `suicide_demotes` |
| Loadout | Current tier weapon + knife. No pickups, no grenades. Infinite reserve ammo. | — |
| Respawn | 3.0 s delay, spawn point farthest from living opponents | `respawn_delay_s` |
| Spawn protection | 1.5 s, cancelled when the player fires | `spawn_protection_s` |
| Victory | First kill with the tier 20 weapon ends the match | — |
| Time limit | 10 minutes. On timeout the highest tier wins | `time_limit_s` |
| Tiebreak on timeout | 1) higher tier, 2) more total kills, 3) fewer deaths, 4) who reached the tier first | — |

Notes:
- "Melee" means the knife attack (key `V`), available on every tier. On tier 20 it is also the
  primary weapon.
- Kills by a weapon of a lower tier cannot happen, because each player only holds one gun.
- Demotion below tier 1 is clamped.
- All rules run on the authoritative simulation (host). Clients only render the result. This
  keeps the mode ready for the listen-server networking phase.

## 3. Player-facing text (Spanish)

| Event | Text |
|---|---|
| Promotion | `¡Subiste de nivel! Arma: {weapon}` |
| Humiliated | `¡Humillado! Bajaste al nivel {tier}` |
| Suicide | `Suicidio: bajaste al nivel {tier}` |
| Final weapon | `¡Último nivel: cuchillo!` |
| Victory | `¡{player} gana la partida!` |
| Timeout | `Tiempo agotado` |
