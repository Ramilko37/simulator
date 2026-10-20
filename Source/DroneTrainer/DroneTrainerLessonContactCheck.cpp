#if WITH_DEV_AUTOMATION_TESTS
#include "DroneTrainerDronePawn.h"
#include "DroneTrainerLevelLayout.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/CoreDelegates.h"
#include "Framework/Application/SlateApplication.h"

// Real solver contact check; only fixture placement is teleported. Not a route flight.
class FDroneTrainerLessonContactCheck
{
public:
    TWeakObjectPtr<ADroneTrainerDronePawn> Drone;
    int Phase=0;
    const float Rates[3]={30,60,120};
    float Time=0;
    bool Passed=true;
    FString Samples=TEXT("case,time_s,z_cm,vz_cm_s,state,landing_s\n");
    void Prepare(int Case)
    {
        const int RateIndex=Case/12; Case%=12;
        FApp::SetFixedDeltaTime(1./Rates[RateIndex]);
        const bool Hard=Case==2 || Case==3 || Case==7;
        const bool Controlled=Case==1 || (Case>=4 && Case<=6);
        auto* D=Drone.Get();
        D->AttemptState=EDroneAttemptState::Paused; D->RestartAttempt();
        if ((Case==8 || Case==9))
        {
            // Exercise the real countdown and initial support, without fixture teleport/descent.
            if (FParse::Param(FCommandLine::Get(),TEXT("Stage05StartLegacyGap")))
            {
                const FTransform Saved=D->AttemptStartTransform;
                D->AttemptStartTransform.AddToTranslation(FVector(0,0,3-D->StartGroundClearance));
                D->AttemptState=EDroneAttemptState::Paused; D->RestartAttempt();
                D->AttemptStartTransform=Saved;
            }
            UE_LOG(LogTemp,Display,TEXT("LESSON_START_SETUP case=%d agl_cm=%.4f gravity=%d"),Case,D->AltitudeMeters*100,D->PhysicsBody->IsGravityEnabled());
            D->bFlightCheckActive=true; D->FlightCheckInput=FVector4(0,0,0,0); Time=0;
            return;
        }
        D->PhysicsBody->SetSimulatePhysics(false);
        const FBox Pad=D->Layout->LandingPad->GetComponentsBoundingBox();
        FVector Site(Pad.GetCenter().X,Pad.GetCenter().Y,Pad.Max.Z);
        if (Case==4) { Site=D->AttemptStartTransform.GetLocation(); Site.Z-=D->PhysicsBody->GetScaledBoxExtent().Z+D->StartGroundClearance; }
        if (Case==5 || Case==7)
        {
            Site=D->AttemptStartTransform.GetLocation()+FVector(1000,0,1000);
            FHitResult Ground;
            if (!WTrace(D,Site,Ground)) { Passed=false; }
            else Site=Ground.ImpactPoint;
        }
        D->SetActorLocation(Site+FVector(0,0,10+(Hard ? 15 : Controlled ? 290 : .1)),false,nullptr,ETeleportType::TeleportPhysics);
        D->SetActorRotation(Case>=10 ? FRotator(0,269,0) : FRotator::ZeroRotator);
        D->PhysicsBody->SetSimulatePhysics(true); D->PhysicsBody->SetEnableGravity(true);
        D->PhysicsBody->SetPhysicsLinearVelocity(FVector(0,0,Case==3 ? -10000 : Hard ? -200 : 0)); D->PhysicsBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
        if (Case>=10)
        {
            D->PhysicsBody->SetPhysicsLinearVelocity(FVector(Case==10 ? 33.46 : 0,Case==11 ? 33.46 : 0,-19.99));
        }
        D->AttemptState=EDroneAttemptState::Running; D->LessonPhase=Case>=4 && Case<10 ? EDroneLessonPhase::Route : EDroneLessonPhase::Landing; D->GatesPassed=Case>=4 && Case<10 ? 0 : 5; D->bHasTakenOff=true;
        D->bFlightCheckActive=true; D->bAltitudeHoldActive=Controlled; D->TargetAltitude=D->GetActorLocation().Z; D->FlightCheckInput=FVector4(0,0,0,Controlled ? -1 : 0);
        D->PreviousFlightPosition=D->GetActorLocation(); Time=0;
    }
    bool WTrace(ADroneTrainerDronePawn* D,FVector P,FHitResult& Hit)
    {
        return D->GetWorld()->LineTraceSingleByChannel(Hit,P,P-FVector(0,0,3000),ECC_GameTraceChannel1,
            FCollisionQueryParams(SCENE_QUERY_STAT(ContactFixtureGround),false,D));
    }
    void Start(UWorld* W)
    {
        for (TActorIterator<ADroneTrainerDronePawn> It(W);It;++It) Drone=*It;
        if (!Drone.IsValid()) { delete this; return; }
        // Numeric solver fixture has no interactive window. Native focus is manual QA.
        FCoreDelegates::ApplicationWillDeactivateDelegate.RemoveAll(Drone.Get());
        FCoreDelegates::ApplicationWillEnterBackgroundDelegate.RemoveAll(Drone.Get());
        if (FSlateApplication::IsInitialized()) FSlateApplication::Get().OnApplicationActivationStateChanged().RemoveAll(Drone.Get());
        FApp::SetFixedDeltaTime(1./120); FApp::SetUseFixedTimeStep(true);
        Prepare(0);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FDroneTrainerLessonContactCheck::Tick));
    }
    bool Tick(float)
    {
        if (!Drone.IsValid()) { FApp::SetUseFixedTimeStep(false); delete this; return false; }
        auto* D=Drone.Get(); Time+=1.f/Rates[Phase/12];
        Samples+=FString::Printf(TEXT("%d,%.4f,%.4f,%.4f,%d,%.4f\n"),Phase,Time,D->GetActorLocation().Z,D->PhysicsBody->GetPhysicsLinearVelocity().Z,int(D->AttemptState),D->LandingSeconds);
        const int Case=Phase%12;
        bool Success=false;
        if ((Case==8 || Case==9))
        {
            if (D->AttemptState==EDroneAttemptState::Running && D->ElapsedSeconds==0)
                UE_LOG(LogTemp,Display,TEXT("LESSON_START_RELEASE case=%d agl_cm=%.4f vz=%.4f"),Case,D->AltitudeMeters*100,D->PhysicsBody->GetPhysicsLinearVelocity().Z);
            if (Case==9 && Time>5) D->FlightCheckInput.W=1;
            const bool TakingOff=Case==9 && D->LessonPhase==EDroneLessonPhase::Route && D->AltitudeMeters>=3;
            const bool Active=D->AttemptState==EDroneAttemptState::Countdown || D->AttemptState==EDroneAttemptState::Running;
            if (Active && Time<(Case==8 ? 8 : 12) && !TakingOff) return true;
            Success=D->AttemptState==EDroneAttemptState::Running && D->GatesPassed==0 &&
                (Case==8 ? D->bLandingSettled && !D->bHasTakenOff && D->PhysicsBody->GetPhysicsLinearVelocity().Size()<10 : TakingOff);
        }
        else
        {
            const bool Resting=Case>=4 && Case<=6 && Time>2 && D->bLandingSettled && D->PhysicsBody->GetPhysicsLinearVelocity().Size()<10;
            if (Time<22 && D->AttemptState==EDroneAttemptState::Running && !Resting) return true;
            Success=(Case<2 || Case>=10) ? D->AttemptState==EDroneAttemptState::Completed :
                Case>=4 && Case<=6 ? Resting && D->AttemptState==EDroneAttemptState::Running && D->GatesPassed==0 :
                D->AttemptState==EDroneAttemptState::Failed && D->FailureReason==EDroneFailureReason::Collision;
        }
        Passed &= Success;
        const TCHAR* Names[]={TEXT("soft_actual_Chaos"),TEXT("controlled_descent"),TEXT("hard_actual_Chaos"),TEXT("hard_CCD"),
            TEXT("return_to_start"),TEXT("terrain_touchdown"),TEXT("pad_before_gates"),TEXT("hard_terrain"),TEXT("idle_start"),TEXT("start_then_takeoff"),TEXT("soft_drift_x_yaw269"),TEXT("soft_drift_y_yaw269")};
        UE_LOG(LogTemp,Display,TEXT("%s %s fps=%.0f %s"),(Case==8 || Case==9) ? TEXT("LESSON_START_CHECK") : TEXT("LESSON_CONTACT_CHECK"),Names[Case],Rates[Phase/12],Success ? TEXT("PASS") : TEXT("FAIL"));
        if (++Phase<36) { Prepare(Phase); return true; }
        FFileHelper::SaveStringToFile(Samples,*(FPaths::ProjectSavedDir()/TEXT("Stage03-contacts.csv")));
        UE_LOG(LogTemp,Display,TEXT("LESSON_CONTACT_COMPLETE %s"),Passed ? TEXT("PASS") : TEXT("FAIL"));
        D->bFlightCheckActive=false; FApp::SetUseFixedTimeStep(false);
        if (FParse::Param(FCommandLine::Get(),TEXT("Stage03AutoExit"))) FPlatformMisc::RequestExitWithStatus(false,Passed ? 0 : 1);
        delete this; return false;
    }
};
static FAutoConsoleCommandWithWorld ContactCommand(TEXT("DroneTrainer.CheckLessonContacts"),TEXT("E3 real solver soft/hard landing fixtures"),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W) { if (W && W->IsGameWorld()) (new FDroneTrainerLessonContactCheck)->Start(W); }));
#endif
