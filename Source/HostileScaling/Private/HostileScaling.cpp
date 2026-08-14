#include "HostileScaling.h"
#include "HostileScalingGlobals.h"
#include "HostileScalingSubsystem.h"

#include "EngineUtils.h"
#include "Patching/NativeHookManager.h"
#include "FGHealthComponent.h"
#include "FGCharacterPlayer.h"
#include "FGWaterVolume.h"
#include "Creature/FGCreature.h"
#include "Creature/FGCreatureSpawner.h"

DEFINE_LOG_CATEGORY(LogHostileScaling);

namespace
{
    // Hostile = any creature that is neither passive (doggos, ambient fauna)
    // nor tamed. Checked per-instance instead of by class so it works for
    // every creature blueprint regardless of its C++ ancestry.
    bool IsHostileCreature(AActor* Actor)
    {
        AFGCreature* Creature = Cast<AFGCreature>(Actor);
        return Creature && !Creature->IsPassiveCreature() && !Creature->IsTamed();
    }

    // Multiplies damage dealt BY hostile creatures. Health scaling is done by
    // writing real health values on the creature (see HostileScalingSubsystem)
    // so health-display mods and scanners show the true scaled numbers.
    float AdjustCreatureDamage(AActor* DamagedActor, AController* InstigatedBy, AActor* DamageCauser, float Damage)
    {
        const float DamageMult = HostileScaling::GCreatureDamageMultiplier;
        if (FMath::IsNearlyEqual(DamageMult, 1.0f))
        {
            return Damage;
        }

        APawn* InstigatorPawn = InstigatedBy ? InstigatedBy->GetPawn() : nullptr;
        const bool bFromHostile = IsHostileCreature(DamageCauser) || IsHostileCreature(InstigatorPawn);
        const bool bTargetApplies = DamagedActor &&
            (DamagedActor->IsA<AFGCharacterPlayer>() || HostileScaling::GScaleDamageToBuildings);

        return (bFromHostile && bTargetApplies) ? Damage * DamageMult : Damage;
    }

    // Respawn-keeper diagnostic (v1 measures, builds no correction machinery):
    // logs a fresh creature's location and whether any streamed AFGWaterVolume
    // encompasses it. Needs no friend access, so it lives here with its only
    // caller. Streamed-out volumes cannot answer, which is acceptable — spawns
    // happen near players, where the local volumes are loaded. GetSpawner()
    // may be None for a fresh spawn (the spawner assigns itself after the
    // spawn call); the location + verdict are the data that matter.
    void LogCreatureSpawnWaterDiagnostic(AFGCreature* Creature)
    {
        UWorld* World = Creature ? Creature->GetWorld() : nullptr;
        if (!World)
        {
            return;
        }

        const FVector Location = Creature->GetActorLocation();

        // Deep-ocean guard (NodeShuffle "Layer 2"): the open ocean has NO
        // streamed AFGWaterVolume at all, so volume containment alone
        // false-negatives the worst case. -4500 was measured for
        // NodeShuffle's node dataset (legit land bottoms out ~-3500, seabed
        // starts ~-4900); may need recalibration for creature terrain.
        const bool bDeepOcean = Location.Z < -4500.0f;
        bool bInWater = bDeepOcean;
        if (!bInWater)
        {
            for (TActorIterator<AFGWaterVolume> It(World); It; ++It)
            {
                AFGWaterVolume* Volume = *It;
                if (IsValid(Volume) && Volume->EncompassesPoint(Location))
                {
                    bInWater = true;
                    break;
                }
            }
        }

        UE_LOG(LogHostileScaling, Display,
            TEXT("[Respawn] Spawn water-check: %s at (%.0f, %.0f, %.0f) inWater=%d deepOcean=%d spawner=%s"),
            *GetNameSafe(Creature), Location.X, Location.Y, Location.Z,
            bInWater ? 1 : 0, bDeepOcean ? 1 : 0, *GetNameSafe(Creature->GetSpawner()));
    }
}

void FHostileScalingModule::StartupModule()
{
    UE_LOG(LogHostileScaling, Display, TEXT("===== HostileScaling BUILD v1.1.1-rc3 2026-08-13 LOADED ====="));

#if !WITH_EDITOR
    // Scale creature health the moment a creature spawns (or streams in from
    // a save). The subsystem also re-applies periodically to follow tier-ups;
    // both paths are idempotent because the target is computed from the
    // creature class's vanilla default health.
    SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGCreature, BeginPlay, [](AFGCreature* Self)
    {
        if (Self && Self->HasAuthority())
        {
            AHostileScalingSubsystem::ApplyCreatureHealthScale(Self);

            // Respawn-keeper diagnostic: while any spawn radius is widened
            // (and verbose logging is on), record whether new creatures land
            // in water — measures the widened-radius risk.
            if (HostileScaling::GVerboseRespawnLogging && HostileScaling::GWidenedSpawnRadiusCount > 0)
            {
                LogCreatureSpawnWaterDiagnostic(Self);
            }
        }
    });

    // Keep-creatures-respawning companion hook: the engine periodically
    // recomputes each spawner's cached near-base flag; re-pin it false right
    // after every recompute while the feature is on. TraceForNearbyBase is
    // public, NON-virtual, non-inline and not a UFUNCTION, so the plain
    // SUBSCRIBE variant is correct (the UOBJECT/virtual variant is only for
    // virtuals). The protected write lives in the subsystem (friend access);
    // this lambda only decides. Fire logging is verbose-gated and answers
    // the re-trace-cadence question from the first instrumented build.
    SUBSCRIBE_METHOD_AFTER(AFGCreatureSpawner::TraceForNearbyBase, [](AFGCreatureSpawner* Self)
    {
        if (!IsValid(Self) || !Self->HasAuthority())
        {
            return;
        }
        // Read the engine's freshly-traced value BEFORE our pin can overwrite
        // it, then pin, then log: nearBaseAfterTrace is what the engine
        // computed and pinnedByUs is the measured outcome of our own call
        // (PinNearBaseFalse vetoes passive spawners, so the keeper global alone
        // would assert a pin that never happened). PinNearBaseFalse re-checks
        // validity and authority internally, so hoisting it is safe.
        const bool bNearBaseFromTrace = Self->IsNearBase();
        const bool bKeeperOn = HostileScaling::GKeepCreaturesRespawning;
        const bool bPinned = bKeeperOn && AHostileScalingSubsystem::PinNearBaseFalse(Self);
        if (HostileScaling::GVerboseRespawnLogging)
        {
            UE_LOG(LogHostileScaling, Display,
                TEXT("[Respawn] TraceForNearbyBase fired: t=%.1fs spawner=%s nearBaseAfterTrace=%d keeperOn=%d pinnedByUs=%d"),
                Self->GetWorld() ? Self->GetWorld()->GetTimeSeconds() : -1.0f,
                *GetNameSafe(Self), bNearBaseFromTrace ? 1 : 0,
                bKeeperOn ? 1 : 0, bPinned ? 1 : 0);
        }
    });

    // Damage reaches health components through three separate entry points
    // depending on how it was applied (generic, point/hit-based, radial).
    // Hook all of them. These functions are virtual, so the UOBJECT macro
    // variant is required — it resolves the implementation through the CDO.

    SUBSCRIBE_UOBJECT_METHOD(UFGHealthComponent, TakeDamage,
        [](auto& Scope, UFGHealthComponent* Self, AActor* damagedActor, float damageAmount,
           const UDamageType* damageType, AController* instigatedBy, AActor* damageCauser)
    {
        const float NewDamage = AdjustCreatureDamage(damagedActor, instigatedBy, damageCauser, damageAmount);
        if (NewDamage != damageAmount)
        {
            Scope(Self, damagedActor, NewDamage, damageType, instigatedBy, damageCauser);
        }
    });

    SUBSCRIBE_UOBJECT_METHOD(UFGHealthComponent, TakePointDamage,
        [](auto& Scope, UFGHealthComponent* Self, AActor* damagedActor, float damage,
           AController* instigatedBy, FVector hitLocation, UPrimitiveComponent* hitComponent,
           FName boneName, FVector shotFromDirection, const UDamageType* damageType, AActor* damageCauser)
    {
        const float NewDamage = AdjustCreatureDamage(damagedActor, instigatedBy, damageCauser, damage);
        if (NewDamage != damage)
        {
            Scope(Self, damagedActor, NewDamage, instigatedBy, hitLocation, hitComponent,
                boneName, shotFromDirection, damageType, damageCauser);
        }
    });

    SUBSCRIBE_UOBJECT_METHOD(UFGHealthComponent, TakeRadialDamage,
        [](auto& Scope, UFGHealthComponent* Self, AActor* damagedActor, float damage,
           const UDamageType* damageType, FVector hitLocation, const FHitResult& hitInfo,
           AController* instigatedBy, AActor* damageCauser)
    {
        const float NewDamage = AdjustCreatureDamage(damagedActor, instigatedBy, damageCauser, damage);
        if (NewDamage != damage)
        {
            Scope(Self, damagedActor, NewDamage, damageType, hitLocation, hitInfo, instigatedBy, damageCauser);
        }
    });
#endif
}

IMPLEMENT_GAME_MODULE(FHostileScalingModule, HostileScaling);
