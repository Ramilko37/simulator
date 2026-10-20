#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "Containers/Ticker.h"
#include "GameFramework/Pawn.h"
#include "DroneTrainerPrecision.h"
#include "DroneTrainerDronePawn.generated.h"

class ADroneTrainerGate;
class AStaticMeshActor;
class UCameraComponent;
class UAudioComponent;
class USoundWave;
class USpringArmComponent;
class ADroneTrainerLevelLayout;
class UInputComponent;
class UStaticMeshComponent;

UENUM()
enum class EDroneAttemptState : uint8
{
    MainMenu,
    Countdown,
    Running,
    Paused,
    Completed,
    Failed,
    Aborted,
    Error
};

UENUM()
enum class EDroneFailureReason : uint8
{
    None,
    Collision,
    TimeLimit,
    OutOfBounds,
    RuntimeError
};

UENUM()
enum class EDroneLessonPhase : uint8 { Takeoff = 0, Route = 2, Landing = 3 };

class FDroneContactCapture;

UCLASS()
class DRONETRAINER_API ADroneTrainerDronePawn : public APawn
{
    GENERATED_BODY()

public:
    ADroneTrainerDronePawn();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    bool IsSettingsOpen() const { return bSettingsOpen; }
    bool IsPrecisionLesson() const { return bPrecisionLesson; }
    DronePrecision::EStep GetPrecisionStep() const { return PrecisionStep; }
    float GetPositionHoldSeconds() const { return PositionHoldSeconds; }
    float GetRequiredHeading() const;
    FString GetPrecisionTargetName() const;
    FString GetPositionCorrection() const;
    FString GetPrecisionFeedback() const;
    int32 GetPositionsCompleted() const;
    AStaticMeshActor* GetLandingPad() const;
    EDroneLessonPhase GetLessonPhase() const { return LessonPhase; }
    float GetBoundarySeconds() const { return BoundarySeconds; }
    bool IsGateArmed() const { return bGateArmed; }
    FVector GetLessonTarget() const;
    FString GetLessonInstruction() const;
    EDroneAttemptState GetAttemptState() const { return AttemptState; }
    EDroneFailureReason GetFailureReason() const { return FailureReason; }
    const FString& GetFailureDetail() const { return FailureDetail; }
    const FString& GetPerformanceStatus() const { return PerformanceStatus; }
    float GetLandingSeconds() const { return LandingSeconds; }
    float GetElapsedSeconds() const { return ElapsedSeconds; }
    int32 GetGatesPassed() const { return GatesPassed; }
    float GetCountdownRemaining() const { return CountdownRemaining; }
    float GetAltitudeMeters() const { return AltitudeMeters; }
    bool HasGroundSurface() const { return bGroundValid; }
    float GetHorizontalSpeed() const { return HorizontalSpeed; }
    float GetVerticalSpeed() const { return VerticalSpeed; }
    ADroneTrainerGate* GetCurrentGate() const;
    const ADroneTrainerLevelLayout* GetLayout() const { return Layout; }
    int32 GetGateCount() const { return RouteGates.Num(); }
    bool HasTakenOff() const { return bHasTakenOff; }
    bool IsLowAltitude() const { return bLowAltitude; }
    float GetSpeedMetersPerSecond() const { return PhysicsBody->GetPhysicsLinearVelocity().Size() / 100.0f; }

private:
    friend class FDroneTrainerLessonContactCheck;
    friend class FDroneTrainerLessonTest;
    friend class FDroneTrainerPrecisionTest;
    friend class FDroneTrainerSettingsTest;
    friend class FDroneContactCapture;
    friend class FDroneTrainerBaselineTest;
    friend class FDroneTrainerFlightTest;
    friend class FDroneTrainerFlightCheck;

    UPROPERTY(VisibleAnywhere, Category = "Drone")
    TObjectPtr<UBoxComponent> PhysicsBody;

    UPROPERTY(VisibleAnywhere, Category = "Drone")
    TObjectPtr<UStaticMeshComponent> VisualMesh;

    UPROPERTY(VisibleAnywhere, Category = "Drone")
    TObjectPtr<UAudioComponent> Ambience;
    UPROPERTY() TObjectPtr<UAudioComponent> Motors;
    UPROPERTY() TObjectPtr<UAudioComponent> LessonAudio;
    UPROPERTY() TArray<TObjectPtr<USoundWave>> LessonSounds;
    bool bSettingsOpen=false;
    float MotorLoad=0, WarningCooldown=0;
    void UpdateAudio(float DeltaSeconds);
    void StopAttemptAudio();
    void PlayLessonSound(int32 Index);
    void ToggleSettings();
    void LowerSensitivity();
    void RaiseSensitivity();
    void LowerVolume();
    void RaiseVolume();
    void ChangeWindowMode();
    void ChangeResolution();
    void AdjustSetting(int32 Setting, float Amount);


    UPROPERTY(VisibleAnywhere, Category = "Drone")
    TObjectPtr<UCameraComponent> FpvCamera;

    UPROPERTY(VisibleAnywhere, Category = "Drone")
    TObjectPtr<UCameraComponent> ThirdPersonCamera;

    UPROPERTY(VisibleAnywhere, Category = "Drone")
    TObjectPtr<USpringArmComponent> CameraArm;

    UPROPERTY() TObjectPtr<ADroneTrainerLevelLayout> Layout;
    UPROPERTY() TObjectPtr<AActor> StartSurface;
    UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> PrecisionObjects;
    UPROPERTY() TObjectPtr<AActor> BasicStartSurface;
    FTransform BasicStartTransform;
    bool bPrecisionLesson=false;
    DronePrecision::EStep PrecisionStep=DronePrecision::EStep::Takeoff;
    float PositionHoldSeconds=0;
    void SelectBasicLesson();
    void SelectPrecisionLesson();
    void SelectLesson(bool Precision);
    bool ValidatePrecisionObjects() const;

    UPROPERTY(EditDefaultsOnly, Category = "Drone|Flight", meta = (ClampMin = "5.0", ClampMax = "30.0"))
    float MaxTiltDegrees = 20.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Drone|Flight", meta = (ClampMin = "10.0", ClampMax = "135.0"))
    float MaxYawRateDegrees = 90.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Drone|Flight", meta = (ClampMin = "50.0", ClampMax = "400.0"))
    float VerticalSpeedCmPerSecond = 200.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Drone|Stabilization")
    float TiltStiffness = 12.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Drone|Stabilization")
    float TiltDamping = 6.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Drone|Stabilization")
    float YawRateGain = 5.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Drone|Stabilization")
    float AltitudeKp = 4.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Drone|Stabilization")
    float AltitudeKd = 4.0f;

    UPROPERTY()
    TArray<TObjectPtr<ADroneTrainerGate>> RouteGates;

#if WITH_DEV_AUTOMATION_TESTS
    bool bAutoRoute = false;
    int32 AutoGate=0, AutoLeg=0, AutoLaps=0;
    double AutoStarted=0, AutoProgress=0;
    float AutoSurvey=0;
    void TickAutoRoute(float DeltaSeconds);
    bool bFlightCheckActive = false;
    FVector4 FlightCheckInput = FVector4(0,0,0,0);
#endif
    float AltitudeMeters = 0.0f;
    float HorizontalSpeed = 0.0f;
    float VerticalSpeed = 0.0f;
    float TelemetryAge = 0.0f;
    bool bGroundValid = false;
    bool bLowAltitude = false;
    bool bVerticalInputActive = false;
    float TargetAltitude = 0.0f;
    float StartAltitude = 0.0f;
    bool bAltitudeHoldActive = false;
    FVector PreviousFlightPosition = FVector::ZeroVector;
    float CountdownRemaining = 3.0f;
    float ElapsedSeconds = 0.0f;
    int32 GatesPassed = 0;
    bool bHasTakenOff = false;
    EDroneAttemptState StateBeforePause = EDroneAttemptState::Countdown;
    EDroneLessonPhase LessonPhase = EDroneLessonPhase::Takeoff;
    float LandingSeconds = 0, BoundarySeconds = 0, MissingGroundSeconds = 0;
    FString GateNotice;
    float GateNoticeSeconds = 0;
    bool bGateArmed = false, bLandingSettled = false, bPendingCollision = false;
    FQuat PreviousFlightRotation = FQuat::Identity;
    FDroneContactCapture* ContactCapture = nullptr;
    struct FContactSample
    {
        FVector Position, Velocity, Normal, Point;
        FQuat Rotation;
        bool bLandingPad = false, bGroundSurface = false;
    };
    TArray<FContactSample> PendingContacts;
    bool ValidateLesson() const;
    void FinishAttempt(EDroneAttemptState State, EDroneFailureReason Reason);
    void UpdateGateVisuals();
    bool IsSoftLanding(const FContactSample& Contact) const;
    bool bThirdPersonView = false;
    // Start supported: a 3cm gap falls fast enough to trigger the hard-landing limit.
    static constexpr float StartGroundClearance = 0.1f;
    FTransform AttemptStartTransform;
    EDroneAttemptState AttemptState = EDroneAttemptState::MainMenu;
    EDroneFailureReason FailureReason = EDroneFailureReason::None;
    FString FailureDetail;
    FString PerformanceStatus;
    FTSTicker::FDelegateHandle PerformanceTicker;
    bool bPerformanceCaptureOwned=false;

    void UpdateTelemetry();
    void ClearFlightInput();
    void HandleFocusLost();
    void HandleApplicationActivation(bool bActive);
    void ApplyFlightControl(float DeltaSeconds, float ForwardInput, float StrafeInput, float YawInput, float VerticalInput);
    void SelectQuality(int32 Value);
    void SelectLowQuality();
    void SelectMediumQuality();
    void TogglePause();
    void ToggleCameraView();
    void ConfirmMenuAction();
    void RestartAttempt();
    void ReturnToMainMenu();
    void ExitGame();
    UFUNCTION()
    void HandleBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
    void UpdateAttempt(float DeltaSeconds);
};
