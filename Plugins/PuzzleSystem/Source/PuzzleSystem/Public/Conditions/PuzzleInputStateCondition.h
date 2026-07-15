#pragma once

#include "CoreMinimal.h"
#include "Conditions/PuzzleCondition.h"
#include "PuzzleInputStateCondition.generated.h"

/** Condition that checks one controller-local input for an expected active state. */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, CollapseCategories)
class PUZZLESYSTEM_API UPuzzleInputStateCondition : public UPuzzleCondition
{
	GENERATED_BODY()

public:
	/** Local input alias configured on the owning controller. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Input")
	FName InputId;

	/** Required active value; invalid input still fails even when false is expected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Input")
	bool bExpectedActive = true;

	/**
	 * Evaluates the expected active state against the controller cache.
	 *
	 * @param Controller Read-only controller context used to query `InputId`.
	 * @return True only when the input is valid and matches `bExpectedActive`.
	 */
	virtual bool EvaluateCondition_Implementation(const APuzzleController* Controller) override;

	/**
	 * Reports the single input ID used by this condition.
	 *
	 * @param OutInputIds Receives `InputId` when it is configured.
	 */
	virtual void GetReferencedInputIds(TSet<FName>& OutInputIds) const override;

	/**
	 * Validates that `InputId` is configured.
	 *
	 * @param OutError Receives a human-readable validation failure.
	 * @return True when `InputId` is not `None`.
	 */
	virtual bool ValidateCondition(FString& OutError) const override;
};
