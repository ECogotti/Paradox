#pragma once

#include "CoreMinimal.h"
#include "Interaction/ParadoxInteractionActionBase.h"
#include "ParadoxReceiverInteractionAction.generated.h"

class UPuzzleReceiverComponent;

/** Native interaction that activates or deactivates one explicitly resolved manual Puzzle Receiver. */
UCLASS(BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxReceiverInteractionAction : public UParadoxInteractionActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction|Receiver")
	UPuzzleReceiverComponent* GetResolvedReceiver() const { return ResolvedReceiver.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction|Receiver")
	EParadoxInteractionStateCommand GetReceiverCommand() const { return ReceiverCommand; }

protected:
	virtual void OnActionInit_Implementation() override;
	virtual void OnActionCleanup_Implementation() override;
	virtual bool CanSatisfyInteractionPreconditions_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual bool IsInteractionOutcomeSatisfied_Implementation() const override;
	virtual void ExecuteInteraction_Implementation() override;

private:
	bool ReadReceiverParameters(FName& OutComponentName, EParadoxInteractionStateCommand& OutCommand, FString& OutDiagnostic) const;
	UPuzzleReceiverComponent* ResolveReceiver(FName ComponentName, FString& OutDiagnostic) const;
	void HandleReceiverStateChanged(UPuzzleReceiverComponent* Receiver, bool bIsActive);
	void HandleReceiverPrerequisitesChanged(UPuzzleReceiverComponent* Receiver, bool bPrerequisitesSatisfied);

	UPROPERTY(Transient)
	TObjectPtr<UPuzzleReceiverComponent> ResolvedReceiver;

	EParadoxInteractionStateCommand ReceiverCommand = EParadoxInteractionStateCommand::Activate;
};
