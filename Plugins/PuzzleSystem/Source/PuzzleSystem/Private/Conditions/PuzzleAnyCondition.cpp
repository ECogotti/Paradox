#include "Conditions/PuzzleAnyCondition.h"

bool UPuzzleAnyCondition::EvaluateCondition_Implementation(const APuzzleController* Controller)
{
	if (Conditions.IsEmpty())
	{
		return false;
	}

	for (const TObjectPtr<UPuzzleCondition>& Condition : Conditions)
	{
		if (Condition && Condition->EvaluateCondition(Controller))
		{
			return true;
		}
	}

	return false;
}
