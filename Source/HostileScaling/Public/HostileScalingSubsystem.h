#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "FGSaveInterface.h"
#include "HostileScalingSubsystem.generated.h"

class AFGCreature;
class AFGCreatureSpawner;

// Server-side subsystem that periodically:
//  - recomputes the global damage/health multipliers read by the damage
//    hooks (see HostileScaling.cpp) based on config and unlocked tier,
//  - applies health scaling to live hostile creatures (tier-ups mid-session),
//  - rescales creature spawner respawn times and spawn counts,
//  - keeps creatures respawning (near-base bypass, kill-slot re-arm,
//    starvation-driven spawn radius widening) when the toggle is on.
//
// Original spawner values are stored in the savegame (keyed by spawner path)
// so scaling is always computed from vanilla values and never compounds
// across sessions or config changes.
UCLASS()
class HOSTILESCALING_API AHostileScalingSubsystem : public AModSubsystem, public IFGSaveInterface
{
    GENERATED_BODY()

public:
    AHostileScalingSubsystem();

    // Sets the creature's max health to vanilla-default * current multiplier,
    // preserving its current health percentage. Idempotent: computed from the
    // class default object, so repeated calls and save/load never compound.
    // Called from the AFGCreature::BeginPlay hook and the periodic refresh.
    static void ApplyCreatureHealthScale(AFGCreature* Creature);

    // Forces the spawner's cached near-base flag to false so vanilla stops
    // suppressing its respawns. The write needs friend access
    // (AccessTransformers.ini), so it lives here; called from the periodic
    // sweep and from the TraceForNearbyBase post-hook in HostileScaling.cpp.
    // Callers gate on the config global; authority is re-checked inside.
    // Returns true when the flag actually flipped (was true).
    static bool PinNearBaseFalse(AFGCreatureSpawner* Spawner);

    // MCP contract v1 (_team/mcp-mod-contract-v1.md), defined in
    // HostileScalingMcp.cpp. Found by name through reflection; no dependency on
    // any MCP module. Both are READ-ONLY: they write nothing in this mod (this
    // actor, spawners, creatures, config values, globals); reading config may let
    // SML's config manager cache a struct entry, the same entry RefreshScaling
    // already creates. Dead code when nobody calls them.
    UFUNCTION()
    FString McpDescribe() const;

    UFUNCTION()
    FString McpSnapshot(const FString& ArgsJson);

    // IFGSaveInterface
    virtual bool ShouldSave_Implementation() const override { return true; }
    virtual bool NeedTransform_Implementation() override { return false; }
    virtual void PreSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PostSaveGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PreLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void PostLoadGame_Implementation(int32 saveVersion, int32 gameVersion) override {}
    virtual void GatherDependencies_Implementation(TArray<UObject*>& out_dependentObjects) override {}

protected:
    virtual void BeginPlay() override;

private:
    void RefreshScaling();
    int32 GetHighestUnlockedTier() const;
    static float ComputeMultiplier(float Base, float PerTier, float Max, int32 Tier);

    // McpSnapshot per-spawner JSON row (read-only; member so the existing
    // friend grant covers the mSpawnData read). DistanceCm < 0 omits distance.
    FString McpSpawnerRow(const AFGCreatureSpawner* Spawner, double DistanceCm) const;

    // Respawn keeper, per spawner (feature toggle already checked by caller;
    // never called for vanilla never-respawn spawners): near-base pin,
    // cadence-respecting kill-slot re-arm, starvation detection with
    // stepwise spawn radius widening. Counters accumulate sweep totals.
    void ApplyRespawnKeeperToSpawner(AFGCreatureSpawner* Spawner, const FString& Key, int32 PassedDays,
        int32& OutSlotsReArmed, int32& OutSpawnersPinned, int32& OutRadiiWidened);

    // Passive/neutral spawner migration revert (defined in
    // HostileScalingPassiveRevert.cpp): trim spawn-count residue an earlier
    // version grew, and hand a pinned near-base flag back to vanilla. Caller
    // guarantees the spawner is valid and classified non-hostile. Counters
    // accumulate sweep totals; OutReTraced is also the per-sweep re-trace
    // budget this function reads.
    void RevertPassiveSpawnerResidue(AFGCreatureSpawner* Spawner, const FString& Key,
        int32& OutSlotsTrimmed, int32& OutSpawnersTrimmed, int32& OutReTraced);

    // Toggle-off / master-off transition: restore widened radii from
    // OriginalSpawnRadii, let vanilla recompute every near-base cache via
    // public TraceForNearbyBase(), and clear transient keeper state.
    void RestoreVanillaRespawnBehavior();

    FTimerHandle RefreshTimerHandle;

    // Vanilla values per spawner (keyed by actor path name), persisted in the
    // savegame so we can always rescale from the original numbers.
    UPROPERTY(SaveGame)
    TMap<FString, int32> OriginalRespawnDays;

    UPROPERTY(SaveGame)
    TMap<FString, int32> OriginalSpawnCounts;

    // Vanilla spawn radius per spawner, captured on first widening.
    // mSpawnRadius itself is NOT SaveGame (resets to vanilla on load), so
    // stored originals always equal vanilla and restoring never compounds.
    UPROPERTY(SaveGame)
    TMap<FString, float> OriginalSpawnRadii;

    // Consecutive sweeps a spawner spent starved (some slot waiting for a
    // spawn location). Transient by design: streaks and widened-set restart
    // each session, radii reset to vanilla on load.
    TMap<FString, int32> StarvationStreaks;

    // Spawners widened THIS session; drives GWidenedSpawnRadiusCount.
    TSet<FString> WidenedSpawnerPaths;

    // Spawners that already logged the capped-and-still-starved notice this
    // starvation episode (cleared when the spawner recovers) — keeps verbose
    // streak lines from repeating forever on a permanently boxed-in spawner.
    TSet<FString> CappedStarvedLoggedPaths;

    // Passive/neutral spawners whose v1.1.0 near-base pin has been handed back
    // to vanilla (one explicit TraceForNearbyBase) this session. Transient, so
    // the re-trace runs once per spawner per session. mCachedIsNearBase is
    // SaveGame: a spawner that streams out and back in before the next save
    // reloads the pinned value and is not re-traced by us again — it relies on
    // vanilla's own periodic re-trace. Spawn-count trimming is NOT gated on
    // this set; it re-converges every sweep.
    TSet<FString> RevertedPassiveSpawnerPaths;

    // Spawners whose hostile/passive classification was already logged (verbose)
    // so the per-spawner [SpawnScale] line prints once each, not every sweep.
    TSet<FString> SpawnScaleLoggedPaths;

    // Detects the keeper's on->off transition (feature toggle or master
    // switch) so vanilla behavior is restored exactly once.
    bool bRespawnKeeperWasActive = false;

    bool bLoggedNeverRespawnCount = false;

    // High-water marks for the classification census, so the census describes the
    // largest spawner set seen this session rather than the first one.
    int32 CensusHighWaterHostile = 0;
    int32 CensusHighWaterPassive = 0;
    int32 CensusHighWaterNullClass = 0;

    int32 LastLoggedTier = -1;
    int32 LastLoggedRespawnState = -1;
};
