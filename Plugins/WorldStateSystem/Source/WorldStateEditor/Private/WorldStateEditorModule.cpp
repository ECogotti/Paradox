#include "WorldStateEditorModule.h"

#include "Components/WorldStateParticipantComponent.h"
#include "Details/WorldStateParticipantComponentCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

void FWorldStateEditorModule::StartupModule()
{
	// Load PropertyEditor explicitly because the customization must be registered before details panels request it.
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyEditor.RegisterCustomClassLayout(
		UWorldStateParticipantComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FWorldStateParticipantComponentCustomization::MakeInstance));
	PropertyEditor.NotifyCustomizationModuleChanged();
}

void FWorldStateEditorModule::ShutdownModule()
{
	// Module availability is checked because editor shutdown order is not guaranteed across dependencies.
	if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		PropertyEditor->UnregisterCustomClassLayout(UWorldStateParticipantComponent::StaticClass()->GetFName());
		PropertyEditor->NotifyCustomizationModuleChanged();
	}
}

IMPLEMENT_MODULE(FWorldStateEditorModule, WorldStateEditor)
