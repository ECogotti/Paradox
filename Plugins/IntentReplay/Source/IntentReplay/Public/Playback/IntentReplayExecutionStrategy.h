#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/GameplayActionTypes.h"
#include "IntentReplayExecutionStrategy.generated.h"

class UGameplayActionComponent;
struct FRecordedIntent;

/**
 * Replaceable submission boundary for already validated and prepared replay requests.
 * Strategy objects do not own scheduling, tracks, sessions, or runtime handles.
 */
UCLASS(BlueprintType, Abstract, EditInlineNew, DefaultToInstanced)
class INTENTREPLAY_API UIntentReplayExecutionStrategy : public UObject
{
	GENERATED_BODY()

public:
	/** Recognizes a semantic terminal outcome that is expected by this adapter; it remains in the journal. */
	virtual bool IsExpectedTerminalResult(const FRecordedIntent& Intent, const FGameplayActionResult& Result) const;

	/** Base implementation rejects safely; concrete strategies must return GameplayActions' result. */
	virtual FGameplayActionSubmissionResult SubmitPreparedRequest(
		UGameplayActionComponent* ActionComponent,
		const FGameplayActionRequest& Request) const;
};

/** Default strategy that calls SubmitAction on the recipient's bound GameplayActionComponent. */
UCLASS()
class INTENTREPLAY_API UIntentReplayDirectExecutionStrategy : public UIntentReplayExecutionStrategy
{
	GENERATED_BODY()

public:
	/** Submits one new request after validating the target component lifetime. */
	virtual FGameplayActionSubmissionResult SubmitPreparedRequest(
		UGameplayActionComponent* ActionComponent,
		const FGameplayActionRequest& Request) const override;
};
