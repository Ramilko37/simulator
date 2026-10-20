#include "DroneTrainerLevelLayout.h"
#include "Components/SceneComponent.h"
#include "Components/LineBatchComponent.h"
#include "DroneTrainerGate.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"

ADroneTrainerLevelLayout::ADroneTrainerLevelLayout()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Layout"));
    Markings = CreateDefaultSubobject<ULineBatchComponent>(TEXT("LessonMarkings"));
    Markings->SetupAttachment(RootComponent);
    Markings->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Markings->PrimaryComponentTick.bCanEverTick = false;
}

void ADroneTrainerLevelLayout::RefreshMarkings()
{
    // Scene lines use depth testing and render before the HUD, unlike Canvas projections.
    // Rebuild only on lesson transitions/reset; lines persist while paused.
    Markings->Flush();
    TArray<FBatchedLine> Lines;
    const FLinearColor Orange(1,0.65f,0.22f);
    auto GroundLine=[&](FVector A,FVector B,FLinearColor Color=FLinearColor::White)
    { Lines.Emplace(A,B,Color,-1.f,2.f,SDPG_World); };
    if (LandingPad && Start)
    {
        FVector Landing=LandingPad->GetComponentsBoundingBox().GetCenter();
        Landing.Z=LandingPad->GetComponentsBoundingBox().Max.Z+2;
        const FVector Offsets[4]={FVector(-200,-200,0),FVector(200,-200,0),FVector(200,200,0),FVector(-200,200,0)};
        for (int I=0;I<4;++I) GroundLine(Landing+Offsets[I],Landing+Offsets[(I+1)%4]);
        FVector StartPoint=Start->GetActorLocation();
        FHitResult Ground;
        if (GetWorld()->LineTraceSingleByChannel(Ground,StartPoint+FVector(0,0,100),StartPoint-FVector(0,0,20000),ECC_GameTraceChannel1))
        {
            StartPoint.Z=Ground.ImpactPoint.Z+2;
            for (int I=0;I<48;++I) GroundLine(StartPoint+FVector(300*FMath::Cos(I*2*PI/48),300*FMath::Sin(I*2*PI/48),0),StartPoint+FVector(300*FMath::Cos((I+1)*2*PI/48),300*FMath::Sin((I+1)*2*PI/48),0));
            GroundLine(StartPoint-FVector(100,0,0),StartPoint+FVector(100,0,0));
            GroundLine(StartPoint-FVector(0,100,0),StartPoint+FVector(0,100,0));
        }
    }
    for (TActorIterator<ADroneTrainerGate> It(GetWorld()); It; ++It)
    {
        if (It->GetLessonState()!=1) continue;
        const FVector C=It->GetActorLocation()+FVector(0,0,3), N=It->GetActorForwardVector(), R=It->GetActorRightVector();
        GroundLine(C-N*250,C+N*250); GroundLine(C+N*250,C+N*150+R*70); GroundLine(C+N*250,C+N*150-R*70);
        const FVector Center=It->GetOpeningCenter(), Right=R*It->OpeningHalfSize.X, Up=It->GetActorUpVector()*It->OpeningHalfSize.Y;
        const FVector Corners[4]={Center-Right-Up,Center+Right-Up,Center+Right+Up,Center-Right+Up};
        for (int I=0; I<4; ++I) GroundLine(Corners[I],Corners[(I+1)%4],Orange);
    }
    Markings->DrawLines(Lines);
}
