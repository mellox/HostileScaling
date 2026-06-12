#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "FGSaveInterface.h"
#include "HostileScalingSubsystem.generated.h"

class AFGCreature;

// Server-side subsystem that periodically:
//  - recomputes the global damage/health multipliers read by the damage
//    hooks (see HostileScaling.cpp) based on config and unlocked tier,
//  - applies health scaling to live hostile creatures (tier-ups mid-session),
//  - rescales creature spawner respawn times and spawn counts.
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

    FTimerHandle RefreshTimerHandle;

    // Vanilla values per spawner (keyed by actor path name), persisted in the
    // savegame so we can always rescale from the original numbers.
    UPROPERTY(SaveGame)
    TMap<FString, int32> OriginalRespawnDays;

    UPROPERTY(SaveGame)
    TMap<FString, int32> OriginalSpawnCounts;

    int32 LastLoggedTier = -1;
};
