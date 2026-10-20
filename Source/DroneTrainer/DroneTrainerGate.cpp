#include "DroneTrainerGate.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

ADroneTrainerGate::ADroneTrainerGate()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("GroundPivot"));
    RootComponent->SetMobility(EComponentMobility::Static);
    Frame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Frame"));
    Frame->SetupAttachment(RootComponent);
    Frame->SetMobility(EComponentMobility::Static);
    // The accepted FBX is centered and normalized to 200 cm; retain its eight collision boxes.
    Frame->SetRelativeTransform(FTransform(FRotator(0, 90, 0), FVector(0, 0, 260), FVector(2.6)));
    Frame->SetCollisionProfileName(TEXT("BlockAll"));
    Frame->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/DroneTrainer/Meshes/SM_TrainingGate"));
    if (Mesh.Succeeded()) Frame->SetStaticMesh(Mesh.Object);
}

void ADroneTrainerGate::SetLessonState(int32 State)
{
    LessonState=State;
    if (!StateMaterial) StateMaterial=Frame->CreateDynamicMaterialInstance(1);
    if (StateMaterial) StateMaterial->SetVectorParameterValue(TEXT("Color"),State==1 ? FLinearColor(1,0.28,0.03) : State==2 ? FLinearColor(0.18,0.22,0.2) : FLinearColor(0.65,0.68,0.7));
}
