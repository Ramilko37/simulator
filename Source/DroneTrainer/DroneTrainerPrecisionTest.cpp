#if WITH_DEV_AUTOMATION_TESTS
#include "DroneTrainerDronePawn.h"
#include "DroneTrainerPrecision.h"
#include "DroneTrainerLevelLayout.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneTrainerPrecisionTest,"DroneTrainer.Precision",EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDroneTrainerPrecisionTest::RunTest(const FString&)
{
    using namespace DronePrecision;
    auto Failure=[](FVector P,float AGL=3,bool Ground=true,float Speed=0,float Yaw=0,float Required=0,bool Heading=true)
    { return HoldFailure(P,FVector::ZeroVector,AGL,Ground,Speed,Yaw,Required,Heading); };
    TestTrue(TEXT("Inclusive hold limits"),Failure(FVector(200,0,0),2.5,true,.5,15).IsEmpty());
    TestTrue(TEXT("Upper altitude inclusive"),Failure(FVector::ZeroVector,3.5).IsEmpty());
    TestTrue(TEXT("Angle wraps 359 to 1"),Failure(FVector::ZeroVector,3,true,0,359,1).IsEmpty());
    TestTrue(TEXT("Angle wraps 1 to 359"),Failure(FVector::ZeroVector,3,true,0,1,359).IsEmpty());
    TestTrue(TEXT("Yaw beyond tolerance"),!Failure(FVector::ZeroVector,3,true,0,15.01).IsEmpty());
    const FString Failures[]={Failure(FVector(200.01,0,0)),Failure(FVector::ZeroVector,2.49),Failure(FVector::ZeroVector,3.51),
        Failure(FVector::ZeroVector,3,true,.501),Failure(FVector::ZeroVector,3,true,0,15.01),Failure(FVector::ZeroVector,3,false),Failure(FVector::ZeroVector,NAN)};
    for (const auto& Reason : Failures)
    {
        EStep Step=EStep::P2; float Held=2.9;
        TestFalse(TEXT("Every violation prevents transition"),Advance(Step,Held,.2,true,true,3,Reason.IsEmpty()));
        TestEqual(TEXT("Every violation resets hold"),Held,0.f);
        TestTrue(TEXT("Completed steps retained"),Step==EStep::P2);
    }
    EStep S=EStep::Takeoff; float Held=0;
    TestFalse(TEXT("Invalid AGL cannot take off"),Advance(S,Held,1,true,false,3,true));
    TestTrue(TEXT("Valid takeoff"),Advance(S,Held,.1,true,true,3,true));
    TestTrue(TEXT("No skipped position"),S==EStep::P1);
    for (EStep Expected : {EStep::P2,EStep::Turn,EStep::P3,EStep::Landing})
    {
        TestFalse(TEXT("Under 3 seconds"),Advance(S,Held,2,true,true,3,true));
        const float Frozen=Held;
        TestFalse(TEXT("Pause cannot advance"),Advance(S,Held,10,false,true,3,true));
        TestEqual(TEXT("Paused hold frozen"),Held,Frozen);
        TestTrue(TEXT("Continuous 3 seconds advances"),Advance(S,Held,1,true,true,3,true));
        TestTrue(TEXT("Ordered step"),S==Expected);
    }
    TestEqual(TEXT("Turn uses the same P2"),TargetIndex(EStep::P2),TargetIndex(EStep::Turn));
    TestFalse(TEXT("Position logic cannot complete landing"),Advance(S,Held,10,true,true,3,true));
    TestTrue(TEXT("Yaw0 forward"),Correction(FVector(1000,0,0),0).Contains(TEXT("Вперёд 10.0")));
    TestTrue(TEXT("Yaw90 right is -X"),Correction(FVector(-1000,0,0),90).Contains(TEXT("Правее 10.0")));
    TestTrue(TEXT("Yaw180 forward is -X"),Correction(FVector(-1000,0,0),180).Contains(TEXT("Вперёд 10.0")));
    TestTrue(TEXT("Yaw270 backward is +Y"),Correction(FVector(0,1000,0),270).Contains(TEXT("Назад 10.0")));
    UWorld* W=nullptr;
    for (const auto& C : GEngine->GetWorldContexts()) if (C.WorldType==EWorldType::Game || C.WorldType==EWorldType::PIE) W=C.World();
    if (!TestNotNull(TEXT("Runtime world"),W)) return false;
    ADroneTrainerDronePawn* D=nullptr;
    for (TActorIterator<ADroneTrainerDronePawn> It(W);It;++It) D=*It;
    if (!TestNotNull(TEXT("Existing pawn"),D)) return false;
    D->bFlightCheckActive=true; D->AttemptState=EDroneAttemptState::Paused; D->ReturnToMainMenu(); D->SelectPrecisionLesson();
    if (!TestTrue(TEXT("Authored precision objects valid"),D->ValidateLesson()))
    {
        AddInfo(FString::Printf(TEXT("Precision=%d state=%d start=%s objects=%d"),D->IsPrecisionLesson(),int32(D->AttemptState),*GetNameSafe(D->StartSurface),D->PrecisionObjects.Num()));
        for (int32 I=0; I<D->PrecisionObjects.Num(); ++I)
            if (auto* A=D->PrecisionObjects[I].Get()) AddInfo(FString::Printf(TEXT("Point %d %s pos=%s yaw=%.2f bounds=%s tags=%s"),I,*A->GetName(),*A->GetActorLocation().ToString(),A->GetActorRotation().Yaw,*A->GetComponentsBoundingBox().GetSize().ToString(),*FString::JoinBy(A->Tags,TEXT(","),[](FName N){ return N.ToString(); })));
        return false;
    }
    TestTrue(TEXT("Gate target disabled"),D->GetCurrentGate()==nullptr);
    D->PrecisionStep=EStep::P1;
    TestTrue(TEXT("Non-colliding P1 supplies its actual target"),FVector::Dist2D(D->GetLessonTarget(),D->PrecisionObjects[1]->GetActorLocation())<.01);
    D->PrecisionStep=EStep::Takeoff;
    D->RestartAttempt(); D->PhysicsBody->SetSimulatePhysics(false); D->UpdateAttempt(3);
    auto Place=[&](int32 Index,float AGL,float Yaw)
    {
        const FBox B=D->PrecisionObjects[Index]->GetComponentsBoundingBox(true);
        D->SetActorLocation(FVector(B.GetCenter().X,B.GetCenter().Y,B.Max.Z+AGL*100),false,nullptr,ETeleportType::TeleportPhysics);
        D->SetActorRotation(FRotator(0,Yaw,0)); D->UpdateTelemetry();
    };
    // State fixtures only: these do not assess real flight or readability.
    const float InitialYaw=D->PrecisionObjects[0]->GetActorRotation().Yaw;
    Place(0,3,InitialYaw); D->ElapsedSeconds=301; D->UpdateAttempt(.1);
    TestTrue(TEXT("No lesson2 timeout after 300s"),D->AttemptState==EDroneAttemptState::Running);
    TestTrue(TEXT("Takeoff reaches P1"),D->PrecisionStep==EStep::P1);
    Place(4,.1,InitialYaw); D->bLandingSettled=true; D->UpdateAttempt(3);
    TestTrue(TEXT("Early safe landing preserves attempt"),D->AttemptState==EDroneAttemptState::Running && D->PrecisionStep==EStep::P1);
    TestEqual(TEXT("Early landing cannot finish"),D->LandingSeconds,0.f);
    for (EStep Step : {EStep::P1,EStep::P2,EStep::Turn,EStep::P3})
    {
        Place(TargetIndex(Step),3,Step==EStep::P1 ? InitialYaw : D->GetRequiredHeading());
        D->UpdateAttempt(1); const float Before=D->ElapsedSeconds;
        D->TogglePause(); D->UpdateAttempt(20);
        TestEqual(TEXT("Attempt time paused"),D->ElapsedSeconds,Before);
        TestEqual(TEXT("Hold paused"),D->PositionHoldSeconds,1.f);
        D->TogglePause(); D->UpdateAttempt(2);
        TestTrue(TEXT("Runtime sequential progression"),D->PrecisionStep==EStep(uint8(Step)+1));
    }
    TestEqual(TEXT("Three positions completed"),D->GetPositionsCompleted(),3);
    const FBox Pad=D->GetLandingPad()->GetComponentsBoundingBox();
    Place(4,.1,180); D->bLandingSettled=true; D->UpdateAttempt(1);
    TestTrue(TEXT("One second contact insufficient"),D->AttemptState==EDroneAttemptState::Running);
    D->UpdateAttempt(1);
    TestTrue(TEXT("Final B2 contact completes"),D->AttemptState==EDroneAttemptState::Completed);
    D->RestartAttempt();
    TestTrue(TEXT("Retry keeps lesson and clears step"),D->IsPrecisionLesson() && D->PrecisionStep==EStep::Takeoff);
    TestEqual(TEXT("Retry clears hold and elapsed"),D->PositionHoldSeconds+D->ElapsedSeconds,0.f);
    D->AttemptState=EDroneAttemptState::Paused; D->ReturnToMainMenu();
    auto* A=D->PrecisionObjects[1].Get(); const auto Tags=A->Tags;
    A->Tags.Reset(); TestFalse(TEXT("Missing object blocks start"),D->ValidateLesson()); A->Tags=Tags;
    auto* Duplicate=W->SpawnActor<AStaticMeshActor>(); Duplicate->Tags.Add(TEXT("L2_P1"));
    TestFalse(TEXT("Duplicate object blocks start"),D->ValidateLesson()); Duplicate->Tags.Reset(); Duplicate->Destroy();
    D->PositionHoldSeconds=2; D->ElapsedSeconds=60; D->PrecisionStep=EStep::Turn;
    D->SelectBasicLesson();
    TestTrue(TEXT("Switch resets full attempt"),!D->IsPrecisionLesson() && D->PositionHoldSeconds==0 && D->ElapsedSeconds==0 && D->PrecisionStep==EStep::Takeoff && D->PendingContacts.IsEmpty());
    TestTrue(TEXT("Basic route still validates"),D->ValidateLesson());
    TestTrue(TEXT("Basic start restored"),D->AttemptStartTransform.Equals(D->BasicStartTransform));
    D->bFlightCheckActive=false;
    UE_LOG(LogTemp,Display,TEXT("PRECISION_FIXTURE_COMPLETE scope=numerical_state_machine_not_flight"));
    return true;
}
#endif
