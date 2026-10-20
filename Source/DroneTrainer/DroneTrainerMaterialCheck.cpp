#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "Materials/MaterialInstance.h"
#include "Materials/Material.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "SceneViewExtension.h"
#include "RenderGraphResources.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "Engine/TextureLODSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
const TCHAR* MaterialRoot = TEXT("/Game/TrainingField/Materials/");

UWorld* GameWorld()
{
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) return Context.World();
    return nullptr;
}

// Opt-in stationary diagnostics. No camera movement, input injection or quality overrides.
class FMaterialViewCheck : public FSceneViewExtensionBase
{
    double Started = FPlatformTime::Seconds();
    bool bReported = false;
public:
    FMaterialViewCheck(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}
    virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder&, FSceneView& View,
        const FRenderTargetBindingSlots& Targets, TRDGUniformBufferRef<FSceneTextureUniformParameters>) override
    {
        if (bReported || FPlatformTime::Seconds() - Started < 8 || !Targets[0].GetTexture()) return;
        bReported = true;
        const FIntPoint Output = View.UnscaledViewRect.Size();
        const FIntPoint InternalBuffer = Targets[0].GetTexture()->Desc.Extent;
        UE_LOG(LogTemp, Display, TEXT("MATERIAL_VIEW output=%dx%d internal_buffer=%dx%d secondary_fraction=%.4f AA=%d motion_blur=%.3f dof_distance=%.3f bloom=%.3f"),
            Output.X, Output.Y, InternalBuffer.X, InternalBuffer.Y, View.Family->SecondaryViewFraction,
            int32(View.AntiAliasingMethod), View.FinalPostProcessSettings.MotionBlurAmount,
            View.FinalPostProcessSettings.DepthOfFieldFocalDistance, View.FinalPostProcessSettings.BloomIntensity);
        UE_LOG(LogTemp, Display, TEXT("MATERIAL_EXPOSURE min=%.4f max=%.4f bias=%.4f gamma=%.4f"),
            View.FinalPostProcessSettings.AutoExposureMinBrightness, View.FinalPostProcessSettings.AutoExposureMaxBrightness,
            View.FinalPostProcessSettings.AutoExposureBias, View.Family->RenderTarget->GetDisplayGamma());
    }
};

TSharedPtr<FMaterialViewCheck, ESPMode::ThreadSafe> ViewCheck;
FAutoConsoleCommand Diagnostics(TEXT("DroneTrainer.MaterialDiagnostics"), TEXT("Log stationary render/texture diagnostics after 12s; -Stage04AutoExit exits afterwards."),
    FConsoleCommandDelegate::CreateLambda([]
    {
        if (ViewCheck) return;
        ViewCheck = FSceneViewExtensions::NewExtension<FMaterialViewCheck>();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
        {
            for (const TCHAR* Name : {TEXT("r.ScreenPercentage"), TEXT("r.ScreenPercentage.Default"), TEXT("r.AntiAliasingMethod"),
                 TEXT("r.BloomQuality"), TEXT("r.MotionBlurQuality"), TEXT("r.DepthOfFieldQuality"), TEXT("r.Refraction.Blur"), TEXT("r.LensFlareQuality"), TEXT("r.TemporalAA.Upsampling"),
                 TEXT("r.TextureStreaming"), TEXT("r.Streaming.PoolSize"), TEXT("r.Streaming.MipBias"), TEXT("r.Tonemapper.Sharpen"), TEXT("r.DynamicRes.OperationMode"), TEXT("r.MaxAnisotropy"), TEXT("r.HDR.EnableHDROutput"), TEXT("r.TonemapperGamma")})
                if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
                    UE_LOG(LogTemp, Display, TEXT("MATERIAL_CVAR %s=%s"), Name, *Var->GetString());
            const UTextureLODSettings* LOD = UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings();
            UE_LOG(LogTemp, Display, TEXT("MATERIAL_FILTER world=%d group_max_aniso=%d"),
                int32(LOD->GetSamplerFilter(TEXTUREGROUP_World)), LOD->GetTextureLODGroup(TEXTUREGROUP_World).MaxAniso);
            for (TObjectIterator<UTexture2D> It; It; ++It)
                if (It->GetPathName().StartsWith(TEXT("/Game/TrainingField/Textures/Stage04/")))
                    UE_LOG(LogTemp, Display, TEXT("MATERIAL_MIP %s size=%dx%d total=%d resident=%d bias=%d srgb=%d compression=%d"),
                        *It->GetPathName(), It->GetSizeX(), It->GetSizeY(), It->GetNumMips(), It->GetNumResidentMips(),
                        It->LODBias, int32(It->SRGB), int32(It->CompressionSettings));
            UE_LOG(LogTemp, Display, TEXT("MATERIAL_DIAGNOSTICS_COMPLETE scope=stationary_not_visual_acceptance"));
            ViewCheck.Reset();
            if (FParse::Param(FCommandLine::Get(), TEXT("Stage04AutoExit"))) FPlatformMisc::RequestExit(false);
            return false;
        }), 12.0f);
    }));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneTrainerMaterialsTest, "DroneTrainer.Materials",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDroneTrainerMaterialsTest::RunTest(const FString&)
{
    UWorld* World = GameWorld();
    if (!TestNotNull(TEXT("Loaded game world"), World)) return false;
    UMaterialInstance* Material = LoadObject<UMaterialInstance>(nullptr, *(FString(MaterialRoot) + TEXT("MI_Landscape_TrainingField")));
    if (!TestNotNull(TEXT("Cooked Landscape instance"), Material)) return false;
    TestEqual(TEXT("No extra exposure boost"), IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.Bias"))->GetFloat(), 0.0f);
    TestEqual(TEXT("Ground grazing-angle anisotropy"), IConsoleManager::Get().FindConsoleVariable(TEXT("r.MaxAnisotropy"))->GetInt(), 16);
    int32 Landscapes = 0, Pads = 0, PrecisionPads = 0;
    TArray<UTexture*> Textures;
    for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
    {
        ++Landscapes;
        TestTrue(TEXT("Landscape actually uses new instance"), It->LandscapeMaterial == Material);
        for (ULandscapeComponent* Component : It->LandscapeComponents)
        {
            int32 MaxActiveLayers = 0;
            for (int32 Index = 0; Index < Component->GetMaterialInstanceCount(false); ++Index)
            {
                UMaterialInstance* Instance = Component->GetMaterialInstance(Index, false);
                if (!TestNotNull(TEXT("Cooked component material instance"), Instance)) continue;
                TArray<UTexture*> Used;
                Instance->GetUsedTextures(Used);
                int32 ActiveLayers = 0;
                for (UTexture* Texture : Used)
                    if (Texture->GetPathName().StartsWith(TEXT("/Game/TrainingField/Textures/Stage04/")))
                    {
                        Textures.AddUnique(Texture);
                        if (Texture->GetName().EndsWith(TEXT("_BC"))) ++ActiveLayers;
                    }
                MaxActiveLayers = FMath::Max(MaxActiveLayers, ActiveLayers);
            }
            TestTrue(TEXT("At most four semantic layers"), MaxActiveLayers >= 1 && MaxActiveLayers <= 4);
            UE_LOG(LogTemp, Display, TEXT("MATERIAL_LAYER_BUDGET component=%s active_layers=%d"), *Component->GetName(), MaxActiveLayers);
        }
    }
    TestEqual(TEXT("One Landscape"), Landscapes, 1);
    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
        if (Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName() == TEXT("SM_TrainingPad"))
        {
            ++Pads;
            if (It->ActorHasTag(TEXT("L2_A2")) || It->ActorHasTag(TEXT("L2_B2"))) ++PrecisionPads;
            TestTrue(TEXT("Pad surface assigned"), Mesh->GetMaterial(0) && Mesh->GetMaterial(0)->GetName() == TEXT("M_TrainingPad_NoZFight"));
            TestTrue(TEXT("Pad mesh bounds unchanged"),FMath::IsNearlyZero(Mesh->GetStaticMesh()->GetBoundingBox().Max.Z,.01));
            TestTrue(TEXT("Pad gameplay bounds unchanged"),FMath::IsNearlyZero(It->GetComponentsBoundingBox().Max.Z,.01));
            FLinearColor Offset;
            TestTrue(TEXT("Pad shader separates visible surface"),Mesh->GetMaterial(0)->GetVectorParameterValue(FMaterialParameterInfo(TEXT("SurfaceOffsetCm")),Offset) && Offset.Equals(FLinearColor(0,0,1,0),.01f));
            const UBodySetup* Body=Mesh->GetStaticMesh()->GetBodySetup();
            if (TestNotNull(TEXT("Pad body"),Body) && TestEqual(TEXT("One pad box"),Body->AggGeom.BoxElems.Num(),1))
            {
                const FKBoxElem& Box=Body->AggGeom.BoxElems[0];
                TestTrue(TEXT("Pad contact surface unchanged"),FMath::IsNearlyZero(Box.Center.Z+Box.Z*.5,.01));
            }
        }
    }
    for (const TCHAR* Name : {TEXT("r.BloomQuality"),TEXT("r.MotionBlurQuality"),TEXT("r.DepthOfFieldQuality"),TEXT("r.Refraction.Blur"),TEXT("r.LensFlareQuality"),TEXT("r.TemporalAA.Upsampling"),TEXT("r.AntiAliasingMethod")})
    {
        const IConsoleVariable* Var=IConsoleManager::Get().FindConsoleVariable(Name);
        if (TestNotNull(Name,Var)) TestEqual(Name,Var->GetInt(),0);
    }
    TestEqual(TEXT("Both first lesson pads retained"), Pads-PrecisionPads, 2);
    TestEqual(TEXT("Two precision lesson pads"), PrecisionPads, 2);
    TestEqual(TEXT("Four PBR sets plus macro"), Textures.Num(), 13);
    for (UTexture* Texture : Textures)
    {
        UTexture2D* Tex = Cast<UTexture2D>(Texture);
        if (!TestNotNull(TEXT("Texture2D"), Tex)) continue;
        const FString Name = Tex->GetName();
        const int32 ExpectedSize = Name == TEXT("T_MacroVariation") ? 512 : 2048;
        TestEqual(Name + TEXT(" cooked width"), Tex->GetSizeX(), ExpectedSize);
        TestEqual(Name + TEXT(" cooked height"), Tex->GetSizeY(), ExpectedSize);
        TestTrue(Name + TEXT(" mip chain"), Tex->GetNumMips() > 1);
        TestEqual(Name + TEXT(" color space"), bool(Tex->SRGB), Name.EndsWith(TEXT("_BC")));
        TestEqual(Name + TEXT(" LOD bias"), Tex->LODBias, 0);
        TestFalse(Name + TEXT(" streaming allowed"), bool(Tex->NeverStream));
#if WITH_EDITORONLY_DATA
        TestFalse(Name + TEXT(" DX normal not flipped twice"), bool(Tex->bFlipGreenChannel));
#endif
        const TextureCompressionSettings Compression = Name.EndsWith(TEXT("_N")) ? TC_Normalmap : Name.EndsWith(TEXT("_BC")) ? TC_Default : TC_Masks;
        TestEqual(Name + TEXT(" compression"), int32(Tex->CompressionSettings), int32(Compression));
    }
    for (const TCHAR* Layer : {TEXT("Grass"), TEXT("Dirt"), TEXT("Gravel"), TEXT("Forest")})
    {
        float Tile = 0;
        TestTrue(TEXT("Detail scale parameter"), Material->GetScalarParameterValue(FMaterialParameterInfo(FName(FString(Layer) + TEXT("TileCm"))), Tile));
        TestTrue(TEXT("Detail tile 2-4 m"), Tile >= 200 && Tile <= 400);
    }
    float Macro = 0;
    TestTrue(TEXT("Macro scale parameter"), Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("MacroTileCm")), Macro));
    TestEqual(TEXT("Macro 30 m"), Macro, 3000.0f);
    return !HasAnyErrors();
}
#endif
