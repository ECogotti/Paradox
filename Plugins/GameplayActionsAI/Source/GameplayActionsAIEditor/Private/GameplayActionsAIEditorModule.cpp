#include "GameplayActionsAIEditorModule.h"

#include "Customizations/GameplayActionExecutionSpecCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Types/GameplayActionExecutionSpec.h"

DEFINE_LOG_CATEGORY(LogGameplayActionsAIEditor);

void FGameplayActionsAIEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyEditor =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyEditor.RegisterCustomPropertyTypeLayout(
		FGameplayActionExecutionSpec::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(
			&FGameplayActionExecutionSpecCustomization::MakeInstance));
}

void FGameplayActionsAIEditorModule::ShutdownModule()
{
	if (FPropertyEditorModule* PropertyEditor =
		FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		PropertyEditor->UnregisterCustomPropertyTypeLayout(
			FGameplayActionExecutionSpec::StaticStruct()->GetFName());
	}
}

IMPLEMENT_MODULE(FGameplayActionsAIEditorModule, GameplayActionsAIEditor)
