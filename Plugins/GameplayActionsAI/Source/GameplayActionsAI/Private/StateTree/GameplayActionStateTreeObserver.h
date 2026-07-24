#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/GameplayActionTypes.h"
#include "GameplayActionStateTreeObserver.generated.h"

class UGameplayActionComponent;

/**
 * Per-StateTree-execution delegate owner.
 *
 * A UObject is used because the core native multicast can then invalidate the binding automatically
 * if world teardown destroys the observer. Explicit Unbind remains mandatory on ExitState.
 */
UCLASS(Transient)
class UGameplayActionStateTreeObserver : public UObject
{
	GENERATED_BODY()

public:
	void Bind(UGameplayActionComponent& Component);
	void BeginSubmission();
	void CompleteSubmission(FGameplayActionHandle Handle);
	void Unbind();

	/** Returns the exact component that accepted the task's handle, if it still exists. */
	UGameplayActionComponent* GetActionComponent() const { return ActionComponent.Get(); }
	bool HasTerminalResult() const { return bHasTerminalResult; }
	const FGameplayActionResult& GetTerminalResult() const { return TerminalResult; }

private:
	void HandleActionEnded(const FGameplayActionEvent& Event);

	TWeakObjectPtr<UGameplayActionComponent> ActionComponent;
	FGameplayActionHandle ObservedHandle;
	FDelegateHandle DelegateHandle;
	TArray<FGameplayActionEvent> DeferredEndedEvents;
	FGameplayActionResult TerminalResult;
	bool bSubmitting = false;
	bool bHasTerminalResult = false;
};
