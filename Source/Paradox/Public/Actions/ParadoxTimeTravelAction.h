#pragma once

#include "Actions/GameplayActionInstance.h"
#include "ParadoxTimeTravelAction.generated.h"

class AParadoxCharacter;
class UNiagaraComponent;

/** Asynchronous recorded time-travel command driven by the character-owned Niagara component. */
UCLASS()
class PARADOX_API UParadoxTimeTravelAction : public UGameplayActionInstance
{
	GENERATED_BODY()

protected:
	virtual bool CanStartAction_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual void OnActionStarted_Implementation() override;
	virtual void OnActionCleanup_Implementation() override;

private:
	UFUNCTION()
	void HandleTimeTravelSystemFinished(UNiagaraComponent* FinishedComponent);

	void CompleteTimeTravel();

	TWeakObjectPtr<AParadoxCharacter> TimeTravelCharacter;
	TWeakObjectPtr<UNiagaraComponent> ActiveNiagaraComponent;
	bool bCompletionRequested = false;
	bool bDepartureCommitted = false;
};
