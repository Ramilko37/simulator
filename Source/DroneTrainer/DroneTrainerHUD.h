#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DroneTrainerHUD.generated.h"

UCLASS()
class DRONETRAINER_API ADroneTrainerHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
};
