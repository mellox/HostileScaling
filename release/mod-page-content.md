# ficsit.app Mod Page Content — HostileScaling

Copy-paste material for the SMR "New Mod" form and first version upload.

---

## Name

Hostile Scaling

## Mod Reference (CANNOT BE CHANGED AFTER CREATION — verify 5 times!)

HostileScaling

(matches the plugin folder, the C++ source folder, the uplugin, and the ConfigId — verified consistent.)

## Short description (shown under the icon in the mod list)

Creatures grow stronger as you progress: health, damage, and spawn rates scale
with your milestone tier. Every rate is configurable in-game, live.

## Full description (Markdown)

```markdown
# Hostile Scaling

Make Massage-2-A-B-P fight back. As you climb the milestone tiers, hostile
creatures grow tougher, hit harder, and respawn faster — so the planet stays
dangerous even after you've industrialized half of it.

## What it does

- **Health scaling** — hostile creatures get real scaled max health
  (health-display mods show the true numbers)
- **Damage scaling** — creature attacks hit harder (melee, projectile, and
  explosion damage all covered)
- **Spawn scaling** — faster respawns and denser packs
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

With defaults, by tier 9 hostiles have ~2.35× health and damage and ~1.9× spawn
rate. Crank the per-tier values and caps for a true nightmare planet.

## Notes

- Works in multiplayer (host/server needs the mod)
- Scaling never compounds across save/load — always computed from vanilla values
- Stacks with other creature mods (MoreEnemySpawns, BloodMoon); their effects
  multiply together, so start gentle if you run several

## Bug reports

Report issues on the mod's GitHub issues page (link on this page) or ping me on
the Satisfactory Modding Discord.
```

## Changelog for first uploaded version (v1.0.1)

```
Initial public release.

- Tier-scaled hostile creature health, damage, and spawn frequency
- All scaling rates and caps configurable in-game with live effect (no restart)
- Health scaling writes real health values, so health-bar mods show the truth
- Passive and tamed creatures (doggos!) are never affected
- Idempotent scaling: never compounds across save/load
```

## Compatibility info to set after upload

- Stable branch: **Works** (tested on 1.1.x, CL 493833, June 2026)
- Experimental: untested — set "Works" only if tested, else leave unknown

## Form fields

- Source link: (create GitHub repo first — suggested: github.com/mellox/HostileScaling)
- Icon: upload `C:\Claude\Projects\HostileScaling\Resources\Icon128.png` (or a larger remake)
- Hidden: NO (leave visible, or the release announcement won't post)
