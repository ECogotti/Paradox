#pragma once

#include "Components/ActorComponent.h"
#include "Presentation/ParadoxOutcomeTypes.h"
#include "ParadoxOutcomePresentationComponent.generated.h"

class UParadoxOutcomeWidget;
class UParadoxTimeLoopComponent;

UENUM(BlueprintType)
enum class EParadoxOutcomePresentationState : uint8
{
	Hidden,
	FadingToBlack,
	HoldingCollapse,
	FadingFromBlack,
	FadingTerminal,
	Terminal
};

/** Controller-owned, real-time presentation adapter for time-loop terminal states. */
UCLASS(ClassGroup = (Paradox), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxOutcomePresentationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxOutcomePresentationComponent();

	bool BeginParadoxPresentation(const FParadoxContext& Context);
	void PresentGameOver(const FParadoxGameOverContext& Context);
	void PresentLevelComplete(const FParadoxLevelCompleteContext& Context);
	void ClearPresentation();

	UFUNCTION(BlueprintPure, Category = "Paradox|Presentation")
	EParadoxOutcomePresentationState GetPresentationState() const
	{
		return PresentationState;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Presentation")
	UParadoxOutcomeWidget* GetActiveOutcomeWidget() const { return ActiveWidget; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Presentation")
	TSubclassOf<UParadoxOutcomeWidget> OutcomeWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Presentation", meta = (ClampMin = "0.0"))
	float FadeDuration = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Presentation", meta = (ClampMin = "0.0"))
	float CollapseHoldDuration = 1.5f;

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Presentation")
	void ReceiveOutcomePresentationStarted(
		const FParadoxOutcomePresentationData& Data);
	virtual void ReceiveOutcomePresentationStarted_Implementation(
		const FParadoxOutcomePresentationData& Data);

private:
	UParadoxTimeLoopComponent* ResolveTimeLoop() const;
	bool EnsureWidget();
	void BeginTerminalPresentation(
		const FParadoxOutcomePresentationData& Data);
	void ApplyOpacity(float Opacity);
	static FParadoxOutcomePresentationData MakeParadoxPresentationData(
		const FParadoxContext& Context);
	static FParadoxOutcomePresentationData MakeGameOverPresentationData(
		const FParadoxGameOverContext& Context);
	static FParadoxOutcomePresentationData MakeLevelCompletePresentationData(
		const FParadoxLevelCompleteContext& Context);

	UFUNCTION()
	void HandleRestartRequested();

	UPROPERTY(Transient)
	TObjectPtr<UParadoxOutcomeWidget> ActiveWidget = nullptr;

	UPROPERTY(Transient)
	FParadoxOutcomePresentationData ActiveData;

	EParadoxOutcomePresentationState PresentationState =
		EParadoxOutcomePresentationState::Hidden;
	double StageStartedAtSeconds = 0.0;
	bool bRecoveryRequested = false;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxOutcomePresentationTestAccessor;
#endif
};
