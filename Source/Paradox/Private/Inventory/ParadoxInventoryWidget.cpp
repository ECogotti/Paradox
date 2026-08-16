#include "Inventory/ParadoxInventoryWidget.h"

#include "Blueprint/WidgetTree.h"
#include "CommonButtonBase.h"
#include "CommonTextBlock.h"
#include "Characters/ParadoxCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Engine/Texture2D.h"
#include "Inventory/ParadoxDropAction.h"
#include "Inventory/ParadoxInventoryActionButtonWidget.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxInventoryNativeCommonButton.h"
#include "Inventory/ParadoxPickupableAction.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "Paradox.h"

#define LOCTEXT_NAMESPACE "ParadoxInventoryWidget"

namespace UE::Paradox::InventoryWidget::Private
{
	FGameplayActionSubmissionResult MakeActionFailure(const FString& Diagnostic)
	{
		FGameplayActionSubmissionResult Result;
		Result.Status = EGameplayActionSubmissionStatus::RejectedInvalidRequest;
		Result.ReasonTag = ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest;
		Result.DiagnosticMessage = Diagnostic;
		return Result;
	}
}

UParadoxInventoryWidget::UParadoxInventoryWidget(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ActionButtonWidgetClass = UParadoxInventoryActionButtonWidget::StaticClass();
}

void UParadoxInventoryWidget::SetInventoryCharacter(AParadoxCharacter* Character)
{
	if (InventoryCharacter.Get() == Character)
	{
		return;
	}
	AParadoxPickupableActor* PreviousItem = GetEquippedPickupable();
	UnbindPresentationSources();
	UnbindInventory();
	InventoryCharacter = Character;
	BindInventory();
	BindPresentationSources();
	AParadoxPickupableActor* NewItem = GetEquippedPickupable();
	ReceiveInventoryItemChanged(PreviousItem, NewItem);
	RefreshInventoryPresentation(true);
}

UParadoxInventoryComponent* UParadoxInventoryWidget::GetInventoryComponent() const
{
	return InventoryCharacter.IsValid()
		? InventoryCharacter->GetInventoryComponent()
		: nullptr;
}

AParadoxPickupableActor* UParadoxInventoryWidget::GetEquippedPickupable() const
{
	UParadoxInventoryComponent* Inventory = GetInventoryComponent();
	return Inventory ? Inventory->GetEquippedItem() : nullptr;
}

bool UParadoxInventoryWidget::HasEquippedItem() const
{
	UParadoxInventoryComponent* Inventory = GetInventoryComponent();
	return Inventory && Inventory->HasItem();
}

TArray<UParadoxPickupableAction*> UParadoxInventoryWidget::GetPickupableActions() const
{
	AParadoxPickupableActor* Pickupable = GetEquippedPickupable();
	return Pickupable ? Pickupable->GetPickupableActions() : TArray<UParadoxPickupableAction*>();
}

bool UParadoxInventoryWidget::CanDrop() const
{
	const AParadoxPlayerController* Controller =
		Cast<AParadoxPlayerController>(GetOwningPlayer());
	const UParadoxDropTargetingComponent* Targeting = Controller
		? Controller->GetDropTargetingComponent()
		: nullptr;
	return InventoryCharacter.IsValid()
		&& InventoryCharacter->GetWorld() == GetWorld()
		&& InventoryCharacter->GetController()
		&& InventoryCharacter->GetGameplayActionComponent()
		&& HasEquippedItem()
		&& Controller
		&& Controller->IsLocalPlayerController()
		&& Targeting
		&& IsValid(Targeting->DropActionDefinition.Get())
		&& !Targeting->IsDropTargetingActive();
}

FParadoxDropTargetingResult UParadoxInventoryWidget::RequestDrop()
{
	if (!InventoryCharacter.IsValid() || InventoryCharacter->GetWorld() != GetWorld())
	{
		FParadoxDropTargetingResult Result;
		Result.Status = EParadoxDropTargetingStatus::InvalidCharacter;
		Result.DiagnosticMessage = TEXT("Inventory widget requires an explicitly bound Paradox Character in the same World.");
		return Result;
	}
	AParadoxPlayerController* Controller = Cast<AParadoxPlayerController>(GetOwningPlayer());
	if (!Controller || !Controller->GetDropTargetingComponent())
	{
		FParadoxDropTargetingResult Result;
		Result.Status = EParadoxDropTargetingStatus::InvalidController;
		Result.DiagnosticMessage = TEXT("Inventory widget requires an owning Paradox Player Controller with Drop Targeting.");
		return Result;
	}
	return Controller->GetDropTargetingComponent()->BeginDropTargeting(
		InventoryCharacter.Get());
}

bool UParadoxInventoryWidget::CanExecutePickupableAction(
	UParadoxPickupableAction* Action) const
{
	return IsValid(Action) && Action->EvaluateExecution(InventoryCharacter.Get()).IsAccepted();
}

FGameplayActionSubmissionResult UParadoxInventoryWidget::RequestPickupableAction(
	UParadoxPickupableAction* Action)
{
	if (!IsValid(Action))
	{
		return UE::Paradox::InventoryWidget::Private::MakeActionFailure(
			TEXT("A valid Pickupable Action descriptor is required."));
	}
	return Action->RequestExecute(InventoryCharacter.Get());
}

void UParadoxInventoryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureNativeFallbackTree();
}

TSharedRef<SWidget> UParadoxInventoryWidget::RebuildWidget()
{
	EnsureNativeFallbackTree();
	return Super::RebuildWidget();
}

void UParadoxInventoryWidget::EnsureNativeFallbackTree()
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

void UParadoxInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (DropButton)
	{
		DropButton->OnClicked().RemoveAll(this);
		DropButton->OnClicked().AddUObject(this, &ThisClass::HandleDropClicked);
	}
	BindInventory();
	BindPresentationSources();
	RefreshInventoryPresentation(true);
}

void UParadoxInventoryWidget::NativeDestruct()
{
	if (DropButton)
	{
		DropButton->OnClicked().RemoveAll(this);
	}
	UnbindPresentationSources();
	UnbindInventory();
	GeneratedActionButtons.Reset();
	Super::NativeDestruct();
}

void UParadoxInventoryWidget::RefreshInventoryPresentation(
	const bool bRebuildActions)
{
	AParadoxPickupableActor* EquippedItem = GetEquippedPickupable();
	const bool bHasItem = IsValid(EquippedItem);
	if (EquipmentStateSwitcher && EquipmentStateSwitcher->GetNumWidgets() >= 2)
	{
		EquipmentStateSwitcher->SetActiveWidgetIndex(bHasItem ? 1 : 0);
	}
	if (EmptySlotIcon)
	{
		UTexture2D* Texture = EmptySlotTexture.LoadSynchronous();
		EmptySlotIcon->SetBrushFromTexture(Texture);
		EmptySlotIcon->SetVisibility(Texture
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
	if (EquippedItemName)
	{
		EquippedItemName->SetText(bHasItem
			? EquippedItem->GetPickupableDisplayName()
			: FText::GetEmpty());
	}
	if (EquippedItemIcon)
	{
		UTexture2D* Texture = bHasItem
			? EquippedItem->GetPickupableIcon().LoadSynchronous()
			: nullptr;
		EquippedItemIcon->SetBrushFromTexture(Texture);
		EquippedItemIcon->SetVisibility(Texture
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
	const bool bDropEnabled = CanDrop();
	if (DropButton)
	{
		DropButton->SetIsInteractionEnabled(bDropEnabled);
	}
	if (bRebuildActions)
	{
		RebuildActionButtons();
	}
	else
	{
		RefreshActionButtonAvailability();
	}
	ReceiveInventoryPresentationUpdated(EquippedItem, bDropEnabled);
}

void UParadoxInventoryWidget::BuildNativeFallbackTree()
{
	EquipmentStateSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(
		UWidgetSwitcher::StaticClass(), TEXT("EquipmentStateSwitcher"));
	WidgetTree->RootWidget = EquipmentStateSwitcher;

	UVerticalBox* EmptyPage = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("EmptyEquipmentPage"));
	EmptySlotIcon = WidgetTree->ConstructWidget<UImage>(
		UImage::StaticClass(), TEXT("EmptySlotIcon"));
	UCommonTextBlock* EmptyLabel = WidgetTree->ConstructWidget<UCommonTextBlock>(
		UCommonTextBlock::StaticClass(), TEXT("EmptySlotLabel"));
	EmptyLabel->SetText(LOCTEXT("EmptySlot", "Empty"));
	EmptyPage->AddChild(EmptySlotIcon);
	EmptyPage->AddChild(EmptyLabel);
	EquipmentStateSwitcher->AddChild(EmptyPage);

	UVerticalBox* OccupiedPage = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("OccupiedEquipmentPage"));
	EquippedItemIcon = WidgetTree->ConstructWidget<UImage>(
		UImage::StaticClass(), TEXT("EquippedItemIcon"));
	EquippedItemName = WidgetTree->ConstructWidget<UCommonTextBlock>(
		UCommonTextBlock::StaticClass(), TEXT("EquippedItemName"));
	UParadoxInventoryNativeCommonButton* NativeDropButton =
		WidgetTree->ConstructWidget<UParadoxInventoryNativeCommonButton>(
			UParadoxInventoryNativeCommonButton::StaticClass(), TEXT("DropButton"));
	DropButton = NativeDropButton;
	NativeDropButton->BuildTextContent(LOCTEXT("Drop", "Drop"));
	SpecialActionsContainer = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("SpecialActionsContainer"));
	OccupiedPage->AddChild(EquippedItemIcon);
	OccupiedPage->AddChild(EquippedItemName);
	OccupiedPage->AddChild(DropButton);
	OccupiedPage->AddChild(SpecialActionsContainer);
	EquipmentStateSwitcher->AddChild(OccupiedPage);
}

void UParadoxInventoryWidget::BindInventory()
{
	UParadoxInventoryComponent* Inventory = GetInventoryComponent();
	if (!Inventory || BoundInventory.Get() == Inventory)
	{
		return;
	}
	UnbindInventory();
	BoundInventory = Inventory;
	Inventory->OnEquippedItemChanged.AddUniqueDynamic(
		this,
		&ThisClass::HandleEquippedItemChanged);
}

void UParadoxInventoryWidget::UnbindInventory()
{
	if (BoundInventory.IsValid())
	{
		BoundInventory->OnEquippedItemChanged.RemoveDynamic(
			this,
			&ThisClass::HandleEquippedItemChanged);
	}
	BoundInventory.Reset();
}

void UParadoxInventoryWidget::BindPresentationSources()
{
	UnbindPresentationSources();
	BoundPickupable = GetEquippedPickupable();
	if (BoundPickupable.IsValid())
	{
		BoundPickupable->OnPickupableActionsChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandlePickupableActionsChanged);
	}
	BoundGameplayActions = InventoryCharacter.IsValid()
		? InventoryCharacter->GetGameplayActionComponent()
		: nullptr;
	if (BoundGameplayActions.IsValid())
	{
		BoundGameplayActions->OnActionEvent.AddUniqueDynamic(
			this,
			&ThisClass::HandleGameplayActionEvent);
	}
	AParadoxPlayerController* Controller = Cast<AParadoxPlayerController>(GetOwningPlayer());
	BoundDropTargeting = Controller ? Controller->GetDropTargetingComponent() : nullptr;
	if (BoundDropTargeting.IsValid())
	{
		BoundDropTargeting->OnDropTargetingChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleDropTargetingChanged);
	}
}

void UParadoxInventoryWidget::UnbindPresentationSources()
{
	if (BoundPickupable.IsValid())
	{
		BoundPickupable->OnPickupableActionsChanged.RemoveDynamic(
			this,
			&ThisClass::HandlePickupableActionsChanged);
	}
	if (BoundGameplayActions.IsValid())
	{
		BoundGameplayActions->OnActionEvent.RemoveDynamic(
			this,
			&ThisClass::HandleGameplayActionEvent);
	}
	if (BoundDropTargeting.IsValid())
	{
		BoundDropTargeting->OnDropTargetingChanged.RemoveDynamic(
			this,
			&ThisClass::HandleDropTargetingChanged);
	}
	BoundPickupable.Reset();
	BoundGameplayActions.Reset();
	BoundDropTargeting.Reset();
}

void UParadoxInventoryWidget::RebuildActionButtons()
{
	if (!SpecialActionsContainer)
	{
		return;
	}
	SpecialActionsContainer->ClearChildren();
	GeneratedActionButtons.Reset();
	UClass* EntryClass = ActionButtonWidgetClass.Get();
	if (!EntryClass || !EntryClass->IsChildOf(UParadoxInventoryActionButtonWidget::StaticClass()))
	{
		EntryClass = UParadoxInventoryActionButtonWidget::StaticClass();
	}
	for (UParadoxPickupableAction* Action : GetPickupableActions())
	{
		if (!IsValid(Action))
		{
			continue;
		}
		UParadoxInventoryActionButtonWidget* Entry =
			CreateWidget<UParadoxInventoryActionButtonWidget>(GetOwningPlayer(), EntryClass);
		if (!Entry)
		{
			PARADOX_LOG_WARNING(
				TEXT("Inventory widget '%s' could not create action entry for '%s'."),
				*GetNameSafe(this),
				*GetNameSafe(Action));
			continue;
		}
		Entry->SetPickupableAction(Action);
		Entry->OnActionRequested.AddUniqueDynamic(
			this,
			&ThisClass::HandleSpecialActionRequested);
		Entry->RefreshActionPresentation(CanExecutePickupableAction(Action));
		SpecialActionsContainer->AddChild(Entry);
		GeneratedActionButtons.Add(Entry);
	}
}

void UParadoxInventoryWidget::RefreshActionButtonAvailability()
{
	for (UParadoxInventoryActionButtonWidget* Entry : GeneratedActionButtons)
	{
		if (IsValid(Entry))
		{
			Entry->RefreshActionPresentation(
				CanExecutePickupableAction(Entry->GetPickupableAction()));
		}
	}
}

void UParadoxInventoryWidget::HandleEquippedItemChanged(
	AParadoxPickupableActor* PreviousItem,
	AParadoxPickupableActor* NewItem)
{
	BindPresentationSources();
	ReceiveInventoryItemChanged(PreviousItem, NewItem);
	RefreshInventoryPresentation(true);
}

void UParadoxInventoryWidget::HandlePickupableActionsChanged(
	AParadoxPickupableActor* Pickupable)
{
	if (Pickupable == GetEquippedPickupable())
	{
		RefreshInventoryPresentation(true);
	}
}

void UParadoxInventoryWidget::HandleGameplayActionEvent(
	const FGameplayActionEvent& Event)
{
	RefreshInventoryPresentation(false);
}

void UParadoxInventoryWidget::HandleDropTargetingChanged(
	const bool bIsTargeting)
{
	RefreshInventoryPresentation(false);
}

void UParadoxInventoryWidget::HandleDropClicked()
{
	const FParadoxDropTargetingResult Result = RequestDrop();
	if (!Result.IsSuccess())
	{
		PARADOX_LOG_WARNING(
			TEXT("Inventory HUD Drop request was rejected for '%s': %s"),
			*GetNameSafe(InventoryCharacter.Get()),
			*Result.DiagnosticMessage);
	}
	RefreshInventoryPresentation(false);
}

void UParadoxInventoryWidget::HandleSpecialActionRequested(
	UParadoxPickupableAction* Action)
{
	const FGameplayActionSubmissionResult Result = RequestPickupableAction(Action);
	if (!Result.IsAccepted())
	{
		PARADOX_LOG_WARNING(
			TEXT("Inventory HUD action '%s' was rejected for '%s': %s"),
			*GetNameSafe(Action),
			*GetNameSafe(InventoryCharacter.Get()),
			*Result.DiagnosticMessage);
	}
	RefreshInventoryPresentation(false);
}

#undef LOCTEXT_NAMESPACE
