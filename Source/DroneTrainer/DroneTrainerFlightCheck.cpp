#if WITH_DEV_AUTOMATION_TESTS
#include "DroneTrainerDronePawn.h"
#include "DroneTrainerGate.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Kismet/GameplayStatics.h"

// Explicit development-only harness: real Chaos world and production force controller,
// deterministic axis injection, no OS keyboard or rendering-performance claims.
class FDroneTrainerFlightCheck
{
public:
    TWeakObjectPtr<ADroneTrainerDronePawn> Drone;
    TWeakObjectPtr<AStaticMeshActor> Wall;
    float Time = 0, Fps = 60, PeakError = 0, HoverError = 0, BrakeTime = -1, BrakeDistance = 0, PeakSpeed = 0;
    FVector BrakeStart, PausedPosition, ForwardStart, StrafeStart;
    float PausedElapsed = 0;
    FString Samples = TEXT("time_s,x_cm,y_cm,z_cm,agl_m,horizontal_mps,vertical_mps,pitch,roll,yaw\n");
    bool Passed = true, TakeoffReached = false;
    float TakeoffHoldError = 0;
    void Require(bool Value, const TCHAR* Name)
    {
        Passed &= Value;
        UE_LOG(LogTemp, Display, TEXT("E2_CHECK %s %s"), Name, Value ? TEXT("PASS") : TEXT("FAIL"));
    }
    void Start(UWorld* World, float Rate)
    {
        Fps = Rate;
        for (TActorIterator<ADroneTrainerDronePawn> It(World); It; ++It) { Drone = *It; }
        if (!Drone.IsValid()) { UE_LOG(LogTemp, Error, TEXT("E2_CHECK no game Pawn")); delete this; return; }
        auto* D = Drone.Get();
        D->RestartAttempt();
        D->bFlightCheckActive = true;
        Require(D->PhysicsBody->GetUnscaledBoxExtent().Equals(FVector(30,30,10)), TEXT("body_extent"));
        Require(FMath::Abs(D->PhysicsBody->GetMass()-2) < .01f, TEXT("mass"));
        Require(D->bGroundValid && FMath::Abs(D->AltitudeMeters-.13f) < .01f, TEXT("spawn_clearance_3cm"));
        const FVector Size = D->VisualMesh->Bounds.BoxExtent * 2;
        Require(Size.Equals(FVector(60,60,20),.1f), TEXT("visual_size"));
        const auto State = D->AttemptState;
        for (int32 I=0; I<20; ++I) D->ToggleCameraView();
        Require(D->AttemptState == State && D->GatesPassed==0 && !D->bThirdPersonView, TEXT("camera_20_preserves_state"));
        D->ToggleCameraView();
        D->AttemptState = EDroneAttemptState::Paused;
        D->RestartAttempt();
        Require(!D->bThirdPersonView && D->FpvCamera->IsActive() && D->PhysicsBody->GetPhysicsLinearVelocity().IsNearlyZero(), TEXT("reset_fpv_velocity"));
        FHitResult Hit; Hit.ImpactNormal = FVector::UpVector;
        D->AttemptState = EDroneAttemptState::Running;
        D->HandleBodyHit(D->PhysicsBody,D->StartSurface,nullptr,FVector::ZeroVector,Hit);
        Require(D->AttemptState == EDroneAttemptState::Running, TEXT("start_contact_allowed"));
        D->HandleBodyHit(D->PhysicsBody,World->GetWorldSettings(),nullptr,FVector::ZeroVector,Hit);
        D->UpdateAttempt(0);
        Require(D->AttemptState == EDroneAttemptState::Failed, TEXT("obstacle_before_takeoff_fails"));
        D->bPendingCollision=false;
        D->AttemptState = EDroneAttemptState::Running;
        APlayerController* PC=CastChecked<APlayerController>(D->GetController());
        D->bVerticalInputActive=true; D->bAltitudeHoldActive=true; D->TargetAltitude=D->GetActorLocation().Z+200;
        PC->PlayerInput->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,EKeys::W,IE_Pressed,1.0f,false,FPlatformTime::Cycles64()));
        PC->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(),1/60.f,false);
        Require(PC->IsInputKeyDown(EKeys::W),TEXT("focus_fixture_key_is_down"));
        if (FSlateApplication::IsInitialized()) FSlateApplication::Get().OnApplicationActivationStateChanged().Broadcast(false);
        else D->HandleFocusLost();
        Require(!PC->IsInputKeyDown(EKeys::W),TEXT("focus_flushes_keys"));
        Require(D->TargetAltitude==D->GetActorLocation().Z && !D->bVerticalInputActive,TEXT("focus_captures_current_altitude"));
        Require(D->AttemptState == EDroneAttemptState::Paused && UGameplayStatics::IsGamePaused(World), TEXT("focus_pauses"));
        if (FSlateApplication::IsInitialized()) FSlateApplication::Get().OnApplicationActivationStateChanged().Broadcast(true);
        Require(D->AttemptState==EDroneAttemptState::Paused,TEXT("activation_does_not_resume"));
        D->TogglePause();
        Require(D->AttemptState == EDroneAttemptState::Running && !UGameplayStatics::IsGamePaused(World),TEXT("esc_resumes"));
        int32 Probes = 0;
        for (const auto& G : D->RouteGates)
        {
            for (const FVector2D Offset : {FVector2D(0,0),FVector2D(-155,0),FVector2D(155,0),FVector2D(0,-180),FVector2D(0,180),FVector2D(-210,0),FVector2D(210,0),FVector2D(0,-210),FVector2D(0,210)})
            {
                const FVector C=G->GetOpeningCenter()+G->GetActorRightVector()*Offset.X+FVector::UpVector*Offset.Y;
                FHitResult H;
                const bool Blocked=World->SweepSingleByChannel(H,C-G->GetActorForwardVector()*200,C+G->GetActorForwardVector()*200,G->GetActorQuat(),ECC_Visibility,
                    FCollisionShape::MakeBox(FVector(30,30,10)),FCollisionQueryParams(SCENE_QUERY_STAT(E2Gate),false,D));
                Require(Blocked == (FMath::Abs(Offset.X)>200 || FMath::Abs(Offset.Y)>200),TEXT("gate_sweep")); ++Probes;
            }
        }
        Require(Probes==45,TEXT("gate_45_probes"));
        D->PhysicsBody->SetSimulatePhysics(false);
        D->SetActorLocation(D->RouteGates[2]->GetOpeningCenter()); D->UpdateTelemetry();
        Require(D->bGroundValid && FMath::Abs(D->AltitudeMeters-3)<.01f,TEXT("agl_terrain_ignores_gate"));
        D->SetActorLocation(FVector(30000,30000,3000)); D->UpdateTelemetry();
        Require(!D->bGroundValid && D->AltitudeMeters==0,TEXT("agl_missing_clears_value"));
        D->SetActorLocation(D->AttemptStartTransform.GetLocation()+FVector(0,0,300-D->PhysicsBody->GetScaledBoxExtent().Z-D->StartGroundClearance));
        D->SetActorRotation(FRotator::ZeroRotator);
        D->PhysicsBody->SetSimulatePhysics(true); D->PhysicsBody->SetEnableGravity(true);
        D->TargetAltitude = D->GetActorLocation().Z; D->bAltitudeHoldActive = true;
        D->PhysicsBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
        D->PhysicsBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        D->AttemptState = EDroneAttemptState::Running;
        Wall=World->SpawnActor<AStaticMeshActor>();
        Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Wall->SetActorLocation(D->GetActorLocation()+FVector(-200,0,68));
        Wall->SetActorScale3D(FVector(.2,2,2));
        Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        FApp::SetFixedDeltaTime(1.0/Rate); FApp::SetUseFixedTimeStep(true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FDroneTrainerFlightCheck::Tick));
    }
    bool Tick(float)
    {
        if (!Drone.IsValid()) { FApp::SetUseFixedTimeStep(false); delete this; return false; }
        auto* D=Drone.Get();
        const float Before=Time; Time+=1/Fps;
        const FVector P=D->GetActorLocation(), V=D->PhysicsBody->GetPhysicsLinearVelocity();
        const FRotator R=D->GetActorRotation();
        if (Before<.25f && Time>=.25f)
        {
            D->CameraArm->TickComponent(1/Fps,LEVELTICK_All,nullptr);
            const float Distance=FVector::Distance(D->ThirdPersonCamera->GetComponentLocation(),D->GetActorLocation());
            UE_LOG(LogTemp,Display,TEXT("E2_CAMERA distance=%.3f fixed=%d"),Distance,D->CameraArm->IsCollisionFixApplied());
            Require(Distance<220 && D->CameraArm->IsCollisionFixApplied(),TEXT("spring_arm_retracts_at_wall"));
            Wall->Destroy();
        }
        Samples += FString::Printf(TEXT("%.4f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f\n"),Time,P.X,P.Y,P.Z,D->AltitudeMeters,V.Size2D()/100,V.Z/100,R.Pitch,R.Roll,R.Yaw);
        if (Time<=10) PeakError=FMath::Max(PeakError,FMath::Abs(P.Z-300)/100);
        if (Before<10 && Time>=10)
        {
            HoverError=PeakError; Require(HoverError<=.5f,TEXT("hover_10s"));
            BrakeStart=P; D->PhysicsBody->SetPhysicsLinearVelocity(FVector(400,0,0));
        }
        if (Before>=10 && Time<=13)
        {
            BrakeDistance=FVector::Dist2D(P,BrakeStart)/100;
            if (V.Size2D()<50 && BrakeTime<0) BrakeTime=Time-10;
        }
        if (Before<13 && Time>=13) { Require(BrakeTime>=0 && BrakeTime<=3 && BrakeDistance<=8,TEXT("brake_4_to_0_5")); ForwardStart=P; }
        if (Before<23 && Time>=23) Require(P.X-ForwardStart.X>3000 && FMath::Abs(P.Y-ForwardStart.Y)<100,TEXT("forward_is_positive_X"));
        D->FlightCheckInput = FVector4(Time>=13 && Time<23 ? 1 : 0,0,Time>=41 && Time<43 ? 1 : 0,Time>=26 && Time<28.5 ? 1 : 0);
        if (Time>=13 && Time<23) PeakSpeed=FMath::Max(PeakSpeed,V.Size2D()/100);
        if (Before<26 && Time>=26) Require(FMath::Abs(R.Pitch)<1 && FMath::Abs(R.Roll)<1,TEXT("tilt_levels_after_release"));
        if (Before<30 && Time>=30) PeakError=0;
        if (Before<37 && Time>=37)
        {
            PausedPosition=P; PausedElapsed=D->ElapsedSeconds; D->TogglePause();
        }
        if (Before<37.3f && Time>=37.3f)
        {
            Require(P.Equals(PausedPosition,.01) && D->ElapsedSeconds==PausedElapsed,TEXT("pause_freezes_physics_and_time"));
            D->TogglePause();
        }
        if (Time>=30 && Time<=40) PeakError=FMath::Max(PeakError,FMath::Abs(P.Z-D->TargetAltitude)/100);
        if (Before<40 && Time>=40) Require(PeakError<=.5f,TEXT("vertical_release_hold_10s"));
        if (FParse::Param(FCommandLine::Get(),TEXT("Stage02Screenshots")))
        {
            if (Before<6 && Time>=6) D->ToggleCameraView();
            if ((Before<5 && Time>=5) || (Before<7 && Time>=7))
            {
                const FIntPoint Size=D->GetWorld()->GetGameViewport()->Viewport->GetSizeXY();
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Stage02-%dx%d-%s.png"),Size.X,Size.Y,D->bThirdPersonView?TEXT("chase"):TEXT("fpv")),false,false);
            }
        }
        if (Before<46 && Time>=46)
        {
            Require(PeakSpeed<=8.05f && PeakSpeed>3,TEXT("speed_cap_and_forward"));
            Require(FMath::Abs(R.Yaw)>30,TEXT("yaw_turns"));
            D->AttemptState=EDroneAttemptState::Paused;
            D->RestartAttempt();
        }
        if (Time>=46)
        {
            TakeoffReached |= D->bGroundValid && D->AltitudeMeters>=3;
            D->FlightCheckInput=FVector4(0,Time>=70 && Time<73 ? 1 : 0,0,Time>=49.5 && !TakeoffReached ? 1 : 0);
        }
        if (Before<49.25f && Time>=49.25f)
        {
            D->CameraArm->TickComponent(1/Fps,LEVELTICK_All,nullptr);
            Require(FVector::Dist(D->ThirdPersonCamera->GetComponentLocation(),D->GetActorLocation())>300 &&
                D->ThirdPersonCamera->GetComponentLocation().Z-D->GetActorLocation().Z>110,TEXT("chase_clear_of_ground_on_pad"));
        }
        if (Time>=60 && Time<=70) TakeoffHoldError=FMath::Max(TakeoffHoldError,FMath::Abs(P.Z-D->TargetAltitude)/100);
        const bool Screenshots=FParse::Param(FCommandLine::Get(),TEXT("Stage02Screenshots"));
        if (!Screenshots && Before<70 && Time>=70)
        {
            Require(TakeoffReached && D->bHasTakenOff && D->AttemptState==EDroneAttemptState::Running,TEXT("countdown_rest_takeoff_from_pad"));
            Require(TakeoffHoldError<=.5f,TEXT("takeoff_hold_10s"));
            StrafeStart=P;
        }
        if (!Screenshots && Before<73 && Time>=73)
        {
            Require(P.Y-StrafeStart.Y>150 && FMath::Abs(P.X-StrafeStart.X)<100,TEXT("strafe_is_positive_Y"));
            const ADroneTrainerGate* Gate=D->RouteGates[0].Get();
            D->PhysicsBody->SetSimulatePhysics(false);
            D->SetActorLocation(Gate->GetOpeningCenter()-Gate->GetActorForwardVector()*300+Gate->GetActorRightVector()*210);
            D->SetActorRotation(Gate->GetActorRotation());
            D->PhysicsBody->SetSimulatePhysics(true);
            D->PhysicsBody->SetPhysicsLinearVelocity(Gate->GetActorForwardVector()*800);
            D->PhysicsBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
            D->PreviousFlightPosition=D->GetActorLocation();
            D->TargetAltitude=D->GetActorLocation().Z;
        }
        if (Time < (Screenshots ? 8 : 74.5f)) return true;
        if (!Screenshots) Require(D->AttemptState==EDroneAttemptState::Failed && D->FailureReason==EDroneFailureReason::Collision,TEXT("dynamic_frame_collision"));
        const FString Base=FPaths::ProjectSavedDir()/FString::Printf(TEXT("Stage02-%d"),FMath::RoundToInt(Fps));
        FFileHelper::SaveStringToFile(Samples,*(Base+TEXT("-samples.csv")));
        const FString Result=FString::Printf(TEXT("status=%s\nscope=%s\nsimulation_fps=%.0f\nhover_error_m=%.4f\nrelease_hold_error_m=%.4f\ntakeoff_hold_error_m=%.4f\nbrake_time_s=%.4f\nbrake_distance_m=%.4f\npeak_forward_mps=%.4f\n"),Passed?TEXT("passed"):TEXT("failed"),Screenshots?TEXT("render_smoke"):TEXT("physics"),Fps,HoverError,PeakError,TakeoffHoldError,BrakeTime,BrakeDistance,PeakSpeed);
        FFileHelper::SaveStringToFile(Result,*(Base+TEXT("-result.txt")));
        UE_LOG(LogTemp,Display,TEXT("E2_FLIGHT_CHECK_COMPLETE %s %s"),*Base,Passed?TEXT("PASS"):TEXT("FAIL"));
        D->bFlightCheckActive=false; FApp::SetUseFixedTimeStep(false);
        if (FParse::Param(FCommandLine::Get(),TEXT("Stage02AutoExit"))) FPlatformMisc::RequestExit(false);
        delete this; return false;
    }
};
static FAutoConsoleCommandWithWorldAndArgs CheckFlight(TEXT("DroneTrainer.CheckFlight"),TEXT("Development physics acceptance: 30, 60 or 120 simulation FPS"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    {
        const float Rate=Args.Num()?FCString::Atof(*Args[0]):60;
        if (World && World->IsGameWorld() && (Rate==30 || Rate==60 || Rate==120)) (new FDroneTrainerFlightCheck)->Start(World,Rate);
    }));
#endif
