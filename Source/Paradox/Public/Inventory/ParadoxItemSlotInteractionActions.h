#pragma once

#include "CoreMinimal.h"
#include "Interaction/ParadoxInteractionActionBase.h"
#include "Inventory/ParadoxInventoryInteractionActions.h"
#include "ParadoxItemSlotInteractionActions.generated.h"

/** Interaction Action that transfers the requester's current insertable into the target Slot. */
UCLASS(BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxInsertItemInteractionAction : public UParadoxInteractionActionBase
{
	GENERATED_BODY()

protected:
	virtual bool CanSatisfyInteractionPreconditions_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual void ExecuteInteraction_Implementation() override;
};

/** Existing Pickup interaction flow specialized only at its source/acquisition boundary. */
UCLASS(BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxPickupFromItemSlotInteractionAction
	: public UParadoxPickupInteractionAction
{
	GENERATED_BODY()

protected:
	virtual bool ValidatePickupSource(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual bool IsPickupSourceAcquired() const override;
	virtual bool CommitPickupSource(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) override;
};
