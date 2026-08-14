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

    // Grouping note (v1.1.1): SML's schema DOES support nested sections
    // (UCP_Section carries HasHeader/HeaderText), but a nested section changes
    // a property's serialized PATH — UConfigPropertySection::Serialize writes a
    // child object under the section key, and FillConfigStruct then wants a
    // matching nested USTRUCT member. That would rename every existing config
    // key and reset users' saved settings, which v1.1.1 is not allowed to do.
    // So the grouping here is expressed with DECLARATION ORDER (the panel lists
    // properties in insertion order) plus a group name on the first label of
    // each group. Real sections are a v1.2 item, to land with the key migration.

    AddBool(TEXT("Enabled"), true,
        TEXT("Enabled"),
        TEXT("Master switch. When off, no scaling is applied, and the one-time cleanup of passive packs an earlier version of the mod grew does not run either (that leftover stays until you turn this back on)."));

    AddFloat(TEXT("HealthBase"), 1.0f, 0.1f, 10.0f,
        TEXT("Health: Base Multiplier"),
        TEXT("Hostile creature max-health multiplier at tier 0. 1x = vanilla. Passive wildlife is never scaled."));
    AddFloat(TEXT("HealthPerTier"), 0.15f, 0.0f, 2.0f,
        TEXT("Health: Added Per Tier"),
        TEXT("Added to the health multiplier for each unlocked milestone tier. 0.15 means +15% of vanilla per tier: at tier 9 the multiplier is 1.0 + 0.15 x 9 = 2.35x, capped by Maximum."));
    AddFloat(TEXT("HealthMax"), 5.0f, 0.1f, 20.0f,
        TEXT("Health: Maximum Multiplier"),
        TEXT("The health multiplier will never exceed this cap, at any tier."));

    AddFloat(TEXT("DamageBase"), 1.0f, 0.1f, 10.0f,
        TEXT("Damage: Base Multiplier"),
        TEXT("Multiplier on damage dealt BY hostile creatures, at tier 0. 1x = vanilla."));
    AddFloat(TEXT("DamagePerTier"), 0.15f, 0.0f, 2.0f,
        TEXT("Damage: Added Per Tier"),
        TEXT("Added to the damage multiplier for each unlocked milestone tier: at tier 9 the multiplier is 1.0 + 0.15 x 9 = 2.35x, capped by Maximum."));
    AddFloat(TEXT("DamageMax"), 5.0f, 0.1f, 20.0f,
        TEXT("Damage: Maximum Multiplier"),
        TEXT("The damage multiplier will never exceed this cap, at any tier."));

    AddBool(TEXT("ScaleDamageToBuildings"), false,
        TEXT("Damage Also Applies To Buildings"),
        TEXT("When on, scaled creature damage also applies to buildings and vehicles, not just players."));

    // Hostile Population. The three sliders below drive TWO effects, which is
    // what a live tester misread in v1.1.0 - the labels now say both.
    AddFloat(TEXT("SpawnBase"), 1.0f, 0.1f, 10.0f,
        TEXT("Respawn + Packs: Base Multiplier"),
        TEXT("Respawn Speed + Pack Size, at tier 0: one multiplier that drives BOTH how fast killed hostiles come back AND (while Also Apply Spawn Scaling to Pack Size is on) how many appear per spawner. Hostile spawners only. 1x = vanilla, 2x = half the vanilla wait, 0.5x = twice the vanilla wait; respawn time is kept in whole in-game days, so the result is rounded. Warning: while Also Apply Spawn Scaling to Pack Size is on, high values here produce very large packs."));
    AddFloat(TEXT("SpawnPerTier"), 0.10f, 0.0f, 2.0f,
        TEXT("Respawn + Packs: Added Per Tier"),
        TEXT("Added to that same Respawn Speed + Pack Size multiplier for each unlocked milestone tier. 0.10 means at tier 9 the multiplier is 1.0 + 0.10 x 9 = 1.9x, so hostile respawn waits are just over half of vanilla, capped by Maximum. Warning: while Also Apply Spawn Scaling to Pack Size is on, this grows packs every tier as well - large values produce very large packs."));
    AddFloat(TEXT("SpawnMax"), 4.0f, 0.1f, 20.0f,
        TEXT("Respawn + Packs: Max Multiplier"),
        TEXT("The Respawn Speed + Pack Size multiplier will never exceed this cap, at any tier. Warning: while Also Apply Spawn Scaling to Pack Size is on, this is also the pack-size cap - 4x means up to about four times the vanilla creatures per hostile spawner, which produces very large packs."));

    AddBool(TEXT("IncreaseSpawnCounts"), true,
        TEXT("Also Apply Spawn Scaling to Pack Size"),
        TEXT("With this off, the three Respawn + Packs sliders change respawn speed only, and hostile packs that already grew stay at their current size until you turn this back on with a lower multiplier. When on, hostile spawners also gain extra creatures (not just faster respawns) as the multiplier grows. Passive creatures (doggos, ambient fauna) never gain extra creatures. Separately - and regardless of this setting - passive packs that an earlier version of the mod grew are trimmed back toward their recorded original size; turn the mod's master Enabled switch off to stop that too."));

    AddBool(TEXT("KeepCreaturesRespawning"), true,
        TEXT("Respawn Near Factories"),
        TEXT("Keeps HOSTILE creature spawners working around your buildings: the mod clears the near-base flag vanilla sets on spawners close to a factory, cleared packs re-arm on their normal (already-scaled) respawn schedule, and a spawner left without a valid spawn spot slowly widens its search radius. Spawners the map marks as never-respawn are not altered, and passive spawners keep vanilla behavior. Turn off to restore vanilla behavior everywhere."));

    AddBool(TEXT("VerboseRespawnLogging"), false,
        TEXT("Diagnostics - Verbose Respawn Logging"),
        TEXT("Troubleshooting only. Writes detailed spawner diagnostics (hostile/passive classification, per-spawner re-arms, near-base pins, radius changes, water checks) to the game log. Leave off for clean logs and normal play."));

    RootSection = Root;
}
