#if WITH_DEV_AUTOMATION_TESTS
#include "DroneTrainerSettings.h"
#include "DroneTrainerDronePawn.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "AudioDevice.h"
#include "AudioThread.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Components/AudioComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneTrainerSettingsTest,"DroneTrainer.Settings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDroneTrainerSettingsTest::RunTest(const FString&)
{
    auto* Settings=Cast<UDroneTrainerSettings>(GEngine->GetGameUserSettings());
    if (!TestNotNull(TEXT("Native settings class"),Settings)) return false;
    const int32 Original=Settings->GetProfile();
    const float OriginalSensitivity=Settings->Sensitivity, OriginalVolume=Settings->MasterVolume;
    const FIntPoint OriginalResolution=Settings->GetScreenResolution();
    const EWindowMode::Type OriginalWindow=Settings->GetFullscreenMode();
    // The process uses -UserDir/-GameUserSettingsINI; keep UE's native config identity.
    const FConfigFile OriginalConfig=*GConfig->FindConfigFile(GGameUserSettingsIni);
    Settings->SaveSettings();
    Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
    Settings->ValidateSettings();
    TestTrue(TEXT("Desktop fullscreen is retained, not downgraded to a small window"),Settings->GetFullscreenMode()==EWindowMode::WindowedFullscreen);
    Settings->SaveSettings(); Settings->LoadSettings(true);
    TestTrue(TEXT("Desktop fullscreen survives settings reload"),Settings->GetFullscreenMode()==EWindowMode::WindowedFullscreen);
    Settings->SetFullscreenMode(OriginalWindow);
    for (const TCHAR* Bad : {TEXT("nan"),TEXT("inf"),TEXT("oops"),TEXT("-1"),TEXT("99")})
    {
        for (const TCHAR* Field : {TEXT("Sensitivity"),TEXT("MasterVolume")})
            GConfig->SetString(*Settings->GetClass()->GetPathName(),Field,Bad,GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni); Settings->LoadSettings(true);
        TestEqual(TEXT("Invalid sensitivity uses safe default"),Settings->Sensitivity,1.f);
        TestEqual(TEXT("Invalid volume uses safe default"),Settings->MasterVolume,0.7f);
    }
    Settings->Sensitivity=1.3f; Settings->MasterVolume=0.2f;
    Settings->SaveSettings(); Settings->LoadSettings(true); Settings->ApplyNonResolutionSettings();
    TestEqual(TEXT("Sensitivity persisted"),Settings->Sensitivity,1.3f);
    TestEqual(TEXT("Volume persisted"),Settings->MasterVolume,0.2f);
    if (auto Device=GEngine->GetMainAudioDevice())
    {
        float Applied=-1;
        FAudioThread::RunCommandOnAudioThread([Device,&Applied]() { Applied=Device->GetTransientPrimaryVolume(); });
        FAudioCommandFence Fence; Fence.BeginFence(); Fence.Wait();
        TestEqual(TEXT("Volume applied to audio device"),Applied,0.2f);
    }
    else AddError(TEXT("Audio device unavailable for volume verification"));
    Settings->SetScreenResolution(FIntPoint(-1,0)); Settings->ValidateSettings();
    TestTrue(TEXT("Invalid resolution replaced by supported positive mode"),Settings->SupportedResolutions().Contains(Settings->GetScreenResolution()));
    for (int32 Value : {0,1})
    {
        Settings->SelectProfile(Value);
        Settings->LoadSettings(true);
        TestEqual(TEXT("Profile persisted and restored"),Settings->GetProfile(),Value);
        Settings->ApplyNonResolutionSettings();
        TestEqual(TEXT("Chosen shadow profile"),Settings->GetShadowQuality(),Value==0 ? 1 : 2);
        TestEqual(TEXT("GI profile"),Settings->GetGlobalIlluminationQuality(),1);
        TestEqual(TEXT("Internal resolution"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetFloat(),100.f);
        TestEqual(TEXT("AA remains off"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt(),0);
        TestFalse(TEXT("Dynamic resolution disabled"),Settings->IsDynamicResolutionEnabled());
        TestEqual(TEXT("Applied shadows"),IConsoleManager::Get().FindConsoleVariable(TEXT("sg.ShadowQuality"))->GetInt(),Settings->GetShadowQuality());
        TestEqual(TEXT("Applied reflections"),IConsoleManager::Get().FindConsoleVariable(TEXT("sg.ReflectionQuality"))->GetInt(),Settings->GetReflectionQuality());
    }
    for (const TCHAR* Bad : {TEXT("Unknown"),TEXT("-1"),TEXT("42"),TEXT("")})
    {
        GConfig->SetString(*Settings->GetClass()->GetPathName(),TEXT("QualityProfile"),Bad,GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
        Settings->LoadSettings(true);
        TestEqual(TEXT("Corrupt selection falls back to medium"),Settings->GetProfile(),1);
    }
    bool FoundPawn=false;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (!Context.World() || !Context.World()->IsGameWorld()) continue;
        for (TActorIterator<ADroneTrainerDronePawn> It(Context.World());It;++It)
        {
            FoundPawn=true;
            auto* D=*It;
            const auto State=D->AttemptState;
            const bool WasPaused=UGameplayStatics::IsGamePaused(Context.World());
            const bool Camera=D->bThirdPersonView;
            const auto Phase=D->LessonPhase;
            const int32 Gates=D->GatesPassed;
            const float Elapsed=D->ElapsedSeconds;
            D->AttemptState=EDroneAttemptState::Paused;
            UGameplayStatics::SetGamePaused(Context.World(),true);
            D->SelectQuality(0);
            D->SelectQuality(1);
            TestTrue(TEXT("Pause preserved"),UGameplayStatics::IsGamePaused(Context.World()));
            TestTrue(TEXT("Attempt remains paused"),D->AttemptState==EDroneAttemptState::Paused);
            TestEqual(TEXT("Camera preserved"),D->bThirdPersonView,Camera);
            TestTrue(TEXT("Lesson phase preserved"),D->LessonPhase==Phase);
            TestEqual(TEXT("Gate progress preserved"),D->GatesPassed,Gates);
            TestEqual(TEXT("Lesson clock preserved"),D->ElapsedSeconds,Elapsed);
            D->bSettingsOpen=false; D->ToggleSettings();
            TestTrue(TEXT("Settings accessible in pause"),D->bSettingsOpen);
            D->TogglePause();
            TestTrue(TEXT("Closing settings preserves pause"),D->AttemptState==EDroneAttemptState::Paused && UGameplayStatics::IsGamePaused(Context.World()));
            D->StopAttemptAudio();
            TestFalse(TEXT("Motor loop stopped"),D->Motors->IsPlaying());
            TestFalse(TEXT("Background stopped"),D->Ambience->IsPlaying());
            D->AttemptState=EDroneAttemptState::Running; D->ToggleSettings();
            TestFalse(TEXT("Settings do not steal running controls"),D->bSettingsOpen);
            D->AttemptState=State;
            UGameplayStatics::SetGamePaused(Context.World(),WasPaused);
        }
    }
    TestTrue(TEXT("Live Pawn checked"),FoundPawn);
    Settings->Sensitivity=OriginalSensitivity; Settings->MasterVolume=OriginalVolume;
    Settings->SetScreenResolution(OriginalResolution); Settings->SetFullscreenMode(OriginalWindow);
    Settings->SelectProfile(Original);
    *GConfig->FindConfigFile(GGameUserSettingsIni)=OriginalConfig;
    GConfig->FindConfigFile(GGameUserSettingsIni)->Dirty=true;
    GConfig->Flush(false,GGameUserSettingsIni);
    return true;
}
#endif
