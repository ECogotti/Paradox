#include "Conditions/PuzzleCompositeCondition.h"

void UPuzzleCompositeCondition::GetReferencedInputIds(TSet<FName>& OutInputIds) const
{
	for (const TObjectPtr<UPuzzleCondition>& Condition : Conditions)
	{
		if (Condition)
		{
			Condition->GetReferencedInputIds(OutInputIds);
		}
	}
}

bool UPuzzleCompositeCondition::ValidateCondition(FString& OutError) const
{
	for (int32 Index = 0; Index < Conditions.Num(); ++Index)
	{
		const UPuzzleCondition* Condition = Conditions[Index];
		if (!Condition)
		{
			OutError = FString::Printf(TEXT("Composite condition has a null child at index %d."), Index);
			return false;
		}

		if (!Condition->ValidateCondition(OutError))
		{
			OutError = FString::Printf(TEXT("Composite child %d is invalid: %s"), Index, *OutError);
			return false;
		}
	}

	return true;
}
