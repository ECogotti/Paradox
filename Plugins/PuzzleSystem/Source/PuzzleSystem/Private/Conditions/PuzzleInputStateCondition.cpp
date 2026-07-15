#include "Conditions/PuzzleInputStateCondition.h"

#include "Controllers/PuzzleController.h"

bool UPuzzleInputStateCondition::EvaluateCondition_Implementation(const APuzzleController* Controller)
{
	if (!Controller || InputId.IsNone())
	{
		return false;
	}

	FPuzzleSignalState InputState;
	if (!Controller->TryGetInputState(InputId, InputState))
	{
		return false;
	}

	return InputState.bIsActive == bExpectedActive;
}

void UPuzzleInputStateCondition::GetReferencedInputIds(TSet<FName>& OutInputIds) const
{
	if (!InputId.IsNone())
	{
		OutInputIds.Add(InputId);
	}
}

bool UPuzzleInputStateCondition::ValidateCondition(FString& OutError) const
{
	if (InputId.IsNone())
	{
		OutError = TEXT("Input state condition has no InputId.");
		return false;
	}

	return true;
}
