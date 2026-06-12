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
| Tier progression | Highest unlocked **milestone** tier is read from the schematic manager. Each multiplier = `Base + PerTier × Tier`, clamped to `[0.1, Max]`. |

Original spawner values are stored **in the savegame** (keyed by spawner path), so scaling is
always recomputed from vanilla numbers — it never compounds across sessions, and lowering the
config later actually lowers the result. All logic is server-side, so multiplayer works with
the mod installed on the host/server.

### Default configuration

With defaults (`PerTier` 0.15/0.15/0.10), at tier 9 hostiles have ~2.35× health, deal ~2.35×
damage, and spawners refill ~1.9× faster. Config file is written to
`<game>/FactoryGame/Configs/HostileScaling.cfg`; the same values are editable in-game under
**Main Menu → Mods → Hostile Scaling** (SML's Mod Config UI).

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
headers (SatisfactoryModLoader `master`, June 2026): `UFGHealthComponent::TakeDamage`
signature, `mMaxHealth`/`mCurrentHealth`, `AFGEnemy`, `AFGCreatureSpawner::mSpawnData`/
`mRespawnTimeIndays` (int32), `FSpawnData` fields, `GetPurchasedSchematicsOfTypes`,
`UFGSchematic::GetType`/`GetTechTier`, SML config classes (`FText` display strings),
module registration properties, and `IFGSaveInterface` signatures.

Runtime-verified: `TakeDamage` is virtual, so the hook uses `SUBSCRIBE_UOBJECT_METHOD`
(plain `SUBSCRIBE_METHOD` asserts at startup with "Attempt to hook virtual function
override without providing object instance for implementation resolution").

`Config/AccessTransformers.ini` grants the subsystem friend access to `UFGHealthComponent`
and `AFGCreatureSpawner`; invalid entries show up as errors in the build log.

## Design notes / known behavior

- Creatures alive at the moment of a tier-up keep their old health scale until they
  die and respawn. Damage and spawn scaling update within ~10 seconds.
- Spawn-count growth reuses existing spawn locations, so packs get denser rather than wider.
- The mod never edits creature assets, so removing the mod cleanly reverts everything except
  the (harmless) extra entries it stored in its own savegame data.
- Works alongside `MoreEnemySpawns`/`BloodMoon`, but multipliers stack with whatever those
  mods do — start with low `PerTier` values if you run them together.
