#pragma once

#include "AI/GridWorldAIController.h"
#include "ParadoxCloneController.generated.h"

/**
 * Non-player controller for reconstructed Paradox clones.
 *
 * GridWorld supplies the precise path-following component required by future Intent Replay
 * playback. This class intentionally owns no input or player UI state.
 */
UCLASS(Blueprintable)
class PARADOX_API AParadoxCloneController : public AGridWorldAIController
{
	GENERATED_BODY()

public:
	AParadoxCloneController(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
