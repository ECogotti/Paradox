#pragma once

#include "GridWorldTypes.h"
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
	/**
	 * When enabled, clone replay replaces the recorded movement goal-contention policy in the
	 * runtime request copy. Disable it to preserve exactly the policy recorded by the player.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Paradox|Clone Replay|GridWorld")
	bool bOverrideGoalContentionPolicy = true;

	/**
	 * Goal-contention policy used only by clone replay when the override is enabled.
	 * Redirect On Completion preserves the recorded route, then claims a nearby free destination
	 * if the original final cell is occupied when the clone reaches it.
	 */
	UPROPERTY(
		EditDefaultsOnly,
		BlueprintReadWrite,
		Category = "Paradox|Clone Replay|GridWorld",
		meta = (EditCondition = "bOverrideGoalContentionPolicy"))
	EGridGoalContentionPolicy GoalContentionPolicyOverride =
		EGridGoalContentionPolicy::RedirectOnCompletion;

	virtual FGameplayActionSubmissionResult SubmitPreparedRequest(
		UGameplayActionComponent* ActionComponent,
		const FGameplayActionRequest& Request) const override;
};
