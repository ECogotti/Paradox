#include "Conditions/PuzzleCondition.h"

bool UPuzzleCondition::EvaluateCondition_Implementation(const APuzzleController* Controller)
{
	return false;
}

void UPuzzleCondition::GetReferencedInputIds(TSet<FName>& OutInputIds) const
{
}

bool UPuzzleCondition::ValidateCondition(FString& OutError) const
{
	return true;
}
