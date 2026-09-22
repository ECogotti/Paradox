#pragma once

#include "CoreMinimal.h"
#include "Inventory/ParadoxPickupableAction.h"
#include "ParadoxUsePickupableAction.generated.h"

/** Generic semantic action that asks the currently equipped pickupable to execute Use. */
UCLASS(BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxUsePickupableAction : public UParadoxPickupableGameplayActionBase
{
	GENERATED_BODY()

protected:
	virtual bool CanExecutePickupableAction_Implementation(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable,
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;

	virtual void ExecutePickupableAction_Implementation(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable) override;
};

/** Replay-safe authored Definition for the generic Use-equipped-item intention. */
UCLASS(BlueprintType)
class PARADOX_API UParadoxUsePickupableActionDefinition
	: public UParadoxPickupableGameplayActionDefinition
{
	GENERATED_BODY()

public:
	UParadoxUsePickupableActionDefinition();

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
