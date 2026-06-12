#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "Configuration/ConfigManager.h"
#include "HostileScalingConfig.generated.h"

// Plain struct mirror of the configuration. Field names MUST match the
// section property keys registered in UHostileScalingConfig exactly —
// UConfigManager::FillConfigurationStruct maps them by name.
USTRUCT(BlueprintType)
struct HOSTILESCALING_API FHostileScalingConfigStruct
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    bool Enabled{true};

    // Final multiplier per stat = Base + PerTier * HighestUnlockedMilestoneTier,
    // clamped to [0.1, Max].
    UPROPERTY(BlueprintReadWrite)
    float HealthBase{1.0f};
    UPROPERTY(BlueprintReadWrite)
    float HealthPerTier{0.15f};
    UPROPERTY(BlueprintReadWrite)
    float HealthMax{5.0f};

    UPROPERTY(BlueprintReadWrite)
    float DamageBase{1.0f};
    UPROPERTY(BlueprintReadWrite)
    float DamagePerTier{0.15f};
    UPROPERTY(BlueprintReadWrite)
    float DamageMax{5.0f};

    UPROPERTY(BlueprintReadWrite)
    float SpawnBase{1.0f};
    UPROPERTY(BlueprintReadWrite)
    float SpawnPerTier{0.10f};
    UPROPERTY(BlueprintReadWrite)
    float SpawnMax{4.0f};

    UPROPERTY(BlueprintReadWrite)
    bool ScaleDamageToBuildings{false};
    UPROPERTY(BlueprintReadWrite)
    bool IncreaseSpawnCounts{true};

    // Retrieves the active configuration values for this mod.
    static FHostileScalingConfigStruct GetActiveConfig(UObject* WorldContext);
};

// C++-defined SML mod configuration. Registered by the root game instance
// module; SML generates the in-game settings UI and persists values to
// FactoryGame/Configs/HostileScaling.cfg.
UCLASS()
class HOSTILESCALING_API UHostileScalingConfig : public UModConfiguration
{
    GENERATED_BODY()

public:
    UHostileScalingConfig();

    // The property tree is built here rather than in the constructor so each
    // property can be outer'd to the root section. SML's dirty/save chain
    // walks property Outers, so the chain must be property -> section ->
    // (duplicated into) root value holder -> config manager. Properties
    // created as constructor subobjects are outer'd to the config object,
    // which silently breaks saving of user edits.
    virtual void PostInitProperties() override;
};
