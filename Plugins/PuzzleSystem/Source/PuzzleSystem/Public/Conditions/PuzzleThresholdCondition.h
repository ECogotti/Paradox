#pragma once

#include "CoreMinimal.h"
#include "Conditions/PuzzleCompositeCondition.h"
#include "PuzzleThresholdCondition.generated.h"

/** Composite condition that succeeds when enough child conditions succeed. */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, CollapseCategories)
class PUZZLESYSTEM_API UPuzzleThresholdCondition : public UPuzzleCompositeCondition
{
	GENERATED_BODY()

public:
	/** Minimum number of child conditions that must evaluate true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Threshold", meta = (ClampMin = "1"))
	int32 RequiredCount = 1;

	/**
	 * Counts true child conditions and compares the count with `RequiredCount`.
	 *
	 * @param Controller Read-only controller context passed to child conditions.
	 * @return True when at least `RequiredCount` children evaluate true.
	 */
	virtual bool EvaluateCondition_Implementation(const APuzzleController* Controller) override;

	/**
	 * Validates the child list and threshold bounds.
	 *
	 * @param OutError Receives a human-readable validation failure.
	 * @return True when `RequiredCount` is within the child count range.
	 */
	virtual bool ValidateCondition(FString& OutError) const override;
};
