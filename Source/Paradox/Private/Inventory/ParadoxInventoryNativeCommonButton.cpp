#include "Inventory/ParadoxInventoryNativeCommonButton.h"

#include "Blueprint/WidgetTree.h"
#include "CommonTextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"

void UParadoxInventoryNativeCommonButton::BuildActionContent(
	UImage*& OutIcon,
	UCommonTextBlock*& OutLabel)
{
	EnsureNativeWidgetTree();
	UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("ActionContent"));
	OutIcon = WidgetTree->ConstructWidget<UImage>(
		UImage::StaticClass(), TEXT("ActionIcon"));
	OutLabel = WidgetTree->ConstructWidget<UCommonTextBlock>(
		UCommonTextBlock::StaticClass(), TEXT("ActionLabel"));
	Content->AddChild(OutIcon);
	Content->AddChild(OutLabel);
	WidgetTree->RootWidget = Content;
}

void UParadoxInventoryNativeCommonButton::BuildTextContent(
	const FText& Label)
{
	EnsureNativeWidgetTree();
	UCommonTextBlock* LabelWidget = WidgetTree->ConstructWidget<UCommonTextBlock>(
		UCommonTextBlock::StaticClass(), TEXT("ButtonLabel"));
	LabelWidget->SetText(Label);
	WidgetTree->RootWidget = LabelWidget;
}

void UParadoxInventoryNativeCommonButton::EnsureNativeWidgetTree()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
}
