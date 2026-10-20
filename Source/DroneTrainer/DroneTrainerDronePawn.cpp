#include "DroneTrainerDronePawn.h"
#include "DroneTrainerSettings.h"
#include "DroneTrainerHUD.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "DroneTrainerLesson.h"
#include "DroneTrainerLevelLayout.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerStart.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/SimCallbackObject.h"
#include "Chaos/ContactModification.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PBDRigidsSolver.h"

#include "Camera/CameraComponent.h"
#include "DroneTrainerGate.h"
#include "DroneTrainerLevelLayout.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Framework/Application/SlateApplication.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UObject/ConstructorHelpers.h"

// The engine freezes the game thread for this callback. Read solver contact velocities
// before impulses; retain every substep so a hard first impact cannot be hidden by rest.
class FDroneContactCapture : public Chaos::TSimCallbackObject<Chaos::FSimCallbackNoInput,
    Chaos::FSimCallbackNoOutput, Chaos::ESimCallbackOptions::ContactModification | Chaos::ESimCallbackOptions::PostIntegrate | Chaos::ESimCallbackOptions::RunOnFrozenGameThread>
{
public:
    ADroneTrainerDronePawn* Pawn;
    FVector PreContactVelocity = FVector::ZeroVector;
    explicit FDroneContactCapture(ADroneTrainerDronePawn* InPawn) : Pawn(InPawn) {}
    virtual void OnPreSimulate_Internal() override {}
    virtual void OnPostIntegrate_Internal() override
    {
        // Per-substep, after forces but before CCD/solver impulses. Contact modification
        // alone is too late for CCD, which can already have changed particle velocity.
        PreContactVelocity = FVector(NAN,NAN,NAN);
        if (const auto Proxy=Pawn->PhysicsBody->BodyInstance.GetPhysicsActor())
            if (const auto* Body=Proxy->GetPhysicsThreadAPI()) PreContactVelocity=Body->V();
    }
    virtual void OnContactModification_Internal(Chaos::FCollisionContactModifier& Modifier) override
    {
        if (Pawn->AttemptState != EDroneAttemptState::Running) return;
        const auto Body = Pawn->PhysicsBody->BodyInstance.GetPhysicsActor();
        const auto Pad = Pawn->GetLandingPad() ? Pawn->GetLandingPad()->GetStaticMeshComponent()->BodyInstance.GetPhysicsActor() : nullptr;
        for (auto& Pair : Modifier)
        {
            const auto Particles = Pair.GetParticlePair();
            const int32 I = Particles[0]->PhysicsProxy() == Body ? 0 : Particles[1]->PhysicsProxy() == Body ? 1 : -1;
            if (I < 0 || Pair.GetIsProbe()) continue;
            for (int32 J=0; J<Pair.GetNumContacts(); ++J)
            {
                if (Pair.IsContactPointDisabled(J) || Pair.GetSeparation(J) > 0) continue;
                ADroneTrainerDronePawn::FContactSample C;
                C.Position = Pair.GetParticlePosition(I); C.Rotation = Pair.GetParticleRotation(I);
                C.Velocity = PreContactVelocity;
                C.Normal = Pair.GetWorldNormal(J) * (I == 0 ? 1 : -1);
                C.Point = Pair.GetWorldContactLocation(J);
                C.bLandingPad = Particles[1-I]->PhysicsProxy() == Pad;
                const auto* Other = Pawn->GetWorld()->GetPhysicsScene()->GetOwningComponent<UPrimitiveComponent>(Particles[1-I]->PhysicsProxy());
                C.bGroundSurface = Other && Other->GetCollisionResponseToChannel(ECC_GameTraceChannel1)==ECR_Block;
                Pawn->PendingContacts.Add(C);
            }
        }
    }
};


ADroneTrainerDronePawn::ADroneTrainerDronePawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;

    PhysicsBody = CreateDefaultSubobject<UBoxComponent>(TEXT("PhysicsBody"));
    SetRootComponent(PhysicsBody);
    PhysicsBody->SetBoxExtent(FVector(30.0f, 30.0f, 10.0f));
    PhysicsBody->SetCollisionProfileName(TEXT("PhysicsActor"));
    PhysicsBody->SetSimulatePhysics(true);
    PhysicsBody->SetEnableGravity(true);
    PhysicsBody->SetLinearDamping(0.0f);
    PhysicsBody->SetAngularDamping(0.05f);
    PhysicsBody->SetUseCCD(true);
    PhysicsBody->BodyInstance.SetContactModification(true);
    PhysicsBody->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
    PhysicsBody->SetNotifyRigidBodyCollision(true);
    PhysicsBody->OnComponentHit.AddDynamic(this, &ADroneTrainerDronePawn::HandleBodyHit);

    VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DroneVisual"));
    VisualMesh->SetupAttachment(PhysicsBody);
    VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);


    static ConstructorHelpers::FObjectFinder<UStaticMesh> DroneMeshAsset(
        TEXT("/Game/DroneTrainer/Meshes/SM_TrainingDrone.SM_TrainingDrone"));
    if (DroneMeshAsset.Succeeded())
    {
        VisualMesh->SetStaticMesh(DroneMeshAsset.Object);
        const FBox Bounds = DroneMeshAsset.Object->GetBoundingBox();
        const FVector Scale = FVector(60, 60, 20) / Bounds.GetSize();
        VisualMesh->SetRelativeScale3D(Scale);
        VisualMesh->SetRelativeLocation(-Bounds.GetCenter() * Scale);
    }

    Ambience=CreateDefaultSubobject<UAudioComponent>(TEXT("OutdoorAmbience"));
    Ambience->SetupAttachment(PhysicsBody);
    Ambience->bAllowSpatialization=false;
    Ambience->bAutoActivate=false;
    static ConstructorHelpers::FObjectFinder<USoundWave> Wind(TEXT("/Game/TrainingField/Audio/S03_OutdoorWind.S03_OutdoorWind"));
    if (Wind.Succeeded()) Ambience->SetSound(Wind.Object);

    Motors=CreateDefaultSubobject<UAudioComponent>(TEXT("Motors"));
    Motors->SetupAttachment(PhysicsBody); Motors->bAutoActivate=false; Motors->bAllowSpatialization=false;
    static ConstructorHelpers::FObjectFinder<USoundWave> MotorSound(TEXT("/Game/TrainingField/Audio/S01_Motors.S01_Motors"));
    if (MotorSound.Succeeded()) Motors->SetSound(MotorSound.Object);
    LessonAudio=CreateDefaultSubobject<UAudioComponent>(TEXT("LessonSignals"));
    LessonAudio->SetupAttachment(PhysicsBody); LessonAudio->bAutoActivate=false; LessonAudio->bAllowSpatialization=false;
    static ConstructorHelpers::FObjectFinder<USoundWave> GateSound(TEXT("/Game/TrainingField/Audio/S02_Gate.S02_Gate"));
    static ConstructorHelpers::FObjectFinder<USoundWave> WarningSound(TEXT("/Game/TrainingField/Audio/S02_Warning.S02_Warning"));
    static ConstructorHelpers::FObjectFinder<USoundWave> SuccessSound(TEXT("/Game/TrainingField/Audio/S02_Success.S02_Success"));
    static ConstructorHelpers::FObjectFinder<USoundWave> FailureSound(TEXT("/Game/TrainingField/Audio/S02_Failure.S02_Failure"));
    LessonSounds={GateSound.Object,WarningSound.Object,SuccessSound.Object,FailureSound.Object};

    FpvCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FpvCamera"));
    FpvCamera->SetupAttachment(PhysicsBody);
    FpvCamera->SetRelativeLocation(FVector(35.0f, 0.0f, 8.0f));
    FpvCamera->SetFieldOfView(100.0f);
    FpvCamera->PostProcessSettings.bOverride_BloomIntensity = true;
    FpvCamera->PostProcessSettings.BloomIntensity = 0;
    FpvCamera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    FpvCamera->PostProcessSettings.MotionBlurAmount = 0;
    FpvCamera->PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
    FpvCamera->PostProcessSettings.DepthOfFieldFocalDistance = 0;
    FpvCamera->PostProcessSettings.bOverride_SceneFringeIntensity = true;
    FpvCamera->PostProcessSettings.SceneFringeIntensity = 0;
    FpvCamera->PostProcessSettings.bOverride_VignetteIntensity = true;
    FpvCamera->PostProcessSettings.VignetteIntensity = 0;

    CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    CameraArm->SetupAttachment(PhysicsBody);
    CameraArm->TargetArmLength = 350.0f;
    CameraArm->TargetOffset = FVector(0, 0, 20);
    CameraArm->SocketOffset = FVector(0, 0, 100);
    CameraArm->bInheritPitch = false;
    CameraArm->bInheritRoll = false;
    CameraArm->bDoCollisionTest = true;
    CameraArm->ProbeSize = 12.0f;
    ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdPersonCamera"));
    ThirdPersonCamera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
    ThirdPersonCamera->SetFieldOfView(90.0f);
    ThirdPersonCamera->PostProcessSettings = FpvCamera->PostProcessSettings;
    ThirdPersonCamera->SetAutoActivate(false);
}

void ADroneTrainerDronePawn::BeginPlay()
{
    Super::BeginPlay();
    for (TActorIterator<ADroneTrainerGate> Gate(GetWorld()); Gate; ++Gate) RouteGates.Add(*Gate);
    RouteGates.Sort([](const ADroneTrainerGate& A, const ADroneTrainerGate& B) { return A.GateIndex < B.GateIndex; });
    TActorIterator<ADroneTrainerLevelLayout> LayoutIt(GetWorld());
    if (LayoutIt) Layout = *LayoutIt;
    AttemptStartTransform = GetActorTransform();
    AttemptStartTransform.SetRotation(FRotator(0, GetActorRotation().Yaw, 0).Quaternion());
    FHitResult Ground;
    const FVector Start = AttemptStartTransform.GetLocation();
    if (GetWorld()->LineTraceSingleByChannel(Ground, Start + FVector(0,0,100),
        Start - FVector(0,0,20000), ECC_GameTraceChannel1, FCollisionQueryParams(SCENE_QUERY_STAT(StartGround), false, this)))
    {
        AttemptStartTransform.SetLocation(FVector(Start.X, Start.Y, Ground.ImpactPoint.Z + PhysicsBody->GetScaledBoxExtent().Z + StartGroundClearance));
        StartSurface = Ground.GetActor();
    }
    BasicStartTransform=AttemptStartTransform;
    BasicStartSurface=StartSurface;
    const FName PointTags[]={TEXT("L2_A2"),TEXT("L2_P1"),TEXT("L2_P2"),TEXT("L2_P3"),TEXT("L2_B2")};
    PrecisionObjects.SetNum(5);
    for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
        for (int32 I=0; I<5; ++I) if (It->ActorHasTag(PointTags[I])) PrecisionObjects[I]=*It;
    PhysicsBody->SetSimulatePhysics(false);
    SetActorTransform(AttemptStartTransform);
    FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &ADroneTrainerDronePawn::HandleFocusLost);
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &ADroneTrainerDronePawn::HandleFocusLost);
    if (FSlateApplication::IsInitialized())
        FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this, &ADroneTrainerDronePawn::HandleApplicationActivation);
    ContactCapture = GetWorld()->GetPhysicsScene()->GetSolver()->CreateAndRegisterSimCallbackObject_External<FDroneContactCapture>(this);
    UpdateTelemetry();
    PhysicsBody->SetMassOverrideInKg(NAME_None, 2.0f, true);
    TargetAltitude = PhysicsBody->GetComponentLocation().Z;
    StartAltitude = TargetAltitude;
    bAltitudeHoldActive = false;
    PhysicsBody->SetEnableGravity(false);
    PreviousFlightPosition = PhysicsBody->GetComponentLocation();
    if (auto* Settings=Cast<UDroneTrainerSettings>(GEngine->GetGameUserSettings()))
    {
        int32 Quality=1;
        if (FParse::Value(FCommandLine::Get(),TEXT("Stage06Quality="),Quality)) Settings->SelectProfile(Quality);
        else Settings->ApplyNonResolutionSettings();
    }
    AttemptState = EDroneAttemptState::MainMenu;
#if WITH_DEV_AUTOMATION_TESTS
    bAutoRoute=FParse::Param(FCommandLine::Get(),TEXT("Stage06AutoRoute"));
    if (bAutoRoute)
    {
        bFlightCheckActive=true; AutoStarted=FPlatformTime::Seconds();
        RestartAttempt();
        UE_LOG(LogTemp,Display,TEXT("AUTO_ROUTE_START real_physics=1 fixed_timestep=0 version=1"));
    }
#endif
    UpdateGateVisuals();
    if (!ValidateLesson()) FinishAttempt(EDroneAttemptState::Error, EDroneFailureReason::RuntimeError);
#if CSV_PROFILER
    if (FParse::Param(FCommandLine::Get(),TEXT("Stage06Profile")))
    {
        const bool Smoke=FParse::Param(FCommandLine::Get(),TEXT("Stage06ProfileSmoke"));
        const double Warmup=Smoke ? 2 : 120, Duration=Smoke ? 3 : 900;
        PerformanceTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
            [this,Warmup,Duration,Started=FPlatformTime::Seconds(),CapturedAt=0.,LastState=AttemptState,Finished=TSharedFuture<FString>()](float) mutable
        {
            const double Now=FPlatformTime::Seconds();
            if (CapturedAt==0)
            {
                PerformanceStatus=FString::Printf(TEXT("Прогрев %.0f / %.0f с. Запись начнётся автоматически"),Now-Started,Warmup);
                if (Now-Started<Warmup) return true;
                if (FCsvProfiler::IsCapturing())
                {
                    PerformanceStatus=TEXT("Запись уже идёт: автоматический запуск отменён");
                    return false;
                }
                FCsvProfiler::Get()->BeginCapture();
                bPerformanceCaptureOwned=true; CapturedAt=Now; LastState=AttemptState;
                UE_LOG(LogTemp,Display,TEXT("PERFORMANCE_CAPTURE_START warmup=%.1f duration=%.1f"),Now-Started,Duration);
                CSV_EVENT_GLOBAL(TEXT("AttemptState=%d"),int32(AttemptState));
                return true;
            }
            if (Finished.IsValid())
            {
                if (!Finished.IsReady()) return true;
                PerformanceStatus=TEXT("Запись сохранена. Можно выйти через меню");
                UE_LOG(LogTemp,Display,TEXT("PERFORMANCE_CAPTURE_COMPLETE file=%s"),*Finished.Get());
#if WITH_DEV_AUTOMATION_TESTS
                if (bAutoRoute) FPlatformMisc::RequestExit(false);
#endif
                return false;
            }
            if (!FCsvProfiler::IsCapturing())
            {
                bPerformanceCaptureOwned=false;
                PerformanceStatus=TEXT("Запись прервана или не началась. Проверьте лог");
                UE_LOG(LogTemp,Warning,TEXT("PERFORMANCE_CAPTURE_INTERRUPTED elapsed=%.1f"),Now-CapturedAt);
                return false;
            }
            if (LastState!=AttemptState)
            {
                CSV_EVENT_GLOBAL(TEXT("AttemptState=%d gates=%d lesson_seconds=%.3f"),int32(AttemptState),GatesPassed,ElapsedSeconds);
                UE_LOG(LogTemp,Display,TEXT("PERFORMANCE_STATE elapsed=%.1f state=%d gates=%d"),Now-CapturedAt,int32(AttemptState),GatesPassed);
                LastState=AttemptState;
            }
            PerformanceStatus=FString::Printf(TEXT("Запись %.0f / %.0f с. Летайте; паузы отмечаются"),Now-CapturedAt,Duration);
            if (Now-CapturedAt<Duration) return true;
            Finished=FCsvProfiler::Get()->EndCapture(); bPerformanceCaptureOwned=false;
            PerformanceStatus=TEXT("Сохранение записи… дождитесь завершения");
            UE_LOG(LogTemp,Display,TEXT("PERFORMANCE_CAPTURE_STOP elapsed=%.1f"),Now-CapturedAt);
            return true;
        }),1.f);
    }
#endif


    if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
    {
        PlayerController->SetInputMode(FInputModeGameOnly());
        PlayerController->bShowMouseCursor = false;
    }
}

void ADroneTrainerDronePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    PlayerInputComponent->BindKey(EKeys::Left,IE_Pressed,this,&ADroneTrainerDronePawn::SelectBasicLesson).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::Right,IE_Pressed,this,&ADroneTrainerDronePawn::SelectPrecisionLesson).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &ADroneTrainerDronePawn::ToggleCameraView);
    PlayerInputComponent->BindKey(EKeys::One,IE_Pressed,this,&ADroneTrainerDronePawn::SelectMediumQuality).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::Two,IE_Pressed,this,&ADroneTrainerDronePawn::SelectLowQuality).bExecuteWhenPaused=true;

    PlayerInputComponent->BindKey(EKeys::O,IE_Pressed,this,&ADroneTrainerDronePawn::ToggleSettings).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::Three,IE_Pressed,this,&ADroneTrainerDronePawn::LowerSensitivity).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::Four,IE_Pressed,this,&ADroneTrainerDronePawn::RaiseSensitivity).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::Five,IE_Pressed,this,&ADroneTrainerDronePawn::LowerVolume).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::Six,IE_Pressed,this,&ADroneTrainerDronePawn::RaiseVolume).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::F,IE_Pressed,this,&ADroneTrainerDronePawn::ChangeWindowMode).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::V,IE_Pressed,this,&ADroneTrainerDronePawn::ChangeResolution).bExecuteWhenPaused=true;
    FInputKeyBinding& PauseBinding = PlayerInputComponent->BindKey(
        EKeys::Escape, IE_Pressed, this, &ADroneTrainerDronePawn::TogglePause);
    PauseBinding.bExecuteWhenPaused = true;

    FInputKeyBinding& ConfirmBinding = PlayerInputComponent->BindKey(
        EKeys::Enter, IE_Pressed, this, &ADroneTrainerDronePawn::ConfirmMenuAction);
    ConfirmBinding.bExecuteWhenPaused = true;

    FInputKeyBinding& RestartBinding = PlayerInputComponent->BindKey(
        EKeys::R, IE_Pressed, this, &ADroneTrainerDronePawn::RestartAttempt);
    RestartBinding.bExecuteWhenPaused = true;

    FInputKeyBinding& MenuBinding = PlayerInputComponent->BindKey(
        EKeys::M, IE_Pressed, this, &ADroneTrainerDronePawn::ReturnToMainMenu);
    MenuBinding.bExecuteWhenPaused = true;

    FInputKeyBinding& ExitBinding = PlayerInputComponent->BindKey(
        EKeys::Q, IE_Pressed, this, &ADroneTrainerDronePawn::ExitGame);
    ExitBinding.bExecuteWhenPaused = true;
}

void ADroneTrainerDronePawn::ToggleCameraView()
{
    bThirdPersonView = !bThirdPersonView;
    FpvCamera->SetActive(!bThirdPersonView);
    ThirdPersonCamera->SetActive(bThirdPersonView);
}

void ADroneTrainerDronePawn::SelectLowQuality() { SelectQuality(0); }
void ADroneTrainerDronePawn::SelectMediumQuality() { SelectQuality(1); }
void ADroneTrainerDronePawn::SelectQuality(int32 Value)
{
    if (AttemptState!=EDroneAttemptState::MainMenu && AttemptState!=EDroneAttemptState::Paused) return;
    if (auto* Settings=Cast<UDroneTrainerSettings>(GEngine->GetGameUserSettings())) Settings->SelectProfile(Value);
}

void ADroneTrainerDronePawn::TogglePause()
{
    if (bSettingsOpen) { bSettingsOpen=false; return; }
    if (!GetWorld())
    {
        return;
    }

    if (AttemptState == EDroneAttemptState::MainMenu)
    {
        return;
    }

    if (AttemptState == EDroneAttemptState::Completed || AttemptState == EDroneAttemptState::Failed ||
        AttemptState == EDroneAttemptState::Aborted || AttemptState == EDroneAttemptState::Error)
    {
        ReturnToMainMenu();
        return;
    }

    ClearFlightInput();
    if (UGameplayStatics::IsGamePaused(GetWorld()))
    {
        UGameplayStatics::SetGamePaused(GetWorld(), false);
        PreviousFlightPosition = PhysicsBody->GetComponentLocation();
        PreviousFlightRotation = PhysicsBody->GetComponentQuat();
        AttemptState = StateBeforePause;
        return;
    }

    StopAttemptAudio();
    StateBeforePause = AttemptState;
    AttemptState = EDroneAttemptState::Paused;
    UGameplayStatics::SetGamePaused(GetWorld(), true);
}

void ADroneTrainerDronePawn::ConfirmMenuAction()
{
    if (AttemptState == EDroneAttemptState::MainMenu && !bSettingsOpen)
    {
        RestartAttempt();
    }
}

void ADroneTrainerDronePawn::SelectBasicLesson() { SelectLesson(false); }
void ADroneTrainerDronePawn::SelectPrecisionLesson() { SelectLesson(true); }
void ADroneTrainerDronePawn::SelectLesson(bool Precision)
{
    if (AttemptState!=EDroneAttemptState::MainMenu || bSettingsOpen) return;
    ReturnToMainMenu();
    bPrecisionLesson=Precision;
    AttemptStartTransform=BasicStartTransform; StartSurface=BasicStartSurface;
    if (Precision && ValidatePrecisionObjects())
    {
        auto* A=PrecisionObjects[0].Get();
        const FBox Box=A->GetComponentsBoundingBox(true);
        AttemptStartTransform=FTransform(FRotator(0,A->GetActorRotation().Yaw,0),
            FVector(Box.GetCenter().X,Box.GetCenter().Y,Box.Max.Z+PhysicsBody->GetScaledBoxExtent().Z+StartGroundClearance));
        StartSurface=A;
    }
    SetActorTransform(AttemptStartTransform); UpdateTelemetry(); UpdateGateVisuals();
    if (!ValidateLesson()) FailureDetail=TEXT("Урок недоступен: проверьте комплект контрольных объектов");
}

AStaticMeshActor* ADroneTrainerDronePawn::GetLandingPad() const
{
    return bPrecisionLesson ? (PrecisionObjects.IsValidIndex(4) ? PrecisionObjects[4].Get() : nullptr) :
        Layout ? Layout->LandingPad.Get() : nullptr;
}

bool ADroneTrainerDronePawn::ValidatePrecisionObjects() const
{
    if (PrecisionObjects.Num()!=5) return false;
    const FName Tags[]={TEXT("L2_A2"),TEXT("L2_P1"),TEXT("L2_P2"),TEXT("L2_P3"),TEXT("L2_B2")};
    int32 Counts[5]={};
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        for (int32 I=0; I<5; ++I) if (It->ActorHasTag(Tags[I]))
        {
            ++Counts[I]; if (*It!=PrecisionObjects[I]) return false;
        }
    for (int32 I=0; I<5; ++I)
    {
        auto* A=PrecisionObjects[I].Get();
        if (Counts[I]!=1 || !IsValid(A) || A->GetActorTransform().ContainsNaN() || !A->GetStaticMeshComponent()->GetStaticMesh()) return false;
        for (int32 J=0; J<I; ++J) if (PrecisionObjects[J]==A) return false;
        const FBox Box=A->GetComponentsBoundingBox(true);
        if (!Box.IsValid || !Layout || Box.Min.X<Layout->FlightMin.X || Box.Max.X>Layout->FlightMax.X || Box.Min.Y<Layout->FlightMin.Y || Box.Max.Y>Layout->FlightMax.Y) return false;
        if ((I==0 || I==4) && (!FMath::IsNearlyEqual(Box.GetSize().X,800.,1.) || !FMath::IsNearlyEqual(Box.GetSize().Y,800.,1.))) return false;
    }
    const FVector Forward=PrecisionObjects[0]->GetActorForwardVector().GetSafeNormal2D();
    const FVector Next=(PrecisionObjects[3]->GetActorLocation()-PrecisionObjects[2]->GetActorLocation()).GetSafeNormal2D();
    return FMath::Abs(FMath::FindDeltaAngleDegrees(Forward.Rotation().Yaw,Next.Rotation().Yaw)-90.f)<0.1f;
}

float ADroneTrainerDronePawn::GetRequiredHeading() const
{
    if (PrecisionObjects.Num()!=5 || !PrecisionObjects[0] || !PrecisionObjects[2] || !PrecisionObjects[3]) return 0;
    return PrecisionStep==DronePrecision::EStep::P2 ? PrecisionObjects[0]->GetActorRotation().Yaw :
        (PrecisionObjects[3]->GetActorLocation()-PrecisionObjects[2]->GetActorLocation()).Rotation().Yaw;
}

int32 ADroneTrainerDronePawn::GetPositionsCompleted() const
{
    return int32(PrecisionStep)>=int32(DronePrecision::EStep::Landing) ? 3 :
        int32(PrecisionStep)>=int32(DronePrecision::EStep::Turn) ? 2 : PrecisionStep==DronePrecision::EStep::P2 ? 1 : 0;
}

FString ADroneTrainerDronePawn::GetPrecisionTargetName() const
{
    const TCHAR* Names[]={TEXT("A2"),TEXT("P1"),TEXT("P2"),TEXT("P3"),TEXT("B2")};
    return Names[DronePrecision::TargetIndex(PrecisionStep)];
}
FString ADroneTrainerDronePawn::GetPositionCorrection() const
{
    return DronePrecision::Correction(GetLessonTarget()-GetActorLocation(),GetActorRotation().Yaw);
}
FString ADroneTrainerDronePawn::GetPrecisionFeedback() const
{
    // Sideways travel teaches heading recovery even before reaching the target circle.
    if (PrecisionStep==DronePrecision::EStep::P2 && FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,GetRequiredHeading()))>15)
        return TEXT("Выровняйте курс");
    return DronePrecision::HoldFailure(GetActorLocation(),GetLessonTarget(),AltitudeMeters,bGroundValid,HorizontalSpeed,
        GetActorRotation().Yaw,GetRequiredHeading(),PrecisionStep==DronePrecision::EStep::P2 || PrecisionStep==DronePrecision::EStep::Turn || PrecisionStep==DronePrecision::EStep::P3);
}

void ADroneTrainerDronePawn::RestartAttempt()
{
    if (!GetWorld() || AttemptState == EDroneAttemptState::Running || AttemptState == EDroneAttemptState::Countdown)
    {
        return;
    }
    bSettingsOpen=false;
    StopAttemptAudio();
    ClearFlightInput();
    const auto* PC=Cast<APlayerController>(GetController());
    if (!ValidateLesson() || !PC || !Cast<ADroneTrainerHUD>(PC->GetHUD()))
    {
        AttemptState=EDroneAttemptState::MainMenu;
        FailureDetail=TEXT("Не удалось подготовить карту, дрон, HUD или маршрут. Q — выход");
        FinishAttempt(EDroneAttemptState::Error,EDroneFailureReason::RuntimeError);
        return;
    }

    if (UGameplayStatics::IsGamePaused(GetWorld()))
    {
        UGameplayStatics::SetGamePaused(GetWorld(), false);
    }

    PhysicsBody->SetSimulatePhysics(false);
    SetActorTransform(AttemptStartTransform);
    PhysicsBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
    PhysicsBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    PhysicsBody->SetEnableGravity(false);
    PhysicsBody->SetSimulatePhysics(true);

    PrecisionStep=DronePrecision::EStep::Takeoff; PositionHoldSeconds=0;
    LessonPhase = EDroneLessonPhase::Takeoff;
    LandingSeconds = BoundarySeconds = MissingGroundSeconds = 0;
    bGateArmed = bLandingSettled = bPendingCollision = false;
    PendingContacts.Reset(); GateNotice.Empty(); GateNoticeSeconds=0; FailureDetail.Empty();
    PreviousFlightRotation = PhysicsBody->GetComponentQuat();
    CountdownRemaining = 3.0f;
    ElapsedSeconds = 0.0f;
    GatesPassed = 0;
    bHasTakenOff = false;
    bAltitudeHoldActive = false;
    StateBeforePause = EDroneAttemptState::Countdown;
    FailureReason = EDroneFailureReason::None;
    TargetAltitude = PhysicsBody->GetComponentLocation().Z;
    StartAltitude = TargetAltitude;
    PreviousFlightPosition = PhysicsBody->GetComponentLocation();
    bVerticalInputActive = false;
    bLowAltitude = false;
    bThirdPersonView = false;
    FpvCamera->SetActive(true);
    ThirdPersonCamera->SetActive(false);
    UpdateTelemetry();
    AttemptState = EDroneAttemptState::Countdown;
    UpdateGateVisuals();
    if (!ValidateLesson()) FinishAttempt(EDroneAttemptState::Error, EDroneFailureReason::RuntimeError);
}

void ADroneTrainerDronePawn::ReturnToMainMenu()
{
    if (AttemptState == EDroneAttemptState::Running || AttemptState == EDroneAttemptState::Countdown) return;
    ClearFlightInput();
    if (!GetWorld())
    {
        return;
    }

    if (UGameplayStatics::IsGamePaused(GetWorld()))
    {
        UGameplayStatics::SetGamePaused(GetWorld(), false);
    }

    PhysicsBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
    PhysicsBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    PhysicsBody->SetSimulatePhysics(false);
    SetActorTransform(AttemptStartTransform);
    StopAttemptAudio(); bSettingsOpen=false;
    PrecisionStep=DronePrecision::EStep::Takeoff; PositionHoldSeconds=0; CountdownRemaining=3;
    ElapsedSeconds=0; GatesPassed=0; LandingSeconds=BoundarySeconds=MissingGroundSeconds=0;
    PendingContacts.Reset(); bPendingCollision=bGateArmed=bLandingSettled=bHasTakenOff=bLowAltitude=false;
    GateNotice.Empty(); GateNoticeSeconds=0; FailureDetail.Empty(); FailureReason=EDroneFailureReason::None;
    LessonPhase=EDroneLessonPhase::Takeoff; UpdateGateVisuals(); UpdateTelemetry();
    StateBeforePause = EDroneAttemptState::Countdown;
    AttemptState = EDroneAttemptState::MainMenu;
}

void ADroneTrainerDronePawn::ExitGame()
{
    if (AttemptState == EDroneAttemptState::MainMenu || AttemptState == EDroneAttemptState::Paused ||
        AttemptState == EDroneAttemptState::Completed || AttemptState == EDroneAttemptState::Failed || AttemptState == EDroneAttemptState::Aborted || AttemptState == EDroneAttemptState::Error)
    {
        UKismetSystemLibrary::QuitGame(this, Cast<APlayerController>(GetController()), EQuitPreference::Quit, false);
    }
}

void ADroneTrainerDronePawn::HandleBodyHit(UPrimitiveComponent*, AActor* OtherActor,
    UPrimitiveComponent* OtherComponent, FVector, const FHitResult& Hit)
{
    if (AttemptState != EDroneAttemptState::Running || OtherActor == this) return;
    const bool StartContact = !bHasTakenOff && Hit.ImpactNormal.Z >= 0.98480775 &&
        FVector::Dist2D(GetActorLocation(), AttemptStartTransform.GetLocation()) <= 300 &&
        (OtherActor == StartSurface || (OtherComponent && OtherComponent->GetCollisionResponseToChannel(ECC_GameTraceChannel1) == ECR_Block));
    // Ground hits need pre-solver speed classification, regardless of lesson phase.
    const bool GroundContact = OtherComponent && OtherComponent->GetCollisionResponseToChannel(ECC_GameTraceChannel1)==ECR_Block;
    if (!StartContact && !GroundContact)
    {
        if (FailureDetail.IsEmpty()) FailureDetail = Cast<ADroneTrainerGate>(OtherActor)
            ? TEXT("Удар о рамку ворот") : TEXT("Удар о препятствие");
        UE_LOG(LogTemp,Display,TEXT("LESSON_OBSTACLE actor=%s component=%s position=%s normal=%s"),
            *GetNameSafe(OtherActor),*GetNameSafe(OtherComponent),*GetActorLocation().ToString(),*Hit.ImpactNormal.ToString());
        bPendingCollision = true;
    }
}

bool ADroneTrainerDronePawn::ValidateLesson() const
{
    if (!Layout || !StartSurface || !ContactCapture) return false;
    if (bPrecisionLesson) return ValidatePrecisionObjects() && Layout->FlightMin.X<Layout->FlightMax.X && Layout->FlightMin.Y<Layout->FlightMax.Y && Layout->MaxAGL>0;
    if (!Layout->Start || !Layout->LandingPad || RouteGates.Num()!=5) return false;
    if (Layout->FlightMin.X >= Layout->FlightMax.X || Layout->FlightMin.Y >= Layout->FlightMax.Y || Layout->MaxAGL <= 0) return false;
    for (int32 I=0; I<5; ++I)
    {
        const auto* G=RouteGates[I].Get();
        if (!IsValid(G) || G->GateIndex!=I+1 || G->GetActorTransform().ContainsNaN() ||
            !G->GetActorScale3D().Equals(FVector::OneVector,0.001) || !G->OpeningHalfSize.Equals(FVector2D(200,200),0.01) ||
            G->OpeningCenterLocal.ContainsNaN() || !G->Frame || !G->Frame->GetStaticMesh()) return false;
    }
    const FBox Box=Layout->LandingPad->GetComponentsBoundingBox();
    return Box.IsValid && FMath::IsNearlyEqual(Box.GetSize().X,800.,1.) && FMath::IsNearlyEqual(Box.GetSize().Y,800.,1.);
}

void ADroneTrainerDronePawn::FinishAttempt(EDroneAttemptState State, EDroneFailureReason Reason)
{
    if (AttemptState == EDroneAttemptState::Completed || AttemptState == EDroneAttemptState::Failed || AttemptState == EDroneAttemptState::Error) return;
    StopAttemptAudio();
    AttemptState=State; FailureReason=Reason;
    if (State==EDroneAttemptState::Completed) PlayLessonSound(2);
    else if (State==EDroneAttemptState::Failed) PlayLessonSound(3);
    PhysicsBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
    PhysicsBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    PhysicsBody->SetSimulatePhysics(false);
    ClearFlightInput();
    UE_LOG(LogTemp, Display, TEXT("LESSON_RESULT state=%d reason=%d gates=%d time=%.3f"),int32(State),int32(Reason),GatesPassed,ElapsedSeconds);
}

bool ADroneTrainerDronePawn::IsSoftLanding(const FContactSample& C) const
{
    if (!C.bLandingPad || !GetLandingPad()) return false;
    return DroneLesson::SoftLanding(C.Position,C.Rotation,PhysicsBody->GetScaledBoxExtent(),C.Velocity,C.Normal,C.Point,GetLandingPad()->GetComponentsBoundingBox());
}

void ADroneTrainerDronePawn::UpdateGateVisuals()
{
    for (int32 I=0; I<RouteGates.Num(); ++I)
        if (IsValid(RouteGates[I])) RouteGates[I]->SetLessonState(bPrecisionLesson ? 0 : I<GatesPassed ? 2 : (LessonPhase==EDroneLessonPhase::Route && I==GatesPassed ? 1 : 0));
    if (Layout) Layout->RefreshMarkings();
}

void ADroneTrainerDronePawn::UpdateAttempt(float DeltaSeconds)
{
    const FVector Position = PhysicsBody->GetComponentLocation();
    if (AttemptState == EDroneAttemptState::Countdown)
    {
        CountdownRemaining = FMath::Max(0.f, CountdownRemaining-DeltaSeconds);
        if (CountdownRemaining == 0)
        {
            PhysicsBody->SetEnableGravity(true);
            AttemptState = EDroneAttemptState::Running;
            PreviousFlightPosition = Position;
            PreviousFlightRotation = PhysicsBody->GetComponentQuat();
        }
        return;
    }
    if (AttemptState != EDroneAttemptState::Running) return;
    // Gameplay uses fresh ground and velocity, independently of HUD refresh cadence.
    UpdateTelemetry();
    const FVector Velocity = PhysicsBody->GetPhysicsLinearVelocity();
    const FQuat Rotation = PhysicsBody->GetComponentQuat();
    if (!ValidateLesson()) { FinishAttempt(EDroneAttemptState::Error,EDroneFailureReason::RuntimeError); return; }
    const bool Outside = Position.X<Layout->FlightMin.X || Position.X>Layout->FlightMax.X || Position.Y<Layout->FlightMin.Y || Position.Y>Layout->FlightMax.Y;
    MissingGroundSeconds = bGroundValid ? 0 : MissingGroundSeconds+DeltaSeconds;
    ElapsedSeconds += DeltaSeconds;
    GateNoticeSeconds=FMath::Max(0.f,GateNoticeSeconds-DeltaSeconds);
    BoundarySeconds = Outside || (bGroundValid && AltitudeMeters*100>Layout->MaxAGL) ? BoundarySeconds+DeltaSeconds : 0;
    const bool Invalid = !ValidateLesson() || Position.ContainsNaN() || Velocity.ContainsNaN() || Rotation.ContainsNaN() || (!bGroundValid && !Outside && MissingGroundSeconds>0.25f);
    bool SurfaceContact=false;
    for (const FContactSample& C : PendingContacts)
    {
        if (C.bGroundSurface || C.bLandingPad)
        {
            const FString ContactFailure=DroneLesson::SurfaceContactFailure(C.Position,C.Rotation,PhysicsBody->GetScaledBoxExtent(),C.Velocity,C.Normal,C.Point);
            const bool Soft=ContactFailure.IsEmpty();
            if (!bLandingSettled && !SurfaceContact) UE_LOG(LogTemp,Display,TEXT("LESSON_TOUCHDOWN phase=%d pad=%d pre_horizontal=%.4f pre_vertical=%.4f soft=%d"),int32(LessonPhase),C.bLandingPad,C.Velocity.Size2D()/100,C.Velocity.Z/100,Soft);
            if (Soft) SurfaceContact=true;
            else
            {
                if (FailureDetail.IsEmpty()) FailureDetail=ContactFailure;
                UE_LOG(LogTemp,Display,TEXT("LESSON_REJECTED_CONTACT pad=%d settled=%d position=%s rotation=%s velocity=%s normal=%s point=%s"),
                    C.bLandingPad,bLandingSettled,*C.Position.ToString(),*C.Rotation.Rotator().ToString(),*C.Velocity.ToString(),*C.Normal.ToString(),*C.Point.ToString());
                bPendingCollision=true;
            }
        }
    }
    PendingContacts.Reset();
    const bool HardBoundary = FMath::Abs(Position.X)>24000 || FMath::Abs(Position.Y)>24000 || Position.Z < -2000 ||
        (bGroundValid && AltitudeMeters>70) || BoundarySeconds>=5 || (!bGroundValid && Outside);
    const int32 Outcome = DroneLesson::Outcome(Invalid,bPendingCollision,HardBoundary,bPrecisionLesson ? 0.f : ElapsedSeconds);
    if (Outcome)
    {
        FinishAttempt(Outcome==1 ? EDroneAttemptState::Error : EDroneAttemptState::Failed,
            Outcome==1 ? EDroneFailureReason::RuntimeError : Outcome==2 ? EDroneFailureReason::Collision : Outcome==3 ? EDroneFailureReason::OutOfBounds : EDroneFailureReason::TimeLimit);
        return;
    }
    // Rest on any safe GroundSurface; only the designated pad can finish the lesson.
    FHitResult Support;
    const float Bottom=DroneLesson::ProjectedHalf(Rotation,PhysicsBody->GetScaledBoxExtent(),FVector::UpVector);
    const bool Supported=GetWorld()->LineTraceSingleByChannel(Support,Position,Position-FVector(0,0,Bottom+0.5),ECC_GameTraceChannel1,
        FCollisionQueryParams(SCENE_QUERY_STAT(LandingSupport),false,this));
    if (SurfaceContact) bLandingSettled=true;
    if (!Supported) { bLandingSettled=false; LandingSeconds=0; }
    bHasTakenOff |= bGroundValid && AltitudeMeters>=0.5f;
    const EDroneLessonPhase OldPhase=LessonPhase;
    if (bPrecisionLesson)
    {
        const bool ValidHold=GetPrecisionFeedback().IsEmpty();
        if (DronePrecision::Advance(PrecisionStep,PositionHoldSeconds,DeltaSeconds,true,bGroundValid,AltitudeMeters,ValidHold))
        {
            LessonPhase=PrecisionStep==DronePrecision::EStep::Landing ? EDroneLessonPhase::Landing : EDroneLessonPhase::Route;
            PlayLessonSound(0);
            UE_LOG(LogTemp,Display,TEXT("PRECISION_STEP %d time=%.3f"),int32(PrecisionStep),ElapsedSeconds);
        }
    }
    if (!bPrecisionLesson || PrecisionStep==DronePrecision::EStep::Landing)
    switch (LessonPhase)
    {
    case EDroneLessonPhase::Takeoff:
        if (bGroundValid && AltitudeMeters>=3.0f) LessonPhase=EDroneLessonPhase::Route;
        break;
    case EDroneLessonPhase::Route:
        if (auto* Gate=GetCurrentGate())
        {
            if (FVector::DotProduct(Position-Gate->GetOpeningCenter(),Gate->GetActorForwardVector())<=-100) bGateArmed=true;
            if (DroneLesson::CrossesGate(*Gate,PreviousFlightPosition,Position,PreviousFlightRotation,Rotation,PhysicsBody->GetScaledBoxExtent(),bGateArmed))
            {
                ++GatesPassed; PlayLessonSound(0); bGateArmed=false; GateNoticeSeconds=0;
                UE_LOG(LogTemp,Display,TEXT("LESSON_GATE G%d time=%.3f"),GatesPassed,ElapsedSeconds);
                if (GatesPassed==5) LessonPhase=EDroneLessonPhase::Landing;
                UpdateGateVisuals();
            }
            else
            {
                const double Before=(PreviousFlightPosition-Gate->GetOpeningCenter()).Dot(Gate->GetActorForwardVector());
                const double After=(Position-Gate->GetOpeningCenter()).Dot(Gate->GetActorForwardVector());
                if (Before<0 && After>=0) { GateNotice=TEXT("Мимо проёма — вернитесь на вход"); GateNoticeSeconds=2; bGateArmed=false; }
                if (Before>0 && After<=0) { GateNotice=TEXT("Обратный пролёт — летите по стрелке"); GateNoticeSeconds=2; }
            }
        }
        break;
    case EDroneLessonPhase::Landing:
    {
        const bool Stable = bLandingSettled && Supported && Support.GetActor()==GetLandingPad() && Velocity.Size()<=10 &&
            DroneLesson::SoftLanding(Position,Rotation,PhysicsBody->GetScaledBoxExtent(),Velocity,Support.ImpactNormal,Support.ImpactPoint,GetLandingPad()->GetComponentsBoundingBox());
        LandingSeconds=DroneLesson::Hold(LandingSeconds,DeltaSeconds,Stable);
        if (LandingSeconds>=2) FinishAttempt(EDroneAttemptState::Completed,EDroneFailureReason::None);
        break;
    }
    }
    if (LessonPhase!=OldPhase)
    {
        UE_LOG(LogTemp,Display,TEXT("LESSON_PHASE %d time=%.3f"),int32(LessonPhase),ElapsedSeconds);
        UpdateGateVisuals();
    }
    PreviousFlightPosition=Position; PreviousFlightRotation=Rotation;
}

FVector ADroneTrainerDronePawn::GetLessonTarget() const
{
    if (bPrecisionLesson)
    {
        const int32 I=DronePrecision::TargetIndex(PrecisionStep);
        if (PrecisionObjects.IsValidIndex(I) && IsValid(PrecisionObjects[I]))
        {
            const FBox Box=PrecisionObjects[I]->GetComponentsBoundingBox(true);
            return FVector(Box.GetCenter().X,Box.GetCenter().Y,Box.Max.Z+(PrecisionStep==DronePrecision::EStep::Landing ? 0 : 300));
        }
        return GetActorLocation();
    }
    if (LessonPhase==EDroneLessonPhase::Route && GetCurrentGate()) return GetCurrentGate()->GetOpeningCenter();
    if (LessonPhase==EDroneLessonPhase::Landing && Layout && Layout->LandingPad) return Layout->LandingPad->GetComponentsBoundingBox().GetCenter();
    return AttemptStartTransform.GetLocation()+FVector(0,0,300-PhysicsBody->GetScaledBoxExtent().Z-StartGroundClearance);
}

FString ADroneTrainerDronePawn::GetLessonInstruction() const
{
    if (bPrecisionLesson)
    {
        switch (PrecisionStep)
        {
        case DronePrecision::EStep::Takeoff: return TEXT("Взлёт A2: поднимитесь до 3 м");
        case DronePrecision::EStep::P1: return TEXT("P1: летите прямо и остановитесь");
        case DronePrecision::EStep::P2: return TEXT("P2: правее, сохраняйте исходный курс");
        case DronePrecision::EStep::Turn: return TEXT("Над P2: поверните вправо на 90°");
        case DronePrecision::EStep::P3: return TEXT("P3: вперёд по новому курсу, остановитесь");
        case DronePrecision::EStep::Landing: return bLandingSettled ? FString::Printf(TEXT("Контакт B2: %.1f / 2 с"),LandingSeconds) : TEXT("B2: остановитесь, затем снижайтесь");
        }
    }
    if (LessonPhase==EDroneLessonPhase::Route && GateNoticeSeconds>0) return GateNotice;
    switch (LessonPhase)
    {
    case EDroneLessonPhase::Takeoff: return TEXT("Взлёт: Space — поднимитесь до 3 м над землёй");
    case EDroneLessonPhase::Route:
        if (!GetCurrentGate()) return TEXT("");
        if (!bGateArmed) return FString::Printf(TEXT("G%d: зайдите с входной стороны"),GetCurrentGate()->GateIndex);
        return FString::Printf(TEXT("G%d: %s"),GetCurrentGate()->GateIndex,
            GetCurrentGate()->GateIndex<=2 ? TEXT("следуйте прямо, ориентир 3 м") :
            GetCurrentGate()->GateIndex<=4 ? TEXT("плавный поворот по дуге") : TEXT("выровняйтесь, готовьтесь к торможению"));
    case EDroneLessonPhase::Landing:
        if (!bLandingSettled) return TEXT("Перед посадкой снизьте скорость по горизонтали до 0,5 м/с");
        return FString::Printf(TEXT("Удерживайте покой: %.1f / 2 с"),LandingSeconds);
    }
    return TEXT("");
}

void ADroneTrainerDronePawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateAudio(DeltaSeconds);

#if WITH_DEV_AUTOMATION_TESTS
    if (bAutoRoute) TickAutoRoute(DeltaSeconds);
    if (bFlightCheckActive)
    {
        UpdateTelemetry();
        UpdateAttempt(DeltaSeconds);
        if (AttemptState == EDroneAttemptState::Running)
            ApplyFlightControl(DeltaSeconds, FlightCheckInput.X, FlightCheckInput.Y, FlightCheckInput.Z, FlightCheckInput.W);
        return;
    }
#endif
    TelemetryAge += DeltaSeconds;
    if (TelemetryAge >= 0.05f) { UpdateTelemetry(); TelemetryAge -= 0.05f; }
    if ((AttemptState == EDroneAttemptState::Running || AttemptState == EDroneAttemptState::Countdown) &&
        GetWorld()->GetGameViewport() && GetWorld()->GetGameViewport()->Viewport &&
        !GetWorld()->GetGameViewport()->Viewport->IsForegroundWindow()) HandleFocusLost();
    APlayerController* PlayerController = Cast<APlayerController>(GetController());
    if (!PlayerController || !PhysicsBody->IsSimulatingPhysics() || DeltaSeconds <= 0.0f)
    {
        return;
    }

    UpdateAttempt(DeltaSeconds);
    if (AttemptState != EDroneAttemptState::Running)
    {
        if (AttemptState == EDroneAttemptState::Completed || AttemptState == EDroneAttemptState::Failed ||
            AttemptState == EDroneAttemptState::Aborted || AttemptState == EDroneAttemptState::Error)
        {
            PhysicsBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
            PhysicsBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
            PhysicsBody->SetSimulatePhysics(false);
        }
        return;
    }

    const float ForwardInput =
        (PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) -
        (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
    const float StrafeInput =
        (PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) -
        (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
    const float YawInput =
        (PlayerController->IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f) -
        (PlayerController->IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f);
    const float VerticalInput =
        (PlayerController->IsInputKeyDown(EKeys::SpaceBar) ? 1.0f : 0.0f) -
        (PlayerController->IsInputKeyDown(EKeys::LeftShift) ? 1.0f : 0.0f);

    ApplyFlightControl(DeltaSeconds, ForwardInput, StrafeInput, YawInput, VerticalInput);
}

void ADroneTrainerDronePawn::ApplyFlightControl(float DeltaSeconds, float ForwardInput, float StrafeInput, float YawInput, float VerticalInput)
{
    const auto* Settings=Cast<UDroneTrainerSettings>(GEngine->GetGameUserSettings());
    const float Sensitivity=Settings ? Settings->Sensitivity : 1.f;
    VerticalInput*=Sensitivity; YawInput*=Sensitivity;
    if (bLandingSettled)
    {
        if (VerticalInput > 0) { bLandingSettled=false; LandingSeconds=0; TargetAltitude=PhysicsBody->GetComponentLocation().Z; }
        else return; // Rest on the pad under gravity; altitude hold must not fight landing.
    }
    if (FMath::Abs(VerticalInput) > KINDA_SMALL_NUMBER)
    {
        if (!bAltitudeHoldActive) TargetAltitude = PhysicsBody->GetComponentLocation().Z;
        bAltitudeHoldActive = true;
        // Slow the target near the ground; holding world Z never follows the terrain.
        const float DescentScale = bGroundValid ? FMath::Clamp((AltitudeMeters - 0.1f) / 0.9f, 0.1f, 1.0f) : 1.0f;
        TargetAltitude += VerticalInput * VerticalSpeedCmPerSecond * (VerticalInput < 0 ? DescentScale : 1.0f) * DeltaSeconds;
        // Bound target lag in the landing flare: the same PD lift controller brakes
        // to <=0.2 m/s instead of chasing a target accumulated below the pad.
        if (VerticalInput < 0 && bGroundValid && AltitudeMeters < 3.5f)
            TargetAltitude = FMath::Max(TargetAltitude, float(PhysicsBody->GetComponentLocation().Z)-20.f);
    }
    else if (bVerticalInputActive)
    {
        TargetAltitude = PhysicsBody->GetComponentLocation().Z;
    }
    bVerticalInputActive = !FMath::IsNearlyZero(VerticalInput);
    FVector2D TiltInput(ForwardInput, StrafeInput);
    if (TiltInput.SizeSquared() > 1.0f)
    {
        TiltInput.Normalize();
    }

    const float TiltRadians = FMath::DegreesToRadians(MaxTiltDegrees*Sensitivity);
    const float TargetPitch = -TiltInput.X * TiltRadians;
    const float TargetRoll = TiltInput.Y * TiltRadians;

    const FRotator CurrentRotation = PhysicsBody->GetComponentRotation();
    const FQuat DesiredRotation = FRotator(
        FMath::RadiansToDegrees(TargetPitch),
        CurrentRotation.Yaw,
        FMath::RadiansToDegrees(TargetRoll)).Quaternion();

    FQuat RotationError = DesiredRotation * PhysicsBody->GetComponentQuat().Inverse();
    RotationError.Normalize();

    FVector ErrorAxis = FVector::ZeroVector;
    float ErrorAngle = 0.0f;
    RotationError.ToAxisAndAngle(ErrorAxis, ErrorAngle);

    const FVector AngularVelocity = PhysicsBody->GetPhysicsAngularVelocityInRadians();
    if (ErrorAngle > PI) ErrorAngle -= 2 * PI;
    FVector RotationAcceleration = ErrorAxis * (ErrorAngle * TiltStiffness) - AngularVelocity * TiltDamping;
    RotationAcceleration.Z = 0;

    const FVector WorldUp = FVector::UpVector;
    const float TargetYawRate = FMath::DegreesToRadians(YawInput * MaxYawRateDegrees);
    const float CurrentYawRate = FVector::DotProduct(AngularVelocity, WorldUp);
    const FVector YawAcceleration = WorldUp * ((TargetYawRate - CurrentYawRate) * YawRateGain);
    PhysicsBody->AddTorqueInRadians(RotationAcceleration + YawAcceleration, NAME_None, true);

    const FVector Velocity = PhysicsBody->GetPhysicsLinearVelocity();
    FVector HorizontalVelocity = Velocity;
    HorizontalVelocity.Z = 0.0f;
    const float Drag = TiltInput.IsNearlyZero() ? 1.4f : FMath::Max(0.45f, HorizontalVelocity.Size() / 800.0f * 0.45f);
    PhysicsBody->AddForce(-HorizontalVelocity * Drag, NAME_None, true);

    if (bAltitudeHoldActive)
    {
        const float AltitudeError = TargetAltitude - PhysicsBody->GetComponentLocation().Z;
        const float VerticalAcceleration = FMath::Clamp(
            AltitudeError * AltitudeKp - Velocity.Z * AltitudeKd,
            -500.0f,
            500.0f);

        const float UpDot = FMath::Max(
            0.5f,
            FVector::DotProduct(PhysicsBody->GetUpVector(), WorldUp));
        const float HoverAcceleration = -GetWorld()->GetGravityZ();
        const float LiftAcceleration = FMath::Min(
            (HoverAcceleration + VerticalAcceleration) / UpDot,
            2600.0f);

        PhysicsBody->AddForce(PhysicsBody->GetUpVector() * LiftAcceleration, NAME_None, true);
    }
}

void ADroneTrainerDronePawn::UpdateTelemetry()
{
    const FVector Position = PhysicsBody->GetComponentLocation();
    FHitResult Ground;
    bGroundValid = GetWorld()->LineTraceSingleByChannel(Ground, Position, Position - FVector(0,0,20000),
        ECC_GameTraceChannel1, FCollisionQueryParams(SCENE_QUERY_STAT(DroneAGL), false, this));
    AltitudeMeters = bGroundValid ? (Position.Z - Ground.ImpactPoint.Z) / 100.0f : 0.0f;
    const FVector Velocity = PhysicsBody->GetPhysicsLinearVelocity();
    HorizontalSpeed = Velocity.Size2D() / 100.0f;
    VerticalSpeed = Velocity.Z / 100.0f;
    bLowAltitude = LessonPhase == EDroneLessonPhase::Route && bGroundValid && AltitudeMeters < (bLowAltitude ? 1.3f : 1.0f);
}

ADroneTrainerGate* ADroneTrainerDronePawn::GetCurrentGate() const
{
    if (bPrecisionLesson) return nullptr;
    return RouteGates.IsValidIndex(GatesPassed) ? RouteGates[GatesPassed].Get() : nullptr;
}

void ADroneTrainerDronePawn::ClearFlightInput()
{
    if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->FlushPressedKeys();
    if (bVerticalInputActive && bAltitudeHoldActive) TargetAltitude = PhysicsBody->GetComponentLocation().Z;
    bVerticalInputActive = false;
}

void ADroneTrainerDronePawn::HandleFocusLost()
{
#if WITH_DEV_AUTOMATION_TESTS
    if (bAutoRoute) return; // Explicit unattended benchmark only; normal focus behaviour stays unchanged.
#endif
    ClearFlightInput();
    if (AttemptState == EDroneAttemptState::Running || AttemptState == EDroneAttemptState::Countdown) TogglePause();
}

void ADroneTrainerDronePawn::HandleApplicationActivation(bool bActive)
{
    if (!bActive) HandleFocusLost();
}

void ADroneTrainerDronePawn::EndPlay(const EEndPlayReason::Type Reason)
{
    StopAttemptAudio();
    FTSTicker::RemoveTicker(PerformanceTicker);
#if CSV_PROFILER
    if (bPerformanceCaptureOwned)
    {
        FCsvProfiler::Get()->EndCapture();
        UE_LOG(LogTemp,Warning,TEXT("PERFORMANCE_CAPTURE_EARLY_EXIT"));
    }
#endif
    if (ContactCapture && GetWorld()->GetPhysicsScene()) GetWorld()->GetPhysicsScene()->GetSolver()->UnregisterAndFreeSimCallbackObject_External(ContactCapture);
    ContactCapture=nullptr;
    FCoreDelegates::ApplicationWillDeactivateDelegate.RemoveAll(this);
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.RemoveAll(this);
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().OnApplicationActivationStateChanged().RemoveAll(this);
    Super::EndPlay(Reason);
}

void ADroneTrainerDronePawn::ToggleSettings()
{
    if (AttemptState==EDroneAttemptState::MainMenu || AttemptState==EDroneAttemptState::Paused) bSettingsOpen=!bSettingsOpen;
}
void ADroneTrainerDronePawn::LowerSensitivity() { AdjustSetting(0,-0.1f); }
void ADroneTrainerDronePawn::RaiseSensitivity() { AdjustSetting(0,0.1f); }
void ADroneTrainerDronePawn::LowerVolume() { AdjustSetting(1,-0.1f); }
void ADroneTrainerDronePawn::RaiseVolume() { AdjustSetting(1,0.1f); }
void ADroneTrainerDronePawn::ChangeWindowMode() { AdjustSetting(2,0); }
void ADroneTrainerDronePawn::ChangeResolution() { AdjustSetting(3,0); }
void ADroneTrainerDronePawn::AdjustSetting(int32 Setting,float Amount)
{
    if (!bSettingsOpen || (AttemptState!=EDroneAttemptState::MainMenu && AttemptState!=EDroneAttemptState::Paused)) return;
    auto* S=Cast<UDroneTrainerSettings>(GEngine->GetGameUserSettings()); if (!S) return;
    if (Setting==0) S->Sensitivity=FMath::Clamp(S->Sensitivity+Amount,0.5f,1.5f);
    if (Setting==1) S->MasterVolume=FMath::Clamp(S->MasterVolume+Amount,0.f,1.f);
    if (Setting==2) S->SetFullscreenMode(S->GetFullscreenMode()==EWindowMode::Windowed ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
    if (Setting==3)
    {
        if (S->GetFullscreenMode()==EWindowMode::WindowedFullscreen) return;
        const auto Values=S->SupportedResolutions();
        S->SetScreenResolution(Values[(Values.IndexOfByKey(S->GetScreenResolution())+1)%Values.Num()]);
    }
    S->ApplySettings(false); S->ConfirmVideoMode(); S->SaveSettings();
}
void ADroneTrainerDronePawn::StopAttemptAudio()
{
    Motors->Stop(); Ambience->Stop(); LessonAudio->Stop(); MotorLoad=0; WarningCooldown=0;
}
void ADroneTrainerDronePawn::PlayLessonSound(int32 Index)
{
    if (!LessonSounds.IsValidIndex(Index) || !LessonSounds[Index]) return;
    LessonAudio->Stop(); LessonAudio->SetSound(LessonSounds[Index]); LessonAudio->SetVolumeMultiplier(0.8f); LessonAudio->Play();
}
void ADroneTrainerDronePawn::UpdateAudio(float Dt)
{
    if (AttemptState!=EDroneAttemptState::Running) return;
    if (!Ambience->IsPlaying()) Ambience->Play();
    if (!Motors->IsPlaying()) Motors->Play();
    const float Load=FMath::Clamp(GetSpeedMetersPerSecond()/12.f + FMath::Abs(VerticalSpeed)/4.f,0.f,1.f);
    MotorLoad=FMath::FInterpTo(MotorLoad,Load,Dt,2.f);
    const bool Signal=LessonAudio->IsPlaying();
    Motors->SetVolumeMultiplier((0.12f+0.12f*MotorLoad)*(Signal ? 0.25f : 1.f));
    Motors->SetPitchMultiplier(0.8f+MotorLoad*0.5f);
    Ambience->SetVolumeMultiplier(Signal ? 0.15f : 0.45f);
    WarningCooldown=FMath::Max(0.f,WarningCooldown-Dt);
    const FVector P=GetActorLocation();
    const bool Warning=bLowAltitude || BoundarySeconds>0 || (Layout && (FMath::Abs(P.X)>Layout->WarningXY || FMath::Abs(P.Y)>Layout->WarningXY || AltitudeMeters*100>Layout->WarningAGL));
    if (Warning && WarningCooldown==0 && !Signal) { PlayLessonSound(1); WarningCooldown=8.f; }
}
