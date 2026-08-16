#include "Inventory/ParadoxInventoryActionButtonWidget.h"

#include "Blueprint/WidgetTree.h"
#include "CommonButtonBase.h"
#include "CommonTextBlock.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Inventory/ParadoxInventoryNativeCommonButton.h"
#include "Inventory/ParadoxPickupableAction.h"

void UParadoxInventoryActionButtonWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureNativeFallbackTree();
}

TSharedRef<SWidget> UParadoxInventoryActionButtonWidget::RebuildWidget()
{
	EnsureNativeFallbackTree();
	return Super::RebuildWidget();
}

void UParadoxInventoryActionButtonWidget::EnsureNativeFallbackTree()
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

void UParadoxInventoryActionButtonWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ActionButton)
	{
		ActionButton->OnClicked().RemoveAll(this);
		ActionButton->OnClicked().AddUObject(
			this, &ThisClass::HandleActionButtonClicked);
	}
	RefreshActionPresentation(bPresentationEnabled);
}

void UParadoxInventoryActionButtonWidget::NativeDestruct()
{
	if (ActionButton)
	{
		ActionButton->OnClicked().RemoveAll(this);
	}
	Super::NativeDestruct();
}

void UParadoxInventoryActionButtonWidget::SetPickupableAction(
	UParadoxPickupableAction* InAction)
{
	PickupableAction = InAction;
	RefreshActionPresentation(bPresentationEnabled);
}

void UParadoxInventoryActionButtonWidget::RefreshActionPresentation(
	const bool bActionEnabled)
{
	bPresentationEnabled = bActionEnabled;
	if (ActionButton)
	{
		ActionButton->SetIsInteractionEnabled(
			IsValid(PickupableAction) && bActionEnabled);
	}
	if (ActionLabel)
	{
		ActionLabel->SetText(IsValid(PickupableAction)
			? PickupableAction->DisplayName
			: FText::GetEmpty());
	}
	if (ActionIcon)
	{
		UTexture2D* Texture = IsValid(PickupableAction)
			? PickupableAction->Icon.LoadSynchronous()
			: nullptr;
		ActionIcon->SetBrushFromTexture(Texture);
		ActionIcon->SetVisibility(Texture
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
	ReceiveActionPresentationUpdated(PickupableAction, bActionEnabled);
}

void UParadoxInventoryActionButtonWidget::BuildNativeFallbackTree()
{
	UParadoxInventoryNativeCommonButton* NativeButton =
		WidgetTree->ConstructWidget<UParadoxInventoryNativeCommonButton>(
			UParadoxInventoryNativeCommonButton::StaticClass(), TEXT("ActionButton"));
	ActionButton = NativeButton;
	WidgetTree->RootWidget = ActionButton;
	UImage* NativeIcon = nullptr;
	UCommonTextBlock* NativeLabel = nullptr;
	NativeButton->BuildActionContent(NativeIcon, NativeLabel);
	ActionIcon = NativeIcon;
	ActionLabel = NativeLabel;
}

void UParadoxInventoryActionButtonWidget::HandleActionButtonClicked()
{
	if (IsValid(PickupableAction) && bPresentationEnabled)
	{
		OnActionRequested.Broadcast(PickupableAction);
	}
}
