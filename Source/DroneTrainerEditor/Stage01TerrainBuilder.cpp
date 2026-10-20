#include "Stage01TerrainBuilder.h"
#include "Editor.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "AssetCompilingManager.h"
#include "PhysicsEngine/BodySetup.h"

bool UStage01TerrainBuilder::FlattenPrecisionField(ALandscape* Land)
{
    if (!Land || Land->GetActorLocation()!=FVector(-25200,-25200,0) || Land->GetActorScale3D()!=FVector(100,100,12.5) || Land->GetEditLayers().Num()!=1) return false;
    ULandscapeInfo* Info=Land->GetLandscapeInfo(); if (!Info) return false;
    Land->Modify();
    FLandscapeEditDataInterface Edit(Info);
    Edit.SetEditLayer(Land->GetEditLayer(0)->GetGuid());
    TArray<uint16> Heights; Heights.SetNumZeroed(505*505);
    Edit.GetHeightDataFast(0,0,504,504,Heights.GetData(),505);
    // Flat 70 x 60m at (110,-65)m, 10m shoulder; first sector x<=60m is untouched.
    for (int32 Y=147; Y<=227; ++Y) for (int32 X=317; X<=407; ++X)
    {
        const float D=FMath::Max(FMath::Abs(X-362.f)-35.f,FMath::Abs(Y-187.f)-30.f);
        const float T=FMath::Clamp(D/10.f,0.f,1.f), Blend=1-T*T*(3-2*T);
        Heights[Y*505+X]=FMath::RoundToInt(FMath::Lerp(float(Heights[Y*505+X]),32768.f,Blend));
    }
    Edit.SetHeightData(317,147,407,227,Heights.GetData()+147*505+317,505,true);
    Edit.Flush(); Land->ForceLayersFullUpdate(); Land->RecreateCollisionComponents(); Land->MarkPackageDirty();
    return true;
}

bool UStage01TerrainBuilder::FlattenTrainingBlock(ALandscape* Land)
{
    if (!Land || Land->GetActorLocation() != FVector(-25200,-25200,0) ||
        Land->GetActorScale3D() != FVector(100,100,12.5)) return false;
    ULandscapeInfo* Info = Land->GetLandscapeInfo();
    if (!Info) return false;
    if (Land->GetEditLayers().Num()!=1) return false;
    Land->Modify();
    FLandscapeEditDataInterface Edit(Info);
    Edit.SetEditLayer(Land->GetEditLayer(0)->GetGuid());
    TArray<uint16> Heights;
    Heights.SetNumZeroed(505*505);
    Edit.GetHeightDataFast(0,0,504,504,Heights.GetData(),505);
    TArray<float> Blend;
    Blend.SetNumUninitialized(505*505);
    for (int32 Y=0;Y<505;++Y) for (int32 X=0;X<505;++X)
    {
        const float LocalX=X-252, LocalY=Y-252+60;
        const float Distance=FMath::Max(FMath::Abs(LocalX)-60.f,FMath::Abs(LocalY)-50.f);
        const float T=FMath::Clamp(Distance/30.f,0.f,1.f);
        Blend[Y*505+X]=1-T*T*(3-2*T);
        Heights[Y*505+X]=FMath::RoundToInt(FMath::Lerp(float(Heights[Y*505+X]),32768.f,Blend[Y*505+X]));
    }
    Edit.SetHeightData(0,0,504,504,Heights.GetData(),505,true);
    // Repaint the changed block with the accepted layers; preserve the outer terrain.
    for (const FString Name : {TEXT("Grass"),TEXT("Dirt"),TEXT("Gravel"),TEXT("Forest")})
    {
        auto* Layer=LoadObject<ULandscapeLayerInfoObject>(nullptr,*(TEXT("/Game/TrainingField/Terrain/LI_")+Name));
        if (!Layer) return false;
        TArray<uint8> Weights;
        Weights.SetNumZeroed(505*505);
        Edit.GetWeightDataFast(Layer,0,0,504,504,Weights.GetData(),505);
        for (int32 Y=0;Y<505;++Y) for (int32 X=0;X<505;++X)
        {
            const int32 LX=X-252, LY=Y-252+60;
            const bool Prep=FMath::Abs(LX)<=15 && LY>=-48 && LY<=-33;
            const bool Road=FMath::Abs(LX)<=2 && LY>=-75 && LY<-48;
            const bool Gravel=Prep || Road;
            const float Target=((Gravel && Name==TEXT("Gravel")) || (!Gravel && Name==TEXT("Grass"))) ? 255.f : 0.f;
            Weights[Y*505+X]=FMath::RoundToInt(FMath::Lerp(float(Weights[Y*505+X]),Target,Blend[Y*505+X]));
        }
        Edit.SetAlphaData(Layer,0,0,504,504,Weights.GetData(),505,ELandscapeLayerPaintingRestriction::None,false,false);
    }
    Edit.Flush();
    Land->ForceLayersFullUpdate();
    Land->RecreateCollisionComponents();
    Land->UpdateAllComponentMaterialInstances();
    Land->MarkPackageDirty();
    return true;
}

FIntVector UStage01TerrainBuilder::GetTerrainStructure(ALandscape* Land)
{
    check(Land);
    return FIntVector(Land->LandscapeComponents.Num(), Land->NumSubsections, Land->SubsectionSizeQuads);
}

int32 UStage01TerrainBuilder::GetGeometryTriangleCount(UStaticMesh* Mesh)
{
    check(Mesh);
    return Mesh->GetNumTriangles(0);
}

ALandscape* UStage01TerrainBuilder::ImportTerrain(const FString& SourceDirectory, UMaterialInterface* Material)
{
    FAssetCompilingManager::Get().FinishAllCompilation();
    TArray<uint8> Raw;
    checkf(FFileHelper::LoadFileToArray(Raw, *(SourceDirectory / TEXT("HF_TrainingField.r16"))) && Raw.Num() == 505*505*2, TEXT("Invalid stage 01 height source"));
    TArray<uint16> Heights;
    Heights.SetNumUninitialized(505*505);
    FMemory::Memcpy(Heights.GetData(), Raw.GetData(), Raw.Num());
    UWorld* World = GEditor->GetEditorWorldContext().World();
    ALandscape* Land = World->SpawnActor<ALandscape>();
    Land->SetActorLocation(FVector(-25200, -25200, 0));
    Land->SetActorScale3D(FVector(100, 100, 12.5));
    Land->LandscapeMaterial = Material;
    Land->BodyInstance.SetResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
    TMap<FGuid, TArray<uint16>> HeightData;
    HeightData.Add(FGuid(), MoveTemp(Heights));
    TArray<FLandscapeImportLayerInfo> Layers;
    for (const FString Name : { TEXT("Grass"), TEXT("Dirt"), TEXT("Gravel"), TEXT("Forest") })
    {
        FLandscapeImportLayerInfo& Layer = Layers.AddDefaulted_GetRef();
        Layer.LayerName = FName(Name);
        const FString PackageName = TEXT("/Game/TrainingField/Terrain/LI_") + Name;
        UPackage* Package = CreatePackage(*PackageName);
        ULandscapeLayerInfoObject* Info = NewObject<ULandscapeLayerInfoObject>(Package, *FString(TEXT("LI_")+Name), RF_Public|RF_Standalone);
        Info->SetLayerName(Layer.LayerName, false);
        FAssetRegistryModule::AssetCreated(Info);
        Package->MarkPackageDirty();
        Layer.LayerInfo = Info;
        check(FFileHelper::LoadFileToArray(Layer.LayerData, *(SourceDirectory / (Name + TEXT(".r8"))))) ;
        check(Layer.LayerData.Num() == 505*505);
    }
    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> LayerData;
    LayerData.Add(FGuid(), MoveTemp(Layers));
    Land->Import(FGuid::NewGuid(), 0, 0, 504, 504, 2, 63, HeightData, *(SourceDirectory / TEXT("HF_TrainingField.png")), LayerData, ELandscapeImportAlphamapType::Additive, {});
    Land->SetActorLabel(TEXT("T01 - Landscape 504m"));
    Land->SetFolderPath(TEXT("Terrain"));
    Land->UpdateAllComponentMaterialInstances();
    Land->PostEditChange();
    return Land;
}

UStaticMesh* UStage01TerrainBuilder::BuildGeometry(const FString& AssetName, const TArray<FVector>& Vertices, const TArray<int32>& Triangles, UMaterialInterface* Material)
{
    check(Triangles.Num()%3 == 0 && Triangles.Num()/3 <= 20000);
    check(AssetName == TEXT("SM_BackgroundTerrain") || AssetName == TEXT("SM_TrainingPad"));
    const FString Name = TEXT("/Game/TrainingField/Terrain/") + AssetName;
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *(Name + TEXT(".") + AssetName));
    const bool bNewMesh = Mesh == nullptr;
    if (bNewMesh) Mesh = NewObject<UStaticMesh>(CreatePackage(*Name), *AssetName, RF_Public|RF_Standalone);
    Mesh->Modify();
    Mesh->GetStaticMaterials().Reset();
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description);
    Attributes.Register();
    Attributes.GetVertexInstanceUVs().SetNumChannels(1);
    TArray<FVertexID> IDs;
    for (const FVector& Position : Vertices)
    {
        FVertexID ID = Description.CreateVertex();
        Attributes.GetVertexPositions()[ID] = FVector3f(Position);
        IDs.Add(ID);
    }
    FPolygonGroupID Group = Description.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Ground");
    for (int32 I=0; I<Triangles.Num(); I+=3)
    {
        TArray<FVertexInstanceID> Corners;
        for (int32 J=0; J<3; ++J)
        {
            // UE MeshDescription uses clockwise front faces; authoring sources use +Z cross products.
            const int32 Index = Triangles[I+2-J];
            FVertexInstanceID Corner = Description.CreateVertexInstance(IDs[Index]);
            Attributes.GetVertexInstanceUVs().Set(Corner, 0, FVector2f(Vertices[Index].X/200, Vertices[Index].Y/200));
            Attributes.GetVertexInstanceNormals()[Corner] = FVector3f(FVector::CrossProduct(
                Vertices[Triangles[I+1]]-Vertices[Triangles[I]], Vertices[Triangles[I+2]]-Vertices[Triangles[I]]).GetSafeNormal());
            Corners.Add(Corner);
        }
        Description.CreatePolygon(Group, Corners);
    }
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Material));
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bBuildSimpleCollision = false;
    Mesh->BuildFromMeshDescriptions({&Description}, Params);
    if (AssetName == TEXT("SM_TrainingPad"))
    {
        Mesh->CreateBodySetup();
        Mesh->GetBodySetup()->AggGeom.EmptyElements();
        FKBoxElem Box(800, 800, 2);
        Box.Center = FVector(0, 0, -1);
        Mesh->GetBodySetup()->AggGeom.BoxElems.Add(Box);
        Mesh->GetBodySetup()->InvalidatePhysicsData();
        Mesh->GetBodySetup()->CreatePhysicsMeshes();
    }
    if (bNewMesh) FAssetRegistryModule::AssetCreated(Mesh);
    Mesh->MarkPackageDirty();
    return Mesh;
}
