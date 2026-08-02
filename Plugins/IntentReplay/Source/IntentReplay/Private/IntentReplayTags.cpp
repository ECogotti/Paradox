#include "IntentReplayTags.h"

namespace IntentReplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(Origin_Replay, "GameplayAction.Origin.Replay");
	UE_DEFINE_GAMEPLAY_TAG(Correlation_RecordedIntent, "IntentReplay.Correlation.RecordedIntent");

	UE_DEFINE_GAMEPLAY_TAG(Failure_NotInitialized, "IntentReplay.Failure.NotInitialized");
	UE_DEFINE_GAMEPLAY_TAG(Failure_NoRecordingSession, "IntentReplay.Failure.NoRecordingSession");
	UE_DEFINE_GAMEPLAY_TAG(Failure_UnrecordableParameters, "IntentReplay.Failure.UnrecordableParameters");
	UE_DEFINE_GAMEPLAY_TAG(Failure_InvalidTrack, "IntentReplay.Failure.InvalidTrack");
	UE_DEFINE_GAMEPLAY_TAG(Failure_DefinitionUnavailable, "IntentReplay.Failure.DefinitionUnavailable");
	UE_DEFINE_GAMEPLAY_TAG(Failure_Compatibility, "IntentReplay.Failure.Compatibility");
	UE_DEFINE_GAMEPLAY_TAG(Failure_SubmissionRejected, "IntentReplay.Failure.SubmissionRejected");
	UE_DEFINE_GAMEPLAY_TAG(Failure_PendingExternalRecovery, "IntentReplay.Failure.PendingExternalRecovery");
	UE_DEFINE_GAMEPLAY_TAG(Failure_RecordedIntentNotFound, "IntentReplay.Failure.RecordedIntentNotFound");
	UE_DEFINE_GAMEPLAY_TAG(Failure_ExternalInterruption, "IntentReplay.Failure.ExternalInterruption");
	UE_DEFINE_GAMEPLAY_TAG(Cancelled_PlaybackStopped, "IntentReplay.Cancelled.PlaybackStopped");
}
