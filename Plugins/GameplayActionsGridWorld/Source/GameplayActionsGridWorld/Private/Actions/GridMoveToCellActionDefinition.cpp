#include "Actions/GridMoveToCellActionDefinition.h"

#include "AITypes.h"
#include "Actions/GridMoveToCellAction.h"
#include "GameplayActionTags.h"
#include "GameplayActionsGridWorldTags.h"
#include "GridWorldTypes.h"
#include "Navigation/GridPathInjectionTypes.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "StructUtils/PropertyBag.h"

namespace GridMoveToCellActionParameters
{
	const FName PathSource(TEXT("PathSource"));
	const FName InjectedPath(TEXT("InjectedPath"));
	const FName GoalLocation(TEXT("GoalLocation"));
	const FName GoalActor(TEXT("GoalActor"));
	const FName AcceptanceRadius(TEXT("AcceptanceRadius"));
	const FName StopOnOverlap(TEXT("StopOnOverlap"));
	const FName AcceptPartialPath(TEXT("AcceptPartialPath"));
	const FName UsePathfinding(TEXT("UsePathfinding"));
	const FName LockAILogic(TEXT("LockAILogic"));
	const FName TrackMovingGoal(TEXT("TrackMovingGoal"));
	const FName RequireNavigableEndLocation(TEXT("RequireNavigableEndLocation"));
	const FName FilterClass(TEXT("FilterClass"));
	const FName AllowStrafe(TEXT("AllowStrafe"));
	const FName GoalContentionPolicy(TEXT("GoalContentionPolicy"));
	const FName MaxAlternativeSearchRadius(TEXT("MaxAlternativeSearchRadius"));
	const FName AdditionalGoalSeparation(TEXT("AdditionalGoalSeparation"));
	const FName AutoRegisterPawnOccupancy(TEXT("AutoRegisterPawnOccupancy"));
	const FName GoalAvailabilityTimeout(TEXT("GoalAvailabilityTimeout"));
	const FName GoalWaitWarningInterval(TEXT("GoalWaitWarningInterval"));
}

UGridMoveToCellActionDefinition::UGridMoveToCellActionDefinition()
{
	InstanceClass = UGridMoveToCellAction::StaticClass();
	ActionTag = GameplayActionsGridWorldTags::Action_MoveToGridCell;
	ExecutionLocks.AddTag(GameplayActionTags::Lock_Movement);
	BlockedPolicy = EGameplayActionBlockedPolicy::Queue;
	JournalRequirement = EGameplayActionJournalRequirement::Optional;
	DebugDescription =
		TEXT("Moves an AI pawn to the navigable center of a GridWorld cell.");

	const TArray<FPropertyBagPropertyDesc> Descriptors = {
		{ GridMoveToCellActionParameters::PathSource,
			EPropertyBagPropertyType::Enum,
			StaticEnum<EGridMovePathSource>() },
		{ GridMoveToCellActionParameters::InjectedPath,
			EPropertyBagPropertyType::Struct,
			FGridInjectedPath::StaticStruct() },
		{ GridMoveToCellActionParameters::GoalLocation,
			EPropertyBagPropertyType::Struct,
			TBaseStructure<FVector>::Get() },
		{ GridMoveToCellActionParameters::GoalActor,
			EPropertyBagPropertyType::Object,
			AActor::StaticClass() },
		{ GridMoveToCellActionParameters::AcceptanceRadius,
			EPropertyBagPropertyType::Float },
		{ GridMoveToCellActionParameters::StopOnOverlap,
			EPropertyBagPropertyType::Enum,
			StaticEnum<EAIOptionFlag::Type>() },
		{ GridMoveToCellActionParameters::AcceptPartialPath,
			EPropertyBagPropertyType::Enum,
			StaticEnum<EAIOptionFlag::Type>() },
		{ GridMoveToCellActionParameters::UsePathfinding,
			EPropertyBagPropertyType::Bool },
		{ GridMoveToCellActionParameters::LockAILogic,
			EPropertyBagPropertyType::Bool },
		{ GridMoveToCellActionParameters::TrackMovingGoal,
			EPropertyBagPropertyType::Bool },
		{ GridMoveToCellActionParameters::RequireNavigableEndLocation,
			EPropertyBagPropertyType::Enum,
			StaticEnum<EAIOptionFlag::Type>() },
		{ GridMoveToCellActionParameters::FilterClass,
			EPropertyBagPropertyType::Class,
			UNavigationQueryFilter::StaticClass() },
		{ GridMoveToCellActionParameters::AllowStrafe,
			EPropertyBagPropertyType::Bool },
		{ GridMoveToCellActionParameters::GoalContentionPolicy,
			EPropertyBagPropertyType::Enum,
			StaticEnum<EGridGoalContentionPolicy>() },
		{ GridMoveToCellActionParameters::MaxAlternativeSearchRadius,
			EPropertyBagPropertyType::Int32 },
		{ GridMoveToCellActionParameters::AdditionalGoalSeparation,
			EPropertyBagPropertyType::Float },
		{ GridMoveToCellActionParameters::AutoRegisterPawnOccupancy,
			EPropertyBagPropertyType::Bool },
		{ GridMoveToCellActionParameters::GoalAvailabilityTimeout,
			EPropertyBagPropertyType::Float },
		{ GridMoveToCellActionParameters::GoalWaitWarningInterval,
			EPropertyBagPropertyType::Float }
	};
	DefaultParameters.InitializeFromBagStruct(UPropertyBag::GetOrCreateFromDescs(Descriptors));
	DefaultParameters.SetValueEnum(
		GridMoveToCellActionParameters::PathSource,
		EGridMovePathSource::Destination);
	DefaultParameters.SetValueStruct(
		GridMoveToCellActionParameters::InjectedPath,
		FGridInjectedPath());

	DefaultParameters.SetValueStruct(
		GridMoveToCellActionParameters::GoalLocation,
		FVector::ZeroVector);
	DefaultParameters.SetValueObject(
		GridMoveToCellActionParameters::GoalActor,
		nullptr);
	DefaultParameters.SetValueFloat(
		GridMoveToCellActionParameters::AcceptanceRadius,
		-1.0f);
	DefaultParameters.SetValueEnum(
		GridMoveToCellActionParameters::StopOnOverlap,
		EAIOptionFlag::Default);
	DefaultParameters.SetValueEnum(
		GridMoveToCellActionParameters::AcceptPartialPath,
		EAIOptionFlag::Default);
	DefaultParameters.SetValueBool(
		GridMoveToCellActionParameters::UsePathfinding,
		true);
	// The action scheduler already serializes movement. Locking the BT/StateTree AI logic here would
	// deadlock the node that is waiting for this action's Ended event, so false is the safe default.
	DefaultParameters.SetValueBool(
		GridMoveToCellActionParameters::LockAILogic,
		false);
	DefaultParameters.SetValueBool(
		GridMoveToCellActionParameters::TrackMovingGoal,
		true);
	DefaultParameters.SetValueEnum(
		GridMoveToCellActionParameters::RequireNavigableEndLocation,
		EAIOptionFlag::Default);
	DefaultParameters.SetValueClass(
		GridMoveToCellActionParameters::FilterClass,
		nullptr);
	DefaultParameters.SetValueBool(
		GridMoveToCellActionParameters::AllowStrafe,
		false);
	DefaultParameters.SetValueEnum(
		GridMoveToCellActionParameters::GoalContentionPolicy,
		EGridGoalContentionPolicy::StopBeforeOccupied);
	DefaultParameters.SetValueInt32(
		GridMoveToCellActionParameters::MaxAlternativeSearchRadius,
		3);
	DefaultParameters.SetValueFloat(
		GridMoveToCellActionParameters::AdditionalGoalSeparation,
		5.0f);
	DefaultParameters.SetValueBool(
		GridMoveToCellActionParameters::AutoRegisterPawnOccupancy,
		true);
	DefaultParameters.SetValueFloat(
		GridMoveToCellActionParameters::GoalAvailabilityTimeout,
		5.0f);
	DefaultParameters.SetValueFloat(
		GridMoveToCellActionParameters::GoalWaitWarningInterval,
		1.0f);
}

void UGridMoveToCellActionDefinition::PostLoad()
{
	Super::PostLoad();
	const UGridMoveToCellActionDefinition* NativeDefaults = GetDefault<UGridMoveToCellActionDefinition>();
	if (this != NativeDefaults && NativeDefaults != nullptr
		&& !DefaultParameters.HasSameLayout(NativeDefaults->DefaultParameters))
	{
		// Existing assets predate PathSource/InjectedPath. Migrate by property identity so authored
		// values survive while the new exact-path fields receive the safe native defaults.
		DefaultParameters.MigrateToNewBagInstance(NativeDefaults->DefaultParameters);
	}

	// This plugin-owned ready-to-use asset previously serialized Reject Occupied. Migrate only that
	// known content default; authored project assets keep any explicit policy selected by their owner.
	static const FString ReadyToUseAssetPath =
		TEXT("/GameplayActionsGridWorld/Definitions/DA_GameplayAction_MoveToGridCell.DA_GameplayAction_MoveToGridCell");
	const TValueOrError<EGridGoalContentionPolicy, EPropertyBagResult> SavedPolicy =
		DefaultParameters.GetValueEnum<EGridGoalContentionPolicy>(
			GridMoveToCellActionParameters::GoalContentionPolicy);
	if (GetPathName() == ReadyToUseAssetPath
		&& SavedPolicy.HasValue()
		&& SavedPolicy.GetValue() == EGridGoalContentionPolicy::RejectOccupied)
	{
		DefaultParameters.SetValueEnum(
			GridMoveToCellActionParameters::GoalContentionPolicy,
			EGridGoalContentionPolicy::StopBeforeOccupied);
		MarkPackageDirty();
	}
}
