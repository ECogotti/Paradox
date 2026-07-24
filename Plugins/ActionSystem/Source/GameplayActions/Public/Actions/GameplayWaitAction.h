#pragma once

#include "CoreMinimal.h"
#include "Actions/GameplayActionInstance.h"
#include "TimerManager.h"
#include "GameplayWaitAction.generated.h"

/**
 * Reference asynchronous action whose Definition declares a numeric Property Bag field named Duration.
 *
 * Duration is validated before acceptance, cached during Action Init, and consumed only by Action Start.
 * This makes queued wait actions cheap and guarantees that no timer exists before the action owns its locks.
 */
UCLASS(Blueprintable)
class GAMEPLAYACTIONS_API UGameplayWaitAction : public UGameplayActionInstance
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

protected:
	virtual bool CanStartAction_Implementation(FGameplayTag& OutFailureReason, FString& OutDiagnostic) const override;
	virtual void OnActionInit_Implementation() override;
	virtual void OnActionStarted_Implementation() override;
	virtual void OnActionPaused_Implementation() override;
	virtual void OnActionResumed_Implementation() override;
	virtual void OnActionCleanup_Implementation() override;

private:
	void HandleTimerCompleted();

	FTimerHandle WaitTimerHandle;
	double CachedDurationSeconds = 0.0;
};
