#pragma once

#include "Interaction/ParadoxInteractionWidgetBase.h"
#include "Puzzles/ParadoxHackingTypes.h"
#include "ParadoxHackingTerminalWidget.generated.h"

class AParadoxHackingTerminal;
class UParadoxHackingLetterWidget;
class UCommonButtonBase;
class UCommonTextBlock;
class UProgressBar;
class UPanelWidget;
class UWidgetSwitcher;
class UGameplayActionComponent;

/** Blueprint owns the complete widget composition. C++ owns bindings and a disposable view. */
UCLASS(Abstract, Blueprintable)
class PARADOX_API UParadoxHackingTerminalWidget : public UParadoxInteractionWidgetBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hacking") TSubclassOf<UParadoxHackingLetterWidget> LetterWidgetClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hacking", meta=(ClampMin="0.01")) float ViewUpdateInterval = 0.05f;
	UFUNCTION(BlueprintCallable, Category="Hacking") void RefreshHackingView();
	UFUNCTION(BlueprintPure, Category="Hacking") FParadoxHackingAttemptSnapshot GetViewedAttempt() const { return ViewedAttempt; }
protected:
	UPROPERTY(BlueprintReadOnly, Category="Hacking", meta=(BindWidget)) TObjectPtr<UCommonButtonBase> StartHackingButton;
	UPROPERTY(BlueprintReadOnly, Category="Hacking", meta=(BindWidget)) TObjectPtr<UCommonTextBlock> PasswordText;
	UPROPERTY(BlueprintReadOnly, Category="Hacking", meta=(BindWidget)) TObjectPtr<UProgressBar> TimeRemainingProgress;
	UPROPERTY(BlueprintReadOnly, Category="Hacking", meta=(BindWidget)) TObjectPtr<UPanelWidget> LetterContainer;
	/** Blueprint supplies three pages: 0 ready/retry, 1 accepted approach or active hacking, 2 hacked. */
	UPROPERTY(BlueprintReadOnly, Category="Hacking", meta=(BindWidget)) TObjectPtr<UWidgetSwitcher> HackingStateSwitcher;
	UFUNCTION(BlueprintImplementableEvent, Category="Hacking|Presentation") void OnLetterWidgetsRebuilt(const TArray<UParadoxHackingLetterWidget*>& Widgets);
	UFUNCTION(BlueprintImplementableEvent, Category="Hacking|Presentation") void OnHackingError();
	UFUNCTION(BlueprintImplementableEvent, Category="Hacking|Presentation") void OnHackingSucceeded();
	UFUNCTION(BlueprintImplementableEvent, Category="Hacking|Presentation") void OnHackingTimedOut();
	UFUNCTION(BlueprintImplementableEvent, Category="Hacking|Presentation") void OnHackingFailed();
	UFUNCTION(BlueprintImplementableEvent, Category="Hacking|Presentation") void OnHackingViewUnavailable(const FString& Diagnostic);
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeSelectionContextAssigned() override;
	virtual void NativeSelectionContextCleared() override;
private:
	UFUNCTION() void HandleStartClicked();
	void HandleLetterClicked(int32 SlotIndex, const FString& Letter);
	void HandleAttemptChanged(const FParadoxHackingAttemptSnapshot& Snapshot);
	void HandleActionEnded(const FGameplayActionEvent& Event);
	bool IsApproachingTerminal() const;
	void ClearLetterViews();
	bool ValidateBindings();
	TWeakObjectPtr<AParadoxHackingTerminal> Terminal;
	// Presentation observes the accepted handle; removing the view never cancels the action.
	TWeakObjectPtr<UGameplayActionComponent> RequesterActions;
	FGameplayActionHandle ViewedActionHandle;
	FDelegateHandle ActionEndedHandle;
	UPROPERTY(Transient) TArray<TObjectPtr<UParadoxHackingLetterWidget>> LetterViews;
	UPROPERTY(Transient) FParadoxHackingAttemptSnapshot ViewedAttempt;
	FGuid BuiltAttemptId;
	FDelegateHandle AttemptChangedHandle;
	FTimerHandle ViewTimer;
	bool bReportedInvalidBindings = false;
	TWeakObjectPtr<UClass> InvalidLetterClass;
};
