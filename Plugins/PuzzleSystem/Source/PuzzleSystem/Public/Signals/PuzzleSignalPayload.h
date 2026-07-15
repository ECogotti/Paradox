#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PuzzleSignalPayload.generated.h"

/**
 * Base class for arbitrary signal-specific data carried by an emitter state.
 *
 * Subclass this in Blueprint or C++ when a condition needs typed data beyond
 * the signal's active/inactive state.
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class PUZZLESYSTEM_API UPuzzleSignalPayload : public UObject
{
	GENERATED_BODY()
};
