#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"

class UStaticMesh;

namespace ParadoxEditorTools
{
    enum class EStaticMeshVoxelPivotPosition : uint8
    {
        Below,
        Center,
        Top
    };

    struct FStaticMeshVoxelBatchOptions
    {
        FVector ScaleMultiplier = FVector(1.0, 1.0, 1.0);
        EStaticMeshVoxelPivotPosition PivotX = EStaticMeshVoxelPivotPosition::Center;
        EStaticMeshVoxelPivotPosition PivotY = EStaticMeshVoxelPivotPosition::Center;
        EStaticMeshVoxelPivotPosition PivotZ = EStaticMeshVoxelPivotPosition::Center;  
        bool bIncludeSubfolders = true;
    };

    class FStaticMeshVoxelBatch final
    {
    public:
        static void OpenForAssets(const TArray<FAssetData>& InSelectedAssets);
        static void OpenForFolders(const TArray<FString>& InSelectedPackagePaths);

    private:
        static bool ShowOptionsDialog(bool bFolderMode, FStaticMeshVoxelBatchOptions& OutOptions);
        static void CollectMeshesFromAssetData(const TArray<FAssetData>& InAssetData, TArray<UStaticMesh*>& OutMeshes);
        static void CollectMeshesFromFolders(
            const TArray<FString>& InSelectedPackagePaths,
            bool bIncludeSubfolders,
            TArray<UStaticMesh*>& OutMeshes);
        static void Execute(const TArray<UStaticMesh*>& InMeshes, const FStaticMeshVoxelBatchOptions& InOptions);
        static bool TransformStaticMesh(
            UStaticMesh& InStaticMesh,
            const FStaticMeshVoxelBatchOptions& InOptions,
            FString& OutFailureReason);
    };
}
