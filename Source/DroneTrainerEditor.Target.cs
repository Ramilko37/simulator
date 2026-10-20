using UnrealBuildTool;
using System.Collections.Generic;

public class DroneTrainerEditorTarget : TargetRules
{
    public DroneTrainerEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("DroneTrainer");
        ExtraModuleNames.Add("DroneTrainerEditor");
    }
}
