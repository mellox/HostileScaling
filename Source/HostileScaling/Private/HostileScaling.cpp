#include "HostileScaling.h"
#include "HostileScalingGlobals.h"
#include "HostileScalingSubsystem.h"

#include "Patching/NativeHookManager.h"
#include "FGHealthComponent.h"
#include "FGCharacterPlayer.h"
#include "Creature/FGCreature.h"

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
}

void FHostileScalingModule::StartupModule()
{
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
