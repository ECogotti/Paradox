#pragma once

#include "Actions/GameplayActionDefinition.h"
#include "Actions/GameplayActionInstance.h"
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/GameplayActionTypes.h"
#include "ParadoxPickupableAction.generated.h"

class AParadoxCharacter;
class AParadoxPickupableActor;
class UTexture2D;

namespace ParadoxPickupableActionParameters
{
	PARADOX_API extern const FName Pickupable;
}

USTRUCT()
struct FParadoxPickupableGameplayActionParameters
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<AParadoxPickupableActor> Pickupable;
};

/** Standard semantic action base for designer-authored behavior supplied by an equipped item. */
UCLASS(Abstract, BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxPickupableGameplayActionBase : public UGameplayActionInstance
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Pickupable Action")
	AParadoxCharacter* GetPickupableActionCharacter() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Pickupable Action")
	AParadoxPickupableActor* GetPickupableActionItem() const;

protected:
	virtual bool CanStartAction_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual void OnActionStarted_Implementation() override;

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Inventory|Pickupable Action", meta = (BlueprintProtected = "true"))
	bool CanExecutePickupableAction(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable,
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const;
	virtual bool CanExecutePickupableAction_Implementation(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable,
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const;

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Inventory|Pickupable Action", meta = (BlueprintProtected = "true"))
	void ExecutePickupableAction(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable);
	virtual void ExecutePickupableAction_Implementation(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Pickupable Action", meta = (BlueprintProtected = "true"))
	void CompletePickupableActionSuccess(const FString& DiagnosticMessage);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Pickupable Action", meta = (BlueprintProtected = "true"))
	void CompletePickupableActionFailure(FGameplayTag ReasonTag, const FString& DiagnosticMessage);
};

/** Definition base that guarantees the replay-safe Pickupable soft-object parameter. */
UCLASS(Abstract, BlueprintType)
class PARADOX_API UParadoxPickupableGameplayActionDefinition : public UGameplayActionDefinition
{
	GENERATED_BODY()

public:
	UParadoxPickupableGameplayActionDefinition();
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** UI-facing item action descriptor; execution always dispatches through GameplayActions. */
UCLASS(BlueprintType)
class PARADOX_API UParadoxPickupableAction : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Inventory|Pickupable Action")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Inventory|Pickupable Action")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Inventory|Pickupable Action")
	TSoftObjectPtr<UGameplayActionDefinition> GameplayActionDefinition;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Pickupable Action")
	FGameplayActionSubmissionResult EvaluateExecution(AParadoxCharacter* Character) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Pickupable Action")
	FGameplayActionSubmissionResult RequestExecute(AParadoxCharacter* Character) const;

private:
	bool BuildRequest(
		AParadoxCharacter* Character,
		FGameplayActionRequest& OutRequest,
		FGameplayActionSubmissionResult& OutFailure) const;
};
