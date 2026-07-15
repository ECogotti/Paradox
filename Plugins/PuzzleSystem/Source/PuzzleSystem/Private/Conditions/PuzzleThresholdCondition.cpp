#include "Conditions/PuzzleThresholdCondition.h"

bool UPuzzleThresholdCondition::EvaluateCondition_Implementation(const APuzzleController* Controller)
{
	if (RequiredCount <= 0 || RequiredCount > Conditions.Num())
	{
		return false;
	}

	int32 PassingCount = 0;
	for (const TObjectPtr<UPuzzleCondition>& Condition : Conditions)
	{
		if (Condition && Condition->EvaluateCondition(Controller))
		{
			++PassingCount;
			if (PassingCount >= RequiredCount)
			{
				return true;
			}
		}
	}

	return false;
}

bool UPuzzleThresholdCondition::ValidateCondition(FString& OutError) const
{
	if (!Super::ValidateCondition(OutError))
	{
		return false;
	}

	if (RequiredCount <= 0)
	{
		OutError = TEXT("Threshold condition RequiredCount must be greater than zero.");
		return false;
	}

	if (RequiredCount > Conditions.Num())
	{
		OutError = FString::Printf(TEXT("Threshold condition requires %d passing children but only has %d children."), RequiredCount, Conditions.Num());
		return false;
	}

	return true;
}
