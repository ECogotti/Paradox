#pragma once

#include "Actions/GameplayActionInstance.h"
#include "ParadoxSetCrouchedAction.generated.h"

/**
 * Instantaneous stance command.
 *
 * The action owns only GameplayAction.Lock.Stance, so it can run while movement keeps its
 * independent GameplayAction.Lock.Movement lock.
 */
UCLASS()
class PARADOX_API UParadoxSetCrouchedAction : public UGameplayActionInstance
{
	GENERATED_BODY()

protected:
	virtual bool CanStartAction_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual void OnActionStarted_Implementation() override;
};
