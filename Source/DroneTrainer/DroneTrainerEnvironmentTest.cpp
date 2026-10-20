#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "LandscapeProxy.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DroneTrainerLevelLayout.h"
#include "DroneTrainerGate.h"
#include "DroneTrainerDronePawn.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneTrainerEnvironmentTest, "DroneTrainer.Environment",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDroneTrainerEnvironmentTest::RunTest(const FString&)
{
    UWorld* World=nullptr;
    for (const auto& C:GEngine->GetWorldContexts())
        if (C.WorldType==EWorldType::Game || C.WorldType==EWorldType::PIE) World=C.World();
    if (!TestNotNull(TEXT("Game world"),World)) return false;
    USoundWave* Wind=LoadObject<USoundWave>(nullptr,TEXT("/Game/TrainingField/Audio/S03_OutdoorWind.S03_OutdoorWind"));
    if (TestNotNull(TEXT("S03 cooked dependency"),Wind))
    {
        TestTrue(TEXT("S03 looping"),Wind->bLooping);
        TestEqual(TEXT("S03 stereo"),Wind->NumChannels,2);
        TestTrue(TEXT("S03 quiet"),Wind->Volume>0 && Wind->Volume<=.2f);
        TestTrue(TEXT("S03 forty seconds"),FMath::IsNearlyEqual(Wind->Duration,40.f,.01f));
        for (TActorIterator<ADroneTrainerDronePawn> It(World);It;++It)
        {
            auto* Audio=It->FindComponentByClass<UAudioComponent>();
            if (TestNotNull(TEXT("S03 Pawn audio component"),Audio))
            {
                TestTrue(TEXT("S03 sound assigned"),Audio->Sound==Wind);
                TestFalse(TEXT("S03 non-spatial ambience"),bool(Audio->bAllowSpatialization));
            }
        }
    }
    ADroneTrainerLevelLayout* Layout=nullptr;
    int32 LayoutCount=0, GateCount=0;
    for (TActorIterator<ADroneTrainerLevelLayout> It(World);It;++It) {Layout=*It;++LayoutCount;}
    if (!TestNotNull(TEXT("Layout"),Layout)) return false;
    TestEqual(TEXT("Single layout"),LayoutCount,1);
    if (!TestNotNull(TEXT("Start"),Layout->Start.Get()) || !TestNotNull(TEXT("Landing"),Layout->LandingPad.Get()) ||
        !TestEqual(TEXT("Five referenced gates"),Layout->Gates.Num(),5)) return false;
    const FVector Origin(0,-6000,0);
    const FVector Expected[5]={FVector(-3200,200,0),FVector(-3200,2200,0),FVector(0,3600,0),FVector(3200,2200,0),FVector(3200,200,0)};
    const double Yaws[5]={90,90,0,-90,-90};
    for (TActorIterator<ADroneTrainerGate> It(World);It;++It) ++GateCount;
    TestEqual(TEXT("Exactly five placed gates"),GateCount,5);
    TArray<FVector> Route;
    Route.Add(Layout->Start->GetActorLocation());
    for (int32 I=0;I<5;++I)
    {
        auto* G=Layout->Gates[I].Get();
        if (!TestNotNull(TEXT("Gate reference"),G)) return false;
        TestEqual(TEXT("Sequential index"),G->GateIndex,I+1);
        TestTrue(TEXT("Authored position"),G->GetActorLocation().Equals(Origin+Expected[I],.1));
        TestTrue(TEXT("Pass direction"),G->GetActorForwardVector().Equals(FRotator(0,Yaws[I],0).Vector(),.001));
        TestTrue(TEXT("4 x 4 m opening"),G->OpeningHalfSize.Equals(FVector2D(200,200),.01));
        TestTrue(TEXT("Opening 3 m above surface"),FMath::Abs(G->GetOpeningCenter().Z-300)<.1);
        if (I==2 || I==3)
        {
            auto* Prev=Layout->Gates[I-1].Get();
            const FVector A=Prev->GetActorLocation(),D=G->GetActorLocation();
            const FVector B=A+Prev->GetActorForwardVector()*(I==2?1400:1800);
            const FVector C=D-G->GetActorForwardVector()*(I==2?1800:1400);
            for (int32 J=1;J<49;++J)
            {const double T=J/48.;Route.Add(FMath::Pow(1-T,3)*A+3*FMath::Square(1-T)*T*B+3*(1-T)*T*T*C+T*T*T*D);}
        }
        else Route.Add(G->GetActorLocation());
    }
    Route.Add(Layout->LandingPad->GetActorLocation());
    TestTrue(TEXT("Start faces first gate"),Layout->Start->GetActorForwardVector().Equals(Layout->Gates[0]->GetActorForwardVector(),.001));
    TestTrue(TEXT("20 m braking approach"),FMath::Abs(FVector::Dist2D(Route.Last(),Layout->Gates[4]->GetActorLocation())-2000)<.1);
    int32 Samples=0;
    for (int32 X=-60;X<=60;X+=2) for (int32 Y=-50;Y<=50;Y+=2)
    {
        FHitResult Hit;FVector P=Origin+FVector(X*100,Y*100,0);
        const bool Found=World->LineTraceSingleByChannel(Hit,P+FVector(0,0,2000),P-FVector(0,0,2000),ECC_GameTraceChannel1);
        TestTrue(TEXT("Continuous flat GroundSurface"),Found && FMath::Abs(Hit.ImpactPoint.Z)<.2 && Hit.ImpactNormal.Z>.999);
        ++Samples;
    }
    int32 PrepCount=0,MarkCount=0,TreeCount=0,GrassCount=0;
    for (TActorIterator<AActor> It(World);It;++It)
    {
        if (It->ActorHasTag(TEXT("E5BSolid")))
        {
            ++PrepCount;
            const FBox Box=It->GetComponentsBoundingBox();
            for (int32 I=1;I<Route.Num();++I)
            {
                FVector A=Route[I-1],B=Route[I],P=Box.GetCenter();A.Z=B.Z=P.Z=0;
                TestTrue(TEXT("Full prep bounds outside 16 m corridor"),FMath::PointDistToSegment(P,A,B)-Box.GetExtent().Size2D()>=800);
            }
        }
        if (It->ActorHasTag(TEXT("E5AMarking"))) ++MarkCount;
        TInlineComponentArray<UPrimitiveComponent*> Components(*It);
        for (auto* C:Components)
        {
            if (It->ActorHasTag(TEXT("E5AMarking"))) TestEqual(TEXT("Markings have no collision"),C->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
            if (It->ActorHasTag(TEXT("E5BSolid"))) TestEqual(TEXT("Prep ignores AGL"),C->GetCollisionResponseToChannel(ECC_GameTraceChannel1),ECR_Ignore);
            if (It->ActorHasTag(TEXT("E5BDecor"))) TestEqual(TEXT("School details have no collision"),C->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
            if (auto* Instances=Cast<UInstancedStaticMeshComponent>(C))
            {
                const UStaticMesh* Mesh=Instances->GetStaticMesh();
                if (!Mesh || Instances->GetInstanceCount()==0) continue;
                const bool Tree=Mesh->GetName().StartsWith(TEXT("SM_Tree_OakSample_"));
                TestEqual(TEXT("Foliage ignores AGL"),C->GetCollisionResponseToChannel(ECC_GameTraceChannel1),ECR_Ignore);
                TestEqual(TEXT("Only trunks collide"),C->GetCollisionEnabled(),Tree?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
                if (Tree)
                {
                    TreeCount+=Instances->GetInstanceCount();
                    TestEqual(TEXT("Collidable trees never culled"),Instances->InstanceEndCullDistance,0);
                    const UBodySetup* Body=Mesh->GetBodySetup();
                    if (TestNotNull(TEXT("Trunk collision"),Body))
                    {
                        TestEqual(TEXT("One trunk capsule"),Body->AggGeom.SphylElems.Num(),1);
                        TestEqual(TEXT("No foliage hull collision"),Body->AggGeom.ConvexElems.Num(),0);
                    }
                }
                else GrassCount+=Instances->GetInstanceCount();
                for (int32 J=0;J<Instances->GetInstanceCount();++J)
                {
                    FTransform T;Instances->GetInstanceTransform(J,T,true);
                    const FBox Box=Mesh->GetBoundingBox().TransformBy(T);
                    if (Tree) for (int32 I=1;I<Route.Num();++I)
                    {
                        FVector A=Route[I-1],B=Route[I],P=Box.GetCenter();A.Z=B.Z=P.Z=0;
                        TestTrue(TEXT("Full tree bounds outside corridor"),FMath::PointDistToSegment(P,A,B)-Box.GetExtent().Size2D()>=800);
                    }
                    else TestTrue(TEXT("Mown grass below 10 cm"),Box.Max.Z<10);
                }
            }
        }
    }
    TestTrue(TEXT("School solid props exist"),PrepCount>10);
    TestEqual(TEXT("One authored distant belt"),TreeCount,54);
    TestTrue(TEXT("Short mown grass exists"),GrassCount>0);
    TestTrue(TEXT("Ground guidance exists"),MarkCount>100);
    UE_LOG(LogTemp,Display,TEXT("E5B_LAYOUT_CHECK ground_samples=%d gates=%d prep=%d markings=%d trees=%d grass=%d"),Samples,GateCount,PrepCount,MarkCount,TreeCount,GrassCount);
    return !HasAnyErrors();
}
#endif
