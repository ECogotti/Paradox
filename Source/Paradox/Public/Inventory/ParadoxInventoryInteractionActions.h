#pragma once

#include "CoreMinimal.h"
#include "Interaction/ParadoxInteractionActionBase.h"
#include "ParadoxInventoryInteractionActions.generated.h"

/** Existing Interaction Action flow with the inventory-specific empty-slot effect. */
UCLASS(BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxPickupInteractionAction : public UParadoxInteractionActionBase
{
	GENERATED_BODY()

protected:
	virtual bool CanSatisfyInteractionPreconditions_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual bool IsInteractionOutcomeSatisfied_Implementation() const override;
	virtual void ExecuteInteraction_Implementation() override;

	/** Source-specific extension used by inserted-item Pickup without duplicating interaction flow. */
	virtual bool ValidatePickupSource(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const;
	virtual bool IsPickupSourceAcquired() const;
	virtual bool CommitPickupSource(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic);
};

/** Existing Interaction Action flow with one atomic held/world pickupable exchange. */
UCLASS(BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxSwapInteractionAction : public UParadoxInteractionActionBase
{
	GENERATED_BODY()

protected:
	virtual bool CanSatisfyInteractionPreconditions_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual bool IsInteractionOutcomeSatisfied_Implementation() const override;
	virtual void ExecuteInteraction_Implementation() override;
};
