#pragma once

#include "CoreMinimal.h"
#include "Conditions/PuzzleCompositeCondition.h"
#include "PuzzleAllCondition.generated.h"

/** Composite condition that succeeds only when every child condition succeeds. */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, CollapseCategories)
class PUZZLESYSTEM_API UPuzzleAllCondition : public UPuzzleCompositeCondition
{
	GENERATED_BODY()

public:
	/**
	 * Evaluates children using logical AND semantics.
	 *
	 * @param Controller Read-only controller context passed to child conditions.
	 * @return True only when at least one child exists and every child is true.
	 */
	virtual bool EvaluateCondition_Implementation(const APuzzleController* Controller) override;
};
