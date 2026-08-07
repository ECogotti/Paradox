#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/GameplayActionComponent.h"
#include "Components/TacticalPauseActionQueueComponent.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Controllers/ParadoxPlayerController.h"
#include "AI/GridWorldPathFollowingComponent.h"
#include "Interaction/GridCellPointerComponent.h"
#include "Prediction/GridPathPreviewComponent.h"
#include "UObject/UnrealType.h"

/** Narrow friend accessor for exercising the adapter's ownership transition without a real World. */
struct FParadoxTacticalPlanningTestAccessor
{
	static void SetActionComponent(
		UTacticalPauseActionQueueComponent& PlanningComponent,
		UGameplayActionComponent* ActionComponent)
	{
		PlanningComponent.ResolvedActionComponent = ActionComponent;
	}

	static void ApplyPauseState(
		UTacticalPauseActionQueueComponent& PlanningComponent,
		const bool bPaused)
	{
		PlanningComponent.ApplyTacticalPauseState(bPaused);
	}

	static bool OwnsSchedulerPause(const UTacticalPauseActionQueueComponent& PlanningComponent)
	{
		return PlanningComponent.bSchedulerPauseOwned;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxTacticalPlanningDefaultsTest,
	"Paradox.TacticalPlanning.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxTacticalPlanningDefaultsTest::RunTest(const FString& Parameters)
{
	const AParadoxPlayerCharacter* Character = GetDefault<AParadoxPlayerCharacter>();
	TestNotNull(TEXT("Paradox Player Character has a CDO"), Character);
	if (Character)
	{
		const UGameplayActionComponent* ActionComponent = Character->GetGameplayActionComponent();
		const UTacticalPauseActionQueueComponent* PlanningComponent =
			Character->GetTacticalPauseActionQueueComponent();
		TestNotNull(TEXT("Character owns the Gameplay Actions scheduler"), ActionComponent);
		TestNotNull(TEXT("Character owns the tactical planning adapter"), PlanningComponent);
		if (PlanningComponent)
		{
			TestFalse(TEXT("Planning adapter has no per-frame tick cost"),
				PlanningComponent->PrimaryComponentTick.bCanEverTick);
			TestTrue(TEXT("Planning adapter targets the character scheduler"),
				PlanningComponent->ActionComponentOverride == ActionComponent);
		}
	}

	const AParadoxPlayerController* Controller = GetDefault<AParadoxPlayerController>();
	TestNotNull(TEXT("Paradox Player Controller has a CDO"), Controller);
	if (Controller)
	{
		TestTrue(TEXT("Player Controller tick is allowed during world pause"),
			Controller->PrimaryActorTick.bTickEvenWhenPaused);
		TestNull(
			TEXT("Controller does not duplicate the path follower's active-presentation master setting"),
			FindFProperty<FProperty>(AParadoxPlayerController::StaticClass(), TEXT("bPresentActivePath")));
		TestNull(
			TEXT("Controller does not duplicate the path follower's cell-renderer setting"),
			FindFProperty<FProperty>(AParadoxPlayerController::StaticClass(), TEXT("bPresentActivePathAsCells")));
		TestNull(
			TEXT("Controller does not duplicate the path follower's line-renderer setting"),
			FindFProperty<FProperty>(AParadoxPlayerController::StaticClass(), TEXT("bPresentActivePathAsLine")));
		TestNotNull(
			TEXT("Path follower remains the active-presentation configuration owner"),
			Controller->FindComponentByClass<UGridWorldPathFollowingComponent>());
		const UGridCellPointerComponent* Pointer =
			Controller->FindComponentByClass<UGridCellPointerComponent>();
		if (TestNotNull(TEXT("Controller owns the GridWorld cell pointer"), Pointer))
		{
			TestFalse(
				TEXT("Paradox pointer does not add an independent cell-hover visualization"),
				Pointer->bApplyHoverToVisualization);
		}

		const FBoolProperty* FullPauseTickProperty = FindFProperty<FBoolProperty>(
			APlayerController::StaticClass(),
			TEXT("bShouldPerformFullTickWhenPaused"));
		TestNotNull(TEXT("UE exposes the full pause-tick property"), FullPauseTickProperty);
		if (FullPauseTickProperty)
		{
			TestTrue(TEXT("Player Controller performs a full tick while paused"),
				FullPauseTickProperty->GetPropertyValue_InContainer(Controller));
		}
		const FEnumProperty* GoalContentionProperty = FindFProperty<FEnumProperty>(
			AParadoxPlayerController::StaticClass(),
			TEXT("MoveGoalContentionPolicy"));
		TestNotNull(TEXT("Player movement exposes exact-goal contention policy"), GoalContentionProperty);
		if (GoalContentionProperty)
		{
			const void* ValueAddress = GoalContentionProperty->ContainerPtrToValuePtr<void>(Controller);
			TestEqual(
				TEXT("Paradox player movement stops before occupied goals by default"),
				GoalContentionProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValueAddress),
				static_cast<int64>(EGridGoalContentionPolicy::StopBeforeOccupied));
		}

		UClass* ProjectControllerClass = LoadObject<UClass>(
			nullptr,
			TEXT("/Game/Characters/Astronaut/Blueprints/BP_PlayerController.BP_PlayerController_C"));
		const AParadoxPlayerController* ProjectControllerDefaults = ProjectControllerClass != nullptr
			? Cast<AParadoxPlayerController>(ProjectControllerClass->GetDefaultObject())
			: nullptr;
		if (TestNotNull(TEXT("Project Player Controller defaults load"), ProjectControllerDefaults)
			&& GoalContentionProperty != nullptr)
		{
			const void* BlueprintValueAddress =
				GoalContentionProperty->ContainerPtrToValuePtr<void>(ProjectControllerDefaults);
			TestEqual(
				TEXT("Project Player Controller inherits Stop Before Occupied"),
				GoalContentionProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(BlueprintValueAddress),
				static_cast<int64>(EGridGoalContentionPolicy::StopBeforeOccupied));

			const UGridPathPreviewComponent* Preview =
				ProjectControllerDefaults->FindComponentByClass<UGridPathPreviewComponent>();
			if (TestNotNull(TEXT("Project Controller owns the path preview component"), Preview))
			{
				TestTrue(TEXT("Project preview presents automatically"), Preview->bAutoPresentPreview);
				TestFalse(TEXT("Project preview cell overlay is disabled"), Preview->bRenderCellOverlay);
				TestTrue(TEXT("Project preview strict line is enabled"), Preview->bRenderLine);
			}

			const UGridCellPointerComponent* ProjectPointer =
				ProjectControllerDefaults->FindComponentByClass<UGridCellPointerComponent>();
			if (TestNotNull(TEXT("Project Controller owns the cell pointer"), ProjectPointer))
			{
				TestFalse(
					TEXT("Project pointer inherits disabled cell-hover visualization"),
					ProjectPointer->bApplyHoverToVisualization);
			}

			const UGridWorldPathFollowingComponent* PathFollower =
				ProjectControllerDefaults->FindComponentByClass<UGridWorldPathFollowingComponent>();
			if (TestNotNull(TEXT("Project Controller owns the GridWorld path follower"), PathFollower))
			{
				TestTrue(TEXT("Project active-path presentation is enabled"), PathFollower->bPresentActivePath);
				TestFalse(
					TEXT("Project active-path cell overlay is disabled"),
					PathFollower->bPresentActivePathAsCellOverlay);
				TestTrue(
					TEXT("Project active-path strict line is enabled"),
					PathFollower->bPresentActivePathAsLine);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxTacticalPlanningPauseOwnershipTest,
	"Paradox.TacticalPlanning.SchedulerPauseOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxTacticalPlanningPauseOwnershipTest::RunTest(const FString& Parameters)
{
	UGameplayActionComponent* ActionComponent = NewObject<UGameplayActionComponent>();
	UTacticalPauseActionQueueComponent* PlanningComponent =
		NewObject<UTacticalPauseActionQueueComponent>();
	FParadoxTacticalPlanningTestAccessor::SetActionComponent(*PlanningComponent, ActionComponent);

	FParadoxTacticalPlanningTestAccessor::ApplyPauseState(*PlanningComponent, true);
	TestTrue(TEXT("Adapter pauses an unpaused scheduler"), ActionComponent->IsActionsPaused());
	TestTrue(TEXT("Adapter records ownership of its scheduler pause"),
		FParadoxTacticalPlanningTestAccessor::OwnsSchedulerPause(*PlanningComponent));
	FParadoxTacticalPlanningTestAccessor::ApplyPauseState(*PlanningComponent, true);
	TestTrue(TEXT("Repeated pause notification preserves scheduler ownership"),
		FParadoxTacticalPlanningTestAccessor::OwnsSchedulerPause(*PlanningComponent));

	FParadoxTacticalPlanningTestAccessor::ApplyPauseState(*PlanningComponent, false);
	TestFalse(TEXT("Adapter resumes the scheduler pause it owns"), ActionComponent->IsActionsPaused());
	TestFalse(TEXT("Ownership is released after resume"),
		FParadoxTacticalPlanningTestAccessor::OwnsSchedulerPause(*PlanningComponent));

	TestEqual(TEXT("External scheduler pause succeeds"),
		ActionComponent->PauseActions(),
		EGameplayActionOperationResult::Succeeded);
	FParadoxTacticalPlanningTestAccessor::ApplyPauseState(*PlanningComponent, true);
	TestFalse(TEXT("Adapter does not claim an existing external pause"),
		FParadoxTacticalPlanningTestAccessor::OwnsSchedulerPause(*PlanningComponent));

	FParadoxTacticalPlanningTestAccessor::ApplyPauseState(*PlanningComponent, false);
	TestTrue(TEXT("Adapter preserves an externally owned scheduler pause"),
		ActionComponent->IsActionsPaused());
	TestEqual(TEXT("Test cleanup resumes the external pause"),
		ActionComponent->ResumeActions(),
		EGameplayActionOperationResult::Succeeded);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
