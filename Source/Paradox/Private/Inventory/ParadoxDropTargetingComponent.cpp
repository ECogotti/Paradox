#include "Inventory/ParadoxDropTargetingComponent.h"

#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Characters/ParadoxCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TacticalPauseActionQueueComponent.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Inventory/ParadoxDropAction.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "Interaction/GridCellPointerComponent.h"
#include "Materials/MaterialInterface.h"
#include "Paradox.h"
#include "Subsystems/TacticalPauseWorldSubsystem.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

UParadoxDropTargetingComponent::UParadoxDropTargetingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UParadoxDropTargetingComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>()
		: nullptr)
	{
		WorldState->OnRestoreStartedNative().AddUObject(
			this,
			&ThisClass::HandleWorldStateRestoreStarted);
	}
}

void UParadoxDropTargetingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndTargeting();
	DestroyDropPreview();
	if (UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>()
		: nullptr)
	{
		WorldState->OnRestoreStartedNative().RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

FParadoxDropTargetingResult UParadoxDropTargetingComponent::BeginDropTargeting(
	AParadoxCharacter* SourceCharacter)
{
	if (bTargetingActive)
	{
		return MakeResult(
			EParadoxDropTargetingStatus::AlreadyActive,
			TEXT("Drop targeting is already active."));
	}
	AParadoxPlayerController* Controller = GetParadoxController();
	if (!Controller || !Controller->IsLocalPlayerController()
		|| !Controller->GetGridPathPreviewComponent())
	{
		return MakeResult(
			EParadoxDropTargetingStatus::InvalidController,
			TEXT("Drop targeting requires the owning local Paradox Player Controller and its shared Grid path preview."));
	}
	if (!IsValid(SourceCharacter) || SourceCharacter->GetWorld() != GetWorld()
		|| !SourceCharacter->GetController() || !SourceCharacter->GetInventoryComponent()
		|| !SourceCharacter->GetGameplayActionComponent())
	{
		return MakeResult(
			EParadoxDropTargetingStatus::InvalidCharacter,
			TEXT("Drop targeting requires an explicitly supplied, controlled Paradox Character in the controller's World, with inventory and Gameplay Actions."));
	}
	AParadoxPickupableActor* EquippedItem =
		SourceCharacter->GetInventoryComponent()->GetEquippedItem();
	if (!IsValid(EquippedItem))
	{
		return MakeResult(
			EParadoxDropTargetingStatus::SlotEmpty,
			TEXT("Drop targeting requires an equipped pickupable."));
	}
	if (!IsValid(DropActionDefinition))
	{
		return MakeResult(
			EParadoxDropTargetingStatus::InvalidDefinition,
			TEXT("Drop Action Definition is not configured."));
	}

	Controller->GetGridPathPreviewComponent()->ClearPreview();
	DropSourceCharacter = SourceCharacter;
	DropSourceItem = EquippedItem;
	BoundInventory = SourceCharacter->GetInventoryComponent();
	HoveredCell = FGridCellId();
	bTargetingActive = true;
	BindRuntimeSources();
	OnDropTargetingChanged.Broadcast(true);
	LogDebugState(TEXT("Begin"));
	return MakeResult(
		EParadoxDropTargetingStatus::Succeeded,
		TEXT("Drop targeting started."));
}

FParadoxDropTargetingResult UParadoxDropTargetingComponent::CancelDropTargeting()
{
	if (!bTargetingActive)
	{
		return MakeResult(
			EParadoxDropTargetingStatus::NotActive,
			TEXT("Drop targeting is not active."));
	}
	EndTargeting();
	LogDebugState(TEXT("Cancelled"));
	return MakeResult(
		EParadoxDropTargetingStatus::Cancelled,
		TEXT("Drop targeting cancelled."));
}

FParadoxDropTargetingResult UParadoxDropTargetingComponent::ConfirmDropTarget()
{
	if (!bTargetingActive)
	{
		return MakeResult(
			EParadoxDropTargetingStatus::NotActive,
			TEXT("Drop targeting is not active."));
	}
	FString SourceDiagnostic;
	if (!ValidateDropSource(SourceDiagnostic))
	{
		LogDebugState(TEXT("SourceInvalidated"), SourceDiagnostic);
		EndTargeting();
		return MakeResult(
			EParadoxDropTargetingStatus::SourceInvalidated,
			SourceDiagnostic);
	}

	AParadoxCharacter* Character = DropSourceCharacter.Get();
	FVector TargetWorldCenter;
	FString Diagnostic;
	UGridPathPreviewComponent* PathPreview = GetParadoxController()
		? GetParadoxController()->GetGridPathPreviewComponent()
		: nullptr;
	if (!HoveredCell.IsValid()
		|| !UParadoxDropAction::ValidateDropCell(
			Character,
			HoveredCell,
			TargetWorldCenter,
			Diagnostic)
		|| !PathPreview)
	{
		RefreshTargetPreview();
		LogDebugState(TEXT("InvalidConfirm"), Diagnostic);
		return MakeResult(
			EParadoxDropTargetingStatus::InvalidTarget,
			Diagnostic.IsEmpty()
				? TEXT("The hovered cell is not a valid Drop target.")
				: Diagnostic);
	}

	FGridInjectedPath InjectedPath;
	FGridPathPreviewResult CommittedPreview;
	if (!PathPreview->PreparePreviewForCommit(InjectedPath, CommittedPreview)
		|| !CommittedPreview.bGoalAdjustedForTerminalPolicy
		|| CommittedPreview.RequestedGoalCell != HoveredCell
		|| !UParadoxDropAction::ValidateApproachPath(
			Character,
			HoveredCell,
			InjectedPath,
			Diagnostic))
	{
		RefreshTargetPreview();
		LogDebugState(TEXT("InvalidPathConfirm"), Diagnostic);
		return MakeResult(
			EParadoxDropTargetingStatus::InvalidTarget,
			Diagnostic.IsEmpty()
				? TEXT("The hovered Drop cell has no committable ordinary approach path.")
				: Diagnostic);
	}

	FGameplayActionRequest Request;
	FParadoxDropTargetingResult Failure;
	if (!BuildDropRequest(HoveredCell, InjectedPath, Request, Failure))
	{
		EndTargeting();
		return Failure;
	}

	FGameplayActionSubmissionResult Submission;
	UTacticalPauseWorldSubsystem* TacticalPause = GetWorld()
		? GetWorld()->GetSubsystem<UTacticalPauseWorldSubsystem>()
		: nullptr;
	if (TacticalPause && TacticalPause->IsPaused())
	{
		AParadoxPlayerCharacter* PlayerCharacter = Cast<AParadoxPlayerCharacter>(Character);
		UTacticalPauseActionQueueComponent* Planning = PlayerCharacter
			? PlayerCharacter->GetTacticalPauseActionQueueComponent()
			: nullptr;
		if (!Planning)
		{
			Failure = MakeResult(
				EParadoxDropTargetingStatus::SubmissionRejected,
				TEXT("Tactical Drop planning requires the Character action queue component."));
			EndTargeting();
			return Failure;
		}
		Submission = Planning->SubmitOrReplaceNextAction(Request);
	}
	else
	{
		Submission = Character->GetGameplayActionComponent()->SubmitAction(Request);
	}

	EndTargeting();
	FParadoxDropTargetingResult Result = MakeResult(
		Submission.IsAccepted()
			? EParadoxDropTargetingStatus::Succeeded
			: EParadoxDropTargetingStatus::SubmissionRejected,
		Submission.DiagnosticMessage);
	Result.SubmissionResult = Submission;
	LogDebugState(
		Submission.IsAccepted() ? TEXT("Confirmed") : TEXT("SubmissionRejected"),
		Submission.DiagnosticMessage);
	return Result;
}

void UParadoxDropTargetingComponent::UpdateTargetFromHit(
	const FHitResult& HitResult,
	const bool bHitSuccessful)
{
	if (!bTargetingActive)
	{
		return;
	}
	AParadoxPlayerController* Controller = GetParadoxController();
	UGridCellPointerComponent* Pointer = Controller
		? Controller->GetGridCellPointerComponent()
		: nullptr;
	FGridCellId NewHoveredCell;
	if (Pointer && bHitSuccessful)
	{
		const FGridCellPointerResult PointerResult = Pointer->UpdateFromHitResult(HitResult);
		if (PointerResult.Status == EGridCellPointerStatus::Success)
		{
			NewHoveredCell = PointerResult.CellId;
		}
	}
	else if (Pointer)
	{
		Pointer->ClearHoveredCell();
	}
	if (NewHoveredCell != HoveredCell)
	{
		HoveredCell = NewHoveredCell;
		RefreshTargetPreview();
	}
}

FParadoxDropTargetingResult UParadoxDropTargetingComponent::MakeResult(
	const EParadoxDropTargetingStatus Status,
	FString Diagnostic) const
{
	FParadoxDropTargetingResult Result;
	Result.Status = Status;
	Result.DiagnosticMessage = MoveTemp(Diagnostic);
	return Result;
}

AParadoxPlayerController* UParadoxDropTargetingComponent::GetParadoxController() const
{
	return Cast<AParadoxPlayerController>(GetOwner());
}

bool UParadoxDropTargetingComponent::ValidateDropSource(FString& OutDiagnostic) const
{
	AParadoxCharacter* Character = DropSourceCharacter.Get();
	AParadoxPickupableActor* Item = DropSourceItem.Get();
	UParadoxInventoryComponent* Inventory = BoundInventory.Get();
	if (!IsValid(Character) || Character->GetWorld() != GetWorld()
		|| !Character->GetController() || !Character->GetGameplayActionComponent()
		|| !Inventory || Character->GetInventoryComponent() != Inventory)
	{
		OutDiagnostic = TEXT("The explicit Drop source Character is no longer valid or executable.");
		return false;
	}
	if (!IsValid(Item) || Inventory->GetEquippedItem() != Item)
	{
		OutDiagnostic = TEXT("The explicitly targeted Character no longer holds the pickupable captured when Drop targeting began.");
		return false;
	}
	return true;
}

void UParadoxDropTargetingComponent::RefreshTargetPreview()
{
	if (!bTargetingActive)
	{
		return;
	}
	FString Diagnostic;
	if (!ValidateDropSource(Diagnostic))
	{
		LogDebugState(TEXT("SourceInvalidated"), Diagnostic);
		EndTargeting();
		return;
	}

	UGridPathPreviewComponent* PathPreview = GetParadoxController()
		? GetParadoxController()->GetGridPathPreviewComponent()
		: nullptr;
	FVector TargetWorldCenter;
	if (!PathPreview || !HoveredCell.IsValid()
		|| !UParadoxDropAction::ValidateDropCell(
			DropSourceCharacter.Get(),
			HoveredCell,
			TargetWorldCenter,
			Diagnostic))
	{
		if (PathPreview)
		{
			PathPreview->ClearPreview();
		}
		HideDropPreview();
		LogDebugState(TEXT("HoverInvalid"), Diagnostic);
		return;
	}

	PathPreview->UpdatePreviewForControllerWithTerminalPolicy(
		DropSourceCharacter->GetController(),
		HoveredCell,
		EGridPathPreviewTerminalPolicy::StopBeforeRequestedGoal);
}

void UParadoxDropTargetingComponent::UpdateDropPreview(
	const FGridPathPreviewResult& Preview)
{
	FVector TargetWorldCenter;
	FString Diagnostic;
	AParadoxPickupableActor* Item = DropSourceItem.Get();
	UStaticMeshComponent* SourceMesh = IsValid(Item) ? Item->GetPickupableMesh() : nullptr;
	if (!bTargetingActive || !Preview.bIsCommittable
		|| !Preview.bGoalAdjustedForTerminalPolicy
		|| Preview.RequestedGoalCell != HoveredCell
		|| !UParadoxDropAction::ValidateDropCell(
			DropSourceCharacter.Get(),
			HoveredCell,
			TargetWorldCenter,
			Diagnostic)
		|| !SourceMesh || !SourceMesh->GetStaticMesh())
	{
		HideDropPreview();
		return;
	}

	UStaticMeshComponent* PreviewMesh = EnsureDropPreview();
	if (!PreviewMesh)
	{
		return;
	}
	PreviewMesh->SetStaticMesh(SourceMesh->GetStaticMesh());
	for (int32 MaterialIndex = 0;
		MaterialIndex < SourceMesh->GetNumMaterials();
		++MaterialIndex)
	{
		PreviewMesh->SetMaterial(
			MaterialIndex,
			DropPreviewMaterial
				? DropPreviewMaterial.Get()
				: SourceMesh->GetMaterial(MaterialIndex));
	}
	const FTransform ActorDropTransform =
		Item->GetDropPlacementTransform(TargetWorldCenter);
	PreviewMesh->SetWorldTransform(
		SourceMesh->GetRelativeTransform() * ActorDropTransform);
	SetDropPreviewVisible(true);
}

void UParadoxDropTargetingComponent::SetDropPreviewVisible(const bool bVisible)
{
	if (IsValid(DropPreviewActor))
	{
		DropPreviewActor->SetActorHiddenInGame(!bVisible);
	}
	if (IsValid(DropPreviewComponent))
	{
		DropPreviewComponent->SetHiddenInGame(!bVisible);
		DropPreviewComponent->SetVisibility(bVisible, true);
	}
}

void UParadoxDropTargetingComponent::HideDropPreview()
{
	SetDropPreviewVisible(false);
}

void UParadoxDropTargetingComponent::DestroyDropPreview()
{
	if (IsValid(DropPreviewActor))
	{
		DropPreviewActor->Destroy();
	}
	else if (IsValid(DropPreviewComponent))
	{
		DropPreviewComponent->DestroyComponent();
	}
	DropPreviewComponent = nullptr;
	DropPreviewActor = nullptr;
}

UStaticMeshComponent* UParadoxDropTargetingComponent::EnsureDropPreview()
{
	if (IsValid(DropPreviewActor) && IsValid(DropPreviewComponent))
	{
		return DropPreviewComponent;
	}
	DestroyDropPreview();
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* PreviewActor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!PreviewActor)
	{
		return nullptr;
	}
	DropPreviewActor = PreviewActor;
	DropPreviewActor->SetActorEnableCollision(false);
	DropPreviewActor->SetActorTickEnabled(false);
	DropPreviewActor->SetReplicates(false);
	DropPreviewActor->SetActorHiddenInGame(true);
	DropPreviewComponent = PreviewActor->GetStaticMeshComponent();
	if (!DropPreviewComponent)
	{
		DropPreviewActor->Destroy();
		DropPreviewActor = nullptr;
		return nullptr;
	}
	DropPreviewComponent->SetMobility(EComponentMobility::Movable);
	DropPreviewComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DropPreviewComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	DropPreviewComponent->SetGenerateOverlapEvents(false);
	DropPreviewComponent->SetCanEverAffectNavigation(false);
	DropPreviewComponent->SetSimulatePhysics(false);
	DropPreviewComponent->SetEnableGravity(false);
	DropPreviewComponent->SetCastShadow(false);
	DropPreviewComponent->SetHiddenInGame(true);
	DropPreviewComponent->SetVisibility(false, true);
	return DropPreviewComponent;
}

void UParadoxDropTargetingComponent::BindRuntimeSources()
{
	if (DropSourceCharacter.IsValid())
	{
		DropSourceCharacter->OnDestroyed.AddUniqueDynamic(
			this,
			&ThisClass::HandleDropSourceDestroyed);
	}
	if (DropSourceItem.IsValid())
	{
		DropSourceItem->OnDestroyed.AddUniqueDynamic(
			this,
			&ThisClass::HandleDropSourceDestroyed);
	}
	if (BoundInventory.IsValid())
	{
		BoundInventory->OnEquippedItemChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleDropSourceInventoryChanged);
	}
	if (AParadoxPlayerController* Controller = GetParadoxController())
	{
		if (UGridPathPreviewComponent* Preview = Controller->GetGridPathPreviewComponent())
		{
			Preview->OnPreviewChanged.AddUniqueDynamic(
				this,
				&ThisClass::HandlePathPreviewChanged);
		}
	}
}

void UParadoxDropTargetingComponent::UnbindRuntimeSources()
{
	if (DropSourceCharacter.IsValid())
	{
		DropSourceCharacter->OnDestroyed.RemoveDynamic(
			this,
			&ThisClass::HandleDropSourceDestroyed);
	}
	if (DropSourceItem.IsValid())
	{
		DropSourceItem->OnDestroyed.RemoveDynamic(
			this,
			&ThisClass::HandleDropSourceDestroyed);
	}
	if (BoundInventory.IsValid())
	{
		BoundInventory->OnEquippedItemChanged.RemoveDynamic(
			this,
			&ThisClass::HandleDropSourceInventoryChanged);
	}
	if (AParadoxPlayerController* Controller = GetParadoxController())
	{
		if (UGridPathPreviewComponent* Preview = Controller->GetGridPathPreviewComponent())
		{
			Preview->OnPreviewChanged.RemoveDynamic(
				this,
				&ThisClass::HandlePathPreviewChanged);
		}
	}
	BoundInventory.Reset();
}

void UParadoxDropTargetingComponent::EndTargeting()
{
	const bool bWasActive = bTargetingActive;
	bTargetingActive = false;
	UnbindRuntimeSources();
	if (AParadoxPlayerController* Controller = GetParadoxController())
	{
		if (UGridPathPreviewComponent* Preview = Controller->GetGridPathPreviewComponent())
		{
			Preview->ClearPreview();
		}
		if (UGridCellPointerComponent* Pointer = Controller->GetGridCellPointerComponent())
		{
			Pointer->ClearHoveredCell();
		}
	}
	DestroyDropPreview();
	HoveredCell = FGridCellId();
	DropSourceCharacter.Reset();
	DropSourceItem.Reset();
	if (bWasActive)
	{
		OnDropTargetingChanged.Broadcast(false);
	}
}

bool UParadoxDropTargetingComponent::BuildDropRequest(
	const FGridCellId& TargetCell,
	const FGridInjectedPath& InjectedPath,
	FGameplayActionRequest& OutRequest,
	FParadoxDropTargetingResult& OutFailure) const
{
	FString SourceDiagnostic;
	if (!ValidateDropSource(SourceDiagnostic))
	{
		OutFailure = MakeResult(
			EParadoxDropTargetingStatus::SourceInvalidated,
			SourceDiagnostic);
		return false;
	}
	if (!IsValid(DropActionDefinition))
	{
		OutFailure = MakeResult(
			EParadoxDropTargetingStatus::InvalidDefinition,
			TEXT("Drop request requires a configured Definition."));
		return false;
	}
	FGameplayActionRequestCreationResult Creation =
		UGameplayActionBlueprintLibrary::CreateActionRequest(DropActionDefinition);
	if (!Creation.WasCreated())
	{
		OutFailure = MakeResult(
			EParadoxDropTargetingStatus::InvalidDefinition,
			Creation.DiagnosticMessage);
		return false;
	}

	FParadoxDropActionParameters Values;
	Values.TargetCell = TargetCell;
	Values.PathSource = EGridMovePathSource::ExactInjectedPath;
	Values.InjectedPath = InjectedPath;
	Values.NavigationFilter = InjectedPath.FilterClass;
	auto Copy = [&Creation, &Values](const FName Name)
	{
		const FProperty* Property = Values.StaticStruct()->FindPropertyByName(Name);
		return UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
			Creation.Request,
			Name,
			Property,
			Property ? Property->ContainerPtrToValuePtr<void>(&Values) : nullptr);
	};
	if (Copy(ParadoxDropActionParameters::TargetCell)
			!= EGameplayActionParameterAccessResult::Success
		|| Copy(ParadoxDropActionParameters::PathSource)
			!= EGameplayActionParameterAccessResult::Success
		|| Copy(ParadoxDropActionParameters::InjectedPath)
			!= EGameplayActionParameterAccessResult::Success
		|| Copy(ParadoxDropActionParameters::NavigationFilter)
			!= EGameplayActionParameterAccessResult::Success
		|| Copy(ParadoxDropActionParameters::AcceptanceRadius)
			!= EGameplayActionParameterAccessResult::Success
		|| Copy(ParadoxDropActionParameters::AllowStrafe)
			!= EGameplayActionParameterAccessResult::Success)
	{
		OutFailure = MakeResult(
			EParadoxDropTargetingStatus::InvalidDefinition,
			TEXT("Drop Definition does not expose the complete native exact-path schema."));
		return false;
	}
	UGameplayActionBlueprintLibrary::SetRequestContext(
		Creation.Request,
		ParadoxGameplayTags::Origin_Player,
		GetParadoxController(),
		FGameplayActionCorrelationData());
	OutRequest = MoveTemp(Creation.Request);
	return true;
}

void UParadoxDropTargetingComponent::LogDebugState(
	const TCHAR* EventName,
	const FString& Diagnostic) const
{
	if (!bEnableDebug || !IsParadoxInventoryDebugEnabled())
	{
		return;
	}
	const UGridPathPreviewComponent* Preview = GetParadoxController()
		? GetParadoxController()->GetGridPathPreviewComponent()
		: nullptr;
	const FGridPathPreviewResult Latest = Preview
		? Preview->GetLatestPreview()
		: FGridPathPreviewResult();
	PARADOX_LOG_INFO(
		TEXT("Drop targeting event=%s controller=%s source=%s item=%s active=%d hovered=(%d,%d,%d) preview_status=%d exact_cells=%d diagnostic=%s"),
		EventName,
		*GetNameSafe(GetOwner()),
		*GetNameSafe(DropSourceCharacter.Get()),
		*GetNameSafe(DropSourceItem.Get()),
		bTargetingActive ? 1 : 0,
		HoveredCell.Coord.X,
		HoveredCell.Coord.Y,
		HoveredCell.Coord.Layer,
		static_cast<int32>(Latest.Status),
		Latest.Path.Cells.Num(),
		*Diagnostic);
}

void UParadoxDropTargetingComponent::HandlePathPreviewChanged(
	const FGridPathPreviewResult& Preview)
{
	if (bTargetingActive)
	{
		UpdateDropPreview(Preview);
	}
}

void UParadoxDropTargetingComponent::HandleDropSourceInventoryChanged(
	AParadoxPickupableActor* PreviousItem,
	AParadoxPickupableActor* NewItem)
{
	(void)PreviousItem;
	(void)NewItem;
	if (bTargetingActive)
	{
		LogDebugState(
			TEXT("SourceInventoryChanged"),
			TEXT("The Drop source inventory changed during targeting."));
		EndTargeting();
	}
}

void UParadoxDropTargetingComponent::HandleDropSourceDestroyed(AActor* DestroyedActor)
{
	if (bTargetingActive)
	{
		LogDebugState(
			TEXT("SourceDestroyed"),
			FString::Printf(
				TEXT("Drop source Actor '%s' was destroyed during targeting."),
				*GetNameSafe(DestroyedActor)));
		EndTargeting();
	}
}

void UParadoxDropTargetingComponent::HandleWorldStateRestoreStarted(
	const FWorldStateRestoreLifecycleContext& Context)
{
	(void)Context;
	EndTargeting();
}
