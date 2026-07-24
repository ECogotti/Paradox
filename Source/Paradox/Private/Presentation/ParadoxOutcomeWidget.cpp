#include "Presentation/ParadoxOutcomeWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"

void UParadoxOutcomeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildNativeFallbackLayout();
	}
}

void UParadoxOutcomeWidget::SetOutcomeData(
	const FParadoxOutcomePresentationData& InData)
{
	OutcomeData = InData;
	ReceiveOutcomeDataChanged(OutcomeData);
}

void UParadoxOutcomeWidget::SetPresentationOpacity(const float InOpacity)
{
	SetRenderOpacity(FMath::Clamp(InOpacity, 0.0f, 1.0f));
}

void UParadoxOutcomeWidget::ReceiveOutcomeDataChanged_Implementation(
	const FParadoxOutcomePresentationData& InData)
{
	if (TitleText)
	{
		TitleText->SetText(InData.Title);
	}
	if (MessageText)
	{
		MessageText->SetText(InData.Message);
	}
	if (RestartButton)
	{
		RestartButton->SetVisibility(
			InData.bShowRestart
				? ESlateVisibility::Visible
				: ESlateVisibility::Collapsed);
	}
}

void UParadoxOutcomeWidget::BuildNativeFallbackLayout()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(
		UOverlay::StaticClass(),
		TEXT("OutcomeRoot"));
	WidgetTree->RootWidget = Root;

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(),
		TEXT("BlackBackdrop"));
	Backdrop->SetBrushColor(FLinearColor::Black);
	if (UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop))
	{
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(),
		TEXT("OutcomeContent"));
	if (UOverlaySlot* ContentSlot = Root->AddChildToOverlay(Content))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Center);
		ContentSlot->SetVerticalAlignment(VAlign_Center);
	}

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(),
		TEXT("OutcomeTitle"));
	FSlateFontInfo TitleFont = TitleText->GetFont();
	TitleFont.Size = 38;
	TitleText->SetFont(TitleFont);
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	TitleText->SetJustification(ETextJustify::Center);
	Content->AddChildToVerticalBox(TitleText);

	USpacer* TitleSpacer = WidgetTree->ConstructWidget<USpacer>(
		USpacer::StaticClass(),
		TEXT("TitleSpacer"));
	TitleSpacer->SetSize(FVector2D(1.0f, 24.0f));
	Content->AddChildToVerticalBox(TitleSpacer);

	MessageText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(),
		TEXT("OutcomeMessage"));
	FSlateFontInfo MessageFont = MessageText->GetFont();
	MessageFont.Size = 22;
	MessageText->SetFont(MessageFont);
	MessageText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	MessageText->SetJustification(ETextJustify::Center);
	MessageText->SetAutoWrapText(true);
	MessageText->SetWrapTextAt(720.0f);
	Content->AddChildToVerticalBox(MessageText);

	USpacer* ButtonSpacer = WidgetTree->ConstructWidget<USpacer>(
		USpacer::StaticClass(),
		TEXT("ButtonSpacer"));
	ButtonSpacer->SetSize(FVector2D(1.0f, 36.0f));
	Content->AddChildToVerticalBox(ButtonSpacer);

	RestartButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(),
		TEXT("RestartButton"));
	UTextBlock* RestartLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(),
		TEXT("RestartLabel"));
	RestartLabel->SetText(NSLOCTEXT("Paradox", "RestartLevel", "RESTART LEVEL"));
	RestartLabel->SetJustification(ETextJustify::Center);
	RestartButton->AddChild(RestartLabel);
	RestartButton->OnClicked.AddDynamic(
		this,
		&UParadoxOutcomeWidget::HandleRestartClicked);
	if (UVerticalBoxSlot* ButtonSlot =
		Content->AddChildToVerticalBox(RestartButton))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Center);
	}
}

void UParadoxOutcomeWidget::HandleRestartClicked()
{
	OnRestartRequested.Broadcast();
}
