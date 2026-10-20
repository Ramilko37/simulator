using UnrealBuildTool;
public class DroneTrainerEditor : ModuleRules
{
    public DroneTrainerEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "Landscape", "Foliage", "AssetRegistry", "MeshDescription", "StaticMeshDescription" });
    }
}
