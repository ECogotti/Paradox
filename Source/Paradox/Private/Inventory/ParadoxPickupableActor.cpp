#include "Inventory/ParadoxPickupableActor.h"

#include "Actions/GameplayActionDefinition.h"
#include "Characters/ParadoxCharacter.h"
#include "Components/GridNavigationModifierComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Engine/World.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Interaction/ParadoxSelectionComponent.h"
#include "Inventory/ParadoxPickupableAction.h"
#include "Paradox.h"
#include "Puzzles/PressurePlate.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "UObject/ConstructorHelpers.h"

AParadoxPickupableActor::AParadoxPickupableActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	PickupableMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupableMesh"));
	PickupableMesh->SetupAttachment(SceneRoot);
	PickupableMesh->SetSimulatePhysics(false);
	PickupableMesh->SetEnableGravity(false);
	PickupableMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupableMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupableMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PickupableMesh->SetGenerateOverlapEvents(false);
	PickupableMesh->SetCanEverAffectNavigation(false);
	SelectableComponent = CreateDefaultSubobject<UParadoxSelectableComponent>(TEXT("SelectableComponent"));
	SelectableComponent->bShowInteractionCellsWhenSelected = true;
	SmartObjectComponent = CreateDefaultSubobject<USmartObjectComponent>(TEXT("SmartObjectComponent"));
	SmartObjectComponent->SetupAttachment(SceneRoot);
	InteractionComponent = CreateDefaultSubobject<UParadoxInteractionComponent>(TEXT("InteractionComponent"));
	OccupancyComponent = CreateDefaultSubobject<UGridNavigationOccupancyComponent>(TEXT("OccupancyComponent"));
	OccupancyComponent->SetupAttachment(SceneRoot);
	OccupancyComponent->BoxExtent = FVector(25.0, 25.0, 50.0);
	OccupancyComponent->bBlocksWhenConsidered = false;
	OccupancyComponent->AdditionalCost = 1000;
	OccupancyComponent->bIsReservation = false;
	OccupancyComponent->bAutoActivate = false;
	GridNavigationModifierComponent = CreateDefaultSubobject<UGridNavigationModifierComponent>(
		TEXT("GridNavigationModifierComponent"));
	GridNavigationModifierComponent->SetupAttachment(SceneRoot);
	GridNavigationModifierComponent->SetMobility(EComponentMobility::Movable);
	GridNavigationModifierComponent->BoxExtent = OccupancyComponent->BoxExtent;
	GridNavigationModifierComponent->SetRelativeTransform(OccupancyComponent->GetRelativeTransform());
	GridNavigationModifierComponent->bBlockCells = false;
	GridNavigationModifierComponent->bAutoActivate = true;
	WorldStateParticipantComponent = CreateDefaultSubobject<UWorldStateParticipantComponent>(TEXT("WorldStateParticipantComponent"));
	WorldStateParticipantComponent->bCaptureExistence = true;
	WorldStateParticipantComponent->bCaptureActorTransform = true;
	WorldStateParticipantComponent->ExistencePolicy = EWorldStateExistencePolicy::RespawnAndDestroy;

	static ConstructorHelpers::FObjectFinder<USmartObjectDefinition> SmartObjectDefinitionFinder(
		TEXT("/Game/Data/Inventory/DA_ParadoxPickupableSmartObject.DA_ParadoxPickupableSmartObject"));
	if (SmartObjectDefinitionFinder.Succeeded())
	{
		SmartObjectComponent->SetDefinition(SmartObjectDefinitionFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UGameplayActionDefinition> PickupDefinitionFinder(
		TEXT("/Game/Data/GameplayActions/DA_ParadoxPickupInteraction.DA_ParadoxPickupInteraction"));
	if (PickupDefinitionFinder.Succeeded())
	{
		FParadoxInteractionDefinition& PickupDefinition =
			InteractionComponent->InteractionDefinitions.AddDefaulted_GetRef();
		PickupDefinition.InteractionTag = ParadoxGameplayTags::Interaction_Inventory_Pickup;
		PickupDefinition.GameplayActionDefinition = PickupDefinitionFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UGameplayActionDefinition> SwapDefinitionFinder(
		TEXT("/Game/Data/GameplayActions/DA_ParadoxSwapInteraction.DA_ParadoxSwapInteraction"));
	if (SwapDefinitionFinder.Succeeded())
	{
		FParadoxInteractionDefinition& SwapDefinition =
			InteractionComponent->InteractionDefinitions.AddDefaulted_GetRef();
		SwapDefinition.InteractionTag = ParadoxGameplayTags::Interaction_Inventory_Swap;
		SwapDefinition.GameplayActionDefinition = SwapDefinitionFinder.Object;
	}
}

void AParadoxPickupableActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	EnforceNonBlockingPresence();
}

#if WITH_EDITOR
void AParadoxPickupableActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	EnforceNonBlockingPresence();
}
#endif

TArray<UParadoxPickupableAction*> AParadoxPickupableActor::GetPickupableActions() const
{
	TArray<UParadoxPickupableAction*> Result;
	Result.Reserve(PickupableActions.Num());
	for (UParadoxPickupableAction* Action : PickupableActions)
	{
		if (IsValid(Action))
		{
			Result.Add(Action);
		}
	}
	return Result;
}

FText AParadoxPickupableActor::GetPickupableDisplayName() const
{
	return PickupableDisplayName.IsEmpty()
		? FText::FromString(GetName())
		: PickupableDisplayName;
}

void AParadoxPickupableActor::SetPickupableActions(
	const TArray<UParadoxPickupableAction*>& NewActions)
{
	TArray<TObjectPtr<UParadoxPickupableAction>> FilteredActions;
	FilteredActions.Reserve(NewActions.Num());
	for (UParadoxPickupableAction* Action : NewActions)
	{
		if (IsValid(Action) && !FilteredActions.Contains(Action))
		{
			FilteredActions.Add(Action);
		}
	}
	if (PickupableActions == FilteredActions)
	{
		return;
	}
	PickupableActions = MoveTemp(FilteredActions);
	NotifyPickupableActionsChanged();
}

void AParadoxPickupableActor::NotifyPickupableActionsChanged()
{
	OnPickupableActionsChanged.Broadcast(this);
}

void AParadoxPickupableActor::BeginPlay()
{
	Super::BeginPlay();
	CaptureInitialWorldPresentation();
	EnforceNonBlockingPresence();
	if (UWorldStateSubsystem* WorldState = GetWorld() ? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr)
	{
		WorldState->OnRestoreStartedNative().AddUObject(this, &ThisClass::HandleWorldStateRestoreStarted);
		WorldState->OnRestoreCompletedNative().AddUObject(this, &ThisClass::HandleWorldStateRestoreCompleted);
		WorldState->OnRestoreFailedNative().AddUObject(this, &ThisClass::HandleWorldStateRestoreFailed);
	}
	RestoreWorldPresence();
	LogDebugState(TEXT("BeginPlay"));
}

void AParadoxPickupableActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorldStateSubsystem* WorldState = GetWorld() ? GetWorld()->GetSubsystem<UWorldStateSubsystem>() : nullptr)
	{
		WorldState->OnRestoreStartedNative().RemoveAll(this);
		WorldState->OnRestoreCompletedNative().RemoveAll(this);
		WorldState->OnRestoreFailedNative().RemoveAll(this);
	}
	CurrentHolder.Reset();
	Super::EndPlay(EndPlayReason);
}

void AParadoxPickupableActor::CaptureInitialWorldPresentation()
{
	bInitialActorHidden = IsHidden();
	bInitialSelectableCanHover = SelectableComponent ? SelectableComponent->bCanBeHovered : true;
	bInitialSelectableCanSelect = SelectableComponent ? SelectableComponent->bCanBeSelected : true;
	bInitialSmartObjectEnabled = SmartObjectComponent ? SmartObjectComponent->IsSmartObjectEnabled() : true;
	bInitialOccupancyEnabled = OccupancyComponent ? OccupancyComponent->IsActive() : true;
	if (!bInitialPrimitivePresenceCaptured)
	{
		CaptureInitialPrimitivePresence();
	}
	bInitialPresentationCaptured = true;
}

void AParadoxPickupableActor::CaptureInitialPrimitivePresence()
{
	bInitialActorCollisionEnabled = GetActorEnableCollision();
	InitialPrimitivePresenceStates.Reset();
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!IsValid(Primitive))
		{
			continue;
		}
		FInitialPrimitivePresenceState& PrimitiveState =
			InitialPrimitivePresenceStates.Add(Primitive);
		PrimitiveState.CollisionEnabled = Primitive->GetCollisionEnabled();
		PrimitiveState.CollisionResponses = Primitive->GetCollisionResponseToChannels();
	}
	bInitialPrimitivePresenceCaptured = true;
}

bool AParadoxPickupableActor::ShouldUseAuthoredCollisionForCurrentState() const
{
	return PickupableState == EParadoxPickupableState::World
		&& bUseAuthoredWorldCollision;
}

bool AParadoxPickupableActor::ShouldUseAuthoredNavigationForCurrentState() const
{
	return PickupableState == EParadoxPickupableState::World
		&& bUseAuthoredNavigationInfluence;
}

bool AParadoxPickupableActor::ShouldPreserveAuthoredCollisionConfiguration() const
{
	return bUseAuthoredWorldCollision;
}

bool AParadoxPickupableActor::ShouldPreserveAuthoredNavigationConfiguration() const
{
	return bUseAuthoredNavigationInfluence;
}

void AParadoxPickupableActor::EnforceNonBlockingPresence()
{
	const bool bIsAvailableInWorld = PickupableState == EParadoxPickupableState::World;
	const bool bUseAuthoredCollision = ShouldUseAuthoredCollisionForCurrentState();
	const bool bUseAuthoredNavigation = ShouldUseAuthoredNavigationForCurrentState();
	if (!bInitialPrimitivePresenceCaptured
		&& ShouldPreserveAuthoredCollisionConfiguration())
	{
		CaptureInitialPrimitivePresence();
	}
	if (bUseAuthoredCollision)
	{
		if (bInitialPrimitivePresenceCaptured)
		{
			SetActorEnableCollision(bInitialActorCollisionEnabled);
		}
	}
	else
	{
		SetActorEnableCollision(bIsAvailableInWorld);
	}

	if (OccupancyComponent && !ShouldPreserveAuthoredNavigationConfiguration())
	{
		// Pickupables may influence path scoring, but never make a cell impassable or reserved.
		OccupancyComponent->bBlocksWhenConsidered = false;
		OccupancyComponent->AdditionalCost = FMath::Max(1, OccupancyComponent->AdditionalCost);
		OccupancyComponent->bIsReservation = false;
	}
	if (OccupancyComponent)
	{
		const bool bShouldPublishOccupancy = bUseAuthoredNavigation
			&& (!bInitialPresentationCaptured || bInitialOccupancyEnabled);
		OccupancyComponent->SetOccupancyEnabled(bShouldPublishOccupancy);
	}

	TArray<UPrimitiveComponent*> Primitives;
	GetComponents(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!IsValid(Primitive))
		{
			continue;
		}
		Primitive->SetSimulatePhysics(false);
		Primitive->SetEnableGravity(false);

		if (SelectableComponent
			&& Primitive == SelectableComponent->GetInteractionWidget())
		{
			// Selection owns this query-only UI surface. It switches between the UI
			// profile while shown and NoCollision while hidden. Preserve that state
			// so pickupable normalization cannot disable world-widget hover.
			Primitive->SetGenerateOverlapEvents(false);
			Primitive->SetCanEverAffectNavigation(false);
			continue;
		}

		const FInitialPrimitivePresenceState* InitialState =
			bInitialPresentationCaptured
				? InitialPrimitivePresenceStates.Find(Primitive)
				: nullptr;
		Primitive->SetCanEverAffectNavigation(
			bUseAuthoredNavigation);

		if (bUseAuthoredCollision)
		{
			if (InitialState)
			{
				Primitive->SetCollisionEnabled(InitialState->CollisionEnabled);
				Primitive->SetCollisionResponseToChannels(
					InitialState->CollisionResponses);
				Primitive->SetGenerateOverlapEvents(
					CollisionEnabledHasQuery(InitialState->CollisionEnabled));
			}
			continue;
		}

		Primitive->SetGenerateOverlapEvents(false);
		Primitive->SetCollisionResponseToAllChannels(ECR_Ignore);
		if (bIsAvailableInWorld && Primitive == PickupableMesh.Get())
		{
			// World pickupables remain non-blocking but must be hittable by the shared
			// cursor Visibility query that owns hover and selection.
			Primitive->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Primitive->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}
		else
		{
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	SynchronizeNavigationModifierBounds();
	if (GridNavigationModifierComponent)
	{
		GridNavigationModifierComponent->SetBlockingEnabled(
			bUseAuthoredNavigation);
	}
}

void AParadoxPickupableActor::SynchronizeNavigationModifierBounds()
{
	if (!OccupancyComponent || !GridNavigationModifierComponent)
	{
		return;
	}

	bool bBoundsChanged = false;
	const FTransform OccupancyRelativeTransform = OccupancyComponent->GetRelativeTransform();
	if (!GridNavigationModifierComponent->GetRelativeTransform().Equals(OccupancyRelativeTransform))
	{
		GridNavigationModifierComponent->SetRelativeTransform(OccupancyRelativeTransform);
		bBoundsChanged = true;
	}
	const FVector OccupancyExtent = OccupancyComponent->BoxExtent.GetAbs();
	if (!GridNavigationModifierComponent->BoxExtent.Equals(OccupancyExtent))
	{
		GridNavigationModifierComponent->BoxExtent = OccupancyExtent;
		bBoundsChanged = true;
	}
	if (bBoundsChanged)
	{
		GridNavigationModifierComponent->RefreshModifier();
	}
}

void AParadoxPickupableActor::RefreshConfiguredCollisionOverlaps()
{
	if (!ShouldUseAuthoredCollisionForCurrentState())
	{
		return;
	}

	TArray<UPrimitiveComponent*> Primitives;
	GetComponents(Primitives);
	TSet<APressurePlate*> OverlappingPressurePlates;
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (IsValid(Primitive)
			&& Primitive->GetGenerateOverlapEvents()
			&& CollisionEnabledHasQuery(Primitive->GetCollisionEnabled()))
		{
			Primitive->UpdateOverlaps(nullptr, true);
			TArray<UPrimitiveComponent*> OverlappingComponents;
			Primitive->GetOverlappingComponents(OverlappingComponents);
			for (UPrimitiveComponent* OtherComponent : OverlappingComponents)
			{
				if (IsValid(OtherComponent))
				{
					if (APressurePlate* PressurePlate = Cast<APressurePlate>(OtherComponent->GetOwner()))
					{
						OverlappingPressurePlates.Add(PressurePlate);
					}
				}
			}
		}
	}

	// Enabling query collision after a teleport can populate Unreal's overlap cache without
	// producing a BeginOverlap edge. Pressure Plates expose an explicit authoritative resync for
	// exactly this case, so reconcile every physically overlapping plate once after final Drop.
	for (APressurePlate* PressurePlate : OverlappingPressurePlates)
	{
		if (IsValid(PressurePlate))
		{
			PressurePlate->RefreshOccupantFromVolume();
		}
	}
}

void AParadoxPickupableActor::RefreshPresenceAfterPlacement()
{
	EnforceNonBlockingPresence();
	RefreshConfiguredCollisionOverlaps();
}

void AParadoxPickupableActor::ApplyHeldWorldPresence()
{
	ApplyUnavailableWorldPresence(bHideWhileHeld);
}

void AParadoxPickupableActor::ApplyUnavailableWorldPresence(const bool bHideActor)
{
	if (!bInitialPresentationCaptured)
	{
		CaptureInitialWorldPresentation();
	}
	ClearSelectionPresentation();
	if (SelectableComponent)
	{
		SelectableComponent->bCanBeHovered = false;
		SelectableComponent->bCanBeSelected = false;
	}
	if (SmartObjectComponent)
	{
		SmartObjectComponent->K2_SetSmartObjectEnabled(false);
	}
	if (OccupancyComponent)
	{
		OccupancyComponent->SetOccupancyEnabled(false);
	}
	EnforceNonBlockingPresence();
	SetActorHiddenInGame(bHideActor ? true : bInitialActorHidden);
}

void AParadoxPickupableActor::RestoreWorldPresence()
{
	if (!bInitialPresentationCaptured)
	{
		return;
	}
	SetActorHiddenInGame(bInitialActorHidden);
	if (SelectableComponent)
	{
		SelectableComponent->bCanBeHovered = bInitialSelectableCanHover;
		SelectableComponent->bCanBeSelected = bInitialSelectableCanSelect;
	}
	if (SmartObjectComponent)
	{
		SmartObjectComponent->K2_SetSmartObjectEnabled(bInitialSmartObjectEnabled);
	}
	EnforceNonBlockingPresence();
}

void AParadoxPickupableActor::ClearSelectionPresentation()
{
	AParadoxPlayerController* Controller = GetWorld()
		? Cast<AParadoxPlayerController>(GetWorld()->GetFirstPlayerController())
		: nullptr;
	if (Controller && Controller->GetSelectionComponent()
		&& Controller->GetSelectionComponent()->GetSelectedActor() == this)
	{
		Controller->GetSelectionComponent()->DeselectCurrentActor();
	}
}

void AParadoxPickupableActor::SetHeldStateNative(AParadoxCharacter& NewHolder, const bool bNotify)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	CurrentHolder = &NewHolder;
	PickupableState = EParadoxPickupableState::Held;
	ApplyHeldWorldPresence();
	if (bNotify)
	{
		ReceivePickedUp(&NewHolder);
	}
	EnforceNonBlockingPresence();
	LogDebugState(TEXT("Held"));
}

void AParadoxPickupableActor::SetExternallyOwnedStateNative(
	const EParadoxPickupableState NewState,
	const bool bHideActor)
{
	CurrentHolder.Reset();
	PickupableState = NewState;
	ApplyUnavailableWorldPresence(bHideActor);
	EnforceNonBlockingPresence();
}

void AParadoxPickupableActor::PrepareExternalOwnershipForWorldStateRestore()
{
}

bool AParadoxPickupableActor::RestoreExternalOwnershipAfterWorldState()
{
	return false;
}

void AParadoxPickupableActor::SetWorldStateNative(
	const FTransform& WorldTransform,
	AParadoxCharacter* PreviousHolder,
	const bool bNotifyDrop)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorTransform(WorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
	CurrentHolder.Reset();
	PickupableState = EParadoxPickupableState::World;
	RestoreWorldPresence();
	if (OccupancyComponent)
	{
		OccupancyComponent->RefreshOccupancy();
	}
	if (bNotifyDrop)
	{
		ReceiveDropped(PreviousHolder);
	}
	EnforceNonBlockingPresence();
	RefreshConfiguredCollisionOverlaps();
	LogDebugState(TEXT("World"));
}

void AParadoxPickupableActor::PrepareForWorldStateRestore()
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	CurrentHolder.Reset();
	PrepareExternalOwnershipForWorldStateRestore();
	PickupableState = EParadoxPickupableState::RestorePending;
	ApplyHeldWorldPresence();
	LogDebugState(TEXT("RestoreStarted"));
}

void AParadoxPickupableActor::FinishWorldStateRestore(const bool bRestoreSucceeded)
{
	CurrentHolder.Reset();
	if (RestoreExternalOwnershipAfterWorldState())
	{
		if (bRestoreSucceeded)
		{
			ReceiveReturnedToInitialState();
		}
		EnforceNonBlockingPresence();
		LogDebugState(TEXT("RestoreCompletedExternalOwner"));
		return;
	}
	PickupableState = EParadoxPickupableState::World;
	RestoreWorldPresence();
	if (OccupancyComponent)
	{
		OccupancyComponent->RefreshOccupancy();
	}
	if (bRestoreSucceeded)
	{
		ReceiveReturnedToInitialState();
	}
	EnforceNonBlockingPresence();
	RefreshConfiguredCollisionOverlaps();
	LogDebugState(bRestoreSucceeded ? TEXT("RestoreCompleted") : TEXT("RestoreFailed"));
}

FTransform AParadoxPickupableActor::GetDropPlacementTransform(const FVector& CellWorldCenter) const
{
	FTransform Result = GetActorTransform();
	Result.SetLocation(CellWorldCenter + DropPlacementOffset);
	return Result;
}

void AParadoxPickupableActor::LogDebugState(const TCHAR* EventName) const
{
	if (!bEnableDebug || !IsParadoxInventoryDebugEnabled())
	{
		return;
	}
	PARADOX_LOG_INFO(
		TEXT("Inventory pickupable event=%s actor=%s state=%d holder=%s actions=%d effects=%d"),
		EventName,
		*GetNameSafe(this),
		static_cast<int32>(PickupableState),
		*GetNameSafe(CurrentHolder.Get()),
		PickupableActions.Num(),
		PassiveEffects.Num());
}

void AParadoxPickupableActor::HandleWorldStateRestoreStarted(
	const FWorldStateRestoreLifecycleContext& Context)
{
	(void)Context;
	PrepareForWorldStateRestore();
}

void AParadoxPickupableActor::HandleWorldStateRestoreCompleted(
	const FWorldStateRestoreResult& Result)
{
	(void)Result;
	FinishWorldStateRestore(true);
}

void AParadoxPickupableActor::HandleWorldStateRestoreFailed(
	const FWorldStateRestoreResult& Result)
{
	(void)Result;
	PARADOX_LOG_ERROR(
		TEXT("World State restore failed while pickupable '%s' was reconciling inventory state."),
		*GetNameSafe(this));
	FinishWorldStateRestore(false);
}
