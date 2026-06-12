#include "HostileScalingConfig.h"

#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

FHostileScalingConfigStruct FHostileScalingConfigStruct::GetActiveConfig(UObject* WorldContext)
{
    FHostileScalingConfigStruct ConfigStruct{};
    const FConfigId ConfigId{"HostileScaling", ""};
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
    {
        UConfigManager* ConfigManager = World->GetGameInstance()->GetSubsystem<UConfigManager>();
        ConfigManager->FillConfigurationStruct(ConfigId, FDynamicStructInfo{FHostileScalingConfigStruct::StaticStruct(), &ConfigStruct});
    }
    return ConfigStruct;
}

UHostileScalingConfig::UHostileScalingConfig()
{
    ConfigId = FConfigId{"HostileScaling", ""};
    DisplayName = FText::FromString(TEXT("Hostile Scaling"));
    Description = FText::FromString(TEXT("Scale hostile creature health, damage and spawn frequency by milestone tier."));
}

void UHostileScalingConfig::PostInitProperties()
{
    Super::PostInitProperties();

    // Only the class default object's tree is used by SML (it acts as the
    // template the config manager duplicates into its live value holder).
    if (RootSection != nullptr || !HasAnyFlags(RF_ClassDefaultObject))
    {
        return;
    }

    // The raw C++ config property classes serialize fine but provide no
    // editor widget, which leaves the in-game Mods menu panel empty. SML's
    // Blueprint subclasses wire up the actual widgets, so instantiate those,
    // falling back to the native classes if they cannot be loaded.
    UClass* SectionClass = LoadClass<UConfigPropertySection>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertySection.BP_ConfigPropertySection_C"));
    if (!SectionClass) { SectionClass = UConfigPropertySection::StaticClass(); }

    UClass* BoolClass = LoadClass<UConfigPropertyBool>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyBool.BP_ConfigPropertyBool_C"));
    if (!BoolClass) { BoolClass = UConfigPropertyBool::StaticClass(); }

    UClass* FloatClass = LoadClass<UConfigPropertyFloat>(nullptr,
        TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyFloat.BP_ConfigPropertyFloat_C"));
    if (!FloatClass) { FloatClass = UConfigPropertyFloat::StaticClass(); }

    UConfigPropertySection* Root = NewObject<UConfigPropertySection>(this, SectionClass, TEXT("RootSection"));
    if (UCP_Section* RootWidget = Cast<UCP_Section>(Root))
    {
        RootWidget->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
        RootWidget->HasHeader = false;
    }
    // SML's BP property classes default bRequiresWorldReload to true, which
    // greys the whole panel out in the pause menu. This mod applies config
    // changes live (the subsystem re-reads every 10s), so allow in-game edits.
    Root->bRequiresWorldReload = false;

    // Properties are outer'd to the section (NOT to this config object) so
    // that MarkDirty's Outer-chain walk reaches the save handler.
    const auto AddBool = [&](const TCHAR* Key, bool Default, const TCHAR* Display, const TCHAR* Tip)
    {
        UConfigPropertyBool* Prop = NewObject<UConfigPropertyBool>(Root, BoolClass, FName(Key));
        Prop->Value = Default;
        // The in-game Reset button does Value = DefaultValue, so DefaultValue
        // must be set explicitly or reset snaps to zero/minimum instead.
        Prop->DefaultValue = Default;
        Prop->DisplayName = FText::FromString(Display);
        Prop->Tooltip = FText::FromString(Tip);
        Prop->bRequiresWorldReload = false;
        Root->SectionProperties.Add(Key, Prop);
    };

    const auto AddFloat = [&](const TCHAR* Key, float Default, float Min, float Max, const TCHAR* Display, const TCHAR* Tip)
    {
        UConfigPropertyFloat* Prop = NewObject<UConfigPropertyFloat>(Root, FloatClass, FName(Key));
        Prop->Value = Default;
        // The in-game Reset button does Value = DefaultValue, so DefaultValue
        // must be set explicitly or reset snaps to zero/minimum instead.
        Prop->DefaultValue = Default;
        Prop->DisplayName = FText::FromString(Display);
        Prop->Tooltip = FText::FromString(Tip);
        Prop->bRequiresWorldReload = false;
        if (UCP_Float* FloatWidget = Cast<UCP_Float>(Prop))
        {
            FloatWidget->WidgetType = ECP_FloatWidgetType::CPF_Spinbox;
            FloatWidget->MinValue = Min;
            FloatWidget->MaxValue = Max;
        }
        Root->SectionProperties.Add(Key, Prop);
    };

    AddBool(TEXT("Enabled"), true,
        TEXT("Enabled"),
        TEXT("Master switch. When off, no scaling is applied."));

    AddFloat(TEXT("HealthBase"), 1.0f, 0.1f, 10.0f,
        TEXT("Health: Base Multiplier"),
        TEXT("Creature max health multiplier at tier 0. 1.0 = vanilla."));
    AddFloat(TEXT("HealthPerTier"), 0.15f, 0.0f, 2.0f,
        TEXT("Health: Added Per Tier"),
        TEXT("Added to the health multiplier for each unlocked milestone tier. 0.15 means +15% per tier."));
    AddFloat(TEXT("HealthMax"), 5.0f, 0.1f, 20.0f,
        TEXT("Health: Maximum Multiplier"),
        TEXT("The health multiplier will never exceed this cap."));

    AddFloat(TEXT("DamageBase"), 1.0f, 0.1f, 10.0f,
        TEXT("Damage: Base Multiplier"),
        TEXT("Creature damage multiplier at tier 0. 1.0 = vanilla."));
    AddFloat(TEXT("DamagePerTier"), 0.15f, 0.0f, 2.0f,
        TEXT("Damage: Added Per Tier"),
        TEXT("Added to the damage multiplier for each unlocked milestone tier."));
    AddFloat(TEXT("DamageMax"), 5.0f, 0.1f, 20.0f,
        TEXT("Damage: Maximum Multiplier"),
        TEXT("The damage multiplier will never exceed this cap."));

    AddFloat(TEXT("SpawnBase"), 1.0f, 0.1f, 10.0f,
        TEXT("Spawns: Base Multiplier"),
        TEXT("Spawn frequency multiplier at tier 0. Higher = faster respawns and more creatures."));
    AddFloat(TEXT("SpawnPerTier"), 0.10f, 0.0f, 2.0f,
        TEXT("Spawns: Added Per Tier"),
        TEXT("Added to the spawn frequency multiplier for each unlocked milestone tier."));
    AddFloat(TEXT("SpawnMax"), 4.0f, 0.1f, 20.0f,
        TEXT("Spawns: Maximum Multiplier"),
        TEXT("The spawn frequency multiplier will never exceed this cap."));

    AddBool(TEXT("ScaleDamageToBuildings"), false,
        TEXT("Scale Damage To Buildings"),
        TEXT("When on, scaled creature damage also applies to buildings and vehicles, not just players."));
    AddBool(TEXT("IncreaseSpawnCounts"), true,
        TEXT("Increase Spawn Counts"),
        TEXT("When on, spawners also gain extra creatures (not just faster respawns) as the multiplier grows."));

    RootSection = Root;
}
