#pragma once

#include "CoreMinimal.h"
#include "Conditions/PuzzleCompositeCondition.h"
#include "PuzzleAnyCondition.generated.h"

/** Composite condition that succeeds when any child condition succeeds. */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, CollapseCategories)
class PUZZLESYSTEM_API UPuzzleAnyCondition : public UPuzzleCompositeCondition
{
	GENERATED_BODY()

public:
	/**
	 * Evaluates children using logical OR semantics.
	 *
	 * @param Controller Read-only controller context passed to child conditions.
	 * @return True when at least one child exists and one child is true.
	 */
	virtual bool EvaluateCondition_Implementation(const APuzzleController* Controller) override;
};
