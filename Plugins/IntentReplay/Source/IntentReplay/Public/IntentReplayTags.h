#pragma once

#include "NativeGameplayTags.h"

namespace IntentReplayTags
{
	/** Origin assigned to every request produced by replay, and excluded from recording by default. */
	INTENTREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Origin_Replay);
	/** Correlation type whose GUID is the source FRecordedIntentId. */
	INTENTREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Correlation_RecordedIntent);

	/** Initialization or binding is unavailable. */
	INTENTREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_NotInitialized);
	/** No recording session can satisfy the requested operation/transaction. */
	INTENTREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_NoRecordingSession);
	/** Accepted request parameters cannot be isolated safely. */
	INTENTREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_UnrecordableParameters);
	/** Track structure or immutable invariants are invalid. */
	INTENTREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_InvalidTrack);
	/** A recorded GameplayAction Definition cannot be resolved. */
	INTENTREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_DefinitionUnavailable);
	/** Current Definition/schema is incompatible with the selected replay policy. */
	INTENTREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_Compatibility);
	/** GameplayActions rejected a prepared replay request. */
	INTENTREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_SubmissionRejected);
	/** Cancellation reason used only for handles owned by a stopped playback session. */
	INTENTREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cancelled_PlaybackStopped);
}
