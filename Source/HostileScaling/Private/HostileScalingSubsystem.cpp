#include "HostileScalingSubsystem.h"
#include "HostileScaling.h"
#include "HostileScalingGlobals.h"
#include "HostileScalingConfig.h"

#include "EngineUtils.h"
#include "TimerManager.h"
#include "FGHealthComponent.h"
#include "FGSchematicManager.h"
#include "FGSchematic.h"
#include "Creature/FGCreature.h"
#include "Creature/FGCreatureSpawner.h"

namespace
{
    constexpr float RefreshIntervalSeconds = 10.0f;
}

AHostileScalingSubsystem::AHostileScalingSubsystem()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AHostileScalingSubsystem::BeginPlay()
{
    Super::BeginPlay();

    // All scaling is server-authoritative; clients receive results via replication.
    if (!HasAuthority())
    {
        return;
    }

    GetWorldTimerManager().SetTimer(
        RefreshTimerHandle, this, &AHostileScalingSubsystem::RefreshScaling,
        RefreshIntervalSeconds, true, 2.0f);
}

float AHostileScalingSubsystem::ComputeMultiplier(float Base, float PerTier, float Max, int32 Tier)
{
    return FMath::Clamp(Base + PerTier * static_cast<float>(Tier), 0.1f, FMath::Max(0.1f, Max));
}

void AHostileScalingSubsystem::ApplyCreatureHealthScale(AFGCreature* Creature)
{
    if (!IsValid(Creature) || Creature->IsPassiveCreature() || Creature->IsTamed())
    {
        return;
    }

    UFGHealthComponent* Health = Creature->GetHealthComponent();
    if (!Health)
    {
        return;
    }

    // Vanilla health comes from the class default object, NOT the instance:
    // instance health is savegame-persisted, so deriving from it would
    // compound the multiplier across sessions.
    const AFGCreature* DefaultCreature = Creature->GetClass()->GetDefaultObject<AFGCreature>();
    const UFGHealthComponent* DefaultHealth = DefaultCreature ? DefaultCreature->GetHealthComponent() : nullptr;
    if (!DefaultHealth)
    {
        return;
    }

    // Direct member access granted via AccessTransformers.ini (Friend).
    const float VanillaMax = DefaultHealth->mMaxHealth;
    const float NewMax = VanillaMax * FMath::Max(0.1f, HostileScaling::GCreatureHealthMultiplier);
    const float OldMax = Health->mMaxHealth;
    if (VanillaMax <= KINDA_SMALL_NUMBER || FMath::IsNearlyEqual(NewMax, OldMax))
    {
        return;
    }

    const float HealthFraction = OldMax > KINDA_SMALL_NUMBER
        ? FMath::Clamp(Health->mCurrentHealth / OldMax, 0.0f, 1.0f)
        : 1.0f;
    Health->mMaxHealth = NewMax;
    Health->mCurrentHealth = NewMax * HealthFraction;

    static bool bLoggedFirstScale = false;
    if (!bLoggedFirstScale)
    {
        UE_LOG(LogHostileScaling, Display, TEXT("Health scaling active: %s %.0f -> %.0f max health"),
            *GetNameSafe(Creature), VanillaMax, NewMax);
        bLoggedFirstScale = true;
    }
}

int32 AHostileScalingSubsystem::GetHighestUnlockedTier() const
{
    AFGSchematicManager* SchematicManager = AFGSchematicManager::Get(GetWorld());
    if (!SchematicManager)
    {
        return 0;
    }

    // Highest tech tier among purchased milestone schematics.
    TArray<TSubclassOf<UFGSchematic>> Purchased;
    SchematicManager->GetPurchasedSchematicsOfTypes({ESchematicType::EST_Milestone}, Purchased);

    int32 MaxTier = 0;
    for (const TSubclassOf<UFGSchematic>& Schematic : Purchased)
    {
        if (Schematic)
        {
            MaxTier = FMath::Max(MaxTier, UFGSchematic::GetTechTier(Schematic));
        }
    }
    return MaxTier;
}

void AHostileScalingSubsystem::RefreshScaling()
{
    const FHostileScalingConfigStruct Config = FHostileScalingConfigStruct::GetActiveConfig(this);

    if (!Config.Enabled)
    {
        HostileScaling::GCreatureDamageMultiplier = 1.0f;
        HostileScaling::GCreatureHealthMultiplier = 1.0f;
        return;
    }

    const int32 Tier = GetHighestUnlockedTier();
    const float HealthMult = ComputeMultiplier(Config.HealthBase, Config.HealthPerTier, Config.HealthMax, Tier);
    const float DamageMult = ComputeMultiplier(Config.DamageBase, Config.DamagePerTier, Config.DamageMax, Tier);
    const float SpawnMult = ComputeMultiplier(Config.SpawnBase, Config.SpawnPerTier, Config.SpawnMax, Tier);

    // Publish state for the damage hooks and the creature BeginPlay hook.
    HostileScaling::GCreatureDamageMultiplier = DamageMult;
    HostileScaling::GCreatureHealthMultiplier = HealthMult;
    HostileScaling::GScaleDamageToBuildings = Config.ScaleDamageToBuildings;

    if (Tier != LastLoggedTier)
    {
        UE_LOG(LogHostileScaling, Display,
            TEXT("Tier %d unlocked. Multipliers — health: %.2f, damage: %.2f, spawns: %.2f"),
            Tier, HealthMult, DamageMult, SpawnMult);
        LastLoggedTier = Tier;
    }

    // ---- Health scaling: reconcile all live hostile creatures. ----------
    // New spawns are handled instantly by the BeginPlay hook; this pass picks
    // up multiplier changes (tier-ups, live config edits) for creatures that
    // are already alive. ApplyCreatureHealthScale is idempotent, so creatures
    // already at the right value are skipped cheaply.
    for (TActorIterator<AFGCreature> It(GetWorld()); It; ++It)
    {
        ApplyCreatureHealthScale(*It);
    }

    // ---- Spawner scaling: recompute from stored vanilla values. ---------
    // Applied idempotently every refresh: the game can regenerate spawn data
    // at runtime (AFGCreatureSpawner::PopulateSpawnData), so we converge back
    // to the target instead of assuming our last write survived.
    for (TActorIterator<AFGCreatureSpawner> It(GetWorld()); It; ++It)
    {
        AFGCreatureSpawner* Spawner = *It;
        if (!IsValid(Spawner))
        {
            continue;
        }

        const FString Key = Spawner->GetPathName();

        // Respawn frequency: divide the vanilla respawn time (in in-game
        // days, integer) by the multiplier. 2x multiplier = respawns roughly
        // twice as fast. Direct member access granted via AccessTransformers.ini.
        const int32 VanillaDays = OriginalRespawnDays.Contains(Key)
            ? OriginalRespawnDays[Key]
            : OriginalRespawnDays.Add(Key, Spawner->mRespawnTimeIndays);
        Spawner->mRespawnTimeIndays = FMath::Max(0, FMath::RoundToInt(static_cast<float>(VanillaDays) / SpawnMult));

        // Spawn counts: grow the spawn data array toward
        // round(vanillaCount * multiplier). Extra entries reuse existing
        // spawn locations, so packs get denser. Shrinking only removes
        // entries that are not backed by a live creature.
        if (Config.IncreaseSpawnCounts)
        {
            const int32 CurrentCount = Spawner->mSpawnData.Num();
            const int32 VanillaCount = OriginalSpawnCounts.Contains(Key)
                ? OriginalSpawnCounts[Key]
                : OriginalSpawnCounts.Add(Key, CurrentCount);

            if (VanillaCount > 0)
            {
                const int32 TargetCount = FMath::Max(1, FMath::RoundToInt(VanillaCount * SpawnMult));

                while (Spawner->mSpawnData.Num() < TargetCount)
                {
                    FSpawnData Copy = Spawner->mSpawnData[Spawner->mSpawnData.Num() % VanillaCount];
                    Copy.Creature = nullptr;
                    Copy.WasKilled = false;
                    Copy.NumTimesKilled = 0;
                    Copy.WaitingForSpawnLocation = false;
                    Spawner->mSpawnData.Add(Copy);
                }

                while (Spawner->mSpawnData.Num() > FMath::Max(TargetCount, VanillaCount))
                {
                    const int32 LastIdx = Spawner->mSpawnData.Num() - 1;
                    if (Spawner->mSpawnData[LastIdx].Creature != nullptr)
                    {
                        break;
                    }
                    Spawner->mSpawnData.RemoveAt(LastIdx);
                }
            }
        }
    }
}
