#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Signals/PuzzleSignalPayload.h"
#include "PuzzleSignalTypes.generated.h"

/** Cached state for one gameplay-tagged signal channel. */
USTRUCT(BlueprintType)
struct PUZZLESYSTEM_API FPuzzleSignalState
{
	GENERATED_BODY()

	/** True when this cache entry came from a valid emitter signal; invalid inputs fail closed in conditions. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle")
	bool bIsValid = false;

	/** Stateful active/inactive value published by the emitter. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle")
	bool bIsActive = false;

	/** Optional typed payload object for signal-specific data. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle")
	TObjectPtr<UPuzzleSignalPayload> Payload = nullptr;

	/** Monotonic change counter used to distinguish republished payload/state updates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle")
	int64 Revision = 0;
};
