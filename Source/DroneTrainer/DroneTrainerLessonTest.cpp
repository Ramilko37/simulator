#if WITH_DEV_AUTOMATION_TESTS
#include "DroneTrainerDronePawn.h"
#include "DroneTrainerLesson.h"
#include "DroneTrainerLevelLayout.h"
#include "Engine/Engine.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

static UWorld* LessonWorld()
{
    for (const auto& C : GEngine->GetWorldContexts()) if (C.WorldType==EWorldType::Game || C.WorldType==EWorldType::PIE) return C.World();
    return nullptr;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneTrainerGateTest,"DroneTrainer.Gates",EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDroneTrainerGateTest::RunTest(const FString&)
{
    UWorld* W=LessonWorld(); if (!TestNotNull(TEXT("Game world"),W)) return false;
    int Count=0;
    for (TActorIterator<ADroneTrainerGate> It(W);It;++It)
    {
        ++Count; const auto& G=**It; const FVector C=G.GetOpeningCenter(),N=G.GetActorForwardVector(),R=G.GetActorRightVector(),U=G.GetActorUpVector();
        auto Cross=[&](FVector A,FVector B,bool Armed=true,FQuat Q=FQuat::Identity) {return DroneLesson::CrossesGate(G,A,B,Q,Q,FVector(30,30,10),Armed);};
        TestTrue(TEXT("Rotated gate forward crossing"),Cross(C-N*150,C+N*150));
        TestFalse(TEXT("Unarmed"),Cross(C-N*150,C+N*150,false));
        TestFalse(TEXT("Backwards"),Cross(C+N*150,C-N*150));
        TestFalse(TEXT("No plane crossing"),Cross(C-N*150,C-N*50));
        TestFalse(TEXT("Parallel"),Cross(C-N*50-R*200,C-N*50+R*200));
        TestFalse(TEXT("Wrong opening"),Cross(C-N*150+R*210,C+N*150+R*210));
        TestFalse(TEXT("Above opening"),Cross(C-N*150+U*200,C+N*150+U*200));
        const FQuat Q=G.GetActorQuat();
        TestTrue(TEXT("5 cm clearance inclusive"),Cross(C-N*150+R*165,C+N*150+R*165,true,Q));
        TestFalse(TEXT("Less than 5 cm clearance"),Cross(C-N*150+R*165.01,C+N*150+R*165.01,true,Q));
        TestFalse(TEXT("Starting on plane is not crossing"),Cross(C,C+N*150));
        TestTrue(TEXT("Fast segment without overlap"),Cross(C-N*1000,C+N*1000));
        TestFalse(TEXT("Reset history cannot cross"),Cross(C+N*150,C+N*150));
        const FQuat Tilt=Q*FRotator(0,0,20).Quaternion();
        TestFalse(TEXT("Tilted body blocks tight edge"),Cross(C-N*150+R*164,C+N*150+R*164,true,Tilt));
        TestTrue(TEXT("Tilted body fits with clearance"),Cross(C-N*150+R*160,C+N*150+R*160,true,Tilt));
        TestFalse(TEXT("NaN rejected"),Cross(FVector(NAN,0,0),C+N*150));
    }
    TestEqual(TEXT("Five placed gates"),Count,5); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneTrainerLessonTest,"DroneTrainer.Lesson",EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDroneTrainerLessonTest::RunTest(const FString&)
{
    UWorld* W=LessonWorld(); if (!TestNotNull(TEXT("Game world"),W)) return false;
    ADroneTrainerDronePawn* D=nullptr;
    for (TActorIterator<ADroneTrainerDronePawn> It(W);It;++It) D=*It;
    if (!TestNotNull(TEXT("Runtime pawn"),D)) return false;
    D->bFlightCheckActive=true;
    auto Reset=[&]() { D->AttemptState=EDroneAttemptState::Paused; D->RestartAttempt(); D->PhysicsBody->SetSimulatePhysics(false); };
    auto Place=[&](FVector P) { D->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics); D->SetActorRotation(FRotator::ZeroRotator); D->UpdateTelemetry(); };
    const FVector Start=D->AttemptStartTransform.GetLocation()+FVector(0,0,300-D->PhysicsBody->GetScaledBoxExtent().Z-D->StartGroundClearance);
    Reset(); TestTrue(TEXT("Valid authored lesson"),D->ValidateLesson());
    auto* Marks=D->Layout->Markings.Get();
    TestTrue(TEXT("World markings registered without collision"),Marks && Marks->IsRegistered() && !Marks->IsCollisionEnabled());
    if (Marks)
    {
        const int32 Count=Marks->BatchedLines.Num();
        TestTrue(TEXT("Start and landing markings exist"),Count>0);
        for (const auto& L : Marks->BatchedLines) TestTrue(TEXT("Markings depth tested and persistent"),L.DepthPriority==SDPG_World && L.RemainingLifeTime<0);
        D->Layout->RefreshMarkings();
        TestEqual(TEXT("Refresh does not accumulate markings"),Marks->BatchedLines.Num(),Count);
    }
    D->UpdateAttempt(1); const float Remaining=D->CountdownRemaining; D->TogglePause(); D->UpdateAttempt(10);
    TestEqual(TEXT("Paused countdown frozen"),D->CountdownRemaining,Remaining); D->TogglePause(); D->UpdateAttempt(.25);
    TestEqual(TEXT("Countdown resumes remaining time"),D->CountdownRemaining,Remaining-.25f);
    D->UpdateAttempt(2); TestTrue(TEXT("Countdown enters Takeoff"),D->LessonPhase==EDroneLessonPhase::Takeoff && D->AttemptState==EDroneAttemptState::Running);
    auto* G=D->RouteGates[0].Get();
    D->PreviousFlightPosition=G->GetOpeningCenter()-G->GetActorForwardVector()*150;
    Place(G->GetOpeningCenter()+G->GetActorForwardVector()*150); D->UpdateAttempt(.016);
    TestEqual(TEXT("Takeoff transition cannot count a gate"),D->GatesPassed,0);
    Reset(); D->UpdateAttempt(3); Place(Start+FVector(1000,0,0));
    FVector Away=D->GetActorLocation(); Away.Z-=D->AltitudeMeters*100.f;
    Place(Away+FVector(0,0,299)); D->UpdateAttempt(.016);
    TestTrue(TEXT("Below 3m stays Takeoff away from start"),D->LessonPhase==EDroneLessonPhase::Takeoff);
    Place(Away+FVector(0,0,300)); D->UpdateAttempt(.016);
    TestTrue(TEXT("Exactly 3m away from start immediately opens route without hover"),D->LessonPhase==EDroneLessonPhase::Route);
    TestEqual(TEXT("Takeoff does not count a gate"),D->GatesPassed,0);
    D->BoundarySeconds=2; D->MissingGroundSeconds=.1; D->TogglePause();
    D->UpdateAttempt(20); TestEqual(TEXT("Boundary paused"),D->BoundarySeconds,2.f);
    D->TogglePause(); TestTrue(TEXT("Phase retained"),D->LessonPhase==EDroneLessonPhase::Route);
    const FBox Pad=D->Layout->LandingPad->GetComponentsBoundingBox(); const FVector Land(Pad.GetCenter().X,Pad.GetCenter().Y,Pad.Max.Z+10);
    auto Soft=[&](FVector V,FVector P,FQuat Q=FQuat::Identity,FVector N=FVector::UpVector) {return DroneLesson::SoftLanding(P,Q,FVector(30,30,10),V,N,FVector(P.X,P.Y,Pad.Max.Z),Pad);};
    TestTrue(TEXT("Soft touchdown at limits"),Soft(FVector(50,0,-40),Land));
    TestFalse(TEXT("Hard vertical preimpact"),Soft(FVector(0,0,-40.01),Land));
    TestFalse(TEXT("Hard horizontal preimpact"),Soft(FVector(50.01,0,0),Land));
    TestFalse(TEXT("Pad edge body plus margin"),Soft(FVector::ZeroVector,Land+FVector(321,0,0)));
    TestFalse(TEXT("Tilt"),Soft(FVector::ZeroVector,Land,FRotator(11,0,0).Quaternion()));
    TestFalse(TEXT("Side contact"),Soft(FVector::ZeroVector,Land,FQuat::Identity,FVector::RightVector));
    auto ContactReason=[&](FVector V,FQuat Q=FQuat::Identity,FVector N=FVector::UpVector,FVector Offset=FVector::ZeroVector)
    { return DroneLesson::SurfaceContactFailure(Land,Q,FVector(30,30,10),V,N,Land-FVector(0,0,10)+Offset); };
    TestTrue(TEXT("Inclusive contact limits have no failure"),ContactReason(FVector(50,0,-40)).IsEmpty());
    TestTrue(TEXT("Horizontal excess is explicit, even just over threshold"),ContactReason(FVector(50.01,0,0)).Contains(TEXT("0,51 > 0,5")));
    TestTrue(TEXT("Vertical excess is explicit"),ContactReason(FVector(0,0,-40.01)).Contains(TEXT("0,41 > 0,4")));
    TestTrue(TEXT("Tilt reason"),ContactReason(FVector::ZeroVector,FRotator(11,0,0).Quaternion()).Contains(TEXT("Наклон")));
    TestTrue(TEXT("Side reason"),ContactReason(FVector::ZeroVector,FQuat::Identity,FVector::RightVector).Contains(TEXT("боковую")));
    TestTrue(TEXT("Body reason"),ContactReason(FVector::ZeroVector,FQuat::Identity,FVector::UpVector,FVector(0,0,4)).Contains(TEXT("корпусом")));
    Reset(); D->UpdateAttempt(3); Place(Land); D->LessonPhase=EDroneLessonPhase::Landing;
    TestTrue(TEXT("Landing hint names horizontal limit"),D->GetLessonInstruction().Contains(TEXT("по горизонтали до 0,5")));
    ADroneTrainerDronePawn::FContactSample Rejected;
    Rejected.Position=Land; Rejected.Velocity=FVector(116.33,0,-20); Rejected.Normal=FVector::UpVector;
    Rejected.Point=Land-FVector(0,0,10); Rejected.Rotation=FQuat::Identity; Rejected.bLandingPad=true;
    D->PendingContacts.Add(Rejected); D->UpdateAttempt(.016);
    TestTrue(TEXT("Failure records preimpact reason"),D->GetFailureDetail().Contains(TEXT("1,17 > 0,5")));
    TestTrue(TEXT("Failure freezes speed without losing reason"),D->PhysicsBody->GetPhysicsLinearVelocity().IsNearlyZero() && !D->GetFailureDetail().IsEmpty());
    Reset(); TestTrue(TEXT("Retry clears old cause"),D->GetFailureDetail().IsEmpty());
    TestEqual(TEXT("Invalid wins"),DroneLesson::Outcome(true,true,true,300),1);
    TestEqual(TEXT("Crash wins"),DroneLesson::Outcome(false,true,true,300),2);
    TestEqual(TEXT("Boundary wins over timeout"),DroneLesson::Outcome(false,false,true,300),3);
    TestEqual(TEXT("Exact 300 fails"),DroneLesson::Outcome(false,false,false,300),4);
    // Three sequential state-machine fixtures. Positions are test inputs, NOT physical flights.
    for (int Run=0;Run<3;++Run)
    {
        Reset(); D->UpdateAttempt(3); Place(Start+FVector(0,0,20)); D->UpdateAttempt(.016);
        TestTrue(TEXT("Takeoff immediately completes"),D->LessonPhase==EDroneLessonPhase::Route);
        G=D->RouteGates[1]; Place(G->GetOpeningCenter()-G->GetActorForwardVector()*150); D->PreviousFlightPosition=D->GetActorLocation(); D->UpdateAttempt(.016);
        Place(G->GetOpeningCenter()+G->GetActorForwardVector()*150); D->UpdateAttempt(.016); TestEqual(TEXT("G2 before G1 ignored"),D->GatesPassed,0);
        for (int I=0;I<5;++I)
        {
            G=D->RouteGates[I]; const FVector C=G->GetOpeningCenter(),N=G->GetActorForwardVector();
            Place(C+N*150); D->PreviousFlightPosition=C+N*150; D->UpdateAttempt(.016); TestFalse(TEXT("Exit side cannot arm"),D->bGateArmed);
            Place(C-N*150); D->UpdateAttempt(.016); TestTrue(TEXT("Entry side arms"),D->bGateArmed);
            Place(C+N*150); D->UpdateAttempt(.016); TestEqual(TEXT("Ordered gate"),D->GatesPassed,I+1);
        }
        G=D->RouteGates[0]; D->PreviousFlightPosition=G->GetOpeningCenter()-G->GetActorForwardVector()*150;
        Place(G->GetOpeningCenter()+G->GetActorForwardVector()*150); D->UpdateAttempt(.016); TestEqual(TEXT("Repeated gate ignored"),D->GatesPassed,5);
        TestTrue(TEXT("G5 does not complete"),D->AttemptState==EDroneAttemptState::Running && D->LessonPhase==EDroneLessonPhase::Landing);
        Place(Land); ADroneTrainerDronePawn::FContactSample C;
        C.Position=Land; C.Point=FVector(Land.X,Land.Y,Pad.Max.Z); C.Normal=FVector::UpVector; C.Rotation=FQuat::Identity; C.Velocity=FVector(0,0,-20); C.bLandingPad=true;
        D->PendingContacts.Add(C); D->UpdateAttempt(1);
        TestTrue(TEXT("Touchdown cuts altitude hold thrust"),D->bLandingSettled); TestTrue(TEXT("One second is insufficient"),D->AttemptState==EDroneAttemptState::Running);
        D->TogglePause(); D->UpdateAttempt(10); TestEqual(TEXT("Landing timer frozen"),D->LandingSeconds,1.f); D->TogglePause();
        Place(Land+FVector(0,0,4)); D->UpdateAttempt(.1); TestEqual(TEXT("Small separation resets contact timer"),D->LandingSeconds,0.f);
        Place(Land); D->PendingContacts.Add(C); D->UpdateAttempt(1); D->UpdateAttempt(1);
        TestTrue(TEXT("Stable two seconds completes"),D->AttemptState==EDroneAttemptState::Completed);
        const float Time=D->ElapsedSeconds; D->bPendingCollision=true; D->UpdateAttempt(1);
        TestTrue(TEXT("Terminal result immutable"),D->AttemptState==EDroneAttemptState::Completed && D->ElapsedSeconds==Time);
        TestFalse(TEXT("Fixture remains FPV"),D->bThirdPersonView);
        UE_LOG(LogTemp,Display,TEXT("LESSON_FIXTURE_COMPLETE run=%d scope=state_machine_not_flight"),Run+1);
    }
    Reset(); TestEqual(TEXT("Reset gates"),D->GatesPassed,0); TestEqual(TEXT("Reset timers"),D->LandingSeconds+D->BoundarySeconds+D->MissingGroundSeconds,0.f);
    TestFalse(TEXT("Reset contacts"),D->bLandingSettled || D->bGateArmed || D->bPendingCollision); TestTrue(TEXT("Reset FPV"),D->FpvCamera->IsActive());
    D->UpdateAttempt(3); Place(FVector(21000,0,300)); D->UpdateAttempt(4); TestTrue(TEXT("Grace period"),D->AttemptState==EDroneAttemptState::Running);
    Place(Start); D->UpdateAttempt(.1); TestEqual(TEXT("Returning clears grace"),D->BoundarySeconds,0.f);
    Place(FVector(21000,0,300)); D->UpdateAttempt(5); TestTrue(TEXT("Grace expires"),D->FailureReason==EDroneFailureReason::OutOfBounds);
    Reset(); D->UpdateAttempt(3); Place(FVector(24100,0,300)); D->UpdateAttempt(.016); TestTrue(TEXT("Hard boundary immediate"),D->FailureReason==EDroneFailureReason::OutOfBounds);
    Reset(); D->UpdateAttempt(3); Place(FVector(0,0,30000)); D->UpdateAttempt(.26); TestTrue(TEXT("Missing ground inside Error"),D->AttemptState==EDroneAttemptState::Error);
    Reset(); D->UpdateAttempt(3); Place(Start); D->ElapsedSeconds=299.9; D->UpdateAttempt(.11); TestTrue(TEXT("Timeout"),D->FailureReason==EDroneFailureReason::TimeLimit);
    Reset(); D->UpdateAttempt(3); Place(Start); D->bPendingCollision=true; D->ElapsedSeconds=300; D->UpdateAttempt(.016); TestTrue(TEXT("Collision before timeout"),D->FailureReason==EDroneFailureReason::Collision);
    Reset(); D->UpdateAttempt(3); D->LessonPhase=EDroneLessonPhase::Landing; Place(Land);
    ADroneTrainerDronePawn::FContactSample Hard; Hard.Position=Land; Hard.Rotation=FQuat::Identity; Hard.Point=Land-FVector(0,0,10); Hard.Normal=FVector::UpVector; Hard.Velocity=FVector(0,0,-200); Hard.bLandingPad=true;
    D->PendingContacts.Add(Hard); D->UpdateAttempt(.016); TestTrue(TEXT("Preimpact hard landing fails even when current speed zero"),D->FailureReason==EDroneFailureReason::Collision);
    Reset(); D->UpdateAttempt(3); D->LessonPhase=EDroneLessonPhase::Route; G=D->RouteGates[0]; D->bGateArmed=true;
    D->PreviousFlightPosition=G->GetOpeningCenter()-G->GetActorForwardVector()*150;
    Place(G->GetOpeningCenter()+G->GetActorForwardVector()*150); D->bPendingCollision=true; D->UpdateAttempt(.016);
    TestEqual(TEXT("Collision step cannot score gate"),D->GatesPassed,0);
    Reset(); D->UpdateAttempt(3); D->LessonPhase=EDroneLessonPhase::Landing; Place(Land); D->bLandingSettled=true; D->LandingSeconds=1.99f; D->ElapsedSeconds=299.99f; D->UpdateAttempt(.02);
    TestTrue(TEXT("Timeout beats final landing progress"),D->FailureReason==EDroneFailureReason::TimeLimit);
    Reset(); D->UpdateAttempt(3); D->LessonPhase=EDroneLessonPhase::Landing; Place(Land); D->bLandingSettled=true; D->LandingSeconds=1.99f; D->bPendingCollision=true; D->UpdateAttempt(.02);
    TestTrue(TEXT("Crash beats final landing progress"),D->FailureReason==EDroneFailureReason::Collision);
    Reset(); Place(Start-FVector(0,0,220)); D->LessonPhase=EDroneLessonPhase::Takeoff; D->UpdateTelemetry(); TestFalse(TEXT("Takeoff has no low-altitude alarm"),D->bLowAltitude);
    D->LessonPhase=EDroneLessonPhase::Route; D->UpdateTelemetry(); TestTrue(TEXT("Route low altitude"),D->bLowAltitude);
    D->LessonPhase=EDroneLessonPhase::Landing; D->UpdateTelemetry(); TestFalse(TEXT("Landing has no low-altitude alarm"),D->bLowAltitude);
    const int32 Index=D->RouteGates[1]->GateIndex; D->RouteGates[1]->GateIndex=1;
    TestFalse(TEXT("Duplicate gate index invalidates world"),D->ValidateLesson());
    D->AttemptState=EDroneAttemptState::MainMenu; D->RestartAttempt();
    TestTrue(TEXT("Incomplete route blocks start"),D->AttemptState==EDroneAttemptState::Error);
    TestFalse(TEXT("Invalid start has no physics"),D->PhysicsBody->IsSimulatingPhysics());
    D->RouteGates[1]->GateIndex=Index;
    Reset(); D->AttemptState=EDroneAttemptState::Paused; D->ReturnToMainMenu(); D->bFlightCheckActive=false; return true;
}
#endif
