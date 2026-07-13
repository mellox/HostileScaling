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

    // Respawn keeper: a spawner counts as starved after this many CONSECUTIVE
    // sweeps (10s each) with a slot waiting for a spawn location. Each starved
    // interval widens the spawn radius one step, so the engine's own location
    // projector gets more ground to try; we never hand-place creatures.
    constexpr int32 StarvationSweepThreshold = 3;
    constexpr float SpawnRadiusWidenStep = 1.5f;
    constexpr float SpawnRadiusMaxScale = 3.0f;
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

    // ---- Respawn keeper sweep diagnostics. -------------------------------
    if (bRespawnKeeperActive)
    {
        HostileScaling::GWidenedSpawnRadiusCount = WidenedSpawnerPaths.Num();

        if (!bLoggedNeverRespawnCount && SpawnersSeen > 0)
        {
            UE_LOG(LogHostileScaling, Display,
                TEXT("[Respawn] Spawner census: %d of %d loaded spawners are vanilla never-respawn (-1); the keeper leaves those untouched"), NeverRespawnSeen, SpawnersSeen);
            bLoggedNeverRespawnCount = true;
        }

        // Verbose-gated: near a base we re-pin every ~30s, so this would spam normal-play logs.
        if (HostileScaling::GVerboseRespawnLogging && SlotsReArmed + SpawnersPinned + RadiiWidened > 0)
        {
            UE_LOG(LogHostileScaling, Display,
                TEXT("[Respawn] Sweep totals: %d slot(s) re-armed, %d spawner(s) near-base-pinned, %d spawn radius(es) widened (day %d, %d spawners)"), SlotsReArmed, SpawnersPinned, RadiiWidened, PassedDays, SpawnersSeen);
        }
    }
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
