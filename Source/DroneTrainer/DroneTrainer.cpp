#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreMisc.h"

class FDroneTrainerModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        // -game still uses the Editor target; Python initializes later in OnPostEngineInit.
        if (!GIsEditor && !IsRunningCommandlet())
        {
            if (IConsoleVariable* Python = IConsoleManager::Get().FindConsoleVariable(TEXT("Engine.Python.IsEnabledByDefault")))
            {
                Python->Set(0, ECVF_SetByCode);
                UE_LOG(LogTemp, Display, TEXT("DroneTrainer: editor Python startup disabled in game mode"));
            }
        }
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FDroneTrainerModule, DroneTrainer, "DroneTrainer");
