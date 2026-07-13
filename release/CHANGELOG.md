# Changelog — Hostile Scaling

All notable changes to this mod. Follows semantic versioning; the `.uplugin`
integer `Version` stays `1` (SemVer major) across all 1.x releases — only
`VersionName`/`SemVersion` change.

## v1.1.0

**New — Keep Creatures Respawning (on by default).** Vanilla Satisfactory stops
respawning creatures near your base and in areas you've cleared out, so the world
gradually empties as you industrialize. This feature keeps spawners active:

- Bypasses vanilla's near-base suppression, so spawners near factories and power keep working.
- Re-arms cleared-out packs on their normal respawn schedule — still governed by your
  spawn-frequency settings, so it does **not** make them respawn instantly.
- If a spawner can't find a valid spot (e.g. you've built over it), it gradually widens its
  search radius so creatures reappear on valid ground nearby.
- Server-side only and fully reversible — turn it off to restore vanilla behavior; the game
  re-suppresses near-base spawners within one re-trace cycle.

**New — Verbose Respawn Logging (off by default).** A debugging toggle that writes detailed
respawn-keeper diagnostics (re-arms, near-base pins, radius changes, water checks) to the
game log. Leave off for clean logs.

**Fix — never-respawn spawners.** A bug present since v1.0.1 clamped map-defined "never
respawn" spawners (respawn time `-1`) to `0`, making them respawn every in-game day. They now
correctly stay disabled once cleared. (Most worlds have very few or no such spawners.)

_Respawn-keeper technique inspired by **Blood Moon** by Chris Cavalluzzi as prior art; no
code was copied — this implementation is independent._

## v1.0.3

- Fixed an out-of-bounds crash in spawn-count scaling.

## v1.0.2

- Reset buttons now restore defaults instead of minimum values.

## v1.0.1

- Initial public release: tier-scaled hostile creature health, damage, and spawn frequency,
  all configurable in-game with live effect (no restart). Health scaling writes real health
  values so health-bar mods show the truth. Passive and tamed creatures are never affected.
