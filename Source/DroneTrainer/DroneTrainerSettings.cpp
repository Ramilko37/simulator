#include "DroneTrainerSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"
#include "AudioDevice.h"
#include "Misc/ConfigCacheIni.h"
#include "String/LexFromString.h"

void UDroneTrainerSettings::LoadSettings(bool bForceReload)
{
    QualityProfile=TEXT("Medium");
    Sensitivity=1.f; MasterVolume=0.7f;
    Super::LoadSettings(bForceReload);
    // Parse raw text too: UE numeric import can turn malformed text into zero.
    for (const auto& Field : {TPair<const TCHAR*,float*>(TEXT("Sensitivity"),&Sensitivity), TPair<const TCHAR*,float*>(TEXT("MasterVolume"),&MasterVolume)})
    {
        FString Raw;
        if (GConfig->GetString(*GetClass()->GetPathName(),Field.Key,Raw,GGameUserSettingsIni) &&
            (!LexTryParseString(*Field.Value,*Raw) || !FMath::IsFinite(*Field.Value)))
            *Field.Value=Field.Value==&Sensitivity ? 1.f : 0.7f;
    }
    // Base ValidateSettings reloads missing/old config; never call it from LoadSettings.
    if (QualityProfile!=TEXT("Low") && QualityProfile!=TEXT("Medium")) QualityProfile=TEXT("Medium");
    if (!FMath::IsFinite(Sensitivity) || Sensitivity<0.5f || Sensitivity>1.5f) Sensitivity=1.f;
    if (!FMath::IsFinite(MasterVolume) || MasterVolume<0.f || MasterVolume>1.f) MasterVolume=0.7f;
}

void UDroneTrainerSettings::ApplyNonResolutionSettings()
{
    ValidateSettings();
    QualityProfile=GetProfile()==0 ? TEXT("Low") : TEXT("Medium");
    // Route CSV identifies Lumen final gather and shadows as the GPU hotspots.
    SetOverallScalabilityLevel(3);
    SetGlobalIlluminationQuality(1);
    SetShadowQuality(GetProfile()==0 ? 1 : 2);
    SetResolutionScaleValueEx(100);
    SetDynamicResolutionEnabled(false);
    Super::ApplyNonResolutionSettings();
    if (GEngine) if (auto Device=GEngine->GetMainAudioDevice()) Device->SetTransientPrimaryVolume(MasterVolume);
    UE_LOG(LogTemp,Display,TEXT("TRAINING_SETTINGS profile=%d GI=%d final_gather=%d shadow=%d reflection=%d foliage=%d resolution=%.1f AA=%d"),
        GetProfile(),GetGlobalIlluminationQuality(),IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.FinalGatherMethod"))->GetInt(),GetShadowQuality(),GetReflectionQuality(),GetFoliageQuality(),ScalabilityQuality.ResolutionQuality,
        IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt());
}

void UDroneTrainerSettings::SelectProfile(int32 Value)
{
    QualityProfile=Value==0 ? TEXT("Low") : TEXT("Medium");
    ApplyNonResolutionSettings();
    SaveSettings();
}

TArray<FIntPoint> UDroneTrainerSettings::SupportedResolutions() const
{
    TArray<FIntPoint> Values;
    UKismetSystemLibrary::GetSupportedFullscreenResolutions(Values);
    Values.RemoveAll([](FIntPoint P) { return P.X<1280 || P.Y<720; });
    Values.Sort([](FIntPoint A,FIntPoint B) { return A.X==B.X ? A.Y<B.Y : A.X<B.X; });
    if (Values.IsEmpty()) Values.Add(FIntPoint(1280,720));
    return Values;
}

void UDroneTrainerSettings::ValidateSettings()
{
    Super::ValidateSettings();
    if (QualityProfile!=TEXT("Low") && QualityProfile!=TEXT("Medium")) QualityProfile=TEXT("Medium");
    if (!FMath::IsFinite(Sensitivity) || Sensitivity<0.5f || Sensitivity>1.5f) Sensitivity=1.f;
    if (!FMath::IsFinite(MasterVolume) || MasterVolume<0.f || MasterVolume>1.f) MasterVolume=0.7f;
    const auto Values=SupportedResolutions();
    if (!Values.Contains(GetScreenResolution())) SetScreenResolution(Values.Contains(FIntPoint(1920,1080)) ? FIntPoint(1920,1080) : Values[0]);
    if (GetFullscreenMode()!=EWindowMode::Windowed && GetFullscreenMode()!=EWindowMode::Fullscreen && GetFullscreenMode()!=EWindowMode::WindowedFullscreen)
        SetFullscreenMode(EWindowMode::WindowedFullscreen);
}
