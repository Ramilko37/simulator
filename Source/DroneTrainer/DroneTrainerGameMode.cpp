#include "DroneTrainerGameMode.h"

#include "DroneTrainerDronePawn.h"
#include "DroneTrainerHUD.h"

ADroneTrainerGameMode::ADroneTrainerGameMode()
{
    DefaultPawnClass = ADroneTrainerDronePawn::StaticClass();
    HUDClass = ADroneTrainerHUD::StaticClass();
}
