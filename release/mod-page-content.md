# ficsit.app Mod Page Content — HostileScaling

Copy-paste material for the SMR mod page and version uploads. Full version
history lives in [CHANGELOG.md](CHANGELOG.md); the per-version block below is the
text to paste into the SMR "changelog" field when uploading a version.

---

## Name

Hostile Scaling

## Mod Reference (set at creation — do not change)

HostileScaling

(matches the plugin folder, the C++ source folder, the uplugin, and the ConfigId.)

## Short description (shown under the icon in the mod list)

Creatures scale with your milestone tier — health, damage, spawn rate — and keep
respawning where vanilla stops them. Fully configurable in-game, live.

## Full description (Markdown)

```markdown
# Hostile Scaling

Make Massage-2-A-B-P fight back. As you climb the milestone tiers, hostile
creatures grow tougher, hit harder, and respawn faster — and they keep coming
back even after you've industrialized half the planet, so the world never goes
quiet.

## What it does

- **Health scaling** — hostile creatures get real scaled max health
  (health-display mods show the true numbers)
- **Damage scaling** — creature attacks hit harder (melee, projectile, and
  explosion damage all covered)
- **Spawn scaling** — faster respawns and denser packs
- **Keep creatures respawning** *(new, on by default)* — vanilla stops
  respawning creatures near your base and in areas you've cleared out; this keeps
  spawners active so the planet stays dangerous. Cleared packs come back on their
  normal (scaled) schedule, and a spawner you've built over widens its search
  radius to find valid ground nearby. Toggle it off for vanilla behavior.
- **Tier progression** — every multiplier is `Base + PerTier × your highest
  unlocked milestone tier`, with a configurable cap

Lizard doggos, tamed creatures, and passive fauna are never affected.

## Configuration

Everything is adjustable from the mod's panel in the **Mods menu** (main menu or
pause menu) and applies to a running game within ~10 seconds — no restart needed:

| Setting | Default |
|---|---|
| Health / Damage base multiplier | 1.0 (vanilla at tier 0) |
| Health / Damage added per tier | +15% per tier |
| Spawn rate added per tier | +10% per tier |
| Maximum multiplier caps | 5× health/damage, 4× spawns |
| Scale damage to buildings | off |
| Increase spawn counts | on |
| Keep creatures respawning | on |
| Verbose respawn logging (debugging) | off |

With defaults, by tier 9 hostiles have ~2.35× health and damage and ~1.9× spawn
rate. Crank the per-tier values and caps for a true nightmare planet.

## Notes

- Works in multiplayer (host/server needs the mod)
- Scaling never compounds across save/load — always computed from vanilla values
- The respawn keeper only writes vanilla-legal spawner state, so it's fully
  reversible and never corrupts a save
- Stacks with other creature mods (MoreEnemySpawns, BloodMoon); their effects
  multiply together, so start gentle if you run several

## Credits

Respawn-keeper technique inspired by **Blood Moon** by Chris Cavalluzzi as prior
art; no code was copied.

## Bug reports

Report issues on the mod's GitHub issues page (link on this page) or ping me on
the Satisfactory Modding Discord.
```

## Changelog for this version (v1.1.0) — paste into the SMR version-upload form

```
New: Keep Creatures Respawning (on by default). Vanilla stops respawning creatures
near your base and in cleared-out areas; this keeps spawners active. Cleared packs
re-arm on their normal (scaled) schedule, near-base suppression is bypassed, and a
spawner you've built over widens its search radius to find valid ground nearby.
Server-side and fully reversible — turn it off for vanilla behavior.

New: Verbose Respawn Logging (off by default) — a debugging toggle for detailed
respawn diagnostics in the game log.

Fix: map-defined "never respawn" spawners were respawning daily due to a scaling
bug present since v1.0.1; they now correctly stay disabled once cleared.

Respawn technique inspired by Blood Moon (Chris Cavalluzzi) as prior art; no code copied.
```

## Compatibility info to set after upload

- Stable branch: **Works** (built and in-game tested on 1.1.x, July 2026)
- Experimental: untested — set "Works" only if tested, else leave unknown

## Form fields

- Source link: https://github.com/mellox/HostileScaling
- Icon: `C:\Claude\Projects\HostileScaling\Resources\Icon128.png`
- Hidden: NO (leave visible, or the release announcement won't post)
