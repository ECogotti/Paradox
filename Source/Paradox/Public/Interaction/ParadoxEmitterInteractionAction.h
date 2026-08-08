#pragma once

#include "CoreMinimal.h"
#include "Graph/PuzzleGraphTypes.h"
#include "Interaction/ParadoxInteractionActionBase.h"
#include "Signals/PuzzleSignalTypes.h"
#include "ParadoxEmitterInteractionAction.generated.h"

class UPuzzleEmitterComponent;

/** Native interaction that publishes an exact On/Off signal on one Puzzle Emitter. */
UCLASS(BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxEmitterInteractionAction : public UParadoxInteractionActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction|Emitter")
	UPuzzleEmitterComponent* GetResolvedEmitter() const { return ResolvedEmitter.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction|Emitter")
	FGameplayTag GetSignalTag() const { return SignalTag; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction|Emitter")
	EParadoxInteractionStateCommand GetEmitterCommand() const { return EmitterCommand; }

protected:
	virtual void OnActionInit_Implementation() override;
	virtual void OnActionCleanup_Implementation() override;
	virtual bool CanSatisfyInteractionPreconditions_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual bool IsInteractionOutcomeSatisfied_Implementation() const override;
	virtual void ExecuteInteraction_Implementation() override;

private:
	bool ReadEmitterParameters(FName& OutComponentName, FGameplayTag& OutSignalTag, EParadoxInteractionStateCommand& OutCommand, FString& OutDiagnostic) const;
	UPuzzleEmitterComponent* ResolveEmitter(FName ComponentName, FString& OutDiagnostic) const;
	bool IsSignalActive(const UPuzzleEmitterComponent& Emitter, FGameplayTag ExactSignalTag) const;
	bool DoGatesAllowActivation(const UPuzzleEmitterComponent& Emitter, FGameplayTag ExactSignalTag, FString& OutDiagnostic) const;
	void HandleSignalChanged(UPuzzleEmitterComponent* Emitter, FGameplayTag ChangedSignalTag, FPuzzleSignalState SignalState);
	void HandleGraphTopologyChanged(int64 Revision, class APuzzleController* Controller, EPuzzleGraphTopologyChangeKind ChangeKind);
	void HandleGraphLinkStateChanged(const FPuzzleGraphLinkHandle& LinkHandle, const FPuzzleGraphLinkState& PreviousState, const FPuzzleGraphLinkState& NewState);

	UPROPERTY(Transient)
	TObjectPtr<UPuzzleEmitterComponent> ResolvedEmitter;

	FGameplayTag SignalTag;
	EParadoxInteractionStateCommand EmitterCommand = EParadoxInteractionStateCommand::Activate;
};
