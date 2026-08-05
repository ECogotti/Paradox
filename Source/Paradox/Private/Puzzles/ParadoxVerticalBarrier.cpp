#include "Puzzles/ParadoxVerticalBarrier.h"

#include "Characters/ParadoxCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationModifierComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameplayActionTags.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Paradox.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Sound/SoundBase.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "TimerManager.h"
#include "Types/PerceptionKnowledgeTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ParadoxVerticalBarrier"

AParadoxVerticalBarrier::AParadoxVerticalBarrier()
{
	MovementMode = EPuzzleTransformMoverMode::PingPong;
	DeactivationBehavior = EPuzzleTransformMoverDeactivationBehavior::Return;
	InitialPosition = EPuzzleTransformMoverInitialPosition::Start;
	bAnimateInitialReceiverState = false;
	TimingMode = EPuzzleTransformMoverTimingMode::MovementTime;
	ForwardMovementTime = 1.0f;
	bWaitForClearPassage = true;
	BillboardRoot->SetMobility(EComponentMobility::Static);

	BarrierMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrierMesh"));
	BarrierMesh->SetupAttachment(BillboardRoot.Get());
	BarrierMesh->SetMobility(EComponentMobility::Movable);
	BarrierMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	BarrierMesh->SetCanEverAffectNavigation(false);

	MovementAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("MovementAudio"));
	MovementAudio->SetupAttachment(BarrierMesh);
	MovementAudio->SetAutoActivate(false);

	MovementVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("MovementVFX"));
	MovementVFX->SetupAttachment(BarrierMesh);
	MovementVFX->SetAutoActivate(false);

	GridNavigationModifier = CreateDefaultSubobject<UGridNavigationModifierComponent>(TEXT("GridNavigationModifier"));
	GridNavigationModifier->SetupAttachment(BillboardRoot.Get());
	GridNavigationModifier->SetMobility(EComponentMobility::Static);
	GridNavigationModifier->BoxExtent = FVector(75.0f, 75.0f, 120.0f);
	GridNavigationModifier->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	GridNavigationModifier->bBlockCells = true;

	PassageOccupancyVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("PassageOccupancyVolume"));
	PassageOccupancyVolume->SetupAttachment(BillboardRoot.Get());
	PassageOccupancyVolume->SetMobility(EComponentMobility::Static);
	PassageOccupancyVolume->InitBoxExtent(GridNavigationModifier->BoxExtent);
	PassageOccupancyVolume->SetRelativeTransform(GridNavigationModifier->GetRelativeTransform());
	PassageOccupancyVolume->SetCollisionProfileName(TEXT("Trigger"));
	PassageOccupancyVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PassageOccupancyVolume->SetGenerateOverlapEvents(true);
	PassageOccupancyVolume->SetSimulatePhysics(false);
	PassageOccupancyVolume->SetCanEverAffectNavigation(false);
	PassageOccupancyVolume->bDynamicObstacle = false;

	WorldStateParticipant = CreateDefaultSubobject<UWorldStateParticipantComponent>(TEXT("WorldStateParticipant"));
	WorldStateParticipant->bCaptureExistence = false;
	WorldStateParticipant->bCaptureActorTransform = false;
	WorldStateParticipant->bCaptureAttachment = false;
	WorldStateParticipant->ExistencePolicy = EWorldStateExistencePolicy::ExistingOnly;
	FWorldStatePropertySelection& MoverSelection = WorldStateParticipant->CapturedProperties.AddDefaulted_GetRef();
	MoverSelection.CaptureSourceId = FWorldStateCaptureSourceId::OwnerActor();
	MoverSelection.PropertyName = GET_MEMBER_NAME_CHECKED(AParadoxVerticalBarrier, WorldStateMoverRuntimeState);

	PerceptionSource = CreateDefaultSubobject<UPerceptionKnowledgeSourceComponent>(TEXT("PerceptionSource"));

	DefaultMovedComponent.ComponentProperty = GET_MEMBER_NAME_CHECKED(AParadoxVerticalBarrier, BarrierMesh);
	if (EndArrow)
	{
		EndArrow->SetRelativeLocation(FVector(0.0f, 0.0f, -240.0f));
	}
}

void AParadoxVerticalBarrier::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SynchronizePassageBounds();
	EnforceComponentInvariants();
	if (BarrierMesh && StartArrow && EndArrow && GetWorld() && !GetWorld()->IsGameWorld())
	{
		BarrierMesh->SetWorldTransform(
			InitialPosition == EPuzzleTransformMoverInitialPosition::End
				? EndArrow->GetComponentTransform()
				: StartArrow->GetComponentTransform());
	}
	if (GridNavigationModifier && GetWorld() && !GetWorld()->IsGameWorld())
	{
		const bool bShouldBlock = InitialPosition != EPuzzleTransformMoverInitialPosition::End;
		bPassageBlockingNavigation = bShouldBlock;
		GridNavigationModifier->SetBlockingEnabled(bShouldBlock);
	}
}

void AParadoxVerticalBarrier::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	SynchronizePassageBounds();
	EnforceComponentInvariants();
}

void AParadoxVerticalBarrier::BeginPlay()
{
	bSuppressPresentation = true;
	bBarrierInitialized = true;
	SynchronizePassageBounds();
	EnforceComponentInvariants();

	if (PassageOccupancyVolume)
	{
		PassageOccupancyVolume->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::HandlePassageBeginOverlap);
		PassageOccupancyVolume->OnComponentEndOverlap.AddUniqueDynamic(this, &ThisClass::HandlePassageEndOverlap);
		bOverlapDelegatesBound = true;
	}
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreCapture.AddUniqueDynamic(this, &ThisClass::HandleWorldStatePreCapture);
		WorldStateParticipant->OnWorldStatePreRestore.AddUniqueDynamic(this, &ThisClass::HandleWorldStatePreRestore);
		WorldStateParticipant->OnWorldStatePropertiesRestored.AddUniqueDynamic(this, &ThisClass::HandleWorldStatePropertiesRestored);
		WorldStateParticipant->OnWorldStateRestored.AddUniqueDynamic(this, &ThisClass::HandleWorldStateParticipantRestored);
		WorldStateParticipant->OnWorldStateRestoreFailed.AddUniqueDynamic(this, &ThisClass::HandleWorldStateParticipantFailed);
	}
	if (UWorldStateSubsystem* WorldState = GetWorld()->GetSubsystem<UWorldStateSubsystem>())
	{
		WorldStateRestoreCompletedHandle = WorldState->OnRestoreCompletedNative().AddUObject(
			this, &ThisClass::HandleWorldStateRestoreCompleted);
		WorldStateRestoreFailedHandle = WorldState->OnRestoreFailedNative().AddUObject(
			this, &ThisClass::HandleWorldStateRestoreFailed);
	}

	DefaultMovementSound = MovementAudio ? MovementAudio->GetSound() : nullptr;
	DefaultMovementNiagaraSystem = MovementVFX ? MovementVFX->GetAsset() : nullptr;
	StopMovementFeedback();
	RefreshPassageOccupants();

	Super::BeginPlay();
	SetMovedComponent(BarrierMesh);
	RebuildDerivedState();
	WorldStateMoverRuntimeState = CaptureRuntimeState();
	bSuppressPresentation = false;
}

void AParadoxVerticalBarrier::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bBarrierInitialized = false;
	bSuppressPresentation = true;
	bApplyingWorldState = true;
	ClearPendingRaiseRetry();
	bRaiseRequestPending = false;

	if (bOverlapDelegatesBound && PassageOccupancyVolume)
	{
		PassageOccupancyVolume->OnComponentBeginOverlap.RemoveDynamic(this, &ThisClass::HandlePassageBeginOverlap);
		PassageOccupancyVolume->OnComponentEndOverlap.RemoveDynamic(this, &ThisClass::HandlePassageEndOverlap);
		bOverlapDelegatesBound = false;
	}
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreCapture.RemoveDynamic(this, &ThisClass::HandleWorldStatePreCapture);
		WorldStateParticipant->OnWorldStatePreRestore.RemoveDynamic(this, &ThisClass::HandleWorldStatePreRestore);
		WorldStateParticipant->OnWorldStatePropertiesRestored.RemoveDynamic(this, &ThisClass::HandleWorldStatePropertiesRestored);
		WorldStateParticipant->OnWorldStateRestored.RemoveDynamic(this, &ThisClass::HandleWorldStateParticipantRestored);
		WorldStateParticipant->OnWorldStateRestoreFailed.RemoveDynamic(this, &ThisClass::HandleWorldStateParticipantFailed);
	}
	if (UWorld* World = GetWorld())
	{
		if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldState->OnRestoreCompletedNative().Remove(WorldStateRestoreCompletedHandle);
			WorldState->OnRestoreFailedNative().Remove(WorldStateRestoreFailedHandle);
		}
	}
	WorldStateRestoreCompletedHandle.Reset();
	WorldStateRestoreFailedHandle.Reset();

	ReleaseAllLiftedActors(EParadoxBarrierPassengerReleaseReason::EndPlay);
	ClearOverlappingActors();
	StopMovementFeedback();
	Super::EndPlay(EndPlayReason);
}

void AParadoxVerticalBarrier::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (ShouldDrawBarrierDebug())
	{
		DrawBarrierDebug();
	}
}

void AParadoxVerticalBarrier::ResetMover()
{
	const bool bPreviousSuppression = bSuppressPresentation;
	bSuppressPresentation = true;
	ClearPendingRaiseRetry();
	bRaiseRequestPending = false;
	bSafetyReturnInProgress = false;
	ReleaseAllLiftedActors(EParadoxBarrierPassengerReleaseReason::Reset);
	ClearOverlappingActors();
	StopMovementFeedback();
	Super::ResetMover();
	RebuildDerivedState();
	if (bBarrierInitialized && !bApplyingWorldState)
	{
		RefreshPassageOccupants();
	}
	bSuppressPresentation = bPreviousSuppression;
}

#if WITH_EDITOR
EDataValidationResult AParadoxVerticalBarrier::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!ShouldValidateMoverData())
	{
		return Result;
	}
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	auto AddError = [&Context, &Result](const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	};

	if (!BarrierMesh || BarrierMesh->GetAttachParent() != BillboardRoot.Get() || BarrierMesh->Mobility != EComponentMobility::Movable)
	{
		AddError(LOCTEXT("BarrierHierarchy", "Vertical Barrier requires a movable BarrierMesh attached to BillboardRoot."));
	}
	if (!PassageOccupancyVolume || PassageOccupancyVolume->GetAttachParent() != BillboardRoot.Get()
		|| !PassageOccupancyVolume->GetGenerateOverlapEvents()
		|| !CollisionEnabledHasQuery(PassageOccupancyVolume->GetCollisionEnabled()))
	{
		AddError(LOCTEXT("OccupancyVolume", "Vertical Barrier requires a query-enabled overlap volume attached to BillboardRoot."));
	}
	if (!GridNavigationModifier || GridNavigationModifier->GetAttachParent() != BillboardRoot.Get())
	{
		AddError(LOCTEXT("GridModifier", "Vertical Barrier requires its native GridNavigationModifier."));
	}
	if (PassageOccupancyVolume && GridNavigationModifier
		&& (!PassageOccupancyVolume->GetUnscaledBoxExtent().Equals(GridNavigationModifier->BoxExtent)
			|| !PassageOccupancyVolume->GetRelativeTransform().Equals(GridNavigationModifier->GetRelativeTransform())))
	{
		AddError(LOCTEXT("BoundsMismatch", "PassageOccupancyVolume must exactly mirror GridNavigationModifier transform and BoxExtent."));
	}
	if (!WorldStateParticipant || !PerceptionSource)
	{
		AddError(LOCTEXT("IntegrationComponents", "Vertical Barrier requires WorldStateParticipant and PerceptionSource."));
	}
	if (!FMath::IsFinite(MovementNoiseLoudness) || MovementNoiseLoudness < 0.0f
		|| !FMath::IsFinite(MovementNoiseMaxRange) || MovementNoiseMaxRange < 0.0f
		|| !FMath::IsFinite(MovementNoiseStrength) || MovementNoiseStrength < 0.0f)
	{
		AddError(LOCTEXT("NoiseValues", "Vertical Barrier noise values must be finite and non-negative."));
	}
	for (const FName RequiredTag : RequiredOccupantActorTags)
	{
		if (RequiredTag.IsNone())
		{
			AddError(LOCTEXT("RequiredActorTag", "RequiredOccupantActorTags cannot contain None."));
			break;
		}
	}
	return Result;
}

void AParadoxVerticalBarrier::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SynchronizePassageBounds();
	EnforceComponentInvariants();
}
#endif

bool AParadoxVerticalBarrier::IsPassageOpen() const
{
	return IsAtEnd() && !IsMovementPaused() && !bPassageBlockingNavigation;
}

bool AParadoxVerticalBarrier::IsPassageBlockingNavigation() const
{
	return bPassageBlockingNavigation;
}

TArray<AActor*> AParadoxVerticalBarrier::GetPassageOccupants() const
{
	TArray<AActor*> Result;
	Result.Reserve(OverlappingActors.Num());
	for (const TPair<TWeakObjectPtr<AActor>, TSet<TWeakObjectPtr<UPrimitiveComponent>>>& Entry : OverlappingActors)
	{
		if (AActor* Actor = Entry.Key.Get())
		{
			Result.Add(Actor);
		}
	}
	return Result;
}

bool AParadoxVerticalBarrier::IsActorBeingLifted(AActor* Actor) const
{
	return IsValid(Actor) && LiftedActors.Contains(Actor);
}

bool AParadoxVerticalBarrier::IsActorOccupyingPassage(AActor* Actor) const
{
	return IsValid(Actor) && OverlappingActors.Contains(Actor);
}

EParadoxBarrierOccupancyRefreshResult AParadoxVerticalBarrier::RefreshPassageOccupants()
{
	if (!bBarrierInitialized)
	{
		return EParadoxBarrierOccupancyRefreshResult::NotInitialized;
	}
	if (!PassageOccupancyVolume)
	{
		return EParadoxBarrierOccupancyRefreshResult::MissingOccupancyVolume;
	}

	PassageOccupancyVolume->UpdateOverlaps(nullptr, false);
	TArray<UPrimitiveComponent*> Components;
	PassageOccupancyVolume->GetOverlappingComponents(Components);
	TMap<TWeakObjectPtr<AActor>, TSet<TWeakObjectPtr<UPrimitiveComponent>>> NewOverlaps;
	for (UPrimitiveComponent* Component : Components)
	{
		AActor* Actor = Component ? Component->GetOwner() : nullptr;
		if (IsOccupantAccepted(Actor, Component))
		{
			NewOverlaps.FindOrAdd(Actor).Add(Component);
		}
	}

	const int32 PreviousCount = OverlappingActors.Num();
	TArray<TWeakObjectPtr<AActor>> RemovedActors;
	for (const TPair<TWeakObjectPtr<AActor>, TSet<TWeakObjectPtr<UPrimitiveComponent>>>& Entry : OverlappingActors)
	{
		if (!NewOverlaps.Contains(Entry.Key))
		{
			RemovedActors.Add(Entry.Key);
		}
	}
	OverlappingActors = MoveTemp(NewOverlaps);
	for (const TWeakObjectPtr<AActor>& RemovedActor : RemovedActors)
	{
		UnbindActorDestroyedIfUnused(RemovedActor.Get());
	}
	for (const TPair<TWeakObjectPtr<AActor>, TSet<TWeakObjectPtr<UPrimitiveComponent>>>& Entry : OverlappingActors)
	{
		BindActorDestroyed(Entry.Key.Get());
	}
	NotifyOccupancyChanged(PreviousCount);
	return EParadoxBarrierOccupancyRefreshResult::Succeeded;
}

bool AParadoxVerticalBarrier::CancelPendingRaiseRequest()
{
	if (!bRaiseRequestPending)
	{
		return false;
	}
	ClearPendingRaiseRetry();
	bRaiseRequestPending = false;
	LastBarrierDiagnostic = TEXT("Pending raise was cancelled by an End command or explicit request.");
	PublishPerceptionState();
	if (!bSuppressPresentation)
	{
		HandlePendingRaiseCancelled();
		OnPendingRaiseCancelled.Broadcast(this);
	}
	return true;
}

bool AParadoxVerticalBarrier::CanActorOccupyPassage_Implementation(
	AActor* OccupantActor,
	UPrimitiveComponent* OccupantComponent) const
{
	return true;
}

void AParadoxVerticalBarrier::HandlePassageOccupancyChanged_Implementation(int32 OccupantCount) {}
void AParadoxVerticalBarrier::HandleRaiseDeferred_Implementation(int32 OccupantCount) {}
void AParadoxVerticalBarrier::HandlePendingRaiseCancelled_Implementation() {}
void AParadoxVerticalBarrier::HandlePassageClearanceRestored_Implementation() {}
void AParadoxVerticalBarrier::HandlePassageNavigationChanged_Implementation(bool bNowNavigable) {}
void AParadoxVerticalBarrier::HandleOccupantLiftStarted_Implementation(AActor* OccupantActor) {}
void AParadoxVerticalBarrier::HandleOccupantLiftFailed_Implementation(AActor* OccupantActor, EParadoxBarrierLiftFailureReason Reason) {}
void AParadoxVerticalBarrier::HandleOccupantLiftCompleted_Implementation(AActor* OccupantActor) {}
void AParadoxVerticalBarrier::HandleAllOccupantsPrepared_Implementation(int32 PreparedCount) {}
void AParadoxVerticalBarrier::HandleAllOccupantsReleased_Implementation(int32 ReleasedCount) {}

void AParadoxVerticalBarrier::OnMovementTargetRequestedNative(const EPuzzleTransformMoverTarget RequestedTarget)
{
	if (RequestedTarget == EPuzzleTransformMoverTarget::End && !bSafetyReturnInProgress)
	{
		CancelPendingRaiseRequest();
	}
}

EPuzzleTransformMoverRequestDecision AParadoxVerticalBarrier::EvaluateMovementRequestNative(
	const EPuzzleTransformMoverTarget RequestedTarget)
{
	if (bApplyingWorldState || !bBarrierInitialized)
	{
		return EPuzzleTransformMoverRequestDecision::Reject;
	}
	if (RequestedTarget == EPuzzleTransformMoverTarget::End)
	{
		return EPuzzleTransformMoverRequestDecision::Accept;
	}

	RefreshPassageOccupants();
	if (bWaitForClearPassage && IsPassageOccupied())
	{
		SetRaiseRequestPending(true);
		return EPuzzleTransformMoverRequestDecision::Defer;
	}

	bRaiseRequestPending = false;
	ClearPendingRaiseRetry();
	if (!bWaitForClearPassage)
	{
		PrepareCurrentOccupantsForLift();
	}
	SetPassageNavigationBlocking(true);
	return EPuzzleTransformMoverRequestDecision::Accept;
}

bool AParadoxVerticalBarrier::ShouldProcessReceiverStateNative(const bool bReceiverActive)
{
	if (bApplyingWorldState || !bBarrierInitialized)
	{
		return false;
	}
	if (bReceiverActive && MovementMode == EPuzzleTransformMoverMode::PingPong)
	{
		CancelPendingRaiseRequest();
	}
	return true;
}

void AParadoxVerticalBarrier::OnMovementStartedNative()
{
	PreviousBarrierLocation = GetMovedComponent() ? GetMovedComponent()->GetComponentLocation() : GetActorLocation();
	StartMovementFeedback(GetMoverState() == EPuzzleTransformMoverState::MovingTowardStart, true);
	PublishPerceptionState();
}

void AParadoxVerticalBarrier::OnMovementResumedNative()
{
	PreviousBarrierLocation = GetMovedComponent() ? GetMovedComponent()->GetComponentLocation() : GetActorLocation();
	StartMovementFeedback(GetMoverState() == EPuzzleTransformMoverState::MovingTowardStart, true);
	PublishPerceptionState();
}

void AParadoxVerticalBarrier::OnMovementReversedNative()
{
	PreviousBarrierLocation = GetMovedComponent() ? GetMovedComponent()->GetComponentLocation() : GetActorLocation();
	StartMovementFeedback(GetMoverState() == EPuzzleTransformMoverState::MovingTowardStart, true);
	PublishPerceptionState();
}

void AParadoxVerticalBarrier::OnMovementPausedNative()
{
	StopMovementFeedback();
	PublishPerceptionState();
}

void AParadoxVerticalBarrier::OnMovementUpdatedNative(float CurrentMovementAlpha, float CurrentEasedAlpha)
{
	ApplyCharacterTransportDelta();
}

void AParadoxVerticalBarrier::OnReachedStartNative()
{
	StopMovementFeedback();
	SetPassageNavigationBlocking(true);
	ReleaseAllLiftedActors(EParadoxBarrierPassengerReleaseReason::ReachedStart);
	EmitMovementNoise(true, true);
	PublishPerceptionState();
}

void AParadoxVerticalBarrier::OnReachedEndNative()
{
	StopMovementFeedback();
	SetPassageNavigationBlocking(false);
	ReleaseAllLiftedActors(EParadoxBarrierPassengerReleaseReason::ReachedEnd);
	bSafetyReturnInProgress = false;
	EmitMovementNoise(false, true);
	PublishPerceptionState();
	if (bRaiseRequestPending && !IsPassageOccupied())
	{
		SchedulePendingRaiseRetry();
	}
}

void AParadoxVerticalBarrier::OnMovedComponentChangingNative(USceneComponent* PreviousComponent, USceneComponent* NewComponent)
{
	ReleaseAllLiftedActors(EParadoxBarrierPassengerReleaseReason::MovedComponentChanged);
}

void AParadoxVerticalBarrier::OnMovedComponentChangedNative(USceneComponent* PreviousComponent, USceneComponent* NewComponent)
{
	PreviousBarrierLocation = NewComponent ? NewComponent->GetComponentLocation() : GetActorLocation();
	RebuildDerivedState();
}

void AParadoxVerticalBarrier::OnMoverResetNative()
{
	RebuildDerivedState();
}

void AParadoxVerticalBarrier::HandlePassageBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!bBarrierInitialized || bApplyingWorldState || !IsOccupantAccepted(OtherActor, OtherComponent))
	{
		return;
	}
	const int32 PreviousCount = OverlappingActors.Num();
	AddOverlappingComponent(OtherActor, OtherComponent);
	NotifyOccupancyChanged(PreviousCount);

	if (GetMoverState() == EPuzzleTransformMoverState::MovingTowardStart)
	{
		if (bWaitForClearPassage)
		{
			SetRaiseRequestPending(true);
			bSafetyReturnInProgress = true;
			RequestMoveTowardEnd();
		}
		else
		{
			PrepareActorForLift(OtherActor);
		}
	}
}

void AParadoxVerticalBarrier::HandlePassageEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex)
{
	if (!bBarrierInitialized || bApplyingWorldState || !OtherActor || !OtherComponent)
	{
		return;
	}
	const int32 PreviousCount = OverlappingActors.Num();
	RemoveOverlappingComponent(OtherActor, OtherComponent);
	NotifyOccupancyChanged(PreviousCount);
	if (PreviousCount > 0 && OverlappingActors.IsEmpty())
	{
		if (!bSuppressPresentation)
		{
			HandlePassageClearanceRestored();
			OnPassageClearanceRestored.Broadcast(this);
		}
		TryRetryPendingRaise();
	}
}

bool AParadoxVerticalBarrier::IsOccupantAccepted(AActor* Actor, UPrimitiveComponent* Component) const
{
	return IsValid(Actor) && Actor != this && !Actor->IsActorBeingDestroyed()
		&& IsValid(Component) && Component->GetOwner() == Actor
		&& !Actor->IsOwnedBy(this)
		&& HasRequiredOccupantTags(Actor)
		&& CanActorOccupyPassage(Actor, Component);
}

bool AParadoxVerticalBarrier::HasRequiredOccupantTags(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	for (const FName RequiredTag : RequiredOccupantActorTags)
	{
		if (RequiredTag.IsNone() || !Actor->ActorHasTag(RequiredTag))
		{
			return false;
		}
	}
	return true;
}

void AParadoxVerticalBarrier::AddOverlappingComponent(AActor* Actor, UPrimitiveComponent* Component)
{
	OverlappingActors.FindOrAdd(Actor).Add(Component);
	BindActorDestroyed(Actor);
}

void AParadoxVerticalBarrier::RemoveOverlappingComponent(AActor* Actor, UPrimitiveComponent* Component)
{
	if (TSet<TWeakObjectPtr<UPrimitiveComponent>>* Components = OverlappingActors.Find(Actor))
	{
		Components->Remove(Component);
		for (auto It = Components->CreateIterator(); It; ++It)
		{
			if (!It->IsValid())
			{
				It.RemoveCurrent();
			}
		}
		if (Components->IsEmpty())
		{
			OverlappingActors.Remove(Actor);
			UnbindActorDestroyedIfUnused(Actor);
		}
	}
}

void AParadoxVerticalBarrier::NotifyOccupancyChanged(const int32 PreviousCount)
{
	if (PreviousCount == OverlappingActors.Num())
	{
		return;
	}
	PublishPerceptionState();
	if (!bSuppressPresentation)
	{
		HandlePassageOccupancyChanged(OverlappingActors.Num());
		OnPassageOccupancyChanged.Broadcast(this, OverlappingActors.Num());
	}
}

void AParadoxVerticalBarrier::ClearOverlappingActors()
{
	TArray<TWeakObjectPtr<AActor>> PreviousActors;
	OverlappingActors.GetKeys(PreviousActors);
	OverlappingActors.Reset();
	for (const TWeakObjectPtr<AActor>& Actor : PreviousActors)
	{
		UnbindActorDestroyedIfUnused(Actor.Get());
	}
}

void AParadoxVerticalBarrier::BindActorDestroyed(AActor* Actor)
{
	if (IsValid(Actor))
	{
		Actor->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleTrackedActorDestroyed);
	}
}

void AParadoxVerticalBarrier::UnbindActorDestroyedIfUnused(AActor* Actor)
{
	if (Actor && !OverlappingActors.Contains(Actor) && !LiftedActors.Contains(Actor))
	{
		Actor->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleTrackedActorDestroyed);
	}
}

void AParadoxVerticalBarrier::HandleTrackedActorDestroyed(AActor* DestroyedActor)
{
	const int32 PreviousCount = OverlappingActors.Num();
	OverlappingActors.Remove(DestroyedActor);
	if (LiftedActors.Contains(DestroyedActor))
	{
		ReleaseLiftedActor(DestroyedActor, EParadoxBarrierPassengerReleaseReason::InvalidPassenger);
	}
	NotifyOccupancyChanged(PreviousCount);
	TryRetryPendingRaise();
}

void AParadoxVerticalBarrier::SetRaiseRequestPending(const bool bPending)
{
	if (bRaiseRequestPending == bPending)
	{
		return;
	}
	bRaiseRequestPending = bPending;
	PublishPerceptionState();
	if (bPending)
	{
		LastBarrierDiagnostic = FString::Printf(TEXT("Raise deferred by %d distinct passage occupant(s)."), OverlappingActors.Num());
		if (!bSuppressPresentation)
		{
			HandleRaiseDeferred(OverlappingActors.Num());
			OnRaiseDeferred.Broadcast(this, OverlappingActors.Num());
		}
	}
}

bool AParadoxVerticalBarrier::IsPendingRaiseStillValid() const
{
	if (!bRaiseRequestPending || bApplyingWorldState || !bBarrierInitialized)
	{
		return false;
	}
	if (MovementMode == EPuzzleTransformMoverMode::PingPong)
	{
		return PuzzleReceiver && !PuzzleReceiver->IsReceiverActive();
	}
	return MovementMode != EPuzzleTransformMoverMode::Latch || !IsLatchCompleted();
}

void AParadoxVerticalBarrier::TryRetryPendingRaise()
{
	ClearPendingRaiseRetry();
	if (!IsPendingRaiseStillValid() || IsPassageOccupied() || !IsAtEnd())
	{
		return;
	}
	RequestMoveTowardStart();
}

void AParadoxVerticalBarrier::SchedulePendingRaiseRetry()
{
	if (!GetWorld() || PendingRaiseRetryHandle.IsValid())
	{
		return;
	}
	PendingRaiseRetryHandle = GetWorld()->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &ThisClass::TryRetryPendingRaise));
}

void AParadoxVerticalBarrier::ClearPendingRaiseRetry()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingRaiseRetryHandle);
	}
	PendingRaiseRetryHandle.Invalidate();
}

int32 AParadoxVerticalBarrier::PrepareCurrentOccupantsForLift()
{
	const TArray<AActor*> Occupants = GetPassageOccupants();
	int32 PreparedCount = 0;
	for (AActor* Actor : Occupants)
	{
		PreparedCount += PrepareActorForLift(Actor) ? 1 : 0;
	}
	if (!bSuppressPresentation)
	{
		HandleAllOccupantsPrepared(PreparedCount);
		OnAllOccupantsPrepared.Broadcast(this, PreparedCount);
	}
	return PreparedCount;
}

bool AParadoxVerticalBarrier::PrepareActorForLift(AActor* Actor)
{
	if (!IsValid(Actor) || LiftedActors.Contains(Actor))
	{
		return LiftedActors.Contains(Actor);
	}

	FLiftedActorRecord Record;
	Record.Actor = Actor;
	const bool bPrepared = Cast<ACharacter>(Actor)
		? PrepareCharacterForLift(CastChecked<ACharacter>(Actor), Record)
		: PrepareAttachedActorForLift(Actor, Record);
	if (!bPrepared)
	{
		return false;
	}
	LiftedActors.Add(Actor, Record);
	BindActorDestroyed(Actor);
	PublishPerceptionState();
	if (!bSuppressPresentation)
	{
		HandleOccupantLiftStarted(Actor);
		OnOccupantLiftStarted.Broadcast(this, Actor);
	}
	return true;
}

bool AParadoxVerticalBarrier::PrepareCharacterForLift(ACharacter* Character, FLiftedActorRecord& OutRecord)
{
	if (!IsValid(Character))
	{
		ReportLiftFailure(Character, EParadoxBarrierLiftFailureReason::InvalidActor);
		return false;
	}
	AParadoxCharacter* ParadoxCharacter = Cast<AParadoxCharacter>(Character);
	UGameplayActionComponent* Actions = ParadoxCharacter ? ParadoxCharacter->GetGameplayActionComponent() : nullptr;
	if (!Actions)
	{
		if (AController* Controller = Character->GetController())
		{
			Controller->StopMovement();
		}
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
		ReportLiftFailure(Character, EParadoxBarrierLiftFailureReason::MissingLocomotionLockOwner);
		return false;
	}

	FGameplayTagContainer MovementLock;
	MovementLock.AddTag(GameplayActionTags::Lock_Movement);
	const EGameplayActionOperationResult LockResult = Actions->AcquireExternalExecutionLocks(
		this,
		MovementLock,
		ParadoxGameplayTags::Result_Interrupted_ByBarrierLift);
	if (LockResult != EGameplayActionOperationResult::Succeeded)
	{
		ReportLiftFailure(Character, EParadoxBarrierLiftFailureReason::LocomotionLockRejected);
		return false;
	}
	if (AController* Controller = Character->GetController())
	{
		Controller->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
	OutRecord.ActionComponent = Actions;
	OutRecord.bOwnsMovementLock = true;
	OutRecord.bCharacter = true;
	return true;
}

bool AParadoxVerticalBarrier::PrepareAttachedActorForLift(AActor* Actor, FLiftedActorRecord& OutRecord)
{
	USceneComponent* Root = IsValid(Actor) ? Actor->GetRootComponent() : nullptr;
	if (!Root)
	{
		ReportLiftFailure(Actor, EParadoxBarrierLiftFailureReason::MissingRootComponent);
		return false;
	}
	if (Root->Mobility != EComponentMobility::Movable || !GetMovedComponent())
	{
		ReportLiftFailure(Actor, EParadoxBarrierLiftFailureReason::UnsupportedMobility);
		return false;
	}

	OutRecord.RootComponent = Root;
	OutRecord.PreviousAttachParent = Root->GetAttachParent();
	OutRecord.PreviousSocket = Root->GetAttachSocketName();
	OutRecord.bWasAttached = Root->GetAttachParent() != nullptr;
	if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Root))
	{
		OutRecord.bWasSimulatingPhysics = Primitive->IsSimulatingPhysics();
		OutRecord.bWasGravityEnabled = Primitive->IsGravityEnabled();
		OutRecord.bPhysicsStateChanged = OutRecord.bWasSimulatingPhysics;
		if (OutRecord.bWasSimulatingPhysics)
		{
			Primitive->SetSimulatePhysics(false);
		}
	}

	if (!Root->AttachToComponent(GetMovedComponent(), FAttachmentTransformRules::KeepWorldTransform))
	{
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Root); OutRecord.bPhysicsStateChanged && Primitive)
		{
			Primitive->SetEnableGravity(OutRecord.bWasGravityEnabled);
			Primitive->SetSimulatePhysics(OutRecord.bWasSimulatingPhysics);
		}
		ReportLiftFailure(Actor, EParadoxBarrierLiftFailureReason::AttachmentFailed);
		return false;
	}
	return true;
}

void AParadoxVerticalBarrier::ReportLiftFailure(AActor* Actor, const EParadoxBarrierLiftFailureReason Reason)
{
	LastBarrierDiagnostic = FString::Printf(
		TEXT("Could not prepare '%s' for lift: %s."),
		*GetNameSafe(Actor),
		*UEnum::GetValueAsString(Reason));
	PARADOX_LOG_WARNING(TEXT("Vertical Barrier '%s': %s"), *GetNameSafe(this), *LastBarrierDiagnostic);
	if (!bSuppressPresentation)
	{
		HandleOccupantLiftFailed(Actor, Reason);
		OnOccupantLiftFailed.Broadcast(this, Actor, Reason);
	}
}

void AParadoxVerticalBarrier::ReleaseLiftedActor(
	const TWeakObjectPtr<AActor>& ActorKey,
	const EParadoxBarrierPassengerReleaseReason Reason)
{
	FLiftedActorRecord* Found = LiftedActors.Find(ActorKey);
	if (!Found)
	{
		return;
	}
	const FLiftedActorRecord Record = *Found;
	LiftedActors.Remove(ActorKey);
	AActor* Actor = Record.Actor.Get();

	if (Record.bOwnsMovementLock)
	{
		if (UGameplayActionComponent* Actions = Record.ActionComponent.Get())
		{
			Actions->ReleaseExternalExecutionLocks(this);
		}
	}
	if (!Record.bCharacter)
	{
		if (USceneComponent* Root = Record.RootComponent.Get())
		{
			Root->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			if (Record.bWasAttached)
			{
				if (USceneComponent* PreviousParent = Record.PreviousAttachParent.Get())
				{
					Root->AttachToComponent(
						PreviousParent,
						FAttachmentTransformRules::KeepWorldTransform,
						Record.PreviousSocket);
				}
			}
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Root); Record.bPhysicsStateChanged && Primitive)
			{
				Primitive->SetEnableGravity(Record.bWasGravityEnabled);
				Primitive->SetSimulatePhysics(Record.bWasSimulatingPhysics);
			}
		}
	}
	UnbindActorDestroyedIfUnused(Actor);
	if (Actor && !bSuppressPresentation)
	{
		HandleOccupantLiftCompleted(Actor);
		OnOccupantLiftCompleted.Broadcast(this, Actor);
	}
}

void AParadoxVerticalBarrier::ReleaseAllLiftedActors(const EParadoxBarrierPassengerReleaseReason Reason)
{
	TArray<TWeakObjectPtr<AActor>> Keys;
	LiftedActors.GetKeys(Keys);
	for (const TWeakObjectPtr<AActor>& Key : Keys)
	{
		ReleaseLiftedActor(Key, Reason);
	}
	PublishPerceptionState();
	if (!Keys.IsEmpty() && !bSuppressPresentation)
	{
		HandleAllOccupantsReleased(Keys.Num());
		OnAllOccupantsReleased.Broadcast(this, Keys.Num());
	}
}

void AParadoxVerticalBarrier::ApplyCharacterTransportDelta()
{
	USceneComponent* Moved = GetMovedComponent();
	if (!Moved)
	{
		return;
	}
	const FVector CurrentLocation = Moved->GetComponentLocation();
	const FVector Delta = CurrentLocation - PreviousBarrierLocation;
	PreviousBarrierLocation = CurrentLocation;
	if (Delta.IsNearlyZero())
	{
		return;
	}
	UPrimitiveComponent* MovedPrimitive = Cast<UPrimitiveComponent>(Moved);
	for (const TPair<TWeakObjectPtr<AActor>, FLiftedActorRecord>& Entry : LiftedActors)
	{
		ACharacter* Character = Entry.Value.bCharacter ? Cast<ACharacter>(Entry.Key.Get()) : nullptr;
		if (!Character || Character->GetMovementBaseObject() == MovedPrimitive)
		{
			continue;
		}
		FHitResult Hit;
		Character->AddActorWorldOffset(Delta, true, &Hit, ETeleportType::None);
	}
}

void AParadoxVerticalBarrier::SynchronizePassageBounds()
{
	if (!GridNavigationModifier || !PassageOccupancyVolume)
	{
		return;
	}
	if (!PassageOccupancyVolume->GetRelativeTransform().Equals(GridNavigationModifier->GetRelativeTransform()))
	{
		PassageOccupancyVolume->SetRelativeTransform(GridNavigationModifier->GetRelativeTransform());
	}
	if (!PassageOccupancyVolume->GetUnscaledBoxExtent().Equals(GridNavigationModifier->BoxExtent))
	{
		PassageOccupancyVolume->SetBoxExtent(GridNavigationModifier->BoxExtent, false);
	}
}

void AParadoxVerticalBarrier::EnforceComponentInvariants()
{
	if (BarrierMesh)
	{
		BarrierMesh->SetCanEverAffectNavigation(false);
	}
	if (PassageOccupancyVolume)
	{
		PassageOccupancyVolume->SetCanEverAffectNavigation(false);
		PassageOccupancyVolume->bDynamicObstacle = false;
		PassageOccupancyVolume->SetGenerateOverlapEvents(true);
	}
	if (GridNavigationModifier)
	{
		// Older Blueprint assets serialized the component's previous false default. This modifier is
		// mandatory for the barrier, so migrate those instances and guarantee runtime activation.
		GridNavigationModifier->bAutoActivate = true;
		if (bBarrierInitialized && GetWorld() && GetWorld()->IsGameWorld() && !GridNavigationModifier->IsActive())
		{
			GridNavigationModifier->Activate(true);
		}
	}
}

void AParadoxVerticalBarrier::SetPassageNavigationBlocking(const bool bBlocking)
{
	if (bPassageBlockingNavigation == bBlocking
		&& (!GridNavigationModifier || GridNavigationModifier->bBlockCells == bBlocking))
	{
		return;
	}
	bPassageBlockingNavigation = bBlocking;
	if (GridNavigationModifier)
	{
		GridNavigationModifier->SetBlockingEnabled(bBlocking);
	}
	PublishPerceptionState();
	if (!bSuppressPresentation)
	{
		HandlePassageNavigationChanged(!bBlocking);
		if (bBlocking)
		{
			OnPassageBecameBlocked.Broadcast(this);
		}
		else
		{
			OnPassageBecameNavigable.Broadcast(this);
		}
	}
}

void AParadoxVerticalBarrier::RebuildDerivedState()
{
	const bool bShouldBlock = !IsAtEnd() || IsMovementPaused();
	SetPassageNavigationBlocking(bShouldBlock);
	PreviousBarrierLocation = GetMovedComponent() ? GetMovedComponent()->GetComponentLocation() : GetActorLocation();
	StopMovementFeedback();
	PublishPerceptionState();
}

void AParadoxVerticalBarrier::PublishPerceptionState()
{
	if (!PerceptionSource)
	{
		return;
	}
	PerceptionSource->SetObservableState(
		ParadoxGameplayTags::State_Barrier_Open,
		FPerceptionKnowledgeValue::MakeBool(IsPassageOpen()));
	PerceptionSource->SetObservableState(
		ParadoxGameplayTags::State_Barrier_BlockingPassage,
		FPerceptionKnowledgeValue::MakeBool(IsPassageBlockingNavigation()));
	PerceptionSource->SetObservableState(
		ParadoxGameplayTags::State_Barrier_Moving,
		FPerceptionKnowledgeValue::MakeBool(IsMoving() && !IsMovementPaused()));
	PerceptionSource->SetObservableState(
		ParadoxGameplayTags::State_Barrier_WaitingForClearance,
		FPerceptionKnowledgeValue::MakeBool(bRaiseRequestPending));
	PerceptionSource->SetObservableState(
		ParadoxGameplayTags::State_Barrier_TransportingOccupants,
		FPerceptionKnowledgeValue::MakeBool(!LiftedActors.IsEmpty()));
}

void AParadoxVerticalBarrier::StartMovementFeedback(const bool bRaising, const bool bEmitNoise)
{
	if (bSuppressPresentation || bApplyingWorldState)
	{
		return;
	}
	if (MovementAudio)
	{
		MovementAudio->Stop();
		USoundBase* Selected = bRaising ? RaiseSound.Get() : LowerSound.Get();
		MovementAudio->SetSound(Selected ? Selected : DefaultMovementSound.Get());
		if (MovementAudio->GetSound())
		{
			MovementAudio->Play();
		}
	}
	if (MovementVFX)
	{
		MovementVFX->Deactivate();
		UNiagaraSystem* Selected = bRaising ? RaiseNiagaraSystem.Get() : LowerNiagaraSystem.Get();
		MovementVFX->SetAsset(Selected ? Selected : DefaultMovementNiagaraSystem.Get());
		if (MovementVFX->GetAsset())
		{
			MovementVFX->Activate(true);
		}
	}
	if (bEmitNoise)
	{
		EmitMovementNoise(bRaising, false);
	}
}

void AParadoxVerticalBarrier::StopMovementFeedback()
{
	if (MovementAudio)
	{
		MovementAudio->Stop();
	}
	if (MovementVFX)
	{
		MovementVFX->Deactivate();
	}
}

void AParadoxVerticalBarrier::EmitMovementNoise(const bool bRaising, const bool bImpact)
{
	if (!PerceptionSource || bSuppressPresentation || bApplyingWorldState
		|| (bImpact ? !bEmitNoiseOnReachedEndpoint : (bRaising ? !bEmitNoiseOnRaiseStart : !bEmitNoiseOnLowerStart)))
	{
		return;
	}
	FPerceptionKnowledgeNoiseRequest Request;
	Request.EventTag = bImpact
		? ParadoxGameplayTags::Event_Noise_Barrier_Impact
		: (bRaising ? ParadoxGameplayTags::Event_Noise_Barrier_Raise : ParadoxGameplayTags::Event_Noise_Barrier_Lower);
	Request.CauseTag = ParadoxGameplayTags::Cause_Barrier_Movement;
	Request.Instigator = this;
	Request.WorldLocation = BarrierMesh ? BarrierMesh->GetComponentLocation() : GetActorLocation();
	Request.bUseSourceLocation = false;
	Request.Loudness = MovementNoiseLoudness;
	Request.MaxRange = MovementNoiseMaxRange;
	Request.Strength = MovementNoiseStrength;
	const FPerceptionKnowledgeOperationResult Result = PerceptionSource->EmitSemanticNoise(Request);
	if (!Result.IsSuccess())
	{
		PARADOX_LOG_WARNING(
			TEXT("Vertical Barrier '%s' failed to emit semantic noise '%s': %s"),
			*GetNameSafe(this), *Request.EventTag.ToString(), *Result.Message);
	}
}

void AParadoxVerticalBarrier::DrawBarrierDebug() const
{
	if (!GetWorld() || !PassageOccupancyVolume)
	{
		return;
	}
	const FColor BoxColor = bRaiseRequestPending
		? FColor::Orange
		: (!LiftedActors.IsEmpty() ? FColor::Cyan : (IsPassageOpen() ? FColor::Green : FColor::Red));
	DrawDebugBox(
		GetWorld(),
		PassageOccupancyVolume->Bounds.Origin,
		PassageOccupancyVolume->Bounds.BoxExtent,
		PassageOccupancyVolume->GetComponentQuat(),
		BoxColor,
		false,
		0.0f,
		0,
		2.0f);
	for (const TPair<TWeakObjectPtr<AActor>, TSet<TWeakObjectPtr<UPrimitiveComponent>>>& Entry : OverlappingActors)
	{
		if (AActor* Actor = Entry.Key.Get())
		{
			DrawDebugLine(GetWorld(), PassageOccupancyVolume->Bounds.Origin, Actor->GetActorLocation(), FColor::Yellow, false, 0.0f);
		}
	}
	for (const TPair<TWeakObjectPtr<AActor>, FLiftedActorRecord>& Entry : LiftedActors)
	{
		if (AActor* Actor = Entry.Key.Get())
		{
			DrawDebugSphere(GetWorld(), Actor->GetActorLocation(), 14.0f, 8, FColor::Cyan, false, 0.0f);
		}
	}
	const FString Label = FString::Printf(
		TEXT("%s\nState: %s Alpha: %.2f%s\nNavigation: %s Policy: %s\nOccupants: %d Passengers: %d Pending: %s Restore: %s\n%s"),
		*GetNameSafe(this),
		*UEnum::GetValueAsString(GetMoverState()),
		GetMovementAlpha(),
		IsMovementPaused() ? TEXT(" (Paused)") : TEXT(""),
		bPassageBlockingNavigation ? TEXT("Blocked") : TEXT("Open"),
		bWaitForClearPassage ? TEXT("Wait for clear") : TEXT("Lift"),
		OverlappingActors.Num(),
		LiftedActors.Num(),
		bRaiseRequestPending ? TEXT("true") : TEXT("false"),
		bApplyingWorldState ? TEXT("true") : TEXT("false"),
		*LastBarrierDiagnostic);
	DrawDebugString(
		GetWorld(),
		PassageOccupancyVolume->Bounds.Origin + FVector(0.0f, 0.0f, DebugVerticalOffset),
		Label,
		nullptr,
		FColor::White,
		0.0f,
		true);
}

bool AParadoxVerticalBarrier::ShouldDrawBarrierDebug() const
{
	return bEnableDebug && IsParadoxVerticalBarrierDebugEnabled();
}

void AParadoxVerticalBarrier::HandleWorldStatePreCapture(FWorldStateParticipantId ParticipantId)
{
	WorldStateMoverRuntimeState = CaptureRuntimeState();
}

void AParadoxVerticalBarrier::HandleWorldStatePreRestore(FWorldStateParticipantId ParticipantId)
{
	bApplyingWorldState = true;
	bSuppressPresentation = true;
	ClearPendingRaiseRetry();
	bRaiseRequestPending = false;
	bSafetyReturnInProgress = false;
	StopMovementFeedback();
	ReleaseAllLiftedActors(EParadoxBarrierPassengerReleaseReason::WorldStateRestore);
	ClearOverlappingActors();
	SetPassageNavigationBlocking(true);
}

void AParadoxVerticalBarrier::HandleWorldStatePropertiesRestored(FWorldStateParticipantId ParticipantId)
{
	if (!RestoreRuntimeState(WorldStateMoverRuntimeState))
	{
		PARADOX_LOG_ERROR(TEXT("Vertical Barrier '%s' could not restore its mover runtime snapshot."), *GetNameSafe(this));
	}
	RebuildDerivedState();
}

void AParadoxVerticalBarrier::HandleWorldStateParticipantRestored(FWorldStateParticipantId ParticipantId)
{
	RebuildDerivedState();
}

void AParadoxVerticalBarrier::HandleWorldStateParticipantFailed(const FWorldStateParticipantResult& Result)
{
	PARADOX_LOG_WARNING(TEXT("Vertical Barrier '%s' participant restore failed; retaining safe derived state."), *GetNameSafe(this));
	RebuildDerivedState();
}

void AParadoxVerticalBarrier::HandleWorldStateRestoreCompleted(const FWorldStateRestoreResult& Result)
{
	FinishWorldStateRestore();
}

void AParadoxVerticalBarrier::HandleWorldStateRestoreFailed(const FWorldStateRestoreResult& Result)
{
	FinishWorldStateRestore();
}

void AParadoxVerticalBarrier::FinishWorldStateRestore()
{
	if (!bApplyingWorldState)
	{
		return;
	}
	RebuildDerivedState();
	bApplyingWorldState = false;
	bSuppressPresentation = false;
	RefreshPassageOccupants();
	PublishPerceptionState();
}

#undef LOCTEXT_NAMESPACE
