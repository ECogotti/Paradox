#include "GameplayActionsGridWorldTags.h"

namespace GameplayActionsGridWorldTags
{
	UE_DEFINE_GAMEPLAY_TAG(Action_MoveToGridCell, "GameplayAction.GridWorld.MoveToGridCell");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Failure_Blocked,
		"GameplayAction.Result.Failure.GridWorld.MoveToGridCell.Blocked");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Failure_OffPath,
		"GameplayAction.Result.Failure.GridWorld.MoveToGridCell.OffPath");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Failure_Aborted,
		"GameplayAction.Result.Failure.GridWorld.MoveToGridCell.Aborted");
	UE_DEFINE_GAMEPLAY_TAG(
		Result_Failure_Invalid,
		"GameplayAction.Result.Failure.GridWorld.MoveToGridCell.Invalid");
}
