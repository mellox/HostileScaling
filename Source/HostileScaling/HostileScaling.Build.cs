using UnrealBuildTool;

public class HostileScaling : ModuleRules
{
    public HostileScaling(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "FactoryGame",
            "SML"
        });
    }
}
