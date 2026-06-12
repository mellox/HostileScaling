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
}
