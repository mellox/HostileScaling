# Changelog — Hostile Scaling

All notable changes to this mod. Follows semantic versioning; the `.uplugin`
integer `Version` stays `1` (SemVer major) across all 1.x releases — only
`VersionName`/`SemVersion` change.

## v1.1.2

**Changed — compatibility update for Satisfactory 1.2.6.** This version requires game build 520140
(Satisfactory 1.2.6) or later and SML 3.12.1 or a later 3.x version.

- No change to how hostile creatures are scaled or kept respawning, and no setting was added,
  removed or renamed.
- **Added — two reporting functions for other mods.** Another mod can call them to read Hostile
  Scaling's current settings, multipliers and spawner state. They only read: they change no
  creature, spawner or setting. Nothing in Hostile Scaling calls them.

## v1.1.1

**Changed — scaling and the respawn keeper are now HOSTILE-only.** Through v1.1.0 the mod scaled
every creature spawner, passive ones included, so doggo and ambient-fauna packs grew and kept
respawning near your base. Health and damage scaling were always hostile-only; the spawner side now
matches.

- A spawner counts as hostile when its creature class's default object is not passive.
- Passive spawners are left at vanilla respawn time, vanilla pack size, and vanilla near-base
  suppression.
- **Migration:** on saves made by v1.1.0 or earlier, passive spawners are walked back — creature-less
  extra pack slots are removed until the pack is back at the size recorded when the mod first saw the
  spawner (never below it, never on a spawner whose recorded size is 0, and never a slot the game is
  mid-spawn on), and the near-base flag is handed back to the game to recompute.
- With the master **Enabled** switch off, nothing is reverted — that residue stays until you turn it
  back on.

**Fix — the near-base bypass is refused for passive spawners at the point that writes the flag,**
not merely skipped by the sweep, so passive wildlife keeps vanilla's near-base suppression even
while **Respawn Near Factories** is on.

**Changed — config panel clarity pass.** No setting value or saved config changes; labels and
descriptions only.

- The three spawn sliders are now labelled **Respawn + Packs**, because that one
  multiplier has always driven both.
- **Increase Spawn Counts** is now **Also Apply Spawn Scaling to Pack Size**, sitting directly under
  the sliders it gates.
- **Keep Creatures Respawning** is now **Respawn Near Factories**.
- Descriptions state the direction of the multiplier (1x = vanilla, 2x = half the vanilla wait,
  0.5x = twice the vanilla wait), and all three sliders carry a warning that large values produce
  very large packs while the pack-size toggle is on.
- **Verbose Respawn Logging** is grouped under Diagnostics and marked troubleshooting-only.

**Added — diagnostics.** The log now carries a hostile/passive/unclassified spawner census, a
per-spawner line naming any passive pack that was trimmed (with before, after, and the recorded
original), and per-sweep totals for both the passive migration and hostile pack shrinkage — so a
support log shows what the classification gate did without turning verbose logging on.

_Respawn-keeper technique inspired by **Blood Moon** by Chris Cavalluzzi as prior art; no
code was copied — this implementation is independent._

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
