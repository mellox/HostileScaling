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

Hostile creatures scale with your milestone tier and keep respawning near your base. Passive wildlife is never scaled.

## Full description (Markdown)

```markdown
# Hostile Scaling

Make MASSAGE-2(A-B)b fight back. As you climb the milestone tiers, hostile
creatures grow tougher and hit harder — and they keep coming back near your
factories, where vanilla stops them.

## What it does

- **Health scaling** — hostile creatures get a real scaled max-health value, written on the creature itself
- **Damage scaling** — damage hostile creatures deal to players is multiplied (and, with the buildings option on, damage they deal to anything else). The mod hooks the game's general, point and radial damage functions
- **Spawn scaling** — shorter respawn waits and, with pack-size scaling on (the
  default), bigger packs, on hostile spawners only. Respawn waits are kept in whole in-game days and rounded, so a short wait may not change: a 1-day wait stays 1 day at any multiplier from 1× to 2×
- **Respawn near factories** *(on by default)* — vanilla stops
  respawning creatures near your base; this keeps **hostile** spawners near your
  base active. Killed creatures' spawn slots are re-armed once their (scaled) respawn wait has passed, and a
  spawner that cannot find a place to spawn widens its search radius step by
  step, up to three times its original size. Passive wildlife keeps vanilla
  behavior. Turn it off to hand the near-base check back to the game.
- **Tier progression** — every multiplier is `Base + PerTier × your highest
  unlocked milestone tier`, with a configurable cap

Lizard doggos and other creatures the game marks as passive, and tamed creatures, get no extra health and deal no extra damage, and spawners of passive creatures get no faster respawns or bigger packs. The one exception is a one-time cleanup:
on saves made by v1.1.0 or earlier, the mod walks passive packs back toward the size it first
recorded and hands their near-base flag back to the game (see the changelog below).

## Configuration

Everything in the table below is adjustable from the mod's panel in the **Mods menu** (main menu or pause menu), and most changes apply to a running game within about 10 seconds, no restart needed. Turning the mod off does not shrink packs that already grew, and creatures that are already alive keep their scaled health:

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

With defaults, at tier 9 hostiles have 2.35× health and damage, and the
Respawn + Packs multiplier is 1.9×. Crank the per-tier values and caps for a
true nightmare planet.

## Notes

- Multiplayer: creature health and spawner changes are made only by the host or server; install the mod on the host or server and on every player's game
- Scaling does not compound across save/load: health is recomputed from each
  creature type's default, and respawn time and pack size from the values the
  mod recorded the first time it saw each spawner
- The respawn keeper changes only spawner values the game itself uses (the
  near-base flag, killed-creature bookkeeping and the search radius); turning
  it off hands the near-base check back to the game and resets the search
  radius it widened on the spawners currently loaded around you

## Credits

Respawn-keeper technique inspired by **Blood Moon** by Chris Cavalluzzi as prior
art; no code was copied.

## Bug reports

Report issues on the mod's GitHub issues page (link on this page) or ping me on
the Satisfactory Modding Discord.
```

## Changelog for this version (v1.1.2) — paste into the SMR version-upload form

```
Changed: compatibility update for Satisfactory 1.2.6. This version requires game build
520140 (Satisfactory 1.2.6) or later and SML 3.12.1 or a later 3.x version.

No change to how hostile creatures are scaled or kept respawning, and no setting was
added, removed or renamed.

Added: two reporting functions for other mods. Another mod can call them to read Hostile Scaling's current settings, multipliers and spawner state. They only read: they change no creature, spawner or setting. Nothing in Hostile Scaling calls them.
```

(The "Added" paragraph is one line on purpose: delete that line and the blank line above it to
ship without the reporting-functions item. The matching bullet in CHANGELOG.md is also one
self-contained bullet.)

## Compatibility info to set after upload

- Stable branch: set **Works** only after the v1.1.2 PACKAGE has passed an in-game check on game
  build 520140. A development build from the same source loaded on 520140 with SML 3.12.1 on
  2026-11-22, but that is a different binary from the package.
- Experimental: untested — set "Works" only if tested, else leave unknown

## Form fields

- Source link: https://github.com/mellox/HostileScaling
- Icon: `C:\Claude\Projects\HostileScaling\Resources\Icon128.png`
- Hidden: NO (leave visible, or the release announcement won't post)
