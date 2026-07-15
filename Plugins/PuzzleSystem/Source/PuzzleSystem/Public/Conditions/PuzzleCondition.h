#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PuzzleCondition.generated.h"

class APuzzleController;

/** Base strategy object for controller condition evaluation. */
UCLASS(Abstract, BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced, CollapseCategories)
class PUZZLESYSTEM_API UPuzzleCondition : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Evaluates whether the owning controller should currently request activation.
	 *
	 * @param Controller Read-only controller context used to query input states.
	 * @return True when this condition is satisfied.
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Puzzle|Condition")
	bool EvaluateCondition(const APuzzleController* Controller);
	virtual bool EvaluateCondition_Implementation(const APuzzleController* Controller);

	/**
	 * Reports local controller input IDs referenced by this condition tree.
	 *
	 * @param OutInputIds Receives every referenced input ID.
	 */
	virtual void GetReferencedInputIds(TSet<FName>& OutInputIds) const;

	/**
	 * Validates condition-specific configuration before controller initialization.
	 *
	 * @param OutError Receives a human-readable error when validation fails.
	 * @return True when the condition can be safely evaluated.
	 */
	virtual bool ValidateCondition(FString& OutError) const;
};
