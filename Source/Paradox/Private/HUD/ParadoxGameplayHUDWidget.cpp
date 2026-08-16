#include "HUD/ParadoxGameplayHUDWidget.h"

#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Controllers/ParadoxPlayerController.h"
#include "HUD/ParadoxGameplayHUDComponent.h"
#include "Inventory/ParadoxInventoryWidget.h"
#include "Paradox.h"
#include "Settings/TacticalPauseSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Widgets/TacticalPauseControlsWidget.h"

void UParadoxGameplayHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureNativeFallbackTree();
	ApplyHUDMode(CurrentMode);
}

TSharedRef<SWidget> UParadoxGameplayHUDWidget::RebuildWidget()
{
	EnsureNativeFallbackTree();
	return Super::RebuildWidget();
}

void UParadoxGameplayHUDWidget::EnsureNativeFallbackTree()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->RootWidget)
	{
		BuildNativeFallbackTree();
	}
}

void UParadoxGameplayHUDWidget::AssignHUDContext(
	UParadoxGameplayHUDComponent* InHUDComponent,
	AParadoxPlayerController* InPlayerController)
{
	HUDComponent = InHUDComponent;
	ParadoxPlayerController = InPlayerController;
	ReceiveHUDContextAssigned();
}

void UParadoxGameplayHUDWidget::ClearHUDContext()
{
	ReceiveHUDContextCleared();
	HUDComponent.Reset();
	ParadoxPlayerController.Reset();
}

bool UParadoxGameplayHUDWidget::ApplyHUDMode(const EParadoxGameplayHUDMode NewMode)
{
	EnsureNativeFallbackTree();
	const EParadoxGameplayHUDMode PreviousMode = CurrentMode;
	CurrentMode = NewMode;
	if (!HUDModeSwitcher || HUDModeSwitcher->GetNumWidgets() < 2)
	{
		PARADOX_LOG_WARNING(
			TEXT("Gameplay HUD widget '%s' requires HUDModeSwitcher with page 0 Normal and page 1 Collapsed; requested mode cannot be presented."),
			*GetNameSafe(this));
		return false;
	}

	const int32 PageIndex = NewMode == EParadoxGameplayHUDMode::Collapsed
		? GetCollapsedModePageIndex()
		: GetNormalModePageIndex();
	HUDModeSwitcher->SetActiveWidgetIndex(PageIndex);
	if (PreviousMode != NewMode)
	{
		OnModeChanged(PreviousMode, NewMode);
	}
	return true;
}

void UParadoxGameplayHUDWidget::SetSectionVisibility(
	const EParadoxGameplayHUDSection Section,
	const ESlateVisibility NewVisibility)
{
	if (UPanelWidget* Container = GetSectionContainer(Section))
	{
		Container->SetVisibility(NewVisibility);
		return;
	}
	if (Section == EParadoxGameplayHUDSection::Equipment)
	{
		if (UParadoxInventoryWidget* EmbeddedEquipment = FindEmbeddedEquipmentWidget())
		{
			EmbeddedEquipment->SetVisibility(NewVisibility);
		}
	}
	else if (Section == EParadoxGameplayHUDSection::TacticalPause)
	{
		if (UTacticalPauseControlsWidget* EmbeddedTacticalPause =
			FindEmbeddedTacticalPauseWidget())
		{
			EmbeddedTacticalPause->SetVisibility(NewVisibility);
		}
	}
}

void UParadoxGameplayHUDWidget::OnModeChanged_Implementation(
	const EParadoxGameplayHUDMode PreviousMode,
	const EParadoxGameplayHUDMode NewMode)
{
}

void UParadoxGameplayHUDWidget::BuildNativeFallbackTree()
{
	HUDModeSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(
		UWidgetSwitcher::StaticClass(), TEXT("HUDModeSwitcher"));
	WidgetTree->RootWidget = HUDModeSwitcher;
	HUDModeSwitcher->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UHorizontalBox* NormalPage = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("NormalModeContainer"));
	NormalPage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	HUDModeSwitcher->AddChild(NormalPage);

	TacticalPauseContainer = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("TacticalPauseContainer"));
	EquipmentContainer = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("EquipmentContainer"));
	StatusContainer = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("StatusContainer"));
	NormalPage->AddChild(TacticalPauseContainer);
	NormalPage->AddChild(EquipmentContainer);
	NormalPage->AddChild(StatusContainer);

	const UTacticalPauseSettings* TacticalPauseSettings =
		GetDefault<UTacticalPauseSettings>();
	UClass* TacticalPauseClass = TacticalPauseSettings
		? TacticalPauseSettings->DefaultWidgetClass.LoadSynchronous()
		: nullptr;
	if (!TacticalPauseClass
		|| !TacticalPauseClass->IsChildOf(UTacticalPauseControlsWidget::StaticClass()))
	{
		PARADOX_LOG_WARNING(
			TEXT("Gameplay HUD native root could not resolve the Tactical Pause default widget class; using the native controls shell."));
		TacticalPauseClass = UTacticalPauseControlsWidget::StaticClass();
	}
	UTacticalPauseControlsWidget* TacticalPauseControls =
		WidgetTree->ConstructWidget<UTacticalPauseControlsWidget>(
			TacticalPauseClass, TEXT("TacticalPauseControls"));
	TacticalPauseContainer->AddChild(TacticalPauseControls);
	UParadoxInventoryWidget* Inventory = WidgetTree->ConstructWidget<UParadoxInventoryWidget>(
		UParadoxInventoryWidget::StaticClass(), TEXT("InventoryWidget"));
	EquipmentContainer->AddChild(Inventory);

	CollapsedModeContainer = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("CollapsedModeContainer"));
	CollapsedModeContainer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	HUDModeSwitcher->AddChild(CollapsedModeContainer);
}

UTacticalPauseControlsWidget* UParadoxGameplayHUDWidget::FindEmbeddedTacticalPauseWidget() const
{
	if (!WidgetTree)
	{
		return nullptr;
	}

	TArray<UWidget*> Widgets;
	WidgetTree->GetAllWidgets(Widgets);
	UTacticalPauseControlsWidget* Result = nullptr;
	for (UWidget* Widget : Widgets)
	{
		UTacticalPauseControlsWidget* TacticalPauseWidget =
			Cast<UTacticalPauseControlsWidget>(Widget);
		if (!TacticalPauseWidget)
		{
			continue;
		}
		if (!Result)
		{
			Result = TacticalPauseWidget;
			continue;
		}
		PARADOX_LOG_WARNING(
			TEXT("Gameplay HUD widget '%s' contains multiple embedded Tactical Pause widgets; '%s' is authoritative and '%s' is ignored."),
			*GetNameSafe(this),
			*GetNameSafe(Result),
			*GetNameSafe(TacticalPauseWidget));
	}
	return Result;
}

UParadoxInventoryWidget* UParadoxGameplayHUDWidget::FindEmbeddedEquipmentWidget() const
{
	if (!WidgetTree)
	{
		return nullptr;
	}

	TArray<UWidget*> Widgets;
	WidgetTree->GetAllWidgets(Widgets);
	UParadoxInventoryWidget* Result = nullptr;
	for (UWidget* Widget : Widgets)
	{
		UParadoxInventoryWidget* InventoryWidget = Cast<UParadoxInventoryWidget>(Widget);
		if (!InventoryWidget)
		{
			continue;
		}
		if (!Result)
		{
			Result = InventoryWidget;
			continue;
		}
		PARADOX_LOG_WARNING(
			TEXT("Gameplay HUD widget '%s' contains multiple embedded inventory widgets; '%s' is authoritative and '%s' is ignored."),
			*GetNameSafe(this),
			*GetNameSafe(Result),
			*GetNameSafe(InventoryWidget));
	}
	return Result;
}

UPanelWidget* UParadoxGameplayHUDWidget::GetSectionContainer(
	const EParadoxGameplayHUDSection Section) const
{
	switch (Section)
	{
	case EParadoxGameplayHUDSection::TacticalPause:
		return TacticalPauseContainer;
	case EParadoxGameplayHUDSection::Equipment:
		return EquipmentContainer;
	case EParadoxGameplayHUDSection::Status:
		return StatusContainer;
	default:
		return nullptr;
	}
}
