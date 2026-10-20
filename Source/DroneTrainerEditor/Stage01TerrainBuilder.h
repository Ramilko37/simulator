#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Stage01TerrainBuilder.generated.h"

class ALandscape;
class UMaterialInterface;
class UStaticMesh;

// Native editor operations absent from this engine's Python/MCP Landscape API.
UCLASS()
class UStage01TerrainBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Lesson02")
    static bool FlattenPrecisionField(ALandscape* Landscape);
    UFUNCTION(BlueprintCallable, Category="Stage05")
    static bool FlattenTrainingBlock(ALandscape* Landscape);
    UFUNCTION(BlueprintPure, Category="Stage01")
    static FIntVector GetTerrainStructure(ALandscape* Landscape);
    UFUNCTION(BlueprintPure, Category="Stage01")
    static int32 GetGeometryTriangleCount(UStaticMesh* Mesh);
    UFUNCTION(BlueprintCallable, Category="Stage01")
    static ALandscape* ImportTerrain(const FString& SourceDirectory, UMaterialInterface* Material);
    UFUNCTION(BlueprintCallable, Category="Stage01")
    static UStaticMesh* BuildGeometry(const FString& AssetName, const TArray<FVector>& Vertices, const TArray<int32>& Triangles, UMaterialInterface* Material);
};
