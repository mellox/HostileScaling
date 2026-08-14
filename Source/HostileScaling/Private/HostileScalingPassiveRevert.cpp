#include "HostileScalingSubsystem.h"
#include "HostileScaling.h"

#include "Creature/FGCreatureSpawner.h"

// Migration revert for PASSIVE/neutral spawners, split out of
// HostileScalingSubsystem.cpp to keep that file under the 500-line limit. This
// is a member of AHostileScalingSubsystem; friend access to AFGCreatureSpawner
// is class-based, so it applies here too.

namespace
{
    // Cap on explicit TraceForNearbyBase() migration re-traces per sweep. The
    // call reaches the engine's proximity system; a freshly streamed region can
    // present dozens of passive spawners in one frame, so the revert is spread
    // across sweeps instead of bursting. Vanilla re-traces active spawners on
    // its own cadence, so a deferred spawner is not stuck.
    constexpr int32 MaxPassiveReTracesPerSweep = 16;
}

void AHostileScalingSubsystem::RevertPassiveSpawnerResidue(AFGCreatureSpawner* Spawner, const FString& Key,
    int32& OutSlotsTrimmed, int32& OutSpawnersTrimmed, int32& OutReTraced)
{
    // Undo the two SaveGame writes v1.1.0 left on passive spawners. Shrink grown
    // packs back toward vanilla EVERY sweep (creature-less trailing slots only)
    // so it converges as members despawn — matching the hostile path's
    // idempotent-convergence pattern instead of a one-shot, which a full herd
    // would defeat. Respawn time and radius are not SaveGame, so they self-heal
    // on load and are not touched here.
    //
    // *VanillaCount == 0 means the spawner was first seen (by an earlier
    // version) before the engine had populated its spawn data — a bookkeeping
    // artifact, not a vanilla pack size. Shrinking to it would delete the whole
    // pack, and mSpawnData is SaveGame, so the deletion would persist. Require
    // > 0, mirroring the VanillaCount > 0 guard the hostile growth path has
    // always carried. Never remove a slot the engine is mid-flight on
    // (WaitingForSpawnLocation): such a slot has no Creature yet, so the
    // live-creature guard alone does not cover it.
    // TODO(2026-08-13): the > 0 guard covers a recorded 0 but not a recorded
    // UNDERCOUNT (spawner first seen mid-PopulateSpawnData). If real saves show
    // trims larger than a slot or two, re-derive the target from
    // mNumberOfCreatures.Max (design-time map data, not our bookkeeping)
    // instead of from OriginalSpawnCounts. See round-3 HIGH-1 alternative (c).
    const int32* VanillaCount = OriginalSpawnCounts.Find(Key);
    int32 SlotsTrimmed = 0;
    if (VanillaCount && *VanillaCount > 0)
    {
        while (Spawner->mSpawnData.Num() > *VanillaCount)
        {
            const int32 LastIdx = Spawner->mSpawnData.Num() - 1;
            if (Spawner->mSpawnData[LastIdx].Creature != nullptr
                || Spawner->mSpawnData[LastIdx].WaitingForSpawnLocation)
            {
                break;
            }
            Spawner->mSpawnData.RemoveAt(LastIdx);
            ++SlotsTrimmed;
        }
    }
    OutSlotsTrimmed += SlotsTrimmed;
    if (SlotsTrimmed > 0)
    {
        ++OutSpawnersTrimmed;
        // Ungated: the only line that names WHICH spawner a trim hit, and
        // mSpawnData is SaveGame, so a wrong trim persists. Measured before/after
        // plus the target actually used — no inferred cause. Bounded: the trim
        // converges, so each spawner prints this a handful of times at most.
        UE_LOG(LogHostileScaling, Display,
            TEXT("[SpawnScale] Passive trim on %s: removed %d slot(s), %d -> %d, recorded original %d"),
            *GetNameSafe(Spawner), SlotsTrimmed,
            Spawner->mSpawnData.Num() + SlotsTrimmed, Spawner->mSpawnData.Num(),
            VanillaCount ? *VanillaCount : -1);
    }

    // Near-base pin: hand the flag back to the game. Re-trace only spawners a
    // PREVIOUS version actually touched — an OriginalRespawnDays entry exists
    // iff an earlier build's sweep reached this spawner (the hostile-only path
    // never writes one for a passive spawner), and a pinned spawner reads
    // mCachedIsNearBase == false. A save with no such entry therefore pays
    // nothing. Budgeted per sweep so a newly streamed region cannot produce a
    // proximity-query burst in one frame. No creature-class test: a spawner
    // whose class failed to resolve can still carry a pin from v1.1.0, which
    // had no class test either.
    if (OutReTraced < MaxPassiveReTracesPerSweep
        && !Spawner->mCachedIsNearBase
        && OriginalRespawnDays.Contains(Key)
        && !RevertedPassiveSpawnerPaths.Contains(Key))
    {
        Spawner->TraceForNearbyBase();
        RevertedPassiveSpawnerPaths.Add(Key);
        ++OutReTraced;
    }
}
