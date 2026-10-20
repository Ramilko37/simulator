#include "DroneTrainerDronePawn.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "DroneTrainerGate.h"
#include "DroneTrainerLevelLayout.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Engine.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Development benchmark driver: only production input axes, camera and restart actions.
// No teleports, fixture time steps, collision exemptions or direct lesson-state writes.
void ADroneTrainerDronePawn::TickAutoRoute(float DeltaSeconds)
{
    const double Now=FPlatformTime::Seconds();
    if (Now-AutoProgress>=10)
    {
        AutoProgress=Now;
        UE_LOG(LogTemp,Display,TEXT("AUTO_ROUTE_PROGRESS wall=%.1f laps=%d state=%d gate=%d leg=%d position=%s speed=%.2f agl=%.2f"),
            Now-AutoStarted,AutoLaps,int32(AttemptState),GatesPassed,AutoLeg,*GetActorLocation().ToString(),HorizontalSpeed,AltitudeMeters);
    }
    FlightCheckInput=FVector4(0,0,0,0);
    if (AttemptState==EDroneAttemptState::Failed || AttemptState==EDroneAttemptState::Error)
    {
        UE_LOG(LogTemp,Error,TEXT("AUTO_ROUTE_FAILED reason=%d detail=%s"),int32(FailureReason),*FailureDetail);
        FPlatformMisc::RequestExit(false); return;
    }
    if (AttemptState==EDroneAttemptState::Completed)
    {
        ++AutoLaps;
        UE_LOG(LogTemp,Display,TEXT("AUTO_ROUTE_LAP_COMPLETE lap=%d lesson_seconds=%.3f gates=%d"),AutoLaps,ElapsedSeconds,GatesPassed);
        if (FParse::Param(FCommandLine::Get(),TEXT("Stage06OneLap"))) { FPlatformMisc::RequestExit(false); return; }
        RestartAttempt(); AutoGate=0; AutoLeg=0; AutoSurvey=0; return;
    }
    if (AttemptState!=EDroneAttemptState::Running) return;
    const FVector Position=PhysicsBody->GetComponentLocation(), Velocity=PhysicsBody->GetPhysicsLinearVelocity();
    FVector Target=AttemptStartTransform.GetLocation()+FVector(0,0,350);
    float YawTarget=GetActorRotation().Yaw;
    bool Descend=false;
    if (LessonPhase==EDroneLessonPhase::Route)
    {
        // A full slow panorama at the start records the forest belt and preparation area.
        if (AutoSurvey<12)
        {
            AutoSurvey+=DeltaSeconds;
            FlightCheckInput.Z=1.f/3;
            if (AutoSurvey>=12) UE_LOG(LogTemp,Display,TEXT("AUTO_ROUTE_SURVEY_COMPLETE"));
        }
        else
        {
            if (AutoLeg==2 && AutoGate<GatesPassed &&
                FVector::DotProduct(Position-RouteGates[AutoGate]->GetOpeningCenter(),RouteGates[AutoGate]->GetActorForwardVector())>250)
            { AutoGate=GatesPassed; AutoLeg=0; }
            if (RouteGates.IsValidIndex(AutoGate))
            {
                auto* Gate=RouteGates[AutoGate].Get();
                Target=Gate->GetOpeningCenter()+Gate->GetActorForwardVector()*(AutoLeg==2 ? 450 : -450);
                if (AutoLeg==0 && FVector::Dist2D(Position,Target)<70 && FMath::Abs(Position.Z-Target.Z)<35 && Velocity.Size2D()<100)
                { AutoLeg=2; Target=Gate->GetOpeningCenter()+Gate->GetActorForwardVector()*450; }
                YawTarget=Gate->GetActorRotation().Yaw;
                const bool Chase=(AutoGate==2 || AutoGate==3);
                if (Chase!=bThirdPersonView)
                {
                    ToggleCameraView(); UE_LOG(LogTemp,Display,TEXT("AUTO_ROUTE_CAMERA chase=%d"),Chase);
                    if (AutoLaps==0) GEngine->Exec(GetWorld(),TEXT("DroneTrainer.MaterialDiagnostics"));
                }
            }
        }
    }
    if (LessonPhase==EDroneLessonPhase::Landing)
    {
        Target=Layout->LandingPad->GetComponentsBoundingBox().GetCenter();
        Target.Z=Layout->LandingPad->GetComponentsBoundingBox().Max.Z+350;
        if (FVector::Dist2D(Position,Target)<35 && Velocity.Size2D()<20) AutoLeg=3;
        Descend=AutoLeg==3;
    }
    const FVector Error=Target-Position;
    const FVector DesiredVelocity=FVector(Error.X,Error.Y,0).GetClampedToMaxSize(400)*0.65;
    const FVector Acceleration=(DesiredVelocity-FVector(Velocity.X,Velocity.Y,0))*1.5+FVector(Velocity.X,Velocity.Y,0)*0.45;
    const FVector Local=FRotator(0,GetActorRotation().Yaw,0).UnrotateVector(Acceleration);
    FlightCheckInput.X=FMath::Clamp(Local.X/350.,-.7,.7);
    FlightCheckInput.Y=FMath::Clamp(Local.Y/350.,-.7,.7);
    if (!(LessonPhase==EDroneLessonPhase::Route && AutoSurvey<12))
        FlightCheckInput.Z=FMath::Clamp(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,YawTarget)/90.,-.5,.5);
    // Feed the existing target-altitude integrator, including its landing flare.
    FlightCheckInput.W=Descend ? -1 : FMath::Clamp((Target.Z-(bAltitudeHoldActive ? TargetAltitude : Position.Z))/200.,-1.,1.);
    if (bLandingSettled) FlightCheckInput=FVector4(0,0,0,0);
}
#endif
