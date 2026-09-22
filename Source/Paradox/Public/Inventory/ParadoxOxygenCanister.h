#pragma once

#include "CoreMinimal.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "ParadoxOxygenCanister.generated.h"

/** Concrete consumable pickupable that restores the holder's current Oxygen resource. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxOxygenCanister : public AParadoxPickupableActor
{
	GENERATED_BODY()

public:
	AParadoxOxygenCanister();

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Oxygen Canister")
	float GetOxygenRestoreSeconds() const { return OxygenRestoreSeconds; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;

	virtual bool CanUseItem_Implementation(
		AParadoxCharacter* Character,
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;

	virtual FParadoxPickupableUseResult ExecuteUseItem_Implementation(
		AParadoxCharacter* Character) override;

	virtual void HandleUseCommitted(
		AParadoxCharacter* Character,
		const FParadoxPickupableUseResult& Result) override;

	virtual void HandleUseFailed(
		AParadoxCharacter* Character,
		const FParadoxPickupableUseResult& Result) override;

	/** Oxygen seconds requested from the authoritative Character component on successful Use. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Paradox|Inventory|Oxygen Canister",
		meta = (UIMin = "1.0", Units = "s"))
	float OxygenRestoreSeconds = 30.0f;

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Inventory|Oxygen Canister", meta = (DisplayName = "On Canister Use Succeeded"))
	void ReceiveCanisterUseSucceeded(
		AParadoxCharacter* Character,
		float RequestedSeconds,
		float ActualRestoredSeconds);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Inventory|Oxygen Canister", meta = (DisplayName = "On Canister Use Failed"))
	void ReceiveCanisterUseFailed(
		AParadoxCharacter* Character,
		FGameplayTag ReasonTag,
		const FString& DiagnosticMessage);

private:
	float PendingActualRestoredSeconds = 0.0f;
};
