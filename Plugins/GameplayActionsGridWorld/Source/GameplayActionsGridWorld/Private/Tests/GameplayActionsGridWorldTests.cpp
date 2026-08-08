#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/GridMoveToCellTask.h"
#include "AI/GridWorldPathFollowingComponent.h"
#include "AIController.h"
#include "Actions/GridMoveToCellAction.h"
#include "Actions/GridMoveToCellActionDefinition.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Execution/GridMoveToCellExecution.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameplayActionTags.h"
#include "GameplayActionsGridWorldTags.h"
#include "Tests/GameplayActionsGridWorldTestTypes.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace GameplayActionsGridWorldTests
{
	int32 CountMoveTasks()
	{
		int32 Count = 0;
		for (TObjectIterator<UGridMoveToCellTask> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject))
			{
				++Count;
			}
		}
		return Count;
	}

	FGameplayActionRequest MakeRequest(UGameplayActionDefinition& Definition)
	{
		return UGameplayActionBlueprintLibrary::CreateActionRequest(&Definition).Request;
	}

	UGameplayActionDefinition* MakeLockHolderDefinition()
	{
		UGameplayActionDefinition* Definition = NewObject<UGameplayActionDefinition>();
		Definition->InstanceClass = UGameplayActionsGridWorldTestLockAction::StaticClass();
		Definition->ActionTag = GameplayActionsGridWorldTags::Action_MoveToGridCell;
		Definition->ExecutionLocks.AddTag(GameplayActionTags::Lock_Movement);
		Definition->BlockedPolicy = EGameplayActionBlockedPolicy::Queue;
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsGridWorldExecutionInvalidInputTest,
	"GameplayActionsGridWorld.Execution.InvalidInputAndOneShotCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsGridWorldExecutionInvalidInputTest::RunTest(const FString& Parameters)
{
	FGridMoveToCellExecutionRequest Request;
	const FGridMoveToCellEvaluationResult Evaluation = UGridMoveToCellExecution::Evaluate(Request);
	TestFalse(TEXT("Evaluation rejects a missing Controller"), Evaluation.bCanExecute);
	TestTrue(TEXT("Evaluation keeps an actionable diagnostic"), !Evaluation.DiagnosticMessage.IsEmpty());
	UGridMoveToCellExecution* Execution = NewObject<UGridMoveToCellExecution>();
	FString Diagnostic;
	TestFalse(TEXT("Execution rejects a missing Controller"), Execution->Start(Request, Diagnostic));
	TestTrue(TEXT("Start failure keeps an actionable diagnostic"), !Diagnostic.IsEmpty());
	Execution->Cancel();
	Execution->Cancel();
	TestFalse(TEXT("Repeated cleanup leaves execution stopped"), Execution->IsRunning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsGridWorldDefinitionTest,
	"GameplayActionsGridWorld.Definition.SchemaAndDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsGridWorldDefinitionTest::RunTest(const FString& Parameters)
{
	UGridMoveToCellActionDefinition* Definition =
		NewObject<UGridMoveToCellActionDefinition>();
	TestEqual(TEXT("Native Definition selects Grid action class"),
		Definition->InstanceClass.Get(),
		UGridMoveToCellAction::StaticClass());
	TestEqual(TEXT("Native Definition uses semantic action tag"),
		Definition->ActionTag,
		GameplayActionsGridWorldTags::Action_MoveToGridCell.GetTag());
	TestTrue(TEXT("Native Definition owns Movement lock"),
		Definition->ExecutionLocks.HasTagExact(GameplayActionTags::Lock_Movement));
	TestEqual(TEXT("Blocked policy defaults to Queue"),
		Definition->BlockedPolicy,
		EGameplayActionBlockedPolicy::Queue);
	TestEqual(TEXT("Journal defaults to Optional"),
		Definition->JournalRequirement,
		EGameplayActionJournalRequirement::Optional);
	TestFalse(TEXT("AI logic lock defaults to false"),
		Definition->DefaultParameters
			.GetValueBool(GridMoveToCellActionParameters::LockAILogic)
			.GetValue());
	TestEqual(
		TEXT("Destination requests remain the default"),
		Definition->DefaultParameters
			.GetValueEnum<EGridMovePathSource>(GridMoveToCellActionParameters::PathSource)
			.GetValue(),
		EGridMovePathSource::Destination);
	TestEqual(
		TEXT("Movement stops before an occupied requested goal by default"),
		Definition->DefaultParameters
			.GetValueEnum<EGridGoalContentionPolicy>(
				GridMoveToCellActionParameters::GoalContentionPolicy)
			.GetValue(),
		EGridGoalContentionPolicy::StopBeforeOccupied);
	const TValueOrError<FStructView, EPropertyBagResult> DefaultInjectedPath =
		Definition->DefaultParameters.GetValueStruct(
			GridMoveToCellActionParameters::InjectedPath,
			FGridInjectedPath::StaticStruct());
	TestTrue(TEXT("Definition exposes the exact injected path payload"), DefaultInjectedPath.HasValue());
	TestTrue(TEXT("Factory accepts native Definition"),
		UGameplayActionBlueprintLibrary::CreateActionRequest(Definition).WasCreated());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsGridWorldQueueLifecycleTest,
	"GameplayActionsGridWorld.Runtime.QueueDoesNotCreateMoveTask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsGridWorldQueueLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsGridWorldTests;

	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(
			GetTransientPackage(),
			UWorld::StaticClass(),
			TEXT("GameplayActionsGridWorldQueueLifecycle")));
	if (!TestNotNull(TEXT("Transient Grid action world"), World))
	{
		return false;
	}
	AAIController* Controller = World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("World-owned AIController"), Controller))
	{
		World->DestroyWorld(false);
		return false;
	}
	UGameplayActionComponent* Component =
		NewObject<UGameplayActionComponent>(Controller);
	Controller->AddInstanceComponent(Component);
	Component->RegisterComponent();

	UGameplayActionDefinition* HolderDefinition = MakeLockHolderDefinition();
	UGridMoveToCellActionDefinition* GridDefinition =
		NewObject<UGridMoveToCellActionDefinition>();

	const FGameplayActionSubmissionResult Holder =
		Component->SubmitAction(MakeRequest(*HolderDefinition));
	const int32 TasksBeforeQueue = CountMoveTasks();
	const FGameplayActionSubmissionResult Queued =
		Component->SubmitAction(MakeRequest(*GridDefinition));

	TestEqual(TEXT("Holder starts"), Holder.Status, EGameplayActionSubmissionStatus::AcceptedStarted);
	TestEqual(TEXT("Grid action enters queue"), Queued.Status, EGameplayActionSubmissionStatus::AcceptedQueued);
	TestEqual(TEXT("Queue residence creates no Grid movement task"),
		CountMoveTasks(),
		TasksBeforeQueue);
	TestTrue(TEXT("Queued instance exists after Action Init"),
		Component->GetActionInstance(Queued.Handle) != nullptr);

	TestEqual(TEXT("Queued action can be cancelled"),
		Component->CancelAction(
			Queued.Handle,
			GameplayActionTags::Result_Cancelled_ByRequester),
		EGameplayActionOperationResult::Succeeded);
	TestEqual(TEXT("Queued cancellation still creates no movement task"),
		CountMoveTasks(),
		TasksBeforeQueue);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsGridWorldAutomaticStartTest,
	"GameplayActionsGridWorld.Runtime.QueueStartsAfterMovementLockRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsGridWorldAutomaticStartTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsGridWorldTests;

	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(
			GetTransientPackage(),
			UWorld::StaticClass(),
			TEXT("GameplayActionsGridWorldAutomaticStart")));
	if (!TestNotNull(TEXT("Transient Grid action world"), World))
	{
		return false;
	}
	AAIController* Controller = World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("World-owned AIController"), Controller))
	{
		World->DestroyWorld(false);
		return false;
	}
	UGameplayActionComponent* Component =
		NewObject<UGameplayActionComponent>(Controller);
	Controller->AddInstanceComponent(Component);
	Component->RegisterComponent();

	UGameplayActionDefinition* HolderDefinition = MakeLockHolderDefinition();
	UGridMoveToCellActionDefinition* GridDefinition =
		NewObject<UGridMoveToCellActionDefinition>();
	const FGameplayActionSubmissionResult Holder =
		Component->SubmitAction(MakeRequest(*HolderDefinition));
	const FGameplayActionSubmissionResult Queued =
		Component->SubmitAction(MakeRequest(*GridDefinition));
	TSet<UGridMoveToCellTask*> TasksBeforeStart;
	for (TObjectIterator<UGridMoveToCellTask> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			TasksBeforeStart.Add(*It);
		}
	}

	UGameplayActionsGridWorldTestLockAction* HolderInstance =
		Cast<UGameplayActionsGridWorldTestLockAction>(
			Component->GetActionInstance(Holder.Handle));
	TestNotNull(TEXT("Movement lock holder is running"), HolderInstance);
	if (HolderInstance)
	{
		HolderInstance->CompleteForTest();
	}

	EGameplayActionState GridState = EGameplayActionState::Queued;
	TestTrue(TEXT("Queued Grid action remains queryable after lock release"),
		Component->GetActionState(Queued.Handle, GridState));
	TestEqual(TEXT("Queued Grid action starts automatically"),
		GridState,
		EGameplayActionState::Running);

	UGridMoveToCellTask* StartedMoveTask = nullptr;
	for (TObjectIterator<UGridMoveToCellTask> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject) && !TasksBeforeStart.Contains(*It))
		{
			StartedMoveTask = *It;
			break;
		}
	}
	TestNotNull(TEXT("Action Start creates the GridWorld movement task"), StartedMoveTask);
	if (StartedMoveTask)
	{
		// Drive the same public task delegate used in production so the test covers result
		// mapping without depending on a complete pawn/path-following fixture.
		StartedMoveTask->OnMoveTaskFinished.Broadcast(EPathFollowingResult::Invalid, Controller);
	}

	FGameplayActionResult GridResult;
	TestTrue(TEXT("Queued Grid action automatically reaches a terminal result"),
		Component->GetActionResult(Queued.Handle, GridResult));
	TestEqual(TEXT("GridWorld Invalid result maps to the bridge reason tag"),
		GridResult.ReasonTag,
		GameplayActionsGridWorldTags::Result_Failure_Invalid.GetTag());
	TestEqual(TEXT("Invalid movement maps to Failed action state"),
		GridResult.TerminalState,
		EGameplayActionState::Failed);
	if (StartedMoveTask)
	{
		StartedMoveTask->OnMoveTaskFinished.Broadcast(EPathFollowingResult::Success, Controller);
		FGameplayActionResult ResultAfterLateCallback;
		TestTrue(TEXT("Terminal result remains queryable after a late task callback"),
			Component->GetActionResult(Queued.Handle, ResultAfterLateCallback));
		TestEqual(TEXT("Late callbacks cannot replace the terminal result"),
			ResultAfterLateCallback.ReasonTag,
			GameplayActionsGridWorldTags::Result_Failure_Invalid.GetTag());
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsGridWorldPlayerControllerTest,
	"GameplayActionsGridWorld.Runtime.PlayerControllerUsesPathFollowing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsGridWorldPlayerControllerTest::RunTest(
	const FString& Parameters)
{
	using namespace GameplayActionsGridWorldTests;

	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(
			GetTransientPackage(),
			UWorld::StaticClass(),
			TEXT("GameplayActionsGridWorldPlayerController")));
	if (!TestNotNull(TEXT("Transient player Grid action world"), World))
	{
		return false;
	}

	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACharacter* Character = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("World-owned PlayerController"), Controller)
		|| !TestNotNull(TEXT("World-owned Character"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}

	UGridWorldPathFollowingComponent* PathFollowing =
		NewObject<UGridWorldPathFollowingComponent>(Controller);
	Controller->AddInstanceComponent(PathFollowing);
	PathFollowing->RegisterComponent();
	Controller->Possess(Character);

	UPathFollowingComponent* InstalledPathFollowing =
		Controller->FindComponentByClass<UPathFollowingComponent>();
	if (TestNotNull(
			TEXT("PlayerController owns a path-following component"),
			InstalledPathFollowing))
	{
		TestTrue(
			TEXT("PlayerController uses GridWorld precise path following"),
			InstalledPathFollowing->IsA<UGridWorldPathFollowingComponent>());
	}
	UGridNavigationOccupancyComponent* PlayerOccupancy =
		UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(*Character);
	if (TestNotNull(TEXT("Player GridWorld follower auto-registers Pawn occupancy"), PlayerOccupancy))
	{
		TestTrue(TEXT("Player occupancy publishes a stable owner identity"), PlayerOccupancy->OccupantId.IsValid());
		TestFalse(TEXT("Player occupancy remains non-blocking for ordinary A*"), PlayerOccupancy->bBlocksWhenConsidered);
	}

	UGameplayActionComponent* Component =
		NewObject<UGameplayActionComponent>(Character);
	Character->AddInstanceComponent(Component);
	Component->RegisterComponent();

	UGridMoveToCellActionDefinition* GridDefinition =
		NewObject<UGridMoveToCellActionDefinition>();
	const FGameplayActionSubmissionResult Submission =
		Component->SubmitAction(MakeRequest(*GridDefinition));

	TestEqual(
		TEXT("Player movement request passes Controller and Path Following validation"),
		Submission.Status,
		EGameplayActionSubmissionStatus::AcceptedStarted);

	FGameplayActionResult Result;
	TestTrue(
		TEXT("Missing GridWorld data produces an observable terminal result"),
		Component->GetActionResult(Submission.Handle, Result));
	TestEqual(
		TEXT("Player Grid projection failure maps to Invalid"),
		Result.ReasonTag,
		GameplayActionsGridWorldTags::Result_Failure_Invalid.GetTag());
	TestEqual(
		TEXT("Player Grid projection failure ends the action"),
		Result.TerminalState,
		EGameplayActionState::Failed);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsGridWorldAssetTest,
	"GameplayActionsGridWorld.Asset.ReadyToUseDefinition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsGridWorldAssetTest::RunTest(const FString& Parameters)
{
	const UGridMoveToCellActionDefinition* Asset =
		LoadObject<UGridMoveToCellActionDefinition>(
			nullptr,
			TEXT("/GameplayActionsGridWorld/Definitions/DA_GameplayAction_MoveToGridCell.DA_GameplayAction_MoveToGridCell"));
	TestNotNull(TEXT("Ready-to-use Grid Move Definition asset loads"), Asset);
	if (Asset)
	{
		TestEqual(
			TEXT("Ready-to-use action stops before occupied goals"),
			Asset->GetDefaultParameters()
				.GetValueEnum<EGridGoalContentionPolicy>(GridMoveToCellActionParameters::GoalContentionPolicy)
				.GetValue(),
			EGridGoalContentionPolicy::StopBeforeOccupied);
		const TValueOrError<UClass*, EPropertyBagResult> FilterClass =
			Asset->GetDefaultParameters().GetValueClass(GridMoveToCellActionParameters::FilterClass);
		TestTrue(TEXT("Ready-to-use action exposes its navigation filter"), FilterClass.HasValue());
		if (FilterClass.HasValue() && FilterClass.GetValue())
		{
			TestEqual(
				TEXT("Ready-to-use action uses the Balanced GridWorld filter"),
				FilterClass.GetValue()->GetPathName(),
				FString(TEXT("/GridWorldSystem/AI/BP_GridQueryFilter_Balanced.BP_GridQueryFilter_Balanced_C")));
		}
	}
	return true;
}

#endif
