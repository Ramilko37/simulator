#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroneTrainerLevelLayout.generated.h"

class ULineBatchComponent;
class APlayerStart;
class AStaticMeshActor;
class ADroneTrainerGate;

UCLASS()
class DRONETRAINER_API ADroneTrainerLevelLayout : public AActor
{
    GENERATED_BODY()
public:
    ADroneTrainerLevelLayout();
    void RefreshMarkings();
    UPROPERTY(VisibleAnywhere, Category="Route") TObjectPtr<ULineBatchComponent> Markings;
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Route") TObjectPtr<APlayerStart> Start;
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Route") TObjectPtr<AStaticMeshActor> LandingPad;
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Route") TArray<TObjectPtr<ADroneTrainerGate>> Gates;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bounds") FVector2D FlightMin = FVector2D(-20000, -20000);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bounds") FVector2D FlightMax = FVector2D(20000, 20000);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bounds") float WarningXY = 18000;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bounds") float WarningAGL = 4000;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bounds") float MaxAGL = 5000;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Route") float CorridorWidth = 1600;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Route") float CorridorHeight = 1000;
};
