#include "ParadoxEditorToolsModule.h"

#include "ContentBrowserMenuContexts.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshVoxelBatch.h"
#include "ToolMenu.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FParadoxEditorToolsModule"

DEFINE_LOG_CATEGORY(LogParadoxEditorTools);

void FParadoxEditorToolsModule::StartupModule()
{
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FParadoxEditorToolsModule::RegisterMenus));
}

void FParadoxEditorToolsModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
}

void FParadoxEditorToolsModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* AssetMenu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu"));
    FToolMenuSection& AssetSection = AssetMenu->FindOrAddSection(TEXT("ParadoxEditorTools"));
    AssetSection.AddDynamicEntry(
        TEXT("ParadoxEditorTools_StaticMeshVoxelBatch_AssetAction"),
        FNewToolMenuSectionDelegate::CreateRaw(
            this,
            &FParadoxEditorToolsModule::PopulateStaticMeshVoxelBatchAssetMenu));

    UToolMenu* FolderMenu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.FolderContextMenu"));
    FToolMenuSection& FolderSection = FolderMenu->FindOrAddSection(TEXT("ParadoxEditorTools"));
    FolderSection.AddDynamicEntry(
        TEXT("ParadoxEditorTools_StaticMeshVoxelBatch_FolderAction"),
        FNewToolMenuSectionDelegate::CreateRaw(
            this,
            &FParadoxEditorToolsModule::PopulateStaticMeshVoxelBatchFolderMenu));
}

void FParadoxEditorToolsModule::PopulateStaticMeshVoxelBatchAssetMenu(FToolMenuSection& InSection)
{
    const UContentBrowserAssetContextMenuContext* Context =
        InSection.FindContext<UContentBrowserAssetContextMenuContext>();

    if (!Context || Context->SelectedAssets.IsEmpty())
    {
        return;
    }

    for (const FAssetData& AssetData : Context->SelectedAssets)
    {
        if (AssetData.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName())
        {
            return;
        }
    }

    const TArray<FAssetData> SelectedAssets = Context->SelectedAssets;

    InSection.AddMenuEntry(
        TEXT("ParadoxEditorTools_StaticMeshVoxelBatch_SelectedMeshes"),
        LOCTEXT("StaticMeshVoxelBatchSelectedMeshes_Label", "Static Mesh Voxel Batch"),
        LOCTEXT(
            "StaticMeshVoxelBatchSelectedMeshes_Tooltip",
            "Correct scale and bake one of nine bounds-based pivot presets into all selected voxel Static Mesh assets."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([SelectedAssets]()
        {
            ParadoxEditorTools::FStaticMeshVoxelBatch::OpenForAssets(SelectedAssets);
        })));
}

void FParadoxEditorToolsModule::PopulateStaticMeshVoxelBatchFolderMenu(FToolMenuSection& InSection)
{
    const UContentBrowserFolderContext* Context = InSection.FindContext<UContentBrowserFolderContext>();

    if (!Context || Context->SelectedPackagePaths.IsEmpty() || !Context->bCanBeModified)
    {
        return;
    }

    const TArray<FString> SelectedPackagePaths = Context->SelectedPackagePaths;

    InSection.AddMenuEntry(
        TEXT("ParadoxEditorTools_StaticMeshVoxelBatch_FolderMeshes"),
        LOCTEXT("StaticMeshVoxelBatchFolderMeshes_Label", "Static Mesh Voxel Batch"),
        LOCTEXT(
            "StaticMeshVoxelBatchFolderMeshes_Tooltip",
            "Find voxel Static Mesh assets in the selected folder or folders, optionally including subfolders, then correct their scale and bake their pivots."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([SelectedPackagePaths]()
        {
            ParadoxEditorTools::FStaticMeshVoxelBatch::OpenForFolders(SelectedPackagePaths);
        })));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FParadoxEditorToolsModule, ParadoxEditorTools)
