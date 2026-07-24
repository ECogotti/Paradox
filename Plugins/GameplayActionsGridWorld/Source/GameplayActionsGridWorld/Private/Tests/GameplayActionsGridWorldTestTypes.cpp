#include "Tests/GameplayActionsGridWorldTestTypes.h"

#include "GameplayActionTags.h"

void UGameplayActionsGridWorldTestLockAction::CompleteForTest()
{
	SucceedAction(
		GameplayActionTags::Result_Success,
		TEXT("Movement lock released by GameplayActionsGridWorld automation test."));
}
