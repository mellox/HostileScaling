#include "HostileScalingSubsystem.h"
#include "HostileScaling.h"
#include "HostileScalingGlobals.h"

#include "EngineUtils.h"
#include "Creature/FGCreatureSpawner.h"

// Respawn-keeper method(s) split out of HostileScalingSubsystem.cpp to keep that
// file under the 500-line limit. These are members of AHostileScalingSubsystem;
// friend access to AFGCreatureSpawner is class-based, so it applies here too.

namespace
{
    // Respawn keeper: a spawner counts as starved after this many CONSECUTIVE
    // sweeps (10s each) with a slot waiting for a spawn location. Each starved
    // interval widens the spawn radius one step, so the engine's own location
    // projector gets more ground to try; we never hand-place creatures.
    constexpr int32 StarvationSweepThreshold = 3;
    constexpr float SpawnRadiusWidenStep = 1.5f;
    constexpr float SpawnRadiusMaxScale = 3.0f;
}

void AHostileScalingSubsystem::ApplyRespawnKeeperToSpawner(AFGCreatureSpawner* Spawner, const FString& Key,
    int32 PassedDays, int32& OutSlotsReArmed, int32& OutSpawnersPinned, int32& OutRadiiWidened)
{
    const bool bVerbose = HostileScaling::GVerboseRespawnLogging;

    // (1) Near-base bypass: pin the cached flag false. The engine's own
    // periodic TraceForNearbyBase would flip it back — the post-hook in
    // HostileScaling.cpp re-pins immediately after each vanilla re-trace.
    if (PinNearBaseFalse(Spawner))
    {
        ++OutSpawnersPinned;
    }

    // (2) Cadence-respecting slot re-arm: only slots whose killed-creature
    // deadline has passed, measured against the ALREADY-SCALED respawn time,
    // so the existing frequency config keeps governing pace. Writing the
    // vanilla post-respawn state (WasKilled=false, NumTimesKilled=0,
    // KilledOnDayNr=-1) is save-legal; the alive-creature guard prevents
    // duplicates. Deliberately NO PopulateSpawnData() in v1 — it can
    // regenerate the array under our count scaling (in-game must-test).
    bool bAnyWaiting = false;
    for (int32 SlotIdx = 0; SlotIdx < Spawner->mSpawnData.Num(); ++SlotIdx)
    {
        FSpawnData& Slot = Spawner->mSpawnData[SlotIdx];
        bAnyWaiting |= Slot.WaitingForSpawnLocation;

        if (PassedDays >= 0
            && Slot.WasKilled && !IsValid(Slot.Creature) && Slot.KilledOnDayNr >= 0
            && (PassedDays - Slot.KilledOnDayNr) >= FMath::Max(0, Spawner->mRespawnTimeIndays))
        {
            if (bVerbose)
            {
                UE_LOG(LogHostileScaling, Display,
                    TEXT("[Respawn] Re-armed slot %d/%d on %s: WasKilled true -> false, KilledOnDayNr %d -> -1, NumTimesKilled %d -> 0 (day %d, respawnDays %d)"),
                    SlotIdx + 1, Spawner->mSpawnData.Num(), *GetNameSafe(Spawner), Slot.KilledOnDayNr, Slot.NumTimesKilled, PassedDays, Spawner->mRespawnTimeIndays);
            }
            Slot.WasKilled = false;
            Slot.NumTimesKilled = 0;
            Slot.KilledOnDayNr = -1;
            ++OutSlotsReArmed;
        }
    }

    // (3) Starvation detector, driven purely by the engine's own can't-place
    // signal: any slot stuck WaitingForSpawnLocation counts the sweep as
    // starved; the streak resets only when NO slot is waiting. Live creatures
    // elsewhere in the pack deliberately do NOT reset it — a partially
    // blocked pack must still widen. Widening is per-spawner because the
    // radius is the only lever the engine exposes (FSpawnData has no per-slot
    // location); it is stepwise and capped, originals captured before the
    // first step and restored on toggle-off.
    if (!bAnyWaiting)
    {
        StarvationStreaks.Remove(Key);
        CappedStarvedLoggedPaths.Remove(Key);
        return;
    }

    int32& Streak = StarvationStreaks.FindOrAdd(Key);
    ++Streak;
    if (bVerbose && !CappedStarvedLoggedPaths.Contains(Key))
    {
        UE_LOG(LogHostileScaling, Display,
            TEXT("[Respawn] Starvation streak %d/%d on %s (slot waiting for spawn location)"),
            Streak, StarvationSweepThreshold, *GetNameSafe(Spawner));
    }
    if (Streak < StarvationSweepThreshold)
    {
        return;
    }
    Streak = 0;

    const float VanillaRadius = OriginalSpawnRadii.Contains(Key)
        ? OriginalSpawnRadii[Key]
        : OriginalSpawnRadii.Add(Key, Spawner->mSpawnRadius);
    const float RadiusCap = VanillaRadius * SpawnRadiusMaxScale;
    if (Spawner->mSpawnRadius + KINDA_SMALL_NUMBER >= RadiusCap)
    {
        // Capped and still starved is not actionable: log once per starvation
        // episode, then go quiet (a permanently boxed-in spawner would
        // otherwise repeat streak lines forever). Cleared on recovery above.
        if (bVerbose && !CappedStarvedLoggedPaths.Contains(Key))
        {
            UE_LOG(LogHostileScaling, Display,
                TEXT("[Respawn] Spawn radius capped (x%.1f) and still starved on %s; going quiet until it recovers"),
                SpawnRadiusMaxScale, *GetNameSafe(Spawner));
            CappedStarvedLoggedPaths.Add(Key);
        }
        return;
    }

    const float Before = Spawner->mSpawnRadius;
    Spawner->mSpawnRadius = FMath::Min(Before * SpawnRadiusWidenStep, RadiusCap);
    WidenedSpawnerPaths.Add(Key);
    HostileScaling::GWidenedSpawnRadiusCount = WidenedSpawnerPaths.Num();
    ++OutRadiiWidened;
    if (bVerbose)
    {
        UE_LOG(LogHostileScaling, Display,
            TEXT("[Respawn] Widened spawn radius on %s: %.0f -> %.0f (vanilla %.0f, cap x%.1f)"),
            *GetNameSafe(Spawner), Before, Spawner->mSpawnRadius, VanillaRadius, SpawnRadiusMaxScale);
    }
}

void AHostileScalingSubsystem::RestoreVanillaRespawnBehavior()
{
    const bool bVerbose = HostileScaling::GVerboseRespawnLogging;
    int32 RadiiRestored = 0;
    int32 SpawnersReTraced = 0;

    for (TActorIterator<AFGCreatureSpawner> It(GetWorld()); It; ++It)
    {
        AFGCreatureSpawner* Spawner = *It;
        if (!IsValid(Spawner))
        {
            continue;
        }

        if (const float* VanillaRadius = OriginalSpawnRadii.Find(Spawner->GetPathName()))
        {
            if (!FMath::IsNearlyEqual(Spawner->mSpawnRadius, *VanillaRadius))
            {
                if (bVerbose)
                {
                    UE_LOG(LogHostileScaling, Display,
                        TEXT("[Respawn] Restored spawn radius on %s: %.0f -> %.0f"),
                        *GetNameSafe(Spawner), Spawner->mSpawnRadius, *VanillaRadius);
                }
                Spawner->mSpawnRadius = *VanillaRadius;
                ++RadiiRestored;
            }
        }

        // Vanilla recomputes the near-base cache honestly. The globals were
        // published (false) before this runs, so the TraceForNearbyBase
        // post-hook will NOT re-pin during these calls. Only loaded spawners
        // are reached; streamed-out ones self-heal when the game re-traces
        // them after streaming in.
        Spawner->TraceForNearbyBase();
        ++SpawnersReTraced;
    }

    StarvationStreaks.Empty();
    WidenedSpawnerPaths.Empty();
    CappedStarvedLoggedPaths.Empty();
    HostileScaling::GWidenedSpawnRadiusCount = 0;

    UE_LOG(LogHostileScaling, Display,
        TEXT("[Respawn] Keeper OFF: restored %d widened spawn radius(es), re-traced near-base on %d loaded spawner(s)"), RadiiRestored, SpawnersReTraced);
}
