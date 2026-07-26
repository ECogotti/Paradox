#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogParadoxEditorTools, Log, All);

#define PARADOXEDITORTOOLS_LOG_INFO(Format, ...) \
    UE_LOG(LogParadoxEditorTools, Log, TEXT(Format), ##__VA_ARGS__)

#define PARADOXEDITORTOOLS_LOG_WARNING(Format, ...) \
    UE_LOG(LogParadoxEditorTools, Warning, TEXT(Format), ##__VA_ARGS__)

#define PARADOXEDITORTOOLS_LOG_ERROR(Format, ...) \
    UE_LOG(LogParadoxEditorTools, Error, TEXT(Format), ##__VA_ARGS__)

//class FToolMenuSection;

class FParadoxEditorToolsModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
    void PopulateStaticMeshVoxelBatchAssetMenu(FToolMenuSection& InSection);
    void PopulateStaticMeshVoxelBatchFolderMenu(FToolMenuSection& InSection);
};
