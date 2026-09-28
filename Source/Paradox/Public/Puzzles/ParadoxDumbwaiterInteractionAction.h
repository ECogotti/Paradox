#pragma once

#include "CoreMinimal.h"
#include "Interaction/ParadoxInteractionActionBase.h"
#include "ParadoxDumbwaiterInteractionAction.generated.h"

/** Semantic interaction that starts the target Dumbwaiter's paired cargo transfer. */
UCLASS(BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxSendDumbwaiterInteractionAction
	: public UParadoxInteractionActionBase
{
	GENERATED_BODY()

protected:
	virtual bool CanSatisfyInteractionPreconditions_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual void ExecuteInteraction_Implementation() override;
};
