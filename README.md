# Hostile Scaling

A Satisfactory mod (SML 3.12+, game version ≥ 491125 / Satisfactory 1.1) that scales hostile
creature **health**, **damage**, and **spawn frequency** as you progress through milestone
tiers. Everything is user-configurable through the in-game Mod Config menu.

## How it works

| System | Mechanism |
|---|---|
| Health | Hostile creatures get real scaled health: max health is set to `vanilla class default × multiplier` (current health percentage preserved), applied instantly on spawn via an `AFGCreature::BeginPlay` hook and reconciled every 10s for live creatures on tier-ups. Computing from the class default makes it idempotent — values never compound across saves. Health-display mods show the true scaled numbers. |
| Damage | Damage dealt **by** hostile creatures is multiplied. Hostile = any `AFGCreature` that is neither passive nor tamed (doggos and ambient fauna untouched), checked per instance so it works for every creature blueprint. By default only damage to players is scaled; a config toggle extends it to buildings/vehicles. |
| Damage paths | All three `UFGHealthComponent` entry points are hooked (`TakeDamage`, `TakePointDamage`, `TakeRadialDamage`) so melee, projectile/hitscan, and explosion damage are all covered. |
| Spawn frequency | The subsystem divides each spawner's respawn time (in in-game days) by the multiplier, and optionally grows each spawner's spawn list so more creatures appear per location. |
| Keep respawning | Vanilla suppresses spawners near your base and eventually stops repopulating cleared areas, so the world empties out as you build. With **Keep Creatures Respawning** on (default), the mod bypasses near-base suppression, re-arms cleared packs on their normal (already-scaled) respawn schedule, and — if a spawner can't find a valid spot (e.g. you floored over it) — gradually widens its search radius so creatures reappear nearby. Toggle it off for vanilla behavior. |
| Tier progression | Highest unlocked **milestone** tier is read from the schematic manager. Each multiplier = `Base + PerTier × Tier`, clamped to `[0.1, Max]`. |

Original spawner values are stored **in the savegame** (keyed by spawner path), so scaling is
always recomputed from vanilla numbers — it never compounds across sessions, and lowering the
config later actually lowers the result. All logic is server-side, so multiplayer works with
the mod installed on the host/server.

### Default configuration

With defaults (`PerTier` 0.15/0.15/0.10), at tier 9 hostiles have ~2.35× health, deal ~2.35×
damage, and spawners refill ~1.9× faster. **Keep Creatures Respawning** is on by default;
**Verbose Respawn Logging** (a debugging aid that writes per-spawner detail to the game log)
is off. Config file is written to `<game>/FactoryGame/Configs/HostileScaling.cfg`; the same
values are editable in-game under **Main Menu → Mods → Hostile Scaling** (SML's Mod Config UI).

## Building the mod

This is C++ source for the SML starter project. You cannot build it standalone — it compiles
inside the SatisfactoryModLoader project. One-time environment setup (full guide:
https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/dependencies):

1. Install **Visual Studio 2022** with "Game development with C++" workload.
2. Install the **Unreal Engine — CSS** custom engine build (linked from the modding docs;
   requires linking your GitHub account to Epic).
3. Install **Wwise** (version specified in the docs) into the engine.
4. Clone the starter project: `git clone https://github.com/satisfactorymodding/SatisfactoryModLoader.git`

Then:

5. Copy this whole `HostileScaling` folder into `<SatisfactoryModLoader>/Mods/HostileScaling`.
6. Right-click `FactoryGame.uproject` → **Generate Visual Studio project files**.
7. Build the `FactoryGame Editor` target (Development Editor / Win64), then open the project
   in the editor.
8. Open **Alpakit** (toolbar), find `HostileScaling`, set your game path
   (`C:\Program Files (x86)\Steam\steamapps\common\Satisfactory`) and click **Alpakit!**
   to build + install directly into your game's `FactoryGame/Mods` folder.
9. Build the `Shipping` client target when you want a distributable `.zip` for ficsit.app.

## Header verification status

All game/SML symbols used by this mod were verified against the actual starter project
headers (SatisfactoryModLoader `master`, July 2026): `UFGHealthComponent::TakeDamage`
signature, `mMaxHealth`/`mCurrentHealth`, `AFGEnemy`, `AFGCreatureSpawner::mSpawnData`/
`mRespawnTimeIndays` (int32), `FSpawnData` fields, `GetPurchasedSchematicsOfTypes`,
`UFGSchematic::GetType`/`GetTechTier`, SML config classes (`FText` display strings),
module registration properties, and `IFGSaveInterface` signatures. Added in v1.1.0:
`AFGCreatureSpawner::TraceForNearbyBase`/`mCachedIsNearBase`/`mSpawnRadius`,
`AFGTimeOfDaySubsystem::GetPassedDays`, and `AFGWaterVolume::EncompassesPoint`.

Runtime-verified: `TakeDamage` is virtual, so its hook uses `SUBSCRIBE_UOBJECT_METHOD`
(plain `SUBSCRIBE_METHOD` asserts at startup with "Attempt to hook virtual function
override without providing object instance for implementation resolution"). By contrast
`TraceForNearbyBase` is non-virtual, so its re-pin hook correctly uses the plain
`SUBSCRIBE_METHOD_AFTER`. In-game testing confirmed the engine re-runs `TraceForNearbyBase`
roughly every 30 seconds per active spawner, which is why the re-pin hook is required in
addition to the periodic sweep.

`Config/AccessTransformers.ini` grants the subsystem friend access to `UFGHealthComponent`
and `AFGCreatureSpawner`; invalid entries show up as errors in the build log.

## Design notes / known behavior

- Creatures alive at the moment of a tier-up keep their old health scale until they
  die and respawn. Damage and spawn scaling update within ~10 seconds.
- Spawn-count growth reuses existing spawn locations, so packs get denser rather than wider.
- **Keep Creatures Respawning** only writes vanilla-legal spawner state (the same values a
  natural respawn produces), so it never corrupts a save and is fully reversible — toggle it
  off and the game re-suppresses spawners near your base within one re-trace cycle. All of its
  work is server-side, so multiplayer only needs the mod on the host/server.
- Spawners flagged **never-respawn** in the map data (rare) stay never-respawn. Through v1.0.3
  a spawn-scaling bug clamped these to a 0-day respawn, making them respawn daily; v1.1.0
  restores their intended behavior (see the changelog).
- The mod never edits creature assets, so removing the mod cleanly reverts everything except
  the (harmless) extra entries it stored in its own savegame data.
- Works alongside `MoreEnemySpawns`/`BloodMoon`, but multipliers stack with whatever those
  mods do — start with low `PerTier` values if you run them together.

The respawn-keeper technique — resetting a spawner's cleared-kill bookkeeping so vanilla
repopulates it naturally — was inspired by **Blood Moon** by Chris Cavalluzzi as prior art.
No code was copied; HostileScaling's implementation is independent.
