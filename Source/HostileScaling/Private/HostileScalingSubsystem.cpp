#include "HostileScalingSubsystem.h"
#include "HostileScaling.h"
#include "HostileScalingGlobals.h"
#include "HostileScalingConfig.h"

#include "EngineUtils.h"
#include "TimerManager.h"
#include "FGHealthComponent.h"
#include "FGSchematicManager.h"
#include "FGSchematic.h"
#include "FGTimeSubsystem.h"
#include "Creature/FGCreature.h"
#include "Creature/FGCreatureSpawner.h"

namespace
{
    constexpr float RefreshIntervalSeconds = 10.0f;

    // True when this spawner's design-time creature class is HOSTILE
    // (non-passive). Reads the class-default object, mirroring the per-instance
    // IsHostileCreature() test the health/damage hooks use so the spawn-scaled
    // set matches. Taming is runtime-only (spawners spawn wild) — not tested.
    bool SpawnerIsHostile(const AFGCreatureSpawner* Spawner)
    {
        if (!IsValid(Spawner))
        {
            return false;
        }
        const TSubclassOf<AFGCreature> CreatureClass = Spawner->GetCreatureClass();
        if (!CreatureClass)
        {
            return false;
        }
        const AFGCreature* CDO = CreatureClass->GetDefaultObject<AFGCreature>();
        return CDO && !CDO->IsPassiveCreature();
    }
}

AHostileScalingSubsystem::AHostileScalingSubsystem()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AHostileScalingSubsystem::BeginPlay()
{
    Super::BeginPlay();

    // Fresh world: force the respawn-keeper globals inert until the first
    // sweep publishes real config state. Globals outlive worlds, so without
    // this a config change made in the main menu could leave the
    // TraceForNearbyBase hook acting on the PREVIOUS session's values for
    // the first couple of seconds.
    HostileScaling::GKeepCreaturesRespawning = false;
    HostileScaling::GVerboseRespawnLogging = false;
    HostileScaling::GWidenedSpawnRadiusCount = 0;

    // Transient per-session state, reset as ONE set. Whether this actor is
    // re-created per world is not asserted here: clearing every transient
    // member together is correct either way, and a half-applied reset (two of
    // seven) would leave the rest carrying the previous world's values if it
    // is not. SaveGame members are deliberately NOT touched.
    RevertedPassiveSpawnerPaths.Empty();
    SpawnScaleLoggedPaths.Empty();
    StarvationStreaks.Empty();
    WidenedSpawnerPaths.Empty();
    CappedStarvedLoggedPaths.Empty();
    bRespawnKeeperWasActive = false;
    bLoggedNeverRespawnCount = false;
    CensusHighWaterHostile = 0;
    CensusHighWaterPassive = 0;
    CensusHighWaterNullClass = 0;
    LastLoggedTier = -1;
    LastLoggedRespawnState = -1;

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

bool AHostileScalingSubsystem::PinNearBaseFalse(AFGCreatureSpawner* Spawner)
{
    // Callers gate on config/globals; authority re-checked so no call path
    // (sweep or hook) can ever write on a client.
    if (!IsValid(Spawner) || !Spawner->HasAuthority())
    {
        return false;
    }

    // Near-base bypass is HOSTILE-only. The TraceForNearbyBase post-hook
    // (HostileScaling.cpp) calls this for every spawner it sees, with no
    // per-spawner hostility filter, so enforce it HERE — the single choke point
    // that writes mCachedIsNearBase — or the hook would re-pin passive spawners
    // (including ones the migration revert just re-traced) whenever the keeper is
    // on. The sweep's own path already only reaches hostile spawners, so this is
    // a no-op there.
    if (!SpawnerIsHostile(Spawner))
    {
        return false;
    }

    // Direct member access granted via AccessTransformers.ini (Friend).
    // mCachedIsNearBase is SaveGame, but a persisted false is a vanilla-legal
    // value: on toggle-off (or mod removal) the game's own TraceForNearbyBase
    // recomputes it honestly.
    if (!Spawner->mCachedIsNearBase)
    {
        return false;
    }

    Spawner->mCachedIsNearBase = false;
    if (HostileScaling::GVerboseRespawnLogging)
    {
        UE_LOG(LogHostileScaling, Display, TEXT("[Respawn] Near-base pin: %s mCachedIsNearBase true -> false"),
            *GetNameSafe(Spawner));
    }
    return true;
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

    // ---- Respawn keeper: publish hook-facing state FIRST. ---------------
    // The hooks read these globals, so they must be correct even on the
    // master-disabled early-return path below. Keeper active = master switch
    // AND its own toggle; verbose also goes quiet when the mod is disabled.
    // GWidenedSpawnRadiusCount is published only where it changes (widen
    // action, sweep end, restore, BeginPlay reset) — never pre-sweep, so the
    // creature hook cannot read a mid-sweep stale value.
    const bool bRespawnKeeperActive = Config.Enabled && Config.KeepCreaturesRespawning;
    HostileScaling::GKeepCreaturesRespawning = bRespawnKeeperActive;
    HostileScaling::GVerboseRespawnLogging = Config.Enabled && Config.VerboseRespawnLogging;

    const int32 RespawnStateNow = bRespawnKeeperActive ? 1 : 0;
    if (RespawnStateNow != LastLoggedRespawnState)
    {
        UE_LOG(LogHostileScaling, Display,
            TEXT("[Respawn] Keep Creatures Respawning: %s (master Enabled=%s, verbose logging=%s)"),
            bRespawnKeeperActive ? TEXT("ON") : TEXT("OFF"), Config.Enabled ? TEXT("true") : TEXT("false"),
            Config.VerboseRespawnLogging ? TEXT("true") : TEXT("false"));
        LastLoggedRespawnState = RespawnStateNow;
    }

    // Off-transition (feature toggle OR master switch): hand near-base back to
    // vanilla and undo radius widening — runs before the master early-return.
    if (!bRespawnKeeperActive && bRespawnKeeperWasActive)
    {
        RestoreVanillaRespawnBehavior();
    }
    bRespawnKeeperWasActive = bRespawnKeeperActive;

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

    // ---- Respawn keeper sweep state. -------------------------------------
    // PassedDays feeds the cadence check; -1 (subsystem unavailable) skips
    // slot re-arm for this sweep rather than re-arming on bad data.
    const AFGTimeOfDaySubsystem* TimeSubsystem = bRespawnKeeperActive ? AFGTimeOfDaySubsystem::Get(GetWorld()) : nullptr;
    const int32 PassedDays = TimeSubsystem ? TimeSubsystem->GetPassedDays() : -1;
    if (bRespawnKeeperActive && PassedDays < 0 && HostileScaling::GVerboseRespawnLogging)
    {
        UE_LOG(LogHostileScaling, Display, TEXT("[Respawn] Time subsystem unavailable; slot re-arm skipped this sweep"));
    }
    int32 SpawnersSeen = 0, NeverRespawnSeen = 0, SlotsReArmed = 0;
    int32 SpawnersPinned = 0, RadiiWidened = 0;

    // Hostile-only gate + migration-revert census counters (this sweep).
    int32 HostileSpawners = 0, PassiveSpawners = 0, NullClassSpawners = 0;
    int32 PassiveSlotsTrimmed = 0, PassiveSpawnersTrimmed = 0, PassiveReTraced = 0;

    // Hostile shrink census (this sweep), symmetric to the passive one. Without
    // it, convergence back toward the recorded vanilla count after the spawn
    // multiplier is lowered is invisible in the log. Note the shrink loop it
    // counts lives inside the IncreaseSpawnCounts block, so these stay 0 while
    // that toggle is off — that is the code path, not an absence of residue.
    int32 HostileSlotsTrimmed = 0, HostileSpawnersTrimmed = 0;

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

        // Hostile-only gate: spawn scaling (respawn frequency + counts) and the
        // respawn keeper apply ONLY to spawners whose creature class is hostile,
        // matching the per-instance IsHostileCreature() filter the health/damage
        // hooks use. Passive/neutral (and null-class) spawners are left vanilla.
        const bool bHostile = SpawnerIsHostile(Spawner);
        const bool bNullClass = !Spawner->GetCreatureClass();
        if (HostileScaling::GVerboseRespawnLogging && !SpawnScaleLoggedPaths.Contains(Key))
        {
            UE_LOG(LogHostileScaling, Display, TEXT("[SpawnScale] Spawner=%s class=%s hostile=%d"),
                *GetNameSafe(Spawner), *GetNameSafe(Spawner->GetCreatureClass().Get()),
                bHostile ? 1 : 0);
            SpawnScaleLoggedPaths.Add(Key);
        }
        if (!bHostile)
        {
            // Passive/neutral or null class: no scaling, no keeper. The near-base
            // pin is blocked for these spawners at the PinNearBaseFalse choke
            // point; the v1.1.0 residue already in the save (grown spawn counts +
            // a pinned near-base flag, both SaveGame) is walked back by
            // RevertPassiveSpawnerResidue (HostileScalingPassiveRevert.cpp).
            if (bNullClass)
            {
                ++NullClassSpawners;
            }
            else
            {
                ++PassiveSpawners;
            }
            RevertPassiveSpawnerResidue(Spawner, Key, PassiveSlotsTrimmed, PassiveSpawnersTrimmed, PassiveReTraced);
            continue;
        }
        ++HostileSpawners;

        // Respawn frequency: divide the vanilla respawn time (in in-game
        // days, integer) by the multiplier. 2x multiplier = respawns roughly
        // twice as fast. Direct member access granted via AccessTransformers.ini.
        // Vanilla -1 is a semantic sentinel (never respawn), not a quantity:
        // it passes through unscaled per the scale-from-vanilla contract.
        // (v1.0.1-v1.0.3 clamped it to 0, making never-respawn spawners
        // respawn daily — restoring true never-respawn is a release-notes item.)
        const int32 VanillaDays = OriginalRespawnDays.Contains(Key)
            ? OriginalRespawnDays[Key]
            : OriginalRespawnDays.Add(Key, Spawner->mRespawnTimeIndays);
        Spawner->mRespawnTimeIndays = VanillaDays < 0
            ? VanillaDays
            : FMath::Max(0, FMath::RoundToInt(static_cast<float>(VanillaDays) / SpawnMult));

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

            if (VanillaCount > 0 && CurrentCount > 0)
            {
                const int32 TargetCount = FMath::Max(1, FMath::RoundToInt(VanillaCount * SpawnMult));

                while (Spawner->mSpawnData.Num() < TargetCount)
                {
                    // Template index must stay inside the LIVE array: the
                    // game can reload/regenerate a spawner with fewer entries
                    // than the stored vanilla count (out-of-bounds crash
                    // otherwise when exploring such spawners).
                    const int32 TemplateRange = FMath::Min(VanillaCount, Spawner->mSpawnData.Num());
                    FSpawnData Copy = Spawner->mSpawnData[Spawner->mSpawnData.Num() % TemplateRange];
                    Copy.Creature = nullptr;
                    Copy.WasKilled = false;
                    Copy.NumTimesKilled = 0;
                    Copy.WaitingForSpawnLocation = false;
                    Spawner->mSpawnData.Add(Copy);
                }

                int32 SlotsTrimmedHere = 0;
                while (Spawner->mSpawnData.Num() > FMath::Max(TargetCount, VanillaCount))
                {
                    // Same in-flight guard as the passive trim: a slot waiting on
                    // a projected spawn location has no Creature yet, so the
                    // live-creature test alone would let us remove it.
                    const int32 LastIdx = Spawner->mSpawnData.Num() - 1;
                    if (Spawner->mSpawnData[LastIdx].Creature != nullptr
                        || Spawner->mSpawnData[LastIdx].WaitingForSpawnLocation)
                    {
                        break;
                    }
                    Spawner->mSpawnData.RemoveAt(LastIdx);
                    ++SlotsTrimmedHere;
                }
                HostileSlotsTrimmed += SlotsTrimmedHere;
                if (SlotsTrimmedHere > 0)
                {
                    ++HostileSpawnersTrimmed;
                    // Mirrors the passive trim line (PassiveRevert.cpp): mSpawnData is
                    // SaveGame, so a wrong hostile trim persists too. Measured
                    // before/after plus the targets actually used; no cause inferred.
                    UE_LOG(LogHostileScaling, Display,
                        TEXT("[SpawnScale] Hostile trim on %s: removed %d slot(s), %d -> %d, target %d, recorded original %d"),
                        *GetNameSafe(Spawner), SlotsTrimmedHere,
                        Spawner->mSpawnData.Num() + SlotsTrimmedHere, Spawner->mSpawnData.Num(),
                        TargetCount, VanillaCount);
                }
            }
        }

        // ---- Respawn keeper. ---------------------------------------------
        // Vanilla never-respawn spawners (mRespawnTimeIndays == -1 in map
        // data — tested via the stored VANILLA value, which the scaling
        // above now also preserves on the live field) are left entirely
        // untouched: no pin, no re-arm, no widening. Counted so the one-time
        // census log below reports how many exist.
        if (bRespawnKeeperActive)
        {
            ++SpawnersSeen;
            if (VanillaDays < 0)
            {
                ++NeverRespawnSeen;
            }
            else
            {
                ApplyRespawnKeeperToSpawner(Spawner, Key, PassedDays, SlotsReArmed, SpawnersPinned, RadiiWidened);
            }
        }
    }

    // ---- Hostile-only gate + migration-revert diagnostics. ---------------
    // Ungated by verbose: with verbose off there would otherwise be no evidence
    // in the log that the gate is live, or that anything was trimmed.
    // Classification counts describe the spawners this sweep iterated.
    // Classification census: re-log whenever a bucket exceeds its previous
    // high-water mark, so the line ends up describing the LARGEST spawner set
    // actually iterated rather than whatever had streamed in 2 s after load.
    // Each re-log requires a strictly larger count, so this converges to silence.
    if (HostileSpawners > CensusHighWaterHostile
        || PassiveSpawners > CensusHighWaterPassive
        || NullClassSpawners > CensusHighWaterNullClass)
    {
        CensusHighWaterHostile   = FMath::Max(CensusHighWaterHostile,   HostileSpawners);
        CensusHighWaterPassive   = FMath::Max(CensusHighWaterPassive,   PassiveSpawners);
        CensusHighWaterNullClass = FMath::Max(CensusHighWaterNullClass, NullClassSpawners);
        UE_LOG(LogHostileScaling, Display,
            TEXT("[SpawnScale] Classification census (loaded spawners this sweep): hostile %d, passive %d, nullClass %d"),
            HostileSpawners, PassiveSpawners, NullClassSpawners);
    }

    // Migration revert: measured outcome only. Prints only on sweeps that
    // changed something, so a settled world stays silent.
    if (PassiveSlotsTrimmed > 0 || PassiveReTraced > 0)
    {
        UE_LOG(LogHostileScaling, Display,
            TEXT("[SpawnScale] Passive revert this sweep: trimmed %d slot(s) across %d spawner(s), re-traced near-base on %d spawner(s)"),
            PassiveSlotsTrimmed, PassiveSpawnersTrimmed, PassiveReTraced);
    }

    // Hostile shrink: same shape, same gating. Measured counts from this sweep
    // only; nothing is inferred about why the target dropped.
    if (HostileSlotsTrimmed > 0)
    {
        UE_LOG(LogHostileScaling, Display,
            TEXT("[SpawnScale] Hostile trim this sweep: trimmed %d slot(s) across %d spawner(s)"),
            HostileSlotsTrimmed, HostileSpawnersTrimmed);
    }

    // ---- Respawn keeper sweep diagnostics. -------------------------------
    if (bRespawnKeeperActive)
    {
        HostileScaling::GWidenedSpawnRadiusCount = WidenedSpawnerPaths.Num();

        if (!bLoggedNeverRespawnCount && SpawnersSeen > 0)
        {
            UE_LOG(LogHostileScaling, Display,
                TEXT("[Respawn] Spawner census: %d of %d loaded HOSTILE spawners are vanilla never-respawn (-1); the keeper leaves those untouched"), NeverRespawnSeen, SpawnersSeen);
            bLoggedNeverRespawnCount = true;
        }

        // Verbose-gated: near a base we re-pin every ~30s, so this would spam normal-play logs.
        if (HostileScaling::GVerboseRespawnLogging && SlotsReArmed + SpawnersPinned + RadiiWidened > 0)
        {
            UE_LOG(LogHostileScaling, Display,
                TEXT("[Respawn] Sweep totals: %d slot(s) re-armed, %d spawner(s) near-base-pinned, %d spawn radius(es) widened (day %d, %d hostile spawners)"), SlotsReArmed, SpawnersPinned, RadiiWidened, PassedDays, SpawnersSeen);
        }
    }
}

// ApplyRespawnKeeperToSpawner() and RestoreVanillaRespawnBehavior() are defined in
// HostileScalingRespawnKeeper.cpp; RevertPassiveSpawnerResidue() in
// HostileScalingPassiveRevert.cpp (split out to keep this file under 500 lines).
