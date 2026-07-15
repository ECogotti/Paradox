#pragma once

#include "CoreMinimal.h"
#include "Conditions/PuzzleCondition.h"
#include "PuzzleCompositeCondition.generated.h"

/** Base class for conditions that evaluate a list of child conditions. */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced, CollapseCategories)
class PUZZLESYSTEM_API UPuzzleCompositeCondition : public UPuzzleCondition
{
	GENERATED_BODY()

public:
	/** Inline child conditions evaluated by the concrete composite condition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Puzzle|Children", meta = (EditInline, AllowEditInlineCustomization, MaxPropertyDepth = "8"))
	TArray<TObjectPtr<UPuzzleCondition>> Conditions;

	/**
	 * Reports every input referenced by child conditions.
	 *
	 * @param OutInputIds Receives the union of child input IDs.
	 */
	virtual void GetReferencedInputIds(TSet<FName>& OutInputIds) const override;

	/**
	 * Validates that every child condition exists and is itself valid.
	 *
	 * @param OutError Receives a human-readable validation failure.
	 * @return True when all children are present and valid.
	 */
	virtual bool ValidateCondition(FString& OutError) const override;
};
