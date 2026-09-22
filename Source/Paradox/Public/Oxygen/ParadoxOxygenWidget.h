#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "ParadoxOxygenWidget.generated.h"

class AActor;
class UParadoxOxygenComponent;

UENUM(BlueprintType)
enum class EParadoxOxygenWarningState : uint8
{
	Normal,
	Low,
	Critical,
	Depleted
};

/** Presentation-only observer for one explicitly supplied Oxygen component. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API UParadoxOxygenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Rebinds to an explicit source. The owning Player/Controller is never used for discovery. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen|Widget")
	void SetObservedOxygenComponent(UParadoxOxygenComponent* InOxygenComponent);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen|Widget")
	void ClearObservedOxygenComponent();

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|Widget")
	UParadoxOxygenComponent* GetObservedOxygenComponent() const
	{
		return ObservedOxygenComponent.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|Widget")
	float GetDisplayedRemainingSeconds() const { return DisplayedRemainingSeconds; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|Widget")
	float GetDisplayedDurationSeconds() const { return DisplayedDurationSeconds; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|Widget")
	float GetDisplayedNormalizedOxygen() const { return DisplayedNormalizedOxygen; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|Widget")
	int32 GetDisplayedWholeSecondsRemaining() const
	{
		return DisplayedWholeSecondsRemaining;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|Widget")
	bool IsDisplayedConsumptionBlocked() const { return bDisplayedConsumptionBlocked; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|Widget")
	float GetDisplayedEffectiveConsumptionSpeed() const
	{
		return DisplayedEffectiveConsumptionSpeed;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|Widget")
	bool IsDisplayedDepleted() const { return bDisplayedDepleted; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|Widget")
	EParadoxOxygenWarningState GetOxygenWarningState() const { return WarningState; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|Widget")
	FText GetFormattedCountdownText() const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen|Widget")
	void RefreshOxygenPresentation();

	/** Presentation enters Low at or below this many remaining seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Oxygen|Widget|Presentation", meta = (ClampMin = "0.0", Units = "s"))
	float LowThresholdSeconds = 30.0f;

	/** Presentation enters Critical at or below this many remaining seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Oxygen|Widget|Presentation", meta = (ClampMin = "0.0", Units = "s"))
	float CriticalThresholdSeconds = 10.0f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Oxygen|Widget", meta = (DisplayName = "On Observed Oxygen Component Changed"))
	void ReceiveObservedOxygenComponentChanged(UParadoxOxygenComponent* NewOxygenComponent);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Oxygen|Widget", meta = (DisplayName = "On Oxygen Display Updated"))
	void ReceiveOxygenDisplayUpdated(
		float RemainingSeconds,
		float DurationSeconds,
		float NormalizedOxygen,
		int32 WholeSecondsRemaining,
		bool bConsumptionBlocked,
		float EffectiveConsumptionSpeed,
		bool bIsDepleted);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Oxygen|Widget", meta = (DisplayName = "On Oxygen Whole Second Changed"))
	void ReceiveOxygenWholeSecondChanged(
		int32 WholeSecondsRemaining,
		float RemainingSeconds,
		float NormalizedOxygen);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Oxygen|Widget", meta = (DisplayName = "On Oxygen Warning State Changed"))
	void ReceiveOxygenWarningStateChanged(
		EParadoxOxygenWarningState PreviousState,
		EParadoxOxygenWarningState NewState);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Oxygen|Widget", meta = (DisplayName = "On Oxygen Depleted Feedback Requested"))
	void ReceiveOxygenDepletedFeedbackRequested();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Oxygen|Widget", meta = (DisplayName = "On Oxygen Reset Feedback Requested"))
	void ReceiveOxygenResetFeedbackRequested();

private:
	void BindObservedOxygen();
	void UnbindObservedOxygen();
	EParadoxOxygenWarningState CalculateWarningState() const;

	UFUNCTION()
	void HandleOxygenChanged(
		float OldRemainingSeconds,
		float NewRemainingSeconds,
		float DurationSeconds,
		float NormalizedOxygen);

	UFUNCTION()
	void HandleWholeSecondChanged(
		int32 WholeSecondsRemaining,
		float RemainingSeconds,
		float NormalizedOxygen);

	UFUNCTION()
	void HandleConsumptionSpeedChanged(float OldSpeed, float NewSpeed);

	UFUNCTION()
	void HandleConsumptionBlockedChanged(bool bIsBlocked);

	UFUNCTION()
	void HandleOxygenDepleted();

	UFUNCTION()
	void HandleOxygenReset();

	UFUNCTION()
	void HandleObservedOwnerDestroyed(AActor* DestroyedActor);

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxOxygenComponent> ObservedOxygenComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxOxygenComponent> BoundOxygenComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> BoundOxygenOwner;

	UPROPERTY(Transient)
	float DisplayedRemainingSeconds = 0.0f;

	UPROPERTY(Transient)
	float DisplayedDurationSeconds = 0.0f;

	UPROPERTY(Transient)
	float DisplayedNormalizedOxygen = 0.0f;

	UPROPERTY(Transient)
	int32 DisplayedWholeSecondsRemaining = 0;

	UPROPERTY(Transient)
	bool bDisplayedConsumptionBlocked = false;

	UPROPERTY(Transient)
	float DisplayedEffectiveConsumptionSpeed = 0.0f;

	UPROPERTY(Transient)
	bool bDisplayedDepleted = true;

	UPROPERTY(Transient)
	EParadoxOxygenWarningState WarningState = EParadoxOxygenWarningState::Depleted;
};
