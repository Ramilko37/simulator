#pragma once
#include "GameFramework/GameUserSettings.h"
#include "DroneTrainerSettings.generated.h"

UCLASS(Config=GameUserSettings)
class DRONETRAINER_API UDroneTrainerSettings : public UGameUserSettings
{
    GENERATED_BODY()
public:
    virtual void LoadSettings(bool bForceReload=false) override;
    virtual void ApplyNonResolutionSettings() override;
    virtual void ValidateSettings() override;
    TArray<FIntPoint> SupportedResolutions() const;
    UPROPERTY(Config) float Sensitivity=1.f;
    UPROPERTY(Config) float MasterVolume=0.7f;
    void SelectProfile(int32 Value);
    int32 GetProfile() const { return QualityProfile==TEXT("Low") ? 0 : 1; }
private:
    UPROPERTY(Config) FString QualityProfile=TEXT("Medium");
};
