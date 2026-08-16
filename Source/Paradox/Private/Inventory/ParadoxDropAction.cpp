#include "Inventory/ParadoxDropAction.h"

#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Characters/ParadoxCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Execution/GridMoveToCellExecution.h"
#include "GameFramework/Controller.h"
#include "GameplayActionTags.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridWorldSnapshot.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Paradox.h"
#include "StructUtils/PropertyBag.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace ParadoxDropActionParameters
{
	const FName TargetCell = GET_MEMBER_NAME_CHECKED(FParadoxDropActionParameters, TargetCell);
	const FName PathSource = GET_MEMBER_NAME_CHECKED(FParadoxDropActionParameters, PathSource);
	const FName InjectedPath = GET_MEMBER_NAME_CHECKED(FParadoxDropActionParameters, InjectedPath);
	const FName NavigationFilter = GET_MEMBER_NAME_CHECKED(FParadoxDropActionParameters, NavigationFilter);
	const FName AcceptanceRadius = GET_MEMBER_NAME_CHECKED(FParadoxDropActionParameters, AcceptanceRadius);
	const FName AllowStrafe = GET_MEMBER_NAME_CHECKED(FParadoxDropActionParameters, bAllowStrafe);
}

namespace UE::Paradox::Drop::Private
{
	bool HasAnyOwner(const TArray<FGuid, TInlineAllocator<2>>& Owners)
	{
		return Owners.ContainsByPredicate([](const FGuid& Id) { return Id.IsValid(); });
	}
}

bool UParadoxDropAction::ValidateDropCell(
	AParadoxCharacter* Character,
	const FGridCellId& TargetCell,
	FVector& OutTargetWorldCenter,
	FString& OutDiagnostic)
{
	OutTargetWorldCenter = FVector::ZeroVector;
	if (!IsValid(Character) || !Character->GetController()
		|| !Character->GetInventoryComponent())
	{
		OutDiagnostic = TEXT("Drop requires a valid controlled Paradox Character with inventory.");
		return false;
	}
	if (!Character->GetInventoryComponent()->HasItem())
	{
		OutDiagnostic = TEXT("Drop requires an equipped pickupable.");
		return false;
	}
	if (!TargetCell.IsValid())
	{
		OutDiagnostic = TEXT("Drop requires a valid semantic GridWorld cell.");
		return false;
	}

	UGridWorldSubsystem* GridWorld = Character->GetWorld()
		? Character->GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	AGridNavigationData* NavigationData = GridWorld ? GridWorld->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavigationData ? NavigationData->GetSnapshot() : nullptr;
	const FGridCellData* TargetData = Snapshot.IsValid() ? Snapshot->FindCell(TargetCell) : nullptr;
	if (!TargetData)
	{
		OutDiagnostic = TEXT("The semantic Drop cell is not present in the current GridWorld snapshot.");
		return false;
	}

	const UGridNavigationOccupancyComponent* CharacterOccupancy =
		UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(*Character);
	const FGuid CharacterOccupancyId = CharacterOccupancy
		? CharacterOccupancy->OccupantId
		: FGuid();
	if (!TargetData->bWalkable || TargetData->bOccupied
		|| UE::Paradox::Drop::Private::HasAnyOwner(TargetData->ReservationOwners)
		|| NavigationData->IsTrafficGoalClaimedByOther(
			TargetCell,
			Character,
			CharacterOccupancyId))
	{
		OutDiagnostic = TEXT("The semantic Drop cell is blocked, occupied, reserved or traffic-claimed.");
		return false;
	}

	OutTargetWorldCenter = TargetData->WorldCenter;
	OutDiagnostic = TEXT("The semantic Drop cell is currently available.");
	return true;
}

bool UParadoxDropAction::ValidateApproachPath(
	AParadoxCharacter* Character,
	const FGridCellId& TargetCell,
	const FGridInjectedPath& InjectedPath,
	FString& OutDiagnostic)
{
	if (!IsValid(Character) || !Character->GetController() || !InjectedPath.IsSet()
		|| InjectedPath.bIsPartial || InjectedPath.Cells.IsEmpty()
		|| InjectedPath.Cells.Last() != InjectedPath.OriginalGoalCell
		|| InjectedPath.RequestedGoalCell != InjectedPath.OriginalGoalCell)
	{
		OutDiagnostic = TEXT("Drop requires a complete exact path ending at its recorded approach cell.");
		return false;
	}

	UGridWorldSubsystem* GridWorld = Character->GetWorld()
		? Character->GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	AGridNavigationData* NavigationData = GridWorld ? GridWorld->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavigationData ? NavigationData->GetSnapshot() : nullptr;
	const FGridCellData* TargetData = Snapshot.IsValid() ? Snapshot->FindCell(TargetCell) : nullptr;
	const int32* ApproachIndex = Snapshot.IsValid()
		? Snapshot->CellIndexById.Find(InjectedPath.OriginalGoalCell)
		: nullptr;
	if (!TargetData || ApproachIndex == nullptr
		|| !TargetData->Neighbors.Contains(*ApproachIndex))
	{
		OutDiagnostic = TEXT("The injected Drop path does not end on an ordinary neighbor of the semantic target cell.");
		return false;
	}

	OutDiagnostic = TEXT("The exact Drop approach path is structurally valid.");
	return true;
}

bool UParadoxDropAction::CanStartAction_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	if (!Super::CanStartAction_Implementation(OutFailureReason, OutDiagnostic))
	{
		return false;
	}

	FParadoxDropActionParameters LocalParameters;
	if (!ReadParameters(LocalParameters, OutDiagnostic))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest;
		return false;
	}
	AParadoxCharacter* Character = GetCharacter();
	if (!Character || !Character->GetInventoryComponent()
		|| !Character->GetInventoryComponent()->HasItem())
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_SlotEmpty;
		OutDiagnostic = TEXT("Drop requires the submitting Character to hold a pickupable.");
		return false;
	}

	FVector TargetWorldCenter;
	if (!ValidateDropCell(Character, LocalParameters.TargetCell, TargetWorldCenter, OutDiagnostic))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_InvalidCell;
		return false;
	}
	if (!ValidateApproachPath(
		Character,
		LocalParameters.TargetCell,
		LocalParameters.InjectedPath,
		OutDiagnostic))
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest;
		return false;
	}
	return true;
}

void UParadoxDropAction::OnActionInit_Implementation()
{
	Super::OnActionInit_Implementation();
	FString Diagnostic;
	ReadParameters(SemanticParameters, Diagnostic);
	AParadoxCharacter* Character = GetCharacter();
	CapturedItem = Character && Character->GetInventoryComponent()
		? Character->GetInventoryComponent()->GetEquippedItem()
		: nullptr;
}

void UParadoxDropAction::OnActionStarted_Implementation()
{
	Super::OnActionStarted_Implementation();
	AParadoxCharacter* Character = GetCharacter();
	if (!Character || !Character->GetInventoryComponent()
		|| Character->GetInventoryComponent()->GetEquippedItem() != CapturedItem
		|| !IsValid(CapturedItem))
	{
		bCompletionRequested = true;
		FailAction(
			ParadoxGameplayTags::Result_Failure_Inventory_OwnershipConflict,
			TEXT("The equipped pickupable changed before Drop execution started."));
		return;
	}

	FVector TargetWorldCenter;
	FString Diagnostic;
	if (!ValidateDropCell(
		Character,
		SemanticParameters.TargetCell,
		TargetWorldCenter,
		Diagnostic)
		|| !ValidateApproachPath(
			Character,
			SemanticParameters.TargetCell,
			SemanticParameters.InjectedPath,
			Diagnostic))
	{
		LogDebugState(TEXT("StartRejected"), Diagnostic);
		bCompletionRequested = true;
		FailAction(ParadoxGameplayTags::Result_Failure_Inventory_TargetInvalidated, Diagnostic);
		return;
	}

	UGridWorldSubsystem* GridWorld = GetWorld()
		? GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	const FGridCellQueryResult CurrentCell = GridWorld
		? GridWorld->ProjectPoint(Character->GetController()->GetNavAgentLocation())
		: FGridCellQueryResult();
	if (CurrentCell.Status == EGridQueryStatus::Success
		&& CurrentCell.CellId == SemanticParameters.InjectedPath.OriginalGoalCell)
	{
		LogDebugState(TEXT("AlreadyAdjacent"));
		PerformDrop(TargetWorldCenter);
		return;
	}

	FGridMoveToCellExecutionRequest Request;
	Request.Controller = Character->GetController();
	Request.PathSource = EGridMovePathSource::ExactInjectedPath;
	Request.InjectedPath = SemanticParameters.InjectedPath;
	Request.AcceptanceRadius = SemanticParameters.AcceptanceRadius;
	Request.AcceptPartialPath = EAIOptionFlag::Disable;
	Request.RequireNavigableEndLocation = EAIOptionFlag::Enable;
	Request.FilterClass = SemanticParameters.InjectedPath.FilterClass;
	Request.bAllowStrafe = SemanticParameters.bAllowStrafe;
	Request.bTrackMovingGoal = false;
	Request.GoalContentionPolicy = EGridGoalContentionPolicy::RejectOccupied;
	MovementExecution = NewObject<UGridMoveToCellExecution>(this);
	const uint32 MovementGeneration = ++OperationGeneration;
	MovementFinishedHandle = MovementExecution->OnFinishedNative().AddWeakLambda(
		this,
		[this, MovementGeneration](const FGridMoveToCellExecutionResult& Result)
		{
			if (MovementGeneration == OperationGeneration)
			{
				HandleMovementFinished(Result);
			}
		});
	FString StartDiagnostic;
	if (!MovementExecution->Start(Request, StartDiagnostic))
	{
		ReleaseMovement(false);
		bCompletionRequested = true;
		FailAction(
			ParadoxGameplayTags::Result_Failure_Inventory_NoReachableExecutionCell,
			StartDiagnostic);
		return;
	}
	LogDebugState(TEXT("MovementStarted"));
}

void UParadoxDropAction::OnActionPaused_Implementation()
{
	if (MovementExecution)
	{
		MovementExecution->Pause();
	}
	Super::OnActionPaused_Implementation();
}

void UParadoxDropAction::OnActionResumed_Implementation()
{
	Super::OnActionResumed_Implementation();
	if (MovementExecution)
	{
		MovementExecution->Resume();
	}
}

void UParadoxDropAction::OnActionCancelled_Implementation(FGameplayTag ReasonTag)
{
	ReleaseMovement(true);
	Super::OnActionCancelled_Implementation(ReasonTag);
}

void UParadoxDropAction::OnActionInterrupted_Implementation(FGameplayTag ReasonTag)
{
	ReleaseMovement(true);
	Super::OnActionInterrupted_Implementation(ReasonTag);
}

void UParadoxDropAction::OnActionAborted_Implementation(FGameplayTag ReasonTag)
{
	ReleaseMovement(true);
	Super::OnActionAborted_Implementation(ReasonTag);
}

void UParadoxDropAction::OnActionCleanup_Implementation()
{
	ReleaseMovement(true);
	CapturedItem = nullptr;
	Super::OnActionCleanup_Implementation();
}

bool UParadoxDropAction::ReadParameters(
	FParadoxDropActionParameters& OutParameters,
	FString& OutDiagnostic) const
{
	const FInstancedPropertyBag& Bag = GetParameters();
	auto Read = [&Bag, &OutParameters](const FName Name)
	{
		const FProperty* Property =
			FParadoxDropActionParameters::StaticStruct()->FindPropertyByName(Name);
		return UGameplayActionBlueprintLibrary::GetBagValueToProperty(
			Bag,
			Name,
			Property,
			Property ? Property->ContainerPtrToValuePtr<void>(&OutParameters) : nullptr)
			== EGameplayActionParameterAccessResult::Success;
	};
	if (!Read(ParadoxDropActionParameters::TargetCell)
		|| !Read(ParadoxDropActionParameters::PathSource)
		|| !Read(ParadoxDropActionParameters::InjectedPath)
		|| !Read(ParadoxDropActionParameters::NavigationFilter)
		|| !Read(ParadoxDropActionParameters::AcceptanceRadius)
		|| !Read(ParadoxDropActionParameters::AllowStrafe)
		|| !OutParameters.TargetCell.IsValid()
		|| OutParameters.PathSource != EGridMovePathSource::ExactInjectedPath
		|| !OutParameters.InjectedPath.IsSet())
	{
		OutDiagnostic = TEXT("Drop requires TargetCell, ExactInjectedPath and the complete native movement schema.");
		return false;
	}
	return true;
}

AParadoxCharacter* UParadoxDropAction::GetCharacter() const
{
	return GetOwningComponent()
		? Cast<AParadoxCharacter>(GetOwningComponent()->GetOwner())
		: nullptr;
}

void UParadoxDropAction::PerformDrop(const FVector& TargetWorldCenter)
{
	AParadoxCharacter* Character = GetCharacter();
	if (!Character || !Character->GetInventoryComponent()
		|| Character->GetInventoryComponent()->GetEquippedItem() != CapturedItem
		|| !IsValid(CapturedItem))
	{
		bCompletionRequested = true;
		FailAction(
			ParadoxGameplayTags::Result_Failure_Inventory_OwnershipConflict,
			TEXT("The equipped pickupable changed before final Drop placement."));
		return;
	}

	const FTransform DropTransform =
		CapturedItem->GetDropPlacementTransform(TargetWorldCenter);
	const FParadoxInventoryOperationResult DropResult =
		Character->GetInventoryComponent()->TryDropAtTransform(DropTransform);
	bCompletionRequested = true;
	if (DropResult.IsSuccess())
	{
		SucceedAction(GameplayActionTags::Result_Success, DropResult.DiagnosticMessage);
	}
	else
	{
		FailAction(
			ParadoxGameplayTags::Result_Failure_Inventory_TargetInvalidated,
			DropResult.DiagnosticMessage);
	}
}

void UParadoxDropAction::HandleMovementFinished(
	const FGridMoveToCellExecutionResult& Result)
{
	ReleaseMovement(false);
	if (bCompletionRequested)
	{
		return;
	}
	if (!Result.IsSuccess())
	{
		bCompletionRequested = true;
		FailAction(
			ParadoxGameplayTags::Result_Failure_Inventory_NoReachableExecutionCell,
			Result.DiagnosticMessage);
		return;
	}

	AParadoxCharacter* Character = GetCharacter();
	FVector TargetWorldCenter;
	FString Diagnostic;
	if (!ValidateDropCell(
		Character,
		SemanticParameters.TargetCell,
		TargetWorldCenter,
		Diagnostic)
		|| !ValidateApproachPath(
			Character,
			SemanticParameters.TargetCell,
			SemanticParameters.InjectedPath,
			Diagnostic))
	{
		bCompletionRequested = true;
		FailAction(
			ParadoxGameplayTags::Result_Failure_Inventory_TargetInvalidated,
			TEXT("The originally selected Drop cell or approach was invalidated; no retarget was attempted."));
		return;
	}

	UGridWorldSubsystem* GridWorld = GetWorld()
		? GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	const FGridCellQueryResult CurrentCell = GridWorld && Character && Character->GetController()
		? GridWorld->ProjectPoint(Character->GetController()->GetNavAgentLocation())
		: FGridCellQueryResult();
	if (CurrentCell.Status != EGridQueryStatus::Success
		|| CurrentCell.CellId != SemanticParameters.InjectedPath.OriginalGoalCell)
	{
		bCompletionRequested = true;
		FailAction(
			ParadoxGameplayTags::Result_Failure_Inventory_TargetInvalidated,
			TEXT("Drop movement did not finish on the recorded ordinary approach cell; no retarget was attempted."));
		return;
	}

	LogDebugState(TEXT("ArrivalValidated"));
	PerformDrop(TargetWorldCenter);
}

void UParadoxDropAction::ReleaseMovement(const bool bCancel)
{
	++OperationGeneration;
	if (!MovementExecution)
	{
		return;
	}
	MovementExecution->OnFinishedNative().Remove(MovementFinishedHandle);
	MovementFinishedHandle.Reset();
	if (bCancel && MovementExecution->IsRunning())
	{
		MovementExecution->Cancel();
	}
	MovementExecution = nullptr;
}

void UParadoxDropAction::LogDebugState(
	const TCHAR* EventName,
	const FString& Diagnostic) const
{
	if (!bEnableDebug || !IsParadoxInventoryDebugEnabled())
	{
		return;
	}
	const FGridCellId ApproachCell = SemanticParameters.InjectedPath.OriginalGoalCell;
	PARADOX_LOG_INFO(
		TEXT("Drop event=%s character=%s item=%s target=(%d,%d,%d) approach=(%d,%d,%d) exact_cells=%d diagnostic=%s"),
		EventName,
		*GetNameSafe(GetCharacter()),
		*GetNameSafe(CapturedItem.Get()),
		SemanticParameters.TargetCell.Coord.X,
		SemanticParameters.TargetCell.Coord.Y,
		SemanticParameters.TargetCell.Coord.Layer,
		ApproachCell.Coord.X,
		ApproachCell.Coord.Y,
		ApproachCell.Coord.Layer,
		SemanticParameters.InjectedPath.Cells.Num(),
		*Diagnostic);
}

UParadoxDropActionDefinition::UParadoxDropActionDefinition()
{
	InstanceClass = UParadoxDropAction::StaticClass();
	ActionTag = ParadoxGameplayTags::Action_Inventory_Drop;
	ExecutionLocks.AddTag(GameplayActionTags::Lock_Movement);
	ExecutionLocks.AddTag(ParadoxGameplayTags::Lock_Inventory);
	ExecutionLocks.AddTag(ParadoxGameplayTags::Lock_Interaction);
	BlockedPolicy = EGameplayActionBlockedPolicy::Reject;
	bInterruptible = true;
	JournalRequirement = EGameplayActionJournalRequirement::Optional;
	DebugDescription = TEXT("Drops the equipped pickupable into one exact GridWorld cell after following the recorded approach path.");
	const TArray<FPropertyBagPropertyDesc> Descriptors = {
		{ParadoxDropActionParameters::TargetCell, EPropertyBagPropertyType::Struct, FGridCellId::StaticStruct()},
		{ParadoxDropActionParameters::PathSource, EPropertyBagPropertyType::Enum, StaticEnum<EGridMovePathSource>()},
		{ParadoxDropActionParameters::InjectedPath, EPropertyBagPropertyType::Struct, FGridInjectedPath::StaticStruct()},
		{ParadoxDropActionParameters::NavigationFilter, EPropertyBagPropertyType::Class, UNavigationQueryFilter::StaticClass()},
		{ParadoxDropActionParameters::AcceptanceRadius, EPropertyBagPropertyType::Float},
		{ParadoxDropActionParameters::AllowStrafe, EPropertyBagPropertyType::Bool}
	};
	DefaultParameters.InitializeFromBagStruct(UPropertyBag::GetOrCreateFromDescs(Descriptors));
	DefaultParameters.SetValueEnum(
		ParadoxDropActionParameters::PathSource,
		EGridMovePathSource::ExactInjectedPath);
	DefaultParameters.SetValueStruct(
		ParadoxDropActionParameters::InjectedPath,
		FGridInjectedPath());
	DefaultParameters.SetValueClass(ParadoxDropActionParameters::NavigationFilter, nullptr);
	DefaultParameters.SetValueFloat(ParadoxDropActionParameters::AcceptanceRadius, -1.0f);
	DefaultParameters.SetValueBool(ParadoxDropActionParameters::AllowStrafe, false);
}

void UParadoxDropActionDefinition::PostLoad()
{
	Super::PostLoad();
	const UGameplayActionDefinition* NativeDefaults =
		GetClass()->GetDefaultObject<UGameplayActionDefinition>();
	if (this != NativeDefaults && NativeDefaults
		&& !DefaultParameters.HasSameLayout(NativeDefaults->DefaultParameters))
	{
		DefaultParameters.MigrateToNewBagInstance(NativeDefaults->DefaultParameters);
	}
}

#if WITH_EDITOR
EDataValidationResult UParadoxDropActionDefinition::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	const auto Require = [this, &Context, &Result](
		const FName Name,
		const EPropertyBagPropertyType Type,
		const UObject* TypeObject)
	{
		const FPropertyBagPropertyDesc* Desc = DefaultParameters.FindPropertyDescByName(Name);
		if (!Desc || Desc->ValueType != Type || Desc->ValueTypeObject != TypeObject)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Drop Definition parameter '%s' is missing or has the wrong type."),
				*Name.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	};
	Require(ParadoxDropActionParameters::TargetCell, EPropertyBagPropertyType::Struct, FGridCellId::StaticStruct());
	Require(ParadoxDropActionParameters::PathSource, EPropertyBagPropertyType::Enum, StaticEnum<EGridMovePathSource>());
	Require(ParadoxDropActionParameters::InjectedPath, EPropertyBagPropertyType::Struct, FGridInjectedPath::StaticStruct());
	Require(ParadoxDropActionParameters::NavigationFilter, EPropertyBagPropertyType::Class, UNavigationQueryFilter::StaticClass());
	Require(ParadoxDropActionParameters::AcceptanceRadius, EPropertyBagPropertyType::Float, nullptr);
	Require(ParadoxDropActionParameters::AllowStrafe, EPropertyBagPropertyType::Bool, nullptr);
	if (!ExecutionLocks.HasTagExact(GameplayActionTags::Lock_Movement)
		|| !ExecutionLocks.HasTagExact(ParadoxGameplayTags::Lock_Inventory)
		|| BlockedPolicy != EGameplayActionBlockedPolicy::Reject
		|| JournalRequirement == EGameplayActionJournalRequirement::Disabled)
	{
		Context.AddError(FText::FromString(TEXT("Drop Definitions require Movement/Inventory locks, Reject policy and journaling.")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
