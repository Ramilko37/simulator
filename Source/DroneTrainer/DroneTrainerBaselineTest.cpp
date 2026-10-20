#if WITH_DEV_AUTOMATION_TESTS
#include "DroneTrainerDronePawn.h"
#include "Camera/CameraComponent.h"
#include "DroneTrainerGate.h"
#include "DroneTrainerLesson.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneTrainerBaselineTest, "DroneTrainer.E0.GatesAndCameras",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDroneTrainerBaselineTest::RunTest(const FString& Parameters)
{
    UWorld* World = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.WorldType == EWorldType::Editor)
        {
            World = Context.World();
            break;
        }
    }
    if (!TestNotNull(TEXT("Loaded editor world"), World)) return false;
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags |= RF_Transient;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADroneTrainerDronePawn* Drone = World->SpawnActor<ADroneTrainerDronePawn>(Spawn);
    if (!TestNotNull(TEXT("Transient regression Pawn"), Drone)) return false;
    Drone->PhysicsBody->SetSimulatePhysics(false);
    Drone->AttemptState = EDroneAttemptState::Running;
    ADroneTrainerGate* Gate = World->SpawnActor<ADroneTrainerGate>(Spawn);
    Gate->SetActorLocation(FVector(7000, -2000, 500));
    Gate->SetActorRotation(FRotator(0, 41, 0));
    Drone->RouteGates.Add(Gate);
    // A second target keeps the existing simple attempt running after the first crossing.
    ADroneTrainerGate* NextGate = World->SpawnActor<ADroneTrainerGate>(Spawn);
    NextGate->SetActorLocation(FVector(8000, 6500, 300));
    Drone->RouteGates.Add(NextGate);
    Drone->StartAltitude = 810.0f;
    Drone->bHasTakenOff = true;
    Drone->PreviousFlightPosition = Gate->GetOpeningCenter() - Gate->GetActorForwardVector()*100;
    Drone->SetActorLocation(Gate->GetOpeningCenter() + Gate->GetActorForwardVector()*100);
    if (DroneLesson::CrossesGate(*Gate, Drone->PreviousFlightPosition, Drone->GetActorLocation(), FQuat::Identity, FQuat::Identity, FVector(30,30,10), true)) ++Drone->GatesPassed;
    TestTrue(TEXT("Below spawn altitude without contact remains Running"),
        Drone->AttemptState == EDroneAttemptState::Running);
    TestEqual(TEXT("Opening crossing counts the gate"), Drone->GatesPassed, 1);
    for (int32 Index = 0; Index < 20; ++Index)
    {
        Drone->ToggleCameraView();
        TestEqual(TEXT("FPV active matches selected view"), Drone->FpvCamera->IsActive(), !Drone->bThirdPersonView);
        TestEqual(TEXT("Chase active matches selected view"), Drone->ThirdPersonCamera->IsActive(), Drone->bThirdPersonView);
        TestEqual(TEXT("Camera preserves gate progress"), Drone->GatesPassed, 1);
        TestTrue(TEXT("Camera preserves attempt state"), Drone->AttemptState == EDroneAttemptState::Running);
    }
    // A real contact must still fail; use a separate transient actor as the obstacle.
    AActor* Obstacle = World->SpawnActor<AActor>(Spawn);
    Drone->HandleBodyHit(Drone->PhysicsBody, Obstacle, nullptr, FVector::ZeroVector, FHitResult());
    TestTrue(TEXT("Physical contact queues collision before progress"), Drone->bPendingCollision);
    Gate->Destroy();
    NextGate->Destroy();
    Obstacle->Destroy();
    Drone->Destroy();
    return true;
}
#endif
