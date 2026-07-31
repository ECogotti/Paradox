#include "GameplayActionTags.h"

namespace GameplayActionTags
{
	UE_DEFINE_GAMEPLAY_TAG(Lock_Root, "GameplayAction.Lock");
	UE_DEFINE_GAMEPLAY_TAG(Lock_Primary, "GameplayAction.Lock.Primary");
	UE_DEFINE_GAMEPLAY_TAG(Lock_Movement, "GameplayAction.Lock.Movement");

	UE_DEFINE_GAMEPLAY_TAG(Result_Success, "GameplayAction.Result.Success");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_Unspecified, "GameplayAction.Result.Failure.Unspecified");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_InvalidRequest, "GameplayAction.Result.Failure.InvalidRequest");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_CannotStart, "GameplayAction.Result.Failure.CannotStart");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_JournalRejected, "GameplayAction.Result.Failure.JournalRejected");
	UE_DEFINE_GAMEPLAY_TAG(Result_Failure_QueueTimeout, "GameplayAction.Result.Failure.QueueTimeout");
	UE_DEFINE_GAMEPLAY_TAG(Result_Cancelled_ByRequester, "GameplayAction.Result.Cancelled.ByRequester");
	UE_DEFINE_GAMEPLAY_TAG(Result_Interrupted_External, "GameplayAction.Result.Interrupted.External");
	UE_DEFINE_GAMEPLAY_TAG(Result_Interrupted_HigherPriority, "GameplayAction.Result.Interrupted.HigherPriority");
	UE_DEFINE_GAMEPLAY_TAG(Result_Aborted_OwnerEndPlay, "GameplayAction.Result.Aborted.OwnerEndPlay");
	UE_DEFINE_GAMEPLAY_TAG(Result_Aborted_SystemReset, "GameplayAction.Result.Aborted.SystemReset");
	UE_DEFINE_GAMEPLAY_TAG(Result_Aborted_ComponentDeactivated, "GameplayAction.Result.Aborted.ComponentDeactivated");
}
