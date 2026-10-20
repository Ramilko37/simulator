using UnrealBuildTool;

public class DroneTrainer : ModuleRules
{
    public DroneTrainer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore", "Chaos", "PhysicsCore", "Landscape", "RenderCore", "RHI" });
    }
}
