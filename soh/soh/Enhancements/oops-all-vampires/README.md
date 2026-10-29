# Oops! All Vampires?

A Vampire-Survivors-style time-survival mode for Ship of Harkinian, built
entirely on Ocarina of Time's world and systems.

## How to play

1. Menubar (Esc) → **Oops! All Vampires?** → **Run** → pick an arena and
   difficulty → **Start Run**.
2. Survive **30 minutes**. You start with the spin attack; everything else
   comes from level-up cards.
3. Move and dodge manually — weapons fire on their own.

- **Victory:** survive to 30:00 ("You survived the night!").
- **Defeat:** run out of hearts. The results screen shows time, level, kills
  and wave, with local bests kept across runs.

## Cards (level-up picks)

**Weapons** (auto-fire):
| Card | Effect |
| --- | --- |
| Spin Attack | Auto spin attack; evolves to Great Spin Storm |
| Bow | Auto-firing arrows; evolves to Light Arrow Barrage |
| Din's Fire | Periodic fire wave around Link |
| Nayru's Love | Damaging barrier while active |
| Bombs | Auto-dropped bombs around Link |
| Hookshot | Auto-pull finisher on the nearest enemy |

**Passives**:
| Card | Effect |
| --- | --- |
| Sword | Damage tier (Master Sword → Giant's Knife) |
| Heart Container | +1 heart, full heal |
| Magic Meter | Magic, then double magic (bigger effects) |
| Magnet | Larger gem vacuum radius |
| Pegasus Boots | Movement speed 2x → 3x |
| Fiber Tunic | Damage taken halved → one-third |
| Bottled Fairy | Auto-revive, one use per level |

## Great Fairy evolutions

When a weapon **and** its paired passive are both maxed, the next **elite**
kill blesses the weapon (the HUD shows `EVOLUTION READY`):

- **Spin L3 + Sword L3** → *Great Spin Storm* (magic charge, double recharge)
- **Bow L3 + Magic L2** → *Light Arrow Barrage* (3-shot light-arrow burst)

Elites are marked with edge arrows on screen. They always drop a big XP gem
and a heart.

## Difficulty tuning

All pacing knobs live in `oops-all-vampires.json` in the game folder (a
template is written on first launch). Edits apply at the next run start;
the menubar has a **Reload Tuning File** button. Tunable: run length, XP
curve, spawn cadence/batch/cap/ring radius/despawn distance, elite start
and interval (normal + finale), hard-mode elite count, wave tiers, and the
seven roster phase-unlock times.

## Arenas

Hyrule Field, Lake Hylia, Gerudo Valley, Lon Lon Ranch — all verified for
full runs.

## Developer notes

- Module: `soh/soh/Enhancements/oops-all-vampires/` (director, weapons,
  HUD, menu, JSON tuning). Hooks use the GameInteractor VB system.
- Headless test hooks: `OAV_AUTOSTART=1` (auto-launch), `OAV_AUTOLEVEL=1`
  (godmode + auto card picks + XP injection), `OAV_AUTOEVOLVE=1` (blesses
  both evolution pairs early).
- Fast-forward soaks (~46x): `LUS_TURBO=1 LUS_VI_INTERVAL_MS=1
  LUS_VI_TIMERS=2 LUS_SKIP_RENDER=1 SDL_AUDIODRIVER=dummy`. The env vars
  are otherwise inert; normal play is unaffected.
- Design doc and status: `PLAN.md` in the workspace root.
