#include "Puzzles/ParadoxTeleportGate.h"

#include "Actions/GameplayActionInstance.h"
#include "Characters/ParadoxCharacter.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/GridNavigationModifierComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Interaction/ParadoxInteractionActionDefinition.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Navigation/GridNavigationData.h"
#include "Paradox.h"
#include "Puzzles/ParadoxTeleportGateInteractionAction.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ParadoxTeleportGate"

AParadoxTeleportGate::AParadoxTeleportGate()
{
	PrimaryActorTick.bCanEverTick = false;
	TransferOutCompletionMode = EParadoxTransferPhaseCompletionMode::Explicit;
	TransferInCompletionMode = EParadoxTransferPhaseCompletionMode::Explicit;

	SelectableComponent = CreateDefaultSubobject<UParadoxSelectableComponent>(TEXT("SelectableComponent"));
	SelectableComponent->bShowInteractionCellsWhenSelected = true;
	SelectableComponent->bShowPuzzleConnectionsWhenSelected = true;

	SmartObjectComponent = CreateDefaultSubobject<USmartObjectComponent>(TEXT("SmartObjectComponent"));
	SmartObjectComponent->SetupAttachment(SceneRoot);
	InteractionComponent = CreateDefaultSubobject<UParadoxInteractionComponent>(TEXT("InteractionComponent"));

	PermanentNavigationBlocker = CreateDefaultSubobject<UGridNavigationModifierComponent>(
		TEXT("PermanentNavigationBlocker"));
	PermanentNavigationBlocker->SetupAttachment(SceneRoot);
	PermanentNavigationBlocker->SetMobility(EComponentMobility::Static);
	PermanentNavigationBlocker->bAutoActivate = true;
	PermanentNavigationBlocker->bBlockCells = true;

	TransitNavigationBlocker = CreateDefaultSubobject<UGridNavigationModifierComponent>(
		TEXT("TransitNavigationBlocker"));
	TransitNavigationBlocker->SetupAttachment(SceneRoot);
	TransitNavigationBlocker->SetMobility(EComponentMobility::Static);
	TransitNavigationBlocker->bAutoActivate = true;
	TransitNavigationBlocker->bBlockCells = false;

	static ConstructorHelpers::FObjectFinder<USmartObjectDefinition> SmartObjectDefinitionFinder(
		TEXT("/Game/Data/GridWorld/SOD_TeleportGate.SOD_TeleportGate"));
	if (SmartObjectDefinitionFinder.Succeeded())
	{
		SmartObjectComponent->SetDefinition(SmartObjectDefinitionFinder.Object);
	}

	static ConstructorHelpers::FObjectFinderOptional<UGameplayActionDefinition> EnterDefinitionFinder(
		TEXT("/Game/Data/GameplayActions/DA_ParadoxEnterTeleportGate.DA_ParadoxEnterTeleportGate"));
	if (EnterDefinitionFinder.Succeeded())
	{
		FParadoxInteractionDefinition& Definition =
			InteractionComponent->InteractionDefinitions.AddDefaulted_GetRef();
		Definition.InteractionTag = ParadoxGameplayTags::Interaction_TeleportGate_Enter;
		Definition.GameplayActionDefinition = EnterDefinitionFinder.Get();
	}
}

void AParadoxTeleportGate::BeginPlay()
{
	Super::BeginPlay();

	if (PermanentNavigationBlocker)
	{
		PermanentNavigationBlocker->bAutoActivate = true;
		if (!PermanentNavigationBlocker->IsActive())
		{
			PermanentNavigationBlocker->Activate(true);
		}
		PermanentNavigationBlocker->SetBlockingEnabled(true);
	}
	if (TransitNavigationBlocker)
	{
		TransitNavigationBlocker->bAutoActivate = true;
		if (!TransitNavigationBlocker->IsActive())
		{
			TransitNavigationBlocker->Activate(true);
		}
		TransitNavigationBlocker->SetBlockingEnabled(false);
	}
}

void AParadoxTeleportGate::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetTransitNavigationBlocking(false);
	Super::EndPlay(EndPlayReason);
	ClearPlacementState();
}

#if WITH_EDITOR
EDataValidationResult AParadoxTeleportGate::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	if (!SelectableComponent || !SmartObjectComponent || !InteractionComponent
		|| !PermanentNavigationBlocker || !TransitNavigationBlocker)
	{
		Context.AddError(LOCTEXT(
			"MissingInteractionComponents",
			"Teleport Gate requires its native Selectable, Smart Object, Interaction, and two Grid Navigation Modifier components."));
		Result = EDataValidationResult::Invalid;
	}
	if (PermanentNavigationBlocker
		&& (PermanentNavigationBlocker->GetAttachParent() != SceneRoot
			|| !PermanentNavigationBlocker->bAutoActivate
			|| !PermanentNavigationBlocker->bBlockCells))
	{
		Context.AddError(LOCTEXT(
			"InvalidPermanentNavigationBlocker",
			"Permanent Navigation Blocker must attach to Scene Root, auto-activate, and block cells."));
		Result = EDataValidationResult::Invalid;
	}
	if (TransitNavigationBlocker
		&& (TransitNavigationBlocker->GetAttachParent() != SceneRoot
			|| !TransitNavigationBlocker->bAutoActivate
			|| TransitNavigationBlocker->bBlockCells))
	{
		Context.AddError(LOCTEXT(
			"InvalidTransitNavigationBlocker",
			"Transit Navigation Blocker must attach to Scene Root, auto-activate, and remain non-blocking while the Gate is idle."));
		Result = EDataValidationResult::Invalid;
	}
	if (TransferOutCompletionMode != EParadoxTransferPhaseCompletionMode::Explicit
		|| TransferInCompletionMode != EParadoxTransferPhaseCompletionMode::Explicit)
	{
		Context.AddError(LOCTEXT(
			"ExplicitCompletionRequired",
			"Teleport Gates require Explicit Transfer Out and Transfer In completion modes."));
		Result = EDataValidationResult::Invalid;
	}
	if (!FMath::IsFinite(TunnelTraversalAcceptanceRadius)
		|| TunnelTraversalAcceptanceRadius <= 0.0f)
	{
		Context.AddError(LOCTEXT(
			"InvalidTunnelTraversalAcceptanceRadius",
			"Tunnel Traversal Acceptance Radius must be finite and greater than zero."));
		Result = EDataValidationResult::Invalid;
	}
	const USmartObjectDefinition* Definition =
		SmartObjectComponent ? SmartObjectComponent->GetDefinition() : nullptr;
	if (!Definition || Definition->GetSlots().Num() != 1)
	{
		Context.AddError(LOCTEXT(
			"ExactlyOneInteractionSlotRequired",
			"Teleport Gate Smart Object definitions must contain exactly one external interaction slot."));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

FParadoxTransferOperationResult AParadoxTeleportGate::EvaluateEnter(
	AParadoxCharacter* Character,
	UGameplayActionInstance* RequestingAction) const
{
	return EvaluateTransfer(Character, Character, RequestingAction);
}

FParadoxTransferOperationResult AParadoxTeleportGate::TryEnter(
	AParadoxCharacter* Character,
	UGameplayActionInstance* RequestingAction)
{
	return RequestTransfer(Character, Character, RequestingAction);
}

bool AParadoxTeleportGate::CanRequestTransferWithAction_Implementation(
	AActor* TransferSubject,
	AActor* Requester,
	UGameplayActionInstance* RequestingAction,
	FString& OutDiagnostic) const
{
	const UParadoxEnterTeleportGateInteractionAction* EnterAction =
		Cast<UParadoxEnterTeleportGateInteractionAction>(RequestingAction);
	if (!EnterAction
		|| TransferSubject != Requester
		|| EnterAction->GetInteractionRequester() != Requester
		|| EnterAction->GetInteractionTarget() != this
		|| EnterAction->GetInteractionTag()
			!= ParadoxGameplayTags::Interaction_TeleportGate_Enter.GetTag())
	{
		OutDiagnostic = TEXT(
			"Teleport Gate transfer requires its exact semantic Enter action from the Character being transferred.");
		return false;
	}
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxTeleportGate::CanTransferSubject_Implementation(
	AActor* TransferSubject,
	AActor* Requester,
	FString& OutDiagnostic) const
{
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(TransferSubject);
	if (!Character || Character != Requester)
	{
		OutDiagnostic = TEXT("Teleport Gate accepts only the requesting Paradox Character itself.");
		return false;
	}
	if (Character->GetWorld() != GetWorld()
		|| !Character->GetController()
		|| !Character->GetCharacterMovement()
		|| !Character->GetGameplayActionComponent())
	{
		OutDiagnostic = TEXT(
			"The Character must belong to this World and own its Controller, Character Movement, and Gameplay Actions components.");
		return false;
	}
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxTeleportGate::CanSourceStartTransfer_Implementation(
	AActor* TransferSubject,
	AActor* Requester,
	AParadoxPairedTransferEndpoint* Destination,
	FString& OutDiagnostic) const
{
	(void)Requester;
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(TransferSubject);
	if (!Character || !Cast<AParadoxTeleportGate>(Destination))
	{
		OutDiagnostic = TEXT("Teleport Gate destination must be another Teleport Gate.");
		return false;
	}
	if (!TransferAnchor || !TransferAnchor->IsRegistered()
		|| TransferAnchor->GetComponentLocation().ContainsNaN())
	{
		OutDiagnostic = TEXT("Teleport Gate source requires a registered finite tunnel Transfer Anchor.");
		return false;
	}
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxTeleportGate::CanDestinationReceiveTransfer_Implementation(
	AActor* TransferSubject,
	AActor* Requester,
	AParadoxPairedTransferEndpoint* Source,
	FString& OutDiagnostic) const
{
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(TransferSubject);
	if (!Character || Character != Requester || !Cast<AParadoxTeleportGate>(Source))
	{
		OutDiagnostic = TEXT("Teleport Gate destination received an incompatible Character or source.");
		return false;
	}

	FParadoxInteractionOption ExitOption;
	if (!ResolveUniqueEnterOption(*Character, *this, ExitOption, OutDiagnostic))
	{
		return false;
	}
	FGridCellQueryResult ExitCell;
	if (!ResolveInteractionCell(ExitOption, ExitCell, OutDiagnostic))
	{
		return false;
	}

	if (UGridNavigationOccupancyComponent* Occupancy =
		UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(*Character))
	{
		FGridTrafficGoalClaimRequest Claim;
		AGridNavigationData* NavigationData = nullptr;
		UGridNavigationOccupancyComponent* ResolvedOccupancy = nullptr;
		if (!BuildExitClaim(
				*Character,
				ExitCell,
				false,
				Claim,
				NavigationData,
				ResolvedOccupancy,
				OutDiagnostic)
			|| ResolvedOccupancy != Occupancy
			|| !NavigationData->CanClaimTrafficGoal(Claim))
		{
			OutDiagnostic = OutDiagnostic.IsEmpty()
				? TEXT("The destination exit cell is occupied or reserved by another Character.")
				: OutDiagnostic;
			return false;
		}
	}

	OutDiagnostic.Reset();
	return true;
}

bool AParadoxTeleportGate::PrepareSubjectForTransfer_Implementation(
	const FParadoxTransferOperationContext& Context,
	FString& OutDiagnostic)
{
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(Context.Subject);
	AParadoxTeleportGate* Destination = Cast<AParadoxTeleportGate>(Context.Destination);
	if (!Character || Context.Source != this || Context.Requester != Character || !Destination)
	{
		OutDiagnostic = TEXT("Teleport Gate preparation received an incoherent transfer context.");
		return false;
	}

	ClearPlacementState();
	FParadoxInteractionOption SourceOption;
	FParadoxInteractionOption DestinationOption;
	if (!ResolveUniqueEnterOption(*Character, *this, SourceOption, OutDiagnostic)
		|| !ResolveUniqueEnterOption(
			*Character,
			*Destination,
			DestinationOption,
			OutDiagnostic))
	{
		return false;
	}

	FGridCellQueryResult SourceCell;
	FGridCellQueryResult ExitCell;
	if (!ResolveInteractionCell(SourceOption, SourceCell, OutDiagnostic)
		|| !ResolveInteractionCell(DestinationOption, ExitCell, OutDiagnostic)
		|| !ResolveSafeTunnelAnchorPlacement(
			*Character,
			*Destination,
			ActiveDestinationTeleportTransform,
			OutDiagnostic))
	{
		return false;
	}
	const UGridWorldSubsystem* GridWorld = GetWorld()
		? GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	const FGridCellQueryResult CurrentCell = GridWorld
		? GridWorld->ProjectPoint(Character->GetActorLocation())
		: FGridCellQueryResult();
	if (CurrentCell.Status != EGridQueryStatus::Success
		|| CurrentCell.CellId != SourceCell.CellId)
	{
		OutDiagnostic = TEXT(
			"The Character is no longer standing on the claimed source interaction cell.");
		ClearPlacementState();
		return false;
	}

	AGridNavigationData* NavigationData = nullptr;
	UGridNavigationOccupancyComponent* Occupancy = nullptr;
	if (!BuildExitClaim(
			*Character,
			ExitCell,
			true,
			ActiveExitClaim,
			NavigationData,
			Occupancy,
			OutDiagnostic)
		|| !NavigationData->TryClaimTrafficGoal(ActiveExitClaim))
	{
		OutDiagnostic = OutDiagnostic.IsEmpty()
			? TEXT("The destination exit cell could not be reserved for this Character.")
			: OutDiagnostic;
		ClearPlacementState();
		return false;
	}

	ActiveExitNavigationData = NavigationData;
	ActiveSourceCell = SourceCell.CellId;
	ActiveExitCell = ExitCell.CellId;
	ActiveSourceSlotTransform = SourceOption.SlotWorldTransform;
	ActiveDestinationSlotTransform = DestinationOption.SlotWorldTransform;
	bHasActiveExitClaim = true;
	StopCharacterMovement(*Character);
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxTeleportGate::PerformTransferCommit_Implementation(
	const FParadoxTransferOperationContext& Context,
	FString& OutDiagnostic)
{
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(Context.Subject);
	AParadoxTeleportGate* Destination = Cast<AParadoxTeleportGate>(Context.Destination);
	AGridNavigationData* NavigationData = ActiveExitNavigationData.Get();
	if (!Character || Context.Source != this || !Destination
		|| !bHasActiveExitClaim || !NavigationData || !ActiveExitCell.IsValid())
	{
		OutDiagnostic = TEXT("Teleport Gate lost its reserved destination before commit.");
		return false;
	}
	UGridNavigationOccupancyComponent* Occupancy =
		UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(*Character);
	if (!Occupancy || Occupancy->OccupantId != ActiveExitClaim.OwnerId)
	{
		OutDiagnostic = TEXT("Teleport Gate lost the Character's reserved GridWorld occupancy identity before commit.");
		return false;
	}

	FTransform FreshTeleportTransform;
	const UGridWorldSubsystem* GridWorld = GetWorld()
		? GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	const FGridCellQueryResult FreshExitCell = GridWorld
		? GridWorld->GetCell(ActiveExitCell)
		: FGridCellQueryResult();
	if (FreshExitCell.Status != EGridQueryStatus::Success
		|| FreshExitCell.CellId != ActiveExitCell
		|| !FreshExitCell.WorldCenter.Equals(ActiveExitClaim.GoalCell.WorldCenter)
		|| !IsActiveExitClaimStillOwned()
		|| !ResolveSafeTunnelAnchorPlacement(
			*Character,
			*Destination,
			FreshTeleportTransform,
			OutDiagnostic))
	{
		OutDiagnostic = OutDiagnostic.IsEmpty()
			? TEXT("The destination tunnel anchor, reserved interaction cell, or traffic claim changed before commit.")
			: OutDiagnostic;
		return false;
	}

	StopCharacterMovement(*Character);
	if (!Character->TeleportTo(
			FreshTeleportTransform.GetLocation(),
			FreshTeleportTransform.Rotator(),
			false,
			true))
	{
		OutDiagnostic = TEXT("Character TeleportTo rejected the validated destination tunnel anchor.");
		return false;
	}

	NavigationData->ReleaseTrafficCorridor(Occupancy->OccupantId, nullptr, false);
	Occupancy->RefreshOccupancy();

	ActiveDestinationTeleportTransform = FreshTeleportTransform;
	bHasCommittedPlacement = true;
	SetTransitNavigationBlocking(true);
	Destination->SetTransitNavigationBlocking(true);

	OutDiagnostic.Reset();
	return true;
}

bool AParadoxTeleportGate::FinalizeTransferredSubject_Implementation(
	const FParadoxTransferOperationContext& Context,
	FString& OutDiagnostic)
{
	ClearPairTransitNavigationBlocking(Context);
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(Context.Subject);
	if (!Character || !bHasCommittedPlacement || !ActiveExitCell.IsValid()
		|| !bHasActiveExitClaim)
	{
		OutDiagnostic = TEXT("Teleport Gate has no coherent committed Character placement to finalize.");
		return false;
	}
	if (!RecoverCharacterToSlot(
			*Character,
			ActiveDestinationSlotTransform,
			ActiveExitCell,
			true,
			OutDiagnostic))
	{
		return false;
	}

	ClearPlacementState();
	OutDiagnostic.Reset();
	return true;
}

void AParadoxTeleportGate::HandleTransferCancelled_Implementation(
	const FParadoxTransferOperationContext& Context,
	const EParadoxTransferCancellationReason Reason,
	const bool bWasCommitted)
{
	ClearPairTransitNavigationBlocking(Context);
	AParadoxCharacter* Character = Cast<AParadoxCharacter>(Context.Subject);
	const bool bWorldObjectsRemainUsable =
		Reason != EParadoxTransferCancellationReason::SubjectDestroyed
		&& Reason != EParadoxTransferCancellationReason::EndpointDestroyed
		&& Reason != EParadoxTransferCancellationReason::EndPlay
		&& GetWorld()
		&& !GetWorld()->bIsTearingDown
		&& HasActorBegunPlay()
		&& (!Character || Character->HasActorBegunPlay());
	const bool bHasRecoveryState = bWasCommitted
		? ActiveExitCell.IsValid()
		: ActiveSourceCell.IsValid();
	if (Character && bHasRecoveryState && bWorldObjectsRemainUsable)
	{
		if (!bWasCommitted)
		{
			// A source recovery needs to reacquire its own cell. Release the remote
			// reservation first so this endpoint owns at most one goal claim.
			ReleaseExitReservation();
		}
		FString RecoveryDiagnostic;
		const bool bRecovered = bWasCommitted
			? RecoverCharacterToSlot(
				*Character,
				ActiveDestinationSlotTransform,
				ActiveExitCell,
				true,
				RecoveryDiagnostic)
			: RecoverCharacterToSlot(
				*Character,
				ActiveSourceSlotTransform,
				ActiveSourceCell,
				true,
				RecoveryDiagnostic);
		if (!bRecovered && !RecoveryDiagnostic.IsEmpty())
		{
			PARADOX_LOG_ERROR(
				TEXT("Teleport Gate '%s' could not recover Character '%s' after cancellation: %s"),
				*GetNameSafe(this),
				*GetNameSafe(Character),
				*RecoveryDiagnostic);
		}
	}
	ClearPlacementState();
}

bool AParadoxTeleportGate::IsLinkedEndpointCompatible(
	const AParadoxPairedTransferEndpoint* Candidate,
	FString& OutDiagnostic) const
{
	if (!Cast<AParadoxTeleportGate>(Candidate))
	{
		OutDiagnostic = TEXT("A Teleport Gate can pair only with another Teleport Gate.");
		return false;
	}
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxTeleportGate::ResolveUniqueEnterOption(
	AParadoxCharacter& Character,
	const AParadoxTeleportGate& Gate,
	FParadoxInteractionOption& OutOption,
	FString& OutDiagnostic) const
{
	OutOption = FParadoxInteractionOption();
	if (!Gate.InteractionComponent)
	{
		OutDiagnostic = TEXT("Teleport Gate has no Interaction Component.");
		return false;
	}
	const FParadoxInteractionQueryResult Query =
		Gate.InteractionComponent->QueryInteractionOptionsByTag(
			&Character,
			ParadoxGameplayTags::Interaction_TeleportGate_Enter);
	if (!Query.IsSuccess())
	{
		OutDiagnostic = Query.DiagnosticMessage;
		return false;
	}

	const FParadoxInteractionOption* Match = nullptr;
	int32 MatchingCount = 0;
	for (const FParadoxInteractionOption& Option : Query.Options)
	{
		if (Option.InteractionTag
			!= ParadoxGameplayTags::Interaction_TeleportGate_Enter.GetTag())
		{
			continue;
		}
		++MatchingCount;
		Match = &Option;
	}
	if (MatchingCount != 1 || !Match)
	{
		OutDiagnostic = FString::Printf(
			TEXT("Teleport Gate requires exactly one Enter interaction slot; resolved %d."),
			MatchingCount);
		return false;
	}
	if (Match->State != EParadoxInteractionOptionState::Free)
	{
		OutDiagnostic = TEXT("The Teleport Gate interaction slot is unavailable.");
		return false;
	}
	OutOption = *Match;
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxTeleportGate::ResolveSafeTunnelAnchorPlacement(
	AParadoxCharacter& Character,
	const AParadoxTeleportGate& Gate,
	FTransform& OutTransform,
	FString& OutDiagnostic) const
{
	UWorld* World = Gate.GetWorld();
	if (!World || !Gate.TransferAnchor)
	{
		OutDiagnostic = TEXT("Teleport Gate has no World or tunnel Transfer Anchor.");
		return false;
	}

	const FVector AnchorLocation = Gate.TransferAnchor->GetComponentLocation();
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	if (!Capsule)
	{
		OutDiagnostic = TEXT("Teleport Gate Character has no capsule for tunnel placement.");
		return false;
	}
	const FRotator SafeRotation = Gate.TransferAnchor->GetComponentRotation();
	const float PlacementTolerance = FMath::Max(
		0.1f,
		Gate.TunnelTraversalAcceptanceRadius);
	FVector DesiredActorLocation = AnchorLocation;
	FVector SafeLocation = DesiredActorLocation;
	bool bFoundSafePlacement = World->FindTeleportSpot(
		&Character,
		SafeLocation,
		SafeRotation);
	bool bWithinPlacementTolerance = bFoundSafePlacement
		&& FVector::DistSquared(SafeLocation, DesiredActorLocation)
			<= FMath::Square(PlacementTolerance);
	if (!bWithinPlacementTolerance)
	{
		// Backward-compatible floor-anchor fallback. TeleportTo expects the ACharacter origin at
		// the capsule center, while designers commonly place an Arrow at the tunnel floor. The
		// second FindTeleportSpot may only make a small correction around that explicit candidate;
		// otherwise a real tunnel obstruction could incorrectly be accepted by moving above it.
		DesiredActorLocation = AnchorLocation
			+ FVector::UpVector * Capsule->GetScaledCapsuleHalfHeight();
		SafeLocation = DesiredActorLocation;
		bFoundSafePlacement = World->FindTeleportSpot(
			&Character,
			SafeLocation,
			SafeRotation);
		bWithinPlacementTolerance = bFoundSafePlacement
			&& FVector::DistSquared(SafeLocation, DesiredActorLocation)
				<= FMath::Square(PlacementTolerance);
	}
	if (!bFoundSafePlacement)
	{
		OutDiagnostic = TEXT("No collision-safe Character placement exists at the tunnel Transfer Anchor.");
		return false;
	}
	if (!bWithinPlacementTolerance)
	{
		OutDiagnostic = TEXT(
			"Collision-safe placement moved farther than the tunnel traversal tolerance from an accepted Transfer Anchor placement.");
		return false;
	}

	OutTransform = FTransform(SafeRotation, SafeLocation, Character.GetActorScale3D());
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxTeleportGate::ResolveInteractionCell(
	const FParadoxInteractionOption& Option,
	FGridCellQueryResult& OutCell,
	FString& OutDiagnostic) const
{
	const UGridWorldSubsystem* GridWorld = GetWorld()
		? GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	OutCell = GridWorld ? GridWorld->GetCell(Option.GridCellId) : FGridCellQueryResult();
	if (OutCell.Status != EGridQueryStatus::Success
		|| !OutCell.CellId.IsValid()
		|| !OutCell.bWalkable)
	{
		OutDiagnostic = TEXT(
			"Teleport Gate interaction slot does not resolve to a valid walkable GridWorld cell.");
		return false;
	}
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxTeleportGate::BuildExitClaim(
	AParadoxCharacter& Character,
	const FGridCellQueryResult& ExitCell,
	const bool bCreateOccupancy,
	FGridTrafficGoalClaimRequest& OutClaim,
	AGridNavigationData*& OutNavigationData,
	UGridNavigationOccupancyComponent*& OutOccupancy,
	FString& OutDiagnostic) const
{
	OutClaim = FGridTrafficGoalClaimRequest();
	OutNavigationData = nullptr;
	OutOccupancy = nullptr;
	UGridWorldSubsystem* GridWorld = Character.GetWorld()
		? Character.GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	OutNavigationData = GridWorld ? GridWorld->GetNavigationData() : nullptr;
	if (!OutNavigationData || ExitCell.Status != EGridQueryStatus::Success || !ExitCell.CellId.IsValid())
	{
		OutDiagnostic = TEXT("GridWorld navigation data or destination cell is unavailable.");
		return false;
	}

	UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	const float AgentRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : 42.0f;
	const float AgentHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() * 2.0f : 192.0f;
	OutOccupancy = UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(Character);
	if (!OutOccupancy && bCreateOccupancy)
	{
		OutOccupancy = UGridNavigationOccupancyComponent::FindOrAddAgentOccupancy(
			Character,
			AgentRadius,
			AgentHeight,
			true);
	}
	if (!OutOccupancy || !OutOccupancy->OccupantId.IsValid())
	{
		OutDiagnostic = TEXT("The Character has no active GridWorld occupancy identity.");
		return false;
	}

	OutClaim.OwnerId = OutOccupancy->OccupantId;
	OutClaim.Claimant = const_cast<AParadoxTeleportGate*>(this);
	OutClaim.Pawn = &Character;
	OutClaim.GoalCell = {ExitCell.CellId, ExitCell.WorldCenter};
	OutClaim.AgentRadius = AgentRadius;
	OutClaim.AgentHeight = AgentHeight;
	OutDiagnostic.Reset();
	return true;
}

void AParadoxTeleportGate::StopCharacterMovement(AParadoxCharacter& Character) const
{
	if (AController* Controller = Character.GetController())
	{
		Controller->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = Character.GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
}

bool AParadoxTeleportGate::RecoverCharacterToSlot(
	AParadoxCharacter& Character,
	const FTransform& SlotTransform,
	const FGridCellId& SlotCell,
	const bool bCommitParking,
	FString& OutDiagnostic)
{
	if (!SlotCell.IsValid())
	{
		OutDiagnostic = TEXT("Teleport Gate recovery has no valid interaction cell.");
		return false;
	}
	AGridNavigationData* NavigationData = ActiveExitNavigationData.Get();
	UGridWorldSubsystem* GridWorld = Character.GetWorld()
		? Character.GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	if (!NavigationData && GridWorld)
	{
		NavigationData = GridWorld->GetNavigationData();
	}
	if (!NavigationData || !GridWorld)
	{
		OutDiagnostic = TEXT("Teleport Gate recovery has no GridWorld navigation data.");
		return false;
	}

	StopCharacterMovement(Character);
	if (!IsCharacterAtSlot(Character, SlotTransform, SlotCell))
	{
		FVector SafeLocation = SlotTransform.GetLocation();
		const FRotator SafeRotation = SlotTransform.Rotator();
		if (!Character.GetWorld()->FindTeleportSpot(
				&Character,
				SafeLocation,
				SafeRotation))
		{
			OutDiagnostic = TEXT("No collision-safe placement exists at the interaction slot.");
			return false;
		}
		const FGridCellQueryResult SafeCell = GridWorld->ProjectPoint(SafeLocation);
		if (SafeCell.Status != EGridQueryStatus::Success || SafeCell.CellId != SlotCell)
		{
			OutDiagnostic = TEXT(
				"Collision-safe interaction placement no longer belongs to the reserved cell.");
			return false;
		}
		if (!Character.TeleportTo(SafeLocation, SafeRotation, false, true))
		{
			OutDiagnostic = TEXT("Character TeleportTo rejected the interaction-slot recovery.");
			return false;
		}
	}

	UGridNavigationOccupancyComponent* Occupancy =
		UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(Character);
	if (!Occupancy || !Occupancy->OccupantId.IsValid())
	{
		OutDiagnostic = TEXT("Character lost its GridWorld occupancy identity during recovery.");
		return false;
	}
	NavigationData->ReleaseTrafficCorridor(Occupancy->OccupantId, nullptr, false);
	if (bCommitParking)
	{
		FGridTrafficGoalClaimRequest ParkingClaim;
		AGridNavigationData* ClaimNavigationData = nullptr;
		UGridNavigationOccupancyComponent* ClaimOccupancy = nullptr;
		const FGridCellQueryResult Cell = GridWorld->GetCell(SlotCell);
		if (!BuildExitClaim(
				Character,
				Cell,
				true,
				ParkingClaim,
				ClaimNavigationData,
				ClaimOccupancy,
				OutDiagnostic))
		{
			return false;
		}
		if (SlotCell == ActiveExitCell && bHasActiveExitClaim)
		{
			ParkingClaim = ActiveExitClaim;
		}
		else if (!ClaimNavigationData->TryClaimTrafficGoal(ParkingClaim))
		{
			OutDiagnostic = TEXT("The recovery interaction cell could not be reserved.");
			return false;
		}
		ClaimNavigationData->CommitTrafficParking(ParkingClaim);
		ClaimNavigationData->ReleaseTrafficGoalClaims(this);
		if (SlotCell == ActiveExitCell)
		{
			bHasActiveExitClaim = false;
		}
	}
	Occupancy->RefreshOccupancy();
	if (!IsCharacterAtSlot(Character, SlotTransform, SlotCell))
	{
		OutDiagnostic = TEXT("Character recovery did not finish on the expected interaction cell.");
		return false;
	}
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxTeleportGate::IsCharacterAtSlot(
	const AParadoxCharacter& Character,
	const FTransform& SlotTransform,
	const FGridCellId& SlotCell) const
{
	const UGridWorldSubsystem* GridWorld = Character.GetWorld()
		? Character.GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	const FGridCellQueryResult CurrentCell = GridWorld
		? GridWorld->ProjectPoint(Character.GetActorLocation())
		: FGridCellQueryResult();
	return CurrentCell.Status == EGridQueryStatus::Success
		&& CurrentCell.CellId == SlotCell
		&& FVector::DistSquared2D(
			Character.GetActorLocation(),
			SlotTransform.GetLocation())
			<= FMath::Square(FMath::Max(0.1f, TunnelTraversalAcceptanceRadius));
}

bool AParadoxTeleportGate::GetActiveTunnelTarget(
	const FGuid& OperationId,
	const bool bDestinationEgress,
	FVector& OutLocation) const
{
	if (!OperationId.IsValid()
		|| OperationId != GetCurrentTransferOperationId()
		|| !IsTransferInProgress())
	{
		return false;
	}
	if (bDestinationEgress)
	{
		if (!bHasCommittedPlacement || !ActiveExitCell.IsValid())
		{
			return false;
		}
		OutLocation = ActiveDestinationSlotTransform.GetLocation();
		return true;
	}
	if (!ActiveSourceCell.IsValid() || !TransferAnchor)
	{
		return false;
	}
	OutLocation = TransferAnchor->GetComponentLocation();
	return true;
}

void AParadoxTeleportGate::SetTransitNavigationBlocking(const bool bBlocking)
{
	if (TransitNavigationBlocker)
	{
		TransitNavigationBlocker->SetBlockingEnabled(bBlocking);
	}
}

void AParadoxTeleportGate::ClearPairTransitNavigationBlocking(
	const FParadoxTransferOperationContext& Context)
{
	SetTransitNavigationBlocking(false);
	if (AParadoxTeleportGate* Source = Cast<AParadoxTeleportGate>(Context.Source))
	{
		Source->SetTransitNavigationBlocking(false);
	}
	if (AParadoxTeleportGate* Destination = Cast<AParadoxTeleportGate>(Context.Destination))
	{
		Destination->SetTransitNavigationBlocking(false);
	}
}

bool AParadoxTeleportGate::IsActiveExitClaimStillOwned() const
{
	const AGridNavigationData* NavigationData = ActiveExitNavigationData.Get();
	const FGridTrafficReservationSnapshotPtr Snapshot = NavigationData
		? NavigationData->GetTrafficReservationSnapshot()
		: nullptr;
	return bHasActiveExitClaim
		&& ActiveExitClaim.OwnerId.IsValid()
		&& ActiveExitCell.IsValid()
		&& Snapshot.IsValid()
		&& Snapshot->Cells.ContainsByPredicate(
			[this](const FGridTrafficReservedCell& Reserved)
			{
				return Reserved.OwnerId == ActiveExitClaim.OwnerId
					&& Reserved.CellId == ActiveExitCell
					&& Reserved.bGoalOrParking;
			});
}

void AParadoxTeleportGate::ReleaseExitReservation()
{
	if (bHasActiveExitClaim)
	{
		if (AGridNavigationData* NavigationData = ActiveExitNavigationData.Get())
		{
			NavigationData->ReleaseTrafficGoalClaims(this);
		}
	}
	bHasActiveExitClaim = false;
	ActiveExitClaim = FGridTrafficGoalClaimRequest();
	ActiveExitNavigationData.Reset();
}

void AParadoxTeleportGate::ClearPlacementState()
{
	ReleaseExitReservation();
	ActiveSourceCell = FGridCellId();
	ActiveExitCell = FGridCellId();
	ActiveSourceSlotTransform = FTransform::Identity;
	ActiveDestinationSlotTransform = FTransform::Identity;
	ActiveDestinationTeleportTransform = FTransform::Identity;
	bHasCommittedPlacement = false;
}

#undef LOCTEXT_NAMESPACE
