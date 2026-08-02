#pragma once

#include "NativeGameplayTags.h"

namespace GameplayActionTags
{
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Lock_Root);
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Lock_Primary);
	/** Shared exact-match lock for actions that exclusively drive character or pawn movement. */
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Lock_Movement);

	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Success);
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_Unspecified);
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_InvalidRequest);
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_CannotStart);
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_JournalRejected);
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Failure_QueueTimeout);
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Cancelled_ByRequester);
	/** Generic fallback for an explicit external interruption without a more specific reason. */
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Interrupted_External);
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Interrupted_HigherPriority);
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Aborted_OwnerEndPlay);
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Aborted_SystemReset);
	GAMEPLAYACTIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Result_Aborted_ComponentDeactivated);
}
