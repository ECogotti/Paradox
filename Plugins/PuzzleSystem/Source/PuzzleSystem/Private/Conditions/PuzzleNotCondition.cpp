#include "Conditions/PuzzleNotCondition.h"

bool UPuzzleNotCondition::EvaluateCondition_Implementation(const APuzzleController* Controller)
{
	return Condition ? !Condition->EvaluateCondition(Controller) : false;
}

void UPuzzleNotCondition::GetReferencedInputIds(TSet<FName>& OutInputIds) const
{
	if (Condition)
	{
		Condition->GetReferencedInputIds(OutInputIds);
	}
}

bool UPuzzleNotCondition::ValidateCondition(FString& OutError) const
{
	if (!Condition)
	{
		OutError = TEXT("NOT condition has no child condition.");
		return false;
	}

	if (!Condition->ValidateCondition(OutError))
	{
		OutError = FString::Printf(TEXT("NOT child is invalid: %s"), *OutError);
		return false;
	}

	return true;
}
