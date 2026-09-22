#include "Oxygen/ParadoxOxygenWidget.h"

#include "GameFramework/Actor.h"
#include "Oxygen/ParadoxOxygenComponent.h"

void UParadoxOxygenWidget::SetObservedOxygenComponent(
	UParadoxOxygenComponent* InOxygenComponent)
{
	if (ObservedOxygenComponent.Get() == InOxygenComponent)
	{
		BindObservedOxygen();
		RefreshOxygenPresentation();
		return;
	}

	UnbindObservedOxygen();
	ObservedOxygenComponent = InOxygenComponent;
	BindObservedOxygen();
	RefreshOxygenPresentation();
	ReceiveObservedOxygenComponentChanged(InOxygenComponent);
}

void UParadoxOxygenWidget::ClearObservedOxygenComponent()
{
	if (!ObservedOxygenComponent.IsValid()
		&& !BoundOxygenComponent.IsValid())
	{
		return;
	}
	UnbindObservedOxygen();
	ObservedOxygenComponent.Reset();
	RefreshOxygenPresentation();
	ReceiveObservedOxygenComponentChanged(nullptr);
}

FText UParadoxOxygenWidget::GetFormattedCountdownText() const
{
	const int32 Minutes = DisplayedWholeSecondsRemaining / 60;
	const int32 Seconds = DisplayedWholeSecondsRemaining % 60;
	return FText::FromString(
		FString::Printf(TEXT("%02d:%02d"), Minutes, Seconds));
}

void UParadoxOxygenWidget::RefreshOxygenPresentation()
{
	UParadoxOxygenComponent* Oxygen = ObservedOxygenComponent.Get();
	DisplayedRemainingSeconds = Oxygen
		? Oxygen->GetRemainingOxygenSeconds()
		: 0.0f;
	DisplayedDurationSeconds = Oxygen
		? Oxygen->GetOxygenDurationSeconds()
		: 0.0f;
	DisplayedNormalizedOxygen = Oxygen
		? Oxygen->GetNormalizedOxygen()
		: 0.0f;
	DisplayedWholeSecondsRemaining = Oxygen
		? Oxygen->GetWholeSecondsRemaining()
		: 0;
	bDisplayedConsumptionBlocked = Oxygen && Oxygen->IsConsumptionBlocked();
	DisplayedEffectiveConsumptionSpeed = Oxygen
		? Oxygen->GetEffectiveConsumptionSpeed()
		: 0.0f;
	bDisplayedDepleted = !Oxygen || Oxygen->IsOxygenDepleted();

	const EParadoxOxygenWarningState PreviousState = WarningState;
	WarningState = CalculateWarningState();
	ReceiveOxygenDisplayUpdated(
		DisplayedRemainingSeconds,
		DisplayedDurationSeconds,
		DisplayedNormalizedOxygen,
		DisplayedWholeSecondsRemaining,
		bDisplayedConsumptionBlocked,
		DisplayedEffectiveConsumptionSpeed,
		bDisplayedDepleted);
	if (PreviousState != WarningState)
	{
		ReceiveOxygenWarningStateChanged(PreviousState, WarningState);
	}
}

void UParadoxOxygenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindObservedOxygen();
	RefreshOxygenPresentation();
}

void UParadoxOxygenWidget::NativeDestruct()
{
	UnbindObservedOxygen();
	Super::NativeDestruct();
}

void UParadoxOxygenWidget::BindObservedOxygen()
{
	UParadoxOxygenComponent* Oxygen = ObservedOxygenComponent.Get();
	if (!Oxygen || BoundOxygenComponent.Get() == Oxygen)
	{
		return;
	}
	UnbindObservedOxygen();
	BoundOxygenComponent = Oxygen;
	Oxygen->OnOxygenChanged.AddUniqueDynamic(
		this,
		&ThisClass::HandleOxygenChanged);
	Oxygen->OnWholeSecondChanged.AddUniqueDynamic(
		this,
		&ThisClass::HandleWholeSecondChanged);
	Oxygen->OnConsumptionSpeedChanged.AddUniqueDynamic(
		this,
		&ThisClass::HandleConsumptionSpeedChanged);
	Oxygen->OnConsumptionBlockedChanged.AddUniqueDynamic(
		this,
		&ThisClass::HandleConsumptionBlockedChanged);
	Oxygen->OnOxygenDepleted.AddUniqueDynamic(
		this,
		&ThisClass::HandleOxygenDepleted);
	Oxygen->OnOxygenReset.AddUniqueDynamic(
		this,
		&ThisClass::HandleOxygenReset);
	BoundOxygenOwner = Oxygen->GetOwner();
	if (BoundOxygenOwner.IsValid())
	{
		BoundOxygenOwner->OnDestroyed.AddUniqueDynamic(
			this,
			&ThisClass::HandleObservedOwnerDestroyed);
	}
}

void UParadoxOxygenWidget::UnbindObservedOxygen()
{
	if (BoundOxygenComponent.IsValid())
	{
		BoundOxygenComponent->OnOxygenChanged.RemoveDynamic(
			this,
			&ThisClass::HandleOxygenChanged);
		BoundOxygenComponent->OnWholeSecondChanged.RemoveDynamic(
			this,
			&ThisClass::HandleWholeSecondChanged);
		BoundOxygenComponent->OnConsumptionSpeedChanged.RemoveDynamic(
			this,
			&ThisClass::HandleConsumptionSpeedChanged);
		BoundOxygenComponent->OnConsumptionBlockedChanged.RemoveDynamic(
			this,
			&ThisClass::HandleConsumptionBlockedChanged);
		BoundOxygenComponent->OnOxygenDepleted.RemoveDynamic(
			this,
			&ThisClass::HandleOxygenDepleted);
		BoundOxygenComponent->OnOxygenReset.RemoveDynamic(
			this,
			&ThisClass::HandleOxygenReset);
	}
	if (BoundOxygenOwner.IsValid())
	{
		BoundOxygenOwner->OnDestroyed.RemoveDynamic(
			this,
			&ThisClass::HandleObservedOwnerDestroyed);
	}
	BoundOxygenComponent.Reset();
	BoundOxygenOwner.Reset();
}

EParadoxOxygenWarningState UParadoxOxygenWidget::CalculateWarningState() const
{
	if (bDisplayedDepleted || DisplayedRemainingSeconds <= 0.0f)
	{
		return EParadoxOxygenWarningState::Depleted;
	}
	const float SafeLow = FMath::Max(0.0f, LowThresholdSeconds);
	const float SafeCritical = FMath::Min(
		FMath::Max(0.0f, CriticalThresholdSeconds),
		SafeLow);
	if (DisplayedRemainingSeconds <= SafeCritical)
	{
		return EParadoxOxygenWarningState::Critical;
	}
	return DisplayedRemainingSeconds <= SafeLow
		? EParadoxOxygenWarningState::Low
		: EParadoxOxygenWarningState::Normal;
}

void UParadoxOxygenWidget::HandleOxygenChanged(
	float OldRemainingSeconds,
	float NewRemainingSeconds,
	float DurationSeconds,
	float NormalizedOxygen)
{
	RefreshOxygenPresentation();
}

void UParadoxOxygenWidget::HandleWholeSecondChanged(
	const int32 WholeSecondsRemaining,
	const float RemainingSeconds,
	const float NormalizedOxygen)
{
	RefreshOxygenPresentation();
	ReceiveOxygenWholeSecondChanged(
		WholeSecondsRemaining,
		RemainingSeconds,
		NormalizedOxygen);
}

void UParadoxOxygenWidget::HandleConsumptionSpeedChanged(
	float OldSpeed,
	float NewSpeed)
{
	RefreshOxygenPresentation();
}

void UParadoxOxygenWidget::HandleConsumptionBlockedChanged(bool bIsBlocked)
{
	RefreshOxygenPresentation();
}

void UParadoxOxygenWidget::HandleOxygenDepleted()
{
	RefreshOxygenPresentation();
	ReceiveOxygenDepletedFeedbackRequested();
}

void UParadoxOxygenWidget::HandleOxygenReset()
{
	RefreshOxygenPresentation();
	ReceiveOxygenResetFeedbackRequested();
}

void UParadoxOxygenWidget::HandleObservedOwnerDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == BoundOxygenOwner.Get())
	{
		ClearObservedOxygenComponent();
	}
}
