#pragma once

#include "CoreMinimal.h"
#include "Conditions/PuzzleCondition.h"
#include "PuzzleNotCondition.generated.h"

/** Condition that inverts one child condition. */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, CollapseCategories)
class PUZZLESYSTEM_API UPuzzleNotCondition : public UPuzzleCondition
{
	GENERATED_BODY()

public:
	/** Inline child condition whose result will be inverted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Puzzle|Child", meta = (EditInline, AllowEditInlineCustomization, MaxPropertyDepth = "8"))
	TObjectPtr<UPuzzleCondition> Condition = nullptr;

	/**
	 * Evaluates and inverts the configured child condition.
	 *
	 * @param Controller Read-only controller context passed to the child.
	 * @return True when a child exists and that child evaluates false.
	 */
	virtual bool EvaluateCondition_Implementation(const APuzzleController* Controller) override;

	/**
	 * Reports every input referenced by the child condition.
	 *
	 * @param OutInputIds Receives child input IDs when a child is configured.
	 */
	virtual void GetReferencedInputIds(TSet<FName>& OutInputIds) const override;

	/**
	 * Validates that a child condition exists and is itself valid.
	 *
	 * @param OutError Receives a human-readable validation failure.
	 * @return True when the child is present and valid.
	 */
	virtual bool ValidateCondition(FString& OutError) const override;
};
