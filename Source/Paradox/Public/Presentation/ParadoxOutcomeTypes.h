#pragma once

#include "CoreMinimal.h"
#include "TimeLoop/ParadoxTimeLoopTypes.h"
#include "ParadoxOutcomeTypes.generated.h"

UENUM(BlueprintType)
enum class EParadoxOutcomeType : uint8
{
	TimelineCollapse,
	GameOver,
	LevelComplete
};

/** Presentation-only value copy; widgets never reconstruct authoritative loop context. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxOutcomePresentationData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Presentation")
	EParadoxOutcomeType OutcomeType = EParadoxOutcomeType::TimelineCollapse;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Presentation")
	FText Title;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Presentation")
	FText Message;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Presentation")
	FParadoxContext ParadoxContext;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Presentation")
	FParadoxGameOverContext GameOverContext;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Presentation")
	FParadoxLevelCompleteContext LevelCompleteContext;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Presentation")
	bool bShowRestart = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FParadoxPresentationRestartEvent);
