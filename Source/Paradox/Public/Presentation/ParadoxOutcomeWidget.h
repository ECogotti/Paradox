#pragma once

#include "Blueprint/UserWidget.h"
#include "Presentation/ParadoxOutcomeTypes.h"
#include "ParadoxOutcomeWidget.generated.h"

class UButton;
class UTextBlock;

/** Native complete outcome screen whose layout and data application can be replaced in Blueprint. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API UParadoxOutcomeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Paradox|Presentation")
	FParadoxPresentationRestartEvent OnRestartRequested;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Presentation")
	void SetOutcomeData(const FParadoxOutcomePresentationData& InData);

	UFUNCTION(BlueprintPure, Category = "Paradox|Presentation")
	FParadoxOutcomePresentationData GetOutcomeData() const { return OutcomeData; }

	UFUNCTION(BlueprintCallable, Category = "Paradox|Presentation")
	void SetPresentationOpacity(float InOpacity);

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Presentation")
	void ReceiveOutcomeDataChanged(const FParadoxOutcomePresentationData& InData);
	virtual void ReceiveOutcomeDataChanged_Implementation(
		const FParadoxOutcomePresentationData& InData);

private:
	void BuildNativeFallbackLayout();

	UFUNCTION()
	void HandleRestartClicked();

	UPROPERTY(Transient)
	FParadoxOutcomePresentationData OutcomeData;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> MessageText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RestartButton = nullptr;
};
