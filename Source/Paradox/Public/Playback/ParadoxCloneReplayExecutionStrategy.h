#pragma once

#include "Playback/IntentReplayExecutionStrategy.h"
#include "ParadoxCloneReplayExecutionStrategy.generated.h"

/**
 * Recipient adapter for clone replay requests.
 *
 * Exact GridWorld paths are recorded with the player's controller-specific query context. Before
 * submission, this strategy copies and re-stamps those paths for the clone controller without
 * mutating the immutable Intent Replay track.
 */
UCLASS()
class PARADOX_API UParadoxCloneReplayExecutionStrategy
	: public UIntentReplayDirectExecutionStrategy
{
	GENERATED_BODY()

public:
	virtual FGameplayActionSubmissionResult SubmitPreparedRequest(
		UGameplayActionComponent* ActionComponent,
		const FGameplayActionRequest& Request) const override;
};
