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

## Short description (shown under the icon in the mod list) — max 128 chars

Hostile creatures scale with your milestone tier and keep respawning where vanilla stops them. Passive wildlife is untouched.

## Full description (Markdown)

```markdown
# Hostile Scaling

Make MASSAGE-2(A-B)b fight back. As you climb the milestone tiers, hostile
creatures grow tougher, hit harder, and respawn faster — and they keep coming
back even after you've industrialized half the planet, so the world never goes
quiet.

## What it does

- **Health scaling** — hostile creatures get real scaled max health
  (health-display mods show the true numbers)
- **Damage scaling** — creature attacks hit harder (melee, projectile, and
  explosion damage all covered)
- **Spawn scaling** — faster respawns and denser packs, on hostile spawners only
- **Respawn near factories** *(on by default)* — vanilla stops
  respawning creatures near your base and in areas you've cleared out; this keeps
  **hostile** spawners active so the planet stays dangerous. Cleared packs come back on their
  normal (scaled) schedule, and a spawner you've built over widens its search
  radius to find valid ground nearby. Passive wildlife keeps vanilla behavior.
  Toggle it off for vanilla behavior everywhere.
- **Tier progression** — every multiplier is `Base + PerTier × your highest
  unlocked milestone tier`, with a configurable cap

Lizard doggos, tamed creatures, and passive fauna are never scaled — no extra health, no
extra damage, no faster respawns, no bigger packs. The one exception is a one-time cleanup:
on saves made by v1.1.0 or earlier, the mod walks passive packs back to the size it first
recorded and hands their near-base flag back to the game (see the changelog below).

## Configuration

Everything is adjustable from the mod's panel in the **Mods menu** (main menu or
pause menu) and applies to a running game within ~10 seconds — no restart needed:

| Setting | Default |
|---|---|
| Health / Damage base multiplier | 1.0 (vanilla at tier 0) |
| Health / Damage added per tier | +15% per tier |
| Respawn + Packs: Added Per Tier | +10% per tier |
| Maximum multiplier caps | 5× health/damage, 4× respawn speed + pack size |
| Damage also applies to buildings | off |
| Also apply spawn scaling to pack size | on |
| Respawn near factories | on |
| Verbose respawn logging (troubleshooting) | off |

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

## Changelog for this version (v1.1.1) — paste into the SMR version-upload form

```
Changed: spawn scaling and the respawn keeper are now HOSTILE-only. Through v1.1.0 the
mod scaled every creature spawner, passive ones included, so doggo and ambient-fauna
packs grew and kept respawning near your base. Health and damage scaling were always
hostile-only; the spawner side now matches.

Migration: on saves made by v1.1.0 or earlier, passive spawners are walked back —
creature-less extra pack slots are removed until the pack is back at the size recorded
when the mod first saw the spawner, and the near-base flag is handed back to the game.
With the master Enabled switch off, nothing is reverted.

Fixed: the near-base bypass no longer touches passive spawners at all. It is refused at
the single point that writes the flag, so passive wildlife keeps vanilla's near-base
suppression even while the feature is on.

Changed: config panel clarity pass. The three spawn sliders are labelled "Respawn + Packs"
because they always drive both, "Increase Spawn Counts" is now "Also Apply
Spawn Scaling to Pack Size", and "Keep Creatures Respawning" is now "Respawn Near
Factories". Setting values and saved configs are unchanged.

Added: log diagnostics for the hostile/passive split and the passive migration, so a
support log shows what was classified and what was trimmed.

Respawn technique inspired by Blood Moon (Chris Cavalluzzi) as prior art; no code copied.
```

## Compatibility info to set after upload

- Stable branch: set **Works** only after the v1.1.1 runtime checklist passes in-game on the
  current game build; the last recorded in-game test is v1.1.x, July 2026
- Experimental: untested — set "Works" only if tested, else leave unknown

## Form fields

- Source link: https://github.com/mellox/HostileScaling
- Icon: `C:\Claude\Projects\HostileScaling\Resources\Icon128.png`
- Hidden: NO (leave visible, or the release announcement won't post)
