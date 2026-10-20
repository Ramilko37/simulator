#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroneTrainerGate.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class DRONETRAINER_API ADroneTrainerGate : public AActor
{
    GENERATED_BODY()
public:
    ADroneTrainerGate();
    void SetLessonState(int32 State);
    int32 GetLessonState() const { return LessonState; }
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> StateMaterial;
    int32 LessonState = 0;
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Route", meta=(ClampMin="1", ClampMax="5"))
    int32 GateIndex = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Route")
    TObjectPtr<UStaticMeshComponent> Frame;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Route")
    FVector OpeningCenterLocal = FVector(0, 0, 300);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Route")
    FVector2D OpeningHalfSize = FVector2D(200, 200);
    UFUNCTION(BlueprintPure, Category="Route")
    FVector GetOpeningCenter() const { return GetActorTransform().TransformPosition(OpeningCenterLocal); }
    UFUNCTION(BlueprintPure, Category="Route")
    FName GetGateId() const { return FName(*FString::Printf(TEXT("G%d"), GateIndex)); }
};
