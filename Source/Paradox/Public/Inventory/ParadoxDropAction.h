#pragma once

#include "Actions/GameplayActionDefinition.h"
#include "Actions/GameplayActionInstance.h"
#include "CoreMinimal.h"
#include "GridWorldTypes.h"
#include "Navigation/GridPathInjectionTypes.h"
#include "ParadoxDropAction.generated.h"

class AParadoxCharacter;
class AParadoxPickupableActor;
class UGridMoveToCellExecution;
class UNavigationQueryFilter;
struct FGridMoveToCellExecutionResult;

namespace ParadoxDropActionParameters
{
	PARADOX_API extern const FName TargetCell;
	PARADOX_API extern const FName PathSource;
	PARADOX_API extern const FName InjectedPath;
	PARADOX_API extern const FName NavigationFilter;
	PARADOX_API extern const FName AcceptanceRadius;
	PARADOX_API extern const FName AllowStrafe;
}

USTRUCT()
struct FParadoxDropActionParameters
{
	GENERATED_BODY()

	UPROPERTY()
	FGridCellId TargetCell;

	UPROPERTY()
	EGridMovePathSource PathSource = EGridMovePathSource::ExactInjectedPath;

	UPROPERTY()
	FGridInjectedPath InjectedPath;

	UPROPERTY()
	TSubclassOf<UNavigationQueryFilter> NavigationFilter;

	UPROPERTY()
	float AcceptanceRadius = -1.0f;

	UPROPERTY()
	bool bAllowStrafe = false;
};
/** Semantic GridWorld-cell action that follows one exact path to the cell preceding the Drop. */
UCLASS(BlueprintType, Blueprintable, Transient)
class PARADOX_API UParadoxDropAction : public UGameplayActionInstance
{
	GENERATED_BODY()

public:
	/** O(1) semantic-cell validation shared by local targeting and action execution. */
	static bool ValidateDropCell(
		AParadoxCharacter* Character,
		const FGridCellId& TargetCell,
		FVector& OutTargetWorldCenter,
		FString& OutDiagnostic);

	/** Validates that an injected movement prefix ends on an ordinary neighbor of TargetCell. */
	static bool ValidateApproachPath(
		AParadoxCharacter* Character,
		const FGridCellId& TargetCell,
		const FGridInjectedPath& InjectedPath,
		FString& OutDiagnostic);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Inventory|Debug", meta = (BlueprintProtected = "true"))
	bool bEnableDebug = false;

	virtual bool CanStartAction_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual void OnActionInit_Implementation() override;
	virtual void OnActionStarted_Implementation() override;
	virtual void OnActionPaused_Implementation() override;
	virtual void OnActionResumed_Implementation() override;
	virtual void OnActionCancelled_Implementation(FGameplayTag ReasonTag) override;
	virtual void OnActionInterrupted_Implementation(FGameplayTag ReasonTag) override;
	virtual void OnActionAborted_Implementation(FGameplayTag ReasonTag) override;
	virtual void OnActionCleanup_Implementation() override;

private:
	bool ReadParameters(FParadoxDropActionParameters& OutParameters, FString& OutDiagnostic) const;
	AParadoxCharacter* GetCharacter() const;
	void PerformDrop(const FVector& TargetWorldCenter);
	void HandleMovementFinished(const FGridMoveToCellExecutionResult& Result);
	void ReleaseMovement(bool bCancel);
	void LogDebugState(const TCHAR* EventName, const FString& Diagnostic = FString()) const;

	UPROPERTY(Transient)
	FParadoxDropActionParameters SemanticParameters;

	UPROPERTY(Transient)
	TObjectPtr<AParadoxPickupableActor> CapturedItem;

	UPROPERTY(Transient)
	TObjectPtr<UGridMoveToCellExecution> MovementExecution;

	FDelegateHandle MovementFinishedHandle;
	uint32 OperationGeneration = 0;
	bool bCompletionRequested = false;
};

/** Ready-to-author replay-safe Drop Definition. */
UCLASS(BlueprintType)
class PARADOX_API UParadoxDropActionDefinition : public UGameplayActionDefinition
{
	GENERATED_BODY()

public:
	UParadoxDropActionDefinition();
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
