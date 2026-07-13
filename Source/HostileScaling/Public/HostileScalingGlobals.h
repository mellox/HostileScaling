#pragma once

#include "CoreMinimal.h"

// Shared state between the damage hooks (registered once at module startup)
// and the per-world subsystem that recomputes multipliers as tiers unlock.
// Only ever written from the game thread on the server.
namespace HostileScaling
{
    // Multiplier applied to damage dealt BY hostile creatures.
    extern float GCreatureDamageMultiplier;

    // Multiplier for hostile creature max health. Applied by writing real
    // health values (vanilla default * multiplier) so health-display mods
    // and scanners show the true scaled numbers.
    extern float GCreatureHealthMultiplier;

    // When false, only damage to players is scaled up. When true, creature
    // damage to anything with a health component (buildings, vehicles) is scaled too.
    extern bool GScaleDamageToBuildings;

    // ---- Keep Creatures Respawning ------------------------------------

    // Combined master+feature toggle state (Enabled AND KeepCreaturesRespawning),
    // published by the subsystem each refresh. Read by the
    // AFGCreatureSpawner::TraceForNearbyBase post-hook to decide whether to
    // re-pin the near-base cache. Defaults false so the hook stays inert
    // until the first refresh (config is not readable at module startup).
    extern bool GKeepCreaturesRespawning;

    // Gates the respawn keeper's per-action diagnostic log lines. Forced
    // false while the master switch is off so a disabled mod stays silent.
    extern bool GVerboseRespawnLogging;

    // Number of spawners whose spawn radius the keeper has widened this
    // session. The AFGCreature::BeginPlay hook only runs its water-spawn
    // diagnostic while this is non-zero (and verbose logging is on).
    extern int32 GWidenedSpawnRadiusCount;
}
