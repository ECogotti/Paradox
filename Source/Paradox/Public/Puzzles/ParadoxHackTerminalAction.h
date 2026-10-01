#pragma once

#include "Interaction/ParadoxInteractionActionBase.h"
#include "Interaction/ParadoxInteractionActionDefinition.h"
#include "Puzzles/ParadoxHackingTypes.h"
#include "ParadoxHackTerminalAction.generated.h"

class AParadoxHackingTerminal;

/** Spatial approach followed by journaled background hacking. Non-player origins simulate difficulty. */
UCLASS()
class PARADOX_API UParadoxHackTerminalAction : public UParadoxInteractionActionBase
{
	GENERATED_BODY()
protected:
	virtual bool CanSatisfyInteractionPreconditions_Implementation(FGameplayTag& OutReason, FString& OutDiagnostic) const override;
	virtual bool IsInteractionOutcomeSatisfied_Implementation() const override;
	virtual bool IsInteractionExecutionPending_Implementation() const override;
	virtual void ExecuteInteraction_Implementation() override;
	virtual void OnActionCleanup_Implementation() override;
	virtual void OnActionCancelled_Implementation(FGameplayTag Reason) override;
	virtual void OnActionInterrupted_Implementation(FGameplayTag Reason) override;
	virtual void OnActionAborted_Implementation(FGameplayTag Reason) override;
	virtual FInstancedPropertyBag BuildTerminalOutcomeParameters(EGameplayActionState TerminalState) const override;
private:
	void HandleAttemptChanged(const FParadoxHackingAttemptSnapshot& Snapshot);
	void CancelOwnedAttempt();
	bool ReadRecordedOutcome(double& OutDuration, bool& OutSuccess, FString& OutDiagnostic) const;
	TWeakObjectPtr<AParadoxHackingTerminal> Terminal;
	TWeakObjectPtr<AActor> AttemptInstigator;
	FGuid AttemptId;
	int32 AttemptGeneration = 0;
	FDelegateHandle AttemptChangedHandle;
	FParadoxHackingAttemptSnapshot LastSnapshot;
	bool bCancellingAttempt = false;
};

UCLASS(BlueprintType)
class PARADOX_API UParadoxHackTerminalActionDefinition : public UParadoxInteractionActionDefinition
{
	GENERATED_BODY()
public:
	UParadoxHackTerminalActionDefinition();
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
