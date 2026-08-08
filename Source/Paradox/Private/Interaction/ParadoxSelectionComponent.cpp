#include "Interaction/ParadoxSelectionComponent.h"

#include "Controllers/ParadoxPlayerController.h"
#include "Components/GameplayActionComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Navigation/GridNavigationData.h"
#include "Paradox.h"
#include "Presentation/GridCellOverlayPresentationSubsystem.h"
#include "Presentation/GridRuntimeVisualizationSubsystem.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "Subsystems/WorldStateSubsystem.h"

UParadoxSelectionComponent::UParadoxSelectionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AActor* UParadoxSelectionComponent::GetHoveredActor() const
{
	const UParadoxSelectableComponent* Selectable = CurrentHoveredSelectable.Get();
	return IsValid(Selectable) ? Selectable->GetOwner() : nullptr;
}

AActor* UParadoxSelectionComponent::GetSelectedActor() const
{
	const UParadoxSelectableComponent* Selectable = CurrentSelectedSelectable.Get();
	return IsValid(Selectable) ? Selectable->GetOwner() : nullptr;
}

FParadoxInteractionAvailabilityResult UParadoxSelectionComponent::GetSelectedInteractionAvailability(
	const FGameplayTag InteractionTag) const
{
	if (const FParadoxInteractionAvailabilityResult* Result = CachedInteractionAvailabilityByTag.Find(InteractionTag))
	{
		return *Result;
	}
	FParadoxInteractionAvailabilityResult Missing;
	Missing.InteractionTag = InteractionTag;
	Missing.Status = EParadoxInteractionAvailabilityStatus::NoMatchingInteraction;
	Missing.DiagnosticMessage = TEXT("The selected target has no cached exact interaction with this tag.");
	return Missing;
}

TArray<FParadoxInteractionAvailabilityResult> UParadoxSelectionComponent::GetSelectedInteractionAvailabilities() const
{
	TArray<FParadoxInteractionAvailabilityResult> Results;
	CachedInteractionAvailabilityByTag.GenerateValueArray(Results);
	Results.Sort([](const FParadoxInteractionAvailabilityResult& Left, const FParadoxInteractionAvailabilityResult& Right)
	{
		return Left.InteractionTag.ToString() < Right.InteractionTag.ToString();
	});
	return Results;
}

void UParadoxSelectionComponent::SetSelectionEnabled(const bool bEnabled)
{
	if (bSelectionEnabled == bEnabled)
	{
		return;
	}
	bSelectionEnabled = bEnabled;
	if (!bSelectionEnabled)
	{
		ResetSelectionState();
	}
}

void UParadoxSelectionComponent::DeselectCurrentActor()
{
	SetSelectedSelectable(nullptr);
}

void UParadoxSelectionComponent::ResetSelectionState()
{
	SetHoveredSelectable(nullptr);
	SetSelectedSelectable(nullptr);
}

void UParadoxSelectionComponent::RefreshSelectedInteractionOptions()
{
	RefreshInteractionCellPresentation();
}

void UParadoxSelectionComponent::UpdateHoverFromHitResult(
	const FHitResult& HitResult,
	const bool bHitSuccessful)
{
	SetHoveredSelectable(
		bSelectionEnabled
			? ResolveSelectable(HitResult, bHitSuccessful, true, false)
			: nullptr);
}

bool UParadoxSelectionComponent::HandleSelectionPointerHit(
	const FHitResult& HitResult,
	const bool bHitSuccessful)
{
	if (!bSelectionEnabled)
	{
		return false;
	}

	UParadoxSelectableComponent* HitSelectable = ResolveSelectable(
		HitResult,
		bHitSuccessful,
		false,
		true);
	if (!HitSelectable)
	{
		DeselectCurrentActor();
		return false;
	}

	SetSelectedSelectable(
		CurrentSelectedSelectable.Get() == HitSelectable
			? nullptr
			: HitSelectable);
	return true;
}

void UParadoxSelectionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		if (UGridWorldSubsystem* GridWorld = World->GetSubsystem<UGridWorldSubsystem>())
		{
			GridWorld->OnGridWorldChanged.AddUniqueDynamic(
				this,
				&ThisClass::HandleGridWorldChanged);
		}
		if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldStateRestoreStartedHandle = WorldState->OnRestoreStartedNative().AddUObject(
				this,
				&ThisClass::HandleWorldStateRestoreStarted);
		}
	}
}

void UParadoxSelectionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UGridWorldSubsystem* GridWorld = World->GetSubsystem<UGridWorldSubsystem>())
		{
			GridWorld->OnGridWorldChanged.RemoveDynamic(
				this,
				&ThisClass::HandleGridWorldChanged);
		}
		if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldState->OnRestoreStartedNative().Remove(WorldStateRestoreStartedHandle);
		}
	}
	WorldStateRestoreStartedHandle.Reset();
	ResetSelectionState();
	Super::EndPlay(EndPlayReason);
}

UParadoxSelectableComponent* UParadoxSelectionComponent::ResolveSelectable(
	const FHitResult& HitResult,
	const bool bHitSuccessful,
	const bool bRequireHover,
	const bool bRequireSelection) const
{
	AActor* HitActor = bHitSuccessful ? HitResult.GetActor() : nullptr;
	if (!IsValid(HitActor))
	{
		return nullptr;
	}

	UParadoxSelectableComponent* Selectable = HitActor->FindComponentByClass<UParadoxSelectableComponent>();
	if (!IsValid(Selectable)
		|| (bRequireHover && !Selectable->bCanBeHovered)
		|| (bRequireSelection && !Selectable->bCanBeSelected))
	{
		return nullptr;
	}
	return Selectable;
}

void UParadoxSelectionComponent::SetHoveredSelectable(
	UParadoxSelectableComponent* NewHoveredSelectable)
{
	UParadoxSelectableComponent* PreviousSelectable = CurrentHoveredSelectable.Get();
	if (PreviousSelectable == NewHoveredSelectable)
	{
		return;
	}

	AActor* PreviousActor = IsValid(PreviousSelectable) ? PreviousSelectable->GetOwner() : nullptr;
	if (IsValid(PreviousSelectable))
	{
		PreviousSelectable->SetHoveredFromSelection(false);
	}
	CurrentHoveredSelectable = NewHoveredSelectable;
	if (IsValid(NewHoveredSelectable))
	{
		NewHoveredSelectable->SetHoveredFromSelection(true);
	}
	AActor* NewActor = IsValid(NewHoveredSelectable) ? NewHoveredSelectable->GetOwner() : nullptr;
	OnHoveredActorChanged.Broadcast(PreviousActor, NewActor);

	if (bEnableDebug)
	{
		PARADOX_LOG_INFO(
			TEXT("Selection owner '%s' hover changed from '%s' to '%s'."),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(PreviousActor),
			*GetNameSafe(NewActor));
	}
}

void UParadoxSelectionComponent::SetSelectedSelectable(
	UParadoxSelectableComponent* NewSelectedSelectable)
{
	UParadoxSelectableComponent* PreviousSelectable = CurrentSelectedSelectable.Get();
	if (PreviousSelectable == NewSelectedSelectable)
	{
		return;
	}

	AActor* PreviousActor = IsValid(PreviousSelectable) ? PreviousSelectable->GetOwner() : nullptr;
	EndInteractionCellPresentation();
	if (IsValid(PreviousSelectable))
	{
		PreviousSelectable->SetSelectedFromSelection(false, this);
	}
	CurrentSelectedSelectable = NewSelectedSelectable;
	if (IsValid(NewSelectedSelectable))
	{
		NewSelectedSelectable->SetSelectedFromSelection(true, this);
		BeginInteractionCellPresentation(NewSelectedSelectable);
	}
	AActor* NewActor = IsValid(NewSelectedSelectable) ? NewSelectedSelectable->GetOwner() : nullptr;
	OnSelectedActorChanged.Broadcast(PreviousActor, NewActor);

	if (bEnableDebug)
	{
		PARADOX_LOG_INFO(
			TEXT("Selection owner '%s' selection changed from '%s' to '%s'."),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(PreviousActor),
			*GetNameSafe(NewActor));
	}
}

void UParadoxSelectionComponent::HandleSelectableEndingPlay(
	UParadoxSelectableComponent* Selectable)
{
	if (CurrentHoveredSelectable.Get() == Selectable)
	{
		SetHoveredSelectable(nullptr);
	}
	if (CurrentSelectedSelectable.Get() == Selectable)
	{
		SetSelectedSelectable(nullptr);
	}
}

void UParadoxSelectionComponent::BeginInteractionCellPresentation(
	UParadoxSelectableComponent* SelectedSelectable)
{
	if (!IsValid(SelectedSelectable))
	{
		return;
	}
	AActor* SelectedActor = SelectedSelectable->GetOwner();
	UParadoxInteractionComponent* Interaction = IsValid(SelectedActor)
		? SelectedActor->FindComponentByClass<UParadoxInteractionComponent>()
		: nullptr;
	if (!IsValid(Interaction))
	{
		return;
	}

	SelectedInteractionComponent = Interaction;
	InteractionAffordanceChangedHandle =
		Interaction->OnInteractionAffordanceChangedNative().AddUObject(
			this,
			&ThisClass::HandleInteractionAffordanceChanged);
	ReconcileTrafficReservationBinding();
	const APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
	AActor* Requester = PlayerController ? PlayerController->GetPawn() : nullptr;
	BoundRequesterActionComponent = IsValid(Requester)
		? Requester->FindComponentByClass<UGameplayActionComponent>()
		: nullptr;
	if (UGameplayActionComponent* Actions = BoundRequesterActionComponent.Get())
	{
		Actions->OnActionEvent.AddUniqueDynamic(
			this,
			&ThisClass::HandleRequesterActionEvent);
	}
	RefreshInteractionCellPresentation();
}

void UParadoxSelectionComponent::EndInteractionCellPresentation()
{
	if (UParadoxInteractionComponent* Interaction = SelectedInteractionComponent.Get())
	{
		Interaction->OnInteractionAffordanceChangedNative().Remove(
			InteractionAffordanceChangedHandle);
	}
	InteractionAffordanceChangedHandle.Reset();
	SelectedInteractionComponent.Reset();
	CachedSelectedInteractionOptions = FParadoxInteractionQueryResult();
	CachedInteractionAvailabilityByTag.Reset();
	SelectedInteractionOptionsRefreshedNative.Broadcast(
		CachedSelectedInteractionOptions);
	if (UGameplayActionComponent* Actions = BoundRequesterActionComponent.Get())
	{
		Actions->OnActionEvent.RemoveDynamic(
			this,
			&ThisClass::HandleRequesterActionEvent);
	}
	BoundRequesterActionComponent.Reset();

	if (AGridNavigationData* NavigationData = BoundGridNavigationData.Get())
	{
		NavigationData->OnTrafficReservationsChanged().Remove(
			TrafficReservationsChangedHandle);
	}
	TrafficReservationsChangedHandle.Reset();
	BoundGridNavigationData.Reset();

	if (InteractionCellPresentationHandle.IsSet())
	{
		if (UGridCellOverlayPresentationSubsystem* Presentation = GetWorld()
			? GetWorld()->GetSubsystem<UGridCellOverlayPresentationSubsystem>()
			: nullptr)
		{
			Presentation->ReleaseCellOverlayPresentation(
				InteractionCellPresentationHandle);
		}
		InteractionCellPresentationHandle = FGridCellOverlayPresentationHandle();
	}
}

void UParadoxSelectionComponent::RefreshInteractionCellPresentation()
{
	if (bRefreshingInteractionCells)
	{
		return;
	}
	TGuardValue<bool> RefreshGuard(bRefreshingInteractionCells, true);
	ReconcileTrafficReservationBinding();

	UParadoxInteractionComponent* Interaction = SelectedInteractionComponent.Get();
	const APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
	const AParadoxPlayerController* ParadoxPlayerController =
		Cast<AParadoxPlayerController>(PlayerController);
	AActor* Requester = PlayerController ? PlayerController->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	UGridCellOverlayPresentationSubsystem* Presentation = World
		? World->GetSubsystem<UGridCellOverlayPresentationSubsystem>()
		: nullptr;
	if (!IsValid(Interaction) || !IsValid(Requester) || !Presentation)
	{
		CachedSelectedInteractionOptions = FParadoxInteractionQueryResult();
		CachedSelectedInteractionOptions.Status = !IsValid(Requester)
			? EParadoxInteractionQueryStatus::InvalidRequester
			: EParadoxInteractionQueryStatus::NoOptions;
		CachedSelectedInteractionOptions.DiagnosticMessage =
			TEXT("The selected interaction context is not currently queryable.");
		CachedInteractionAvailabilityByTag.Reset();
		SelectedInteractionOptionsRefreshedNative.Broadcast(
			CachedSelectedInteractionOptions);
		if (Presentation && InteractionCellPresentationHandle.IsSet())
		{
			Presentation->ClearCellOverlayPresentation(
				InteractionCellPresentationHandle);
		}
		return;
	}

	CachedSelectedInteractionOptions =
		Interaction->QueryInteractionOptions(Requester);
	CachedInteractionAvailabilityByTag.Reset();
	for (const FParadoxInteractionDefinition& Definition : Interaction->InteractionDefinitions)
	{
		if (Definition.InteractionTag.IsValid()
			&& !CachedInteractionAvailabilityByTag.Contains(Definition.InteractionTag))
		{
			CachedInteractionAvailabilityByTag.Add(
				Definition.InteractionTag,
				Interaction->EvaluateInteractionAvailability(Requester, Definition.InteractionTag));
		}
	}
	SelectedInteractionOptionsRefreshedNative.Broadcast(
		CachedSelectedInteractionOptions);
	const UParadoxSelectableComponent* SelectedSelectable =
		CurrentSelectedSelectable.Get();
	if (!IsValid(SelectedSelectable)
		|| !SelectedSelectable->bShowInteractionCellsWhenSelected)
	{
		if (InteractionCellPresentationHandle.IsSet())
		{
			Presentation->ClearCellOverlayPresentation(
				InteractionCellPresentationHandle);
		}
		return;
	}

	TMap<FGridCellId, EGridCellOverlayVisualState> CellStates;
	for (const FParadoxInteractionOption& Option :
		CachedSelectedInteractionOptions.Options)
	{
		if (!Option.GridCellId.IsValid()
			|| Option.State == EParadoxInteractionOptionState::GridUnresolved)
		{
			continue;
		}
		const EGridCellOverlayVisualState NewState =
			Option.State == EParadoxInteractionOptionState::Free
				? EGridCellOverlayVisualState::Primary
				: EGridCellOverlayVisualState::Secondary;
		EGridCellOverlayVisualState& Resolved =
			CellStates.FindOrAdd(Option.GridCellId, NewState);
		if (NewState == EGridCellOverlayVisualState::Primary)
		{
			Resolved = EGridCellOverlayVisualState::Primary;
		}
	}

	TArray<FGridCellOverlayEntry> Entries;
	Entries.Reserve(CellStates.Num());
	for (const TPair<FGridCellId, EGridCellOverlayVisualState>& Pair : CellStates)
	{
		FGridCellOverlayEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.CellId = Pair.Key;
		Entry.State = Pair.Value;
	}
	Entries.Sort(
		[](const FGridCellOverlayEntry& Left, const FGridCellOverlayEntry& Right)
		{
			if (Left.CellId.GridId != Right.CellId.GridId)
			{
				return Left.CellId.GridId < Right.CellId.GridId;
			}
			return Left.CellId.Coord < Right.CellId.Coord;
		});

	if (Entries.IsEmpty())
	{
		if (InteractionCellPresentationHandle.IsSet())
		{
			Presentation->ClearCellOverlayPresentation(
				InteractionCellPresentationHandle);
		}
		return;
	}
	if (UGridRuntimeVisualizationSubsystem* Visualization =
		World->GetSubsystem<UGridRuntimeVisualizationSubsystem>())
	{
		Visualization->EnableVisualization(
			ParadoxPlayerController
				? ParadoxPlayerController->GetRuntimeGridCellVisualStyle()
				: nullptr);
	}
	if (InteractionCellPresentationHandle.IsSet())
	{
		if (!Presentation->UpdateCellOverlayPresentation(
			InteractionCellPresentationHandle,
			Entries))
		{
			InteractionCellPresentationHandle = FGridCellOverlayPresentationHandle();
		}
	}
	if (!InteractionCellPresentationHandle.IsSet())
	{
		FGridCellOverlayPresentationRequest Request;
		Request.Owner = this;
		Request.Entries = MoveTemp(Entries);
		Request.Priority = 100;
		Presentation->CreateCellOverlayPresentation(
			Request,
			InteractionCellPresentationHandle);
	}
}

void UParadoxSelectionComponent::ReconcileTrafficReservationBinding()
{
	AGridNavigationData* DesiredNavigationData = nullptr;
	if (SelectedInteractionComponent.IsValid())
	{
		if (UGridWorldSubsystem* GridWorld = GetWorld()
			? GetWorld()->GetSubsystem<UGridWorldSubsystem>()
			: nullptr)
		{
			DesiredNavigationData = GridWorld->GetNavigationData();
		}
	}
	if (BoundGridNavigationData.Get() == DesiredNavigationData)
	{
		return;
	}
	if (AGridNavigationData* PreviousNavigationData = BoundGridNavigationData.Get())
	{
		PreviousNavigationData->OnTrafficReservationsChanged().Remove(
			TrafficReservationsChangedHandle);
	}
	TrafficReservationsChangedHandle.Reset();
	BoundGridNavigationData = DesiredNavigationData;
	if (DesiredNavigationData)
	{
		TrafficReservationsChangedHandle =
			DesiredNavigationData->OnTrafficReservationsChanged().AddUObject(
				this,
				&ThisClass::HandleTrafficReservationsChanged);
	}
}

void UParadoxSelectionComponent::HandleInteractionAffordanceChanged(
	UParadoxInteractionComponent* InteractionComponent)
{
	if (InteractionComponent == SelectedInteractionComponent.Get())
	{
		RefreshInteractionCellPresentation();
	}
}

void UParadoxSelectionComponent::HandleGridWorldChanged(
	const FGridChangeSet& ChangeSet)
{
	(void)ChangeSet;
	if (SelectedInteractionComponent.IsValid())
	{
		RefreshInteractionCellPresentation();
	}
}

void UParadoxSelectionComponent::HandleTrafficReservationsChanged()
{
	RefreshInteractionCellPresentation();
}

void UParadoxSelectionComponent::HandleRequesterActionEvent(const FGameplayActionEvent& Event)
{
	(void)Event;
	RefreshInteractionCellPresentation();
}

void UParadoxSelectionComponent::HandleWorldStateRestoreStarted(
	const FWorldStateRestoreLifecycleContext& Context)
{
	(void)Context;
	ResetSelectionState();
}
