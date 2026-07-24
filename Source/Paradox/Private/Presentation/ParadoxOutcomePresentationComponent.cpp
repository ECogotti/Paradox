#include "Presentation/ParadoxOutcomePresentationComponent.h"

#include "Controllers/ParadoxPlayerController.h"
#include "Engine/World.h"
#include "GameModes/ParadoxGameMode.h"
#include "HAL/PlatformTime.h"
#include "Presentation/ParadoxOutcomeWidget.h"
#include "TimeLoop/ParadoxTimeLoopComponent.h"

UParadoxOutcomePresentationComponent::UParadoxOutcomePresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	OutcomeWidgetClass = UParadoxOutcomeWidget::StaticClass();
}

bool UParadoxOutcomePresentationComponent::BeginParadoxPresentation(
	const FParadoxContext& Context)
{
	const AParadoxPlayerController* Controller =
		Cast<AParadoxPlayerController>(GetOwner());
	if (!Context.IsValid()
		|| !Controller
		|| !Controller->IsLocalPlayerController()
		|| !EnsureWidget())
	{
		return false;
	}

	ActiveData = MakeParadoxPresentationData(Context);
	bRecoveryRequested = false;
	PresentationState = EParadoxOutcomePresentationState::FadingToBlack;
	StageStartedAtSeconds = FPlatformTime::Seconds();
	ReceiveOutcomePresentationStarted(ActiveData);
	ApplyOpacity(FadeDuration <= 0.0f ? 1.0f : 0.0f);
	SetComponentTickEnabled(true);
	return true;
}

void UParadoxOutcomePresentationComponent::PresentGameOver(
	const FParadoxGameOverContext& Context)
{
	BeginTerminalPresentation(MakeGameOverPresentationData(Context));
}

void UParadoxOutcomePresentationComponent::PresentLevelComplete(
	const FParadoxLevelCompleteContext& Context)
{
	BeginTerminalPresentation(MakeLevelCompletePresentationData(Context));
}

void UParadoxOutcomePresentationComponent::ClearPresentation()
{
	if (ActiveWidget)
	{
		ActiveWidget->OnRestartRequested.RemoveDynamic(
			this,
			&UParadoxOutcomePresentationComponent::HandleRestartRequested);
		ActiveWidget->RemoveFromParent();
		ActiveWidget = nullptr;
	}
	ActiveData = FParadoxOutcomePresentationData();
	PresentationState = EParadoxOutcomePresentationState::Hidden;
	bRecoveryRequested = false;
	SetComponentTickEnabled(false);
}

void UParadoxOutcomePresentationComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ClearPresentation();
	Super::EndPlay(EndPlayReason);
}

void UParadoxOutcomePresentationComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const double Elapsed =
		FPlatformTime::Seconds() - StageStartedAtSeconds;
	const double SafeFadeDuration = FMath::Max(
		static_cast<double>(FadeDuration),
		UE_DOUBLE_SMALL_NUMBER);

	switch (PresentationState)
	{
	case EParadoxOutcomePresentationState::FadingToBlack:
		ApplyOpacity(
			FadeDuration <= 0.0f
				? 1.0f
				: static_cast<float>(
					FMath::Clamp(Elapsed / SafeFadeDuration, 0.0, 1.0)));
		if (FadeDuration <= 0.0f || Elapsed >= FadeDuration)
		{
			ApplyOpacity(1.0f);
			if (!bRecoveryRequested)
			{
				bRecoveryRequested = true;
				if (UParadoxTimeLoopComponent* TimeLoop = ResolveTimeLoop())
				{
					TimeLoop->ContinueParadoxRecovery(
						ActiveData.ParadoxContext.EventId);
				}
			}
			PresentationState =
				EParadoxOutcomePresentationState::HoldingCollapse;
			StageStartedAtSeconds = FPlatformTime::Seconds();
		}
		break;
	case EParadoxOutcomePresentationState::HoldingCollapse:
		if (Elapsed >= CollapseHoldDuration)
		{
			PresentationState =
				EParadoxOutcomePresentationState::FadingFromBlack;
			StageStartedAtSeconds = FPlatformTime::Seconds();
		}
		break;
	case EParadoxOutcomePresentationState::FadingFromBlack:
		ApplyOpacity(
			FadeDuration <= 0.0f
				? 0.0f
				: 1.0f - static_cast<float>(
					FMath::Clamp(Elapsed / SafeFadeDuration, 0.0, 1.0)));
		if (FadeDuration <= 0.0f || Elapsed >= FadeDuration)
		{
			ClearPresentation();
		}
		break;
	case EParadoxOutcomePresentationState::FadingTerminal:
		ApplyOpacity(
			FadeDuration <= 0.0f
				? 1.0f
				: static_cast<float>(
					FMath::Clamp(Elapsed / SafeFadeDuration, 0.0, 1.0)));
		if (FadeDuration <= 0.0f || Elapsed >= FadeDuration)
		{
			ApplyOpacity(1.0f);
			PresentationState =
				EParadoxOutcomePresentationState::Terminal;
			SetComponentTickEnabled(false);
		}
		break;
	default:
		break;
	}
}

void UParadoxOutcomePresentationComponent::
	ReceiveOutcomePresentationStarted_Implementation(
		const FParadoxOutcomePresentationData& Data)
{
	if (ActiveWidget)
	{
		ActiveWidget->SetOutcomeData(Data);
	}
}

UParadoxTimeLoopComponent*
UParadoxOutcomePresentationComponent::ResolveTimeLoop() const
{
	const UWorld* World = GetWorld();
	const AParadoxGameMode* GameMode =
		World ? Cast<AParadoxGameMode>(World->GetAuthGameMode()) : nullptr;
	return GameMode ? GameMode->GetTimeLoopComponent() : nullptr;
}

bool UParadoxOutcomePresentationComponent::EnsureWidget()
{
	if (ActiveWidget)
	{
		return true;
	}
	AParadoxPlayerController* Controller =
		Cast<AParadoxPlayerController>(GetOwner());
	if (!Controller || !OutcomeWidgetClass)
	{
		return false;
	}
	ActiveWidget = CreateWidget<UParadoxOutcomeWidget>(
		Controller,
		OutcomeWidgetClass);
	if (!ActiveWidget)
	{
		return false;
	}
	ActiveWidget->OnRestartRequested.AddDynamic(
		this,
		&UParadoxOutcomePresentationComponent::HandleRestartRequested);
	ActiveWidget->AddToViewport(10000);
	return true;
}

void UParadoxOutcomePresentationComponent::BeginTerminalPresentation(
	const FParadoxOutcomePresentationData& Data)
{
	const AParadoxPlayerController* Controller =
		Cast<AParadoxPlayerController>(GetOwner());
	if (!Controller
		|| !Controller->IsLocalPlayerController()
		|| !EnsureWidget())
	{
		return;
	}
	ActiveData = Data;
	bRecoveryRequested = false;
	PresentationState = EParadoxOutcomePresentationState::FadingTerminal;
	StageStartedAtSeconds = FPlatformTime::Seconds();
	ReceiveOutcomePresentationStarted(ActiveData);
	ApplyOpacity(FadeDuration <= 0.0f ? 1.0f : 0.0f);
	SetComponentTickEnabled(true);
}

void UParadoxOutcomePresentationComponent::ApplyOpacity(const float Opacity)
{
	if (ActiveWidget)
	{
		ActiveWidget->SetPresentationOpacity(Opacity);
	}
}

FParadoxOutcomePresentationData
UParadoxOutcomePresentationComponent::MakeParadoxPresentationData(
	const FParadoxContext& Context)
{
	FParadoxOutcomePresentationData Data;
	Data.OutcomeType = EParadoxOutcomeType::TimelineCollapse;
	Data.Title = NSLOCTEXT(
		"Paradox",
		"TimelineCollapseTitle",
		"TIMELINE COLLAPSE");
	Data.Message = FText::Format(
		NSLOCTEXT(
			"Paradox",
			"TimelineCollapseMessage",
			"T{0} witnessed T{1}.\nThe past saw the future."),
		FText::AsNumber(Context.ObserverTemporalIndex),
		FText::AsNumber(Context.TargetTemporalIndex));
	Data.ParadoxContext = Context;
	Data.bShowRestart = false;
	return Data;
}

FParadoxOutcomePresentationData
UParadoxOutcomePresentationComponent::MakeGameOverPresentationData(
	const FParadoxGameOverContext& Context)
{
	FParadoxOutcomePresentationData Data;
	Data.OutcomeType = EParadoxOutcomeType::GameOver;
	Data.Title = NSLOCTEXT(
		"Paradox",
		"GameOverTitle",
		"NO TIMELINES REMAIN");
	Data.Message = NSLOCTEXT(
		"Paradox",
		"GameOverMessage",
		"The loop has no future left.");
	Data.GameOverContext = Context;
	Data.bShowRestart = true;
	return Data;
}

FParadoxOutcomePresentationData
UParadoxOutcomePresentationComponent::MakeLevelCompletePresentationData(
	const FParadoxLevelCompleteContext& Context)
{
	FParadoxOutcomePresentationData Data;
	Data.OutcomeType = EParadoxOutcomeType::LevelComplete;
	Data.Title = NSLOCTEXT(
		"Paradox",
		"LevelCompleteTitle",
		"LEVEL COMPLETE");
	Data.Message = NSLOCTEXT(
		"Paradox",
		"LevelCompleteMessage",
		"The timeline is stable.");
	Data.LevelCompleteContext = Context;
	Data.bShowRestart = true;
	return Data;
}

void UParadoxOutcomePresentationComponent::HandleRestartRequested()
{
	if (UParadoxTimeLoopComponent* TimeLoop = ResolveTimeLoop())
	{
		TimeLoop->RequestRestartLevel();
	}
}
