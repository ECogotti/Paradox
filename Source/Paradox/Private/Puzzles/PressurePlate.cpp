#include "Puzzles/PressurePlate.h"

#include "Components/AudioComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Curves/CurveFloat.h"
#include "DrawDebugHelpers.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Paradox.h"
#include "Sound/SoundBase.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "PressurePlate"

APressurePlate::APressurePlate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.DoNotCreateDefaultSubobject(APuzzleSwitch::SceneRootComponentName))
{
	SwitchMode = EPuzzleSwitchMode::Hold;
	InitialInputState = EPuzzleSwitchInitialInputState::Released;
	bStartActive = false;
	PressDelay = 0.0f;
	ReleaseDelay = 0.0f;
	OutputSignalTag = ParadoxGameplayTags::Puzzle_Signal_Pressed.GetTag();
	PressNoiseEventTag = ParadoxGameplayTags::Event_Noise_PressurePlate_Press.GetTag();
	ReleaseNoiseEventTag = ParadoxGameplayTags::Event_Noise_PressurePlate_Release.GetTag();
	MovementNoiseCauseTag = ParadoxGameplayTags::Cause_PressurePlate_Movement.GetTag();

	BillboardRoot = CreateDefaultSubobject<UBillboardComponent>(TEXT("BillboardRoot"));
	BillboardRoot->SetMobility(EComponentMobility::Static);
	BillboardRoot->SetHiddenInGame(true);
	BillboardRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BillboardRoot->SetCanEverAffectNavigation(false);
	SetRootComponent(BillboardRoot);

	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorMesh"));
	FloorMesh->SetupAttachment(BillboardRoot);
	FloorMesh->SetMobility(EComponentMobility::Static);
	FloorMesh->SetCollisionProfileName(TEXT("BlockAll"));

	PlateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlateMesh"));
	PlateMesh->SetupAttachment(FloorMesh);
	PlateMesh->SetMobility(EComponentMobility::Movable);
	PlateMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlateMesh->SetCanEverAffectNavigation(false);

	MovementAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("MovementAudio"));
	MovementAudio->SetupAttachment(PlateMesh);
	MovementAudio->SetAutoActivate(false);

	MovementVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("MovementVFX"));
	MovementVFX->SetupAttachment(PlateMesh);
	MovementVFX->SetAutoActivate(false);

	OccupancyVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("OccupancyVolume"));
	OccupancyVolume->SetupAttachment(FloorMesh);
	OccupancyVolume->SetMobility(EComponentMobility::Static);
	OccupancyVolume->InitBoxExtent(FVector(75.0f, 75.0f, 40.0f));
	OccupancyVolume->SetRelativeLocation(FVector(0.0f, 0.0f, 40.0f));
	OccupancyVolume->SetCollisionProfileName(TEXT("Trigger"));
	OccupancyVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	OccupancyVolume->SetGenerateOverlapEvents(true);
	OccupancyVolume->SetSimulatePhysics(false);
	OccupancyVolume->SetCanEverAffectNavigation(false);
	OccupancyVolume->bDynamicObstacle = false;

	WorldStateParticipant = CreateDefaultSubobject<UWorldStateParticipantComponent>(TEXT("WorldStateParticipant"));
	WorldStateParticipant->bCaptureExistence = false;
	WorldStateParticipant->bCaptureActorTransform = false;
	WorldStateParticipant->bCaptureAttachment = false;
	WorldStateParticipant->ExistencePolicy = EWorldStateExistencePolicy::ExistingOnly;
	FWorldStatePropertySelection& ActiveStateSelection = WorldStateParticipant->CapturedProperties.AddDefaulted_GetRef();
	ActiveStateSelection.CaptureSourceId = FWorldStateCaptureSourceId::OwnerActor();
	ActiveStateSelection.PropertyName = TEXT("bIsActive");

	PerceptionSource = CreateDefaultSubobject<UPerceptionKnowledgeSourceComponent>(TEXT("PerceptionSource"));
}

void APressurePlate::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	EnforceNavigationSafety();
	RefreshSwitchTickState();
}

void APressurePlate::BeginPlay()
{
	SynchronizeInitialPressurePlateState();
	Super::BeginPlay();

	EnforceNavigationSafety();
	if (!bRaisedTransformCached && PlateMesh)
	{
		RaisedPlateRelativeTransform = PlateMesh->GetRelativeTransform();
		bRaisedTransformCached = true;
	}

	DefaultMovementSound = MovementAudio ? MovementAudio->GetSound() : nullptr;
	DefaultMovementNiagaraSystem = MovementVFX ? MovementVFX->GetAsset() : nullptr;
	StopMovementFeedback();
	StopPlateMovement();
	ReleaseOccupant(false, false);

	if (OccupancyVolume)
	{
		OccupancyVolume->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::HandleOccupancyBeginOverlap);
		OccupancyVolume->OnComponentEndOverlap.AddUniqueDynamic(this, &ThisClass::HandleOccupancyEndOverlap);
		bOverlapDelegatesBound = true;
	}

	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreRestore.AddUniqueDynamic(this, &ThisClass::HandleWorldStatePreRestore);
		WorldStateParticipant->OnWorldStateRestored.AddUniqueDynamic(this, &ThisClass::HandleWorldStateRestored);
		WorldStateParticipant->OnWorldStateRestoreFailed.AddUniqueDynamic(this, &ThisClass::HandleWorldStateRestoreFailed);
	}

	bPressurePlateInitialized = true;
	bSuppressMovementFeedback = true;
	SnapPlateToAuthoritativeState();
	RefreshOccupantFromVolume();
	SnapPlateToAuthoritativeState();
	bSuppressMovementFeedback = false;
	RefreshSwitchTickState();
}

void APressurePlate::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bPressurePlateInitialized = false;
	bSuppressMovementFeedback = true;
	bIsApplyingWorldState = true;

	if (bOverlapDelegatesBound && OccupancyVolume)
	{
		OccupancyVolume->OnComponentBeginOverlap.RemoveDynamic(this, &ThisClass::HandleOccupancyBeginOverlap);
		OccupancyVolume->OnComponentEndOverlap.RemoveDynamic(this, &ThisClass::HandleOccupancyEndOverlap);
		bOverlapDelegatesBound = false;
	}

	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreRestore.RemoveDynamic(this, &ThisClass::HandleWorldStatePreRestore);
		WorldStateParticipant->OnWorldStateRestored.RemoveDynamic(this, &ThisClass::HandleWorldStateRestored);
		WorldStateParticipant->OnWorldStateRestoreFailed.RemoveDynamic(this, &ThisClass::HandleWorldStateRestoreFailed);
	}

	StopPlateMovement();
	StopMovementFeedback();
	ReleaseOccupant(false, false);
	Super::EndPlay(EndPlayReason);
}

void APressurePlate::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bIsPlateMoving)
	{
		UpdatePlateMovement(DeltaSeconds);
	}

	if (ShouldDrawPressurePlateDebug())
	{
		DrawPressurePlateDebug();
	}
}

void APressurePlate::ResetSwitch()
{
	const bool bWasSuppressingFeedback = bSuppressMovementFeedback;
	bSuppressMovementFeedback = true;
	StopPlateMovement();
	StopMovementFeedback();
	ReleaseOccupant(false, !bIsApplyingWorldState);

	SynchronizeInitialPressurePlateState();
	Super::ResetSwitch();
	SnapPlateToAuthoritativeState();

	if (bPressurePlateInitialized && !bIsApplyingWorldState)
	{
		RefreshOccupantFromVolume();
		SnapPlateToAuthoritativeState();
	}

	bSuppressMovementFeedback = bWasSuppressingFeedback;
	RefreshSwitchTickState();
}

#if WITH_EDITOR
EDataValidationResult APressurePlate::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	auto AddError = [&Context, &Result](const FText& Error)
	{
		Context.AddError(Error);
		Result = EDataValidationResult::Invalid;
	};

	if (!BillboardRoot || GetRootComponent() != BillboardRoot)
	{
		AddError(LOCTEXT("InvalidBillboardRoot", "Pressure Plate requires BillboardRoot as its Actor root."));
	}
	if (!FloorMesh || FloorMesh->GetAttachParent() != BillboardRoot)
	{
		AddError(LOCTEXT("InvalidFloorMesh", "Pressure Plate requires FloorMesh attached to BillboardRoot."));
	}
	if (!PlateMesh || (FloorMesh && PlateMesh->GetAttachParent() != FloorMesh))
	{
		AddError(LOCTEXT("InvalidPlateMesh", "Pressure Plate requires PlateMesh attached to FloorMesh."));
	}
	if (!OccupancyVolume || (FloorMesh && OccupancyVolume->GetAttachParent() != FloorMesh))
	{
		AddError(LOCTEXT("InvalidOccupancyVolume", "Pressure Plate requires OccupancyVolume attached to FloorMesh."));
	}
	if (!MovementAudio || (PlateMesh && MovementAudio->GetAttachParent() != PlateMesh))
	{
		AddError(LOCTEXT("InvalidMovementAudio", "Pressure Plate requires MovementAudio attached to PlateMesh."));
	}
	if (!MovementVFX || (PlateMesh && MovementVFX->GetAttachParent() != PlateMesh))
	{
		AddError(LOCTEXT("InvalidMovementVFX", "Pressure Plate requires MovementVFX attached to PlateMesh."));
	}
	if (!WorldStateParticipant)
	{
		AddError(LOCTEXT("MissingWorldStateParticipant", "Pressure Plate requires its native World State participant."));
	}
	if (!PerceptionSource)
	{
		AddError(LOCTEXT("MissingPerceptionSource", "Pressure Plate requires its native PerceptionKnowledge source."));
	}
	if (PlateMesh && PlateMesh->CanEverAffectNavigation())
	{
		AddError(LOCTEXT("PlateAffectsNavigation", "Pressure Plate PlateMesh must never affect navigation."));
	}
	if (OccupancyVolume && OccupancyVolume->CanEverAffectNavigation())
	{
		AddError(LOCTEXT("VolumeAffectsNavigation", "Pressure Plate OccupancyVolume must not affect navigation."));
	}
	if (OccupancyVolume && !OccupancyVolume->GetGenerateOverlapEvents())
	{
		AddError(LOCTEXT("VolumeNoOverlaps", "Pressure Plate OccupancyVolume must generate overlap events."));
	}
	if (OccupancyVolume && !CollisionEnabledHasQuery(OccupancyVolume->GetCollisionEnabled()))
	{
		AddError(LOCTEXT("VolumeNotQueryCapable", "Pressure Plate OccupancyVolume must use a query-capable collision mode."));
	}
	if (!FMath::IsFinite(PressDepth) || PressDepth < 0.0f)
	{
		AddError(LOCTEXT("InvalidPressDepth", "Pressure Plate PressDepth must be finite and non-negative."));
	}
	if (!FMath::IsFinite(PressDuration) || PressDuration < 0.0f)
	{
		AddError(LOCTEXT("InvalidPressDuration", "Pressure Plate PressDuration must be finite and non-negative."));
	}
	if (!FMath::IsFinite(ReleaseDuration) || ReleaseDuration < 0.0f)
	{
		AddError(LOCTEXT("InvalidReleaseDuration", "Pressure Plate ReleaseDuration must be finite and non-negative."));
	}
	if (!FMath::IsFinite(MovementNoiseLoudness) || MovementNoiseLoudness < 0.0f
		|| !FMath::IsFinite(MovementNoiseMaxRange) || MovementNoiseMaxRange < 0.0f
		|| !FMath::IsFinite(MovementNoiseStrength) || MovementNoiseStrength < 0.0f)
	{
		AddError(LOCTEXT("InvalidNoiseValues", "Pressure Plate movement-noise values must be finite and non-negative."));
	}
	if (bEmitNoiseOnPressMovement && !PressNoiseEventTag.IsValid())
	{
		AddError(LOCTEXT("InvalidPressNoiseTag", "Pressure Plate requires a valid PressNoiseEventTag when press noise is enabled."));
	}
	if (bEmitNoiseOnReleaseMovement && !ReleaseNoiseEventTag.IsValid())
	{
		AddError(LOCTEXT("InvalidReleaseNoiseTag", "Pressure Plate requires a valid ReleaseNoiseEventTag when release noise is enabled."));
	}
	if ((bEmitNoiseOnPressMovement || bEmitNoiseOnReleaseMovement) && !MovementNoiseCauseTag.IsValid())
	{
		AddError(LOCTEXT("InvalidNoiseCauseTag", "Pressure Plate requires a valid MovementNoiseCauseTag when movement noise is enabled."));
	}
	for (const FName RequiredTag : RequiredOccupantActorTags)
	{
		if (RequiredTag.IsNone())
		{
			AddError(LOCTEXT("InvalidRequiredOccupantActorTag", "Pressure Plate RequiredOccupantActorTags contains an empty Actor Tag."));
			break;
		}
	}

	return Result;
}

bool APressurePlate::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty && InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(APuzzleSwitch, bStartActive))
	{
		return false;
	}

	return Super::CanEditChange(InProperty);
}

void APressurePlate::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName ChangedPropertyName = PropertyChangedEvent.Property
		? PropertyChangedEvent.Property->GetFName()
		: NAME_None;
	if (ChangedPropertyName.IsNone()
		|| ChangedPropertyName == GET_MEMBER_NAME_CHECKED(APuzzleSwitch, InitialInputState))
	{
		SynchronizeInitialPressurePlateState();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
	EnforceNavigationSafety();
	RefreshSwitchTickState();
}
#endif

bool APressurePlate::RefreshOccupantFromVolume()
{
	if (!bPressurePlateInitialized || !OccupancyVolume)
	{
		PARADOX_LOG_WARNING(
			TEXT("Pressure Plate '%s' cannot refresh occupancy before initialization or without OccupancyVolume."),
			*GetNameSafe(this));
		return false;
	}

	OccupancyVolume->UpdateOverlaps(nullptr, false);
	return ReconcileOccupancy() && SynchronizeSwitchInputWithOccupancy();
}

bool APressurePlate::CanOccupantActivatePlate_Implementation(
	AActor* OccupantActor,
	UPrimitiveComponent* OccupantComponent) const
{
	return true;
}

void APressurePlate::HandleOccupantAccepted_Implementation(AActor* OccupantActor)
{
}

void APressurePlate::HandleOccupantReleased_Implementation(AActor* OccupantActor)
{
}

void APressurePlate::HandleOccupantReplaced_Implementation(AActor* PreviousOccupant, AActor* NewOccupant)
{
}

void APressurePlate::HandleOccupancyStateChanged_Implementation(
	EPressurePlateOccupancyState PreviousState,
	EPressurePlateOccupancyState NewState)
{
}

void APressurePlate::HandlePlateMovementStarted_Implementation(bool bMovingDown)
{
}

void APressurePlate::HandlePlateMovementCompleted_Implementation(bool bIsPressed)
{
}

void APressurePlate::HandleSwitchActivated_Implementation()
{
	Super::HandleSwitchActivated_Implementation();
	StartPlateMovement(true);
}

void APressurePlate::HandleSwitchDeactivated_Implementation()
{
	Super::HandleSwitchDeactivated_Implementation();
	StartPlateMovement(false);
}

void APressurePlate::HandleSwitchReset_Implementation()
{
	Super::HandleSwitchReset_Implementation();
}

bool APressurePlate::ShouldEnableSwitchTick() const
{
	return Super::ShouldEnableSwitchTick() || bIsPlateMoving;
}

void APressurePlate::HandleOccupancyBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!bPressurePlateInitialized || bIsApplyingWorldState
		|| !IsOccupantCandidateAccepted(OtherActor, OtherComponent))
	{
		return;
	}

	if (OccupancyState == EPressurePlateOccupancyState::Free)
	{
		TArray<TWeakObjectPtr<UPrimitiveComponent>> InitialComponents;
		InitialComponents.Add(OtherComponent);
		AcquireOccupant(OtherActor, InitialComponents);
		return;
	}

	if (CurrentOccupant.Get() == OtherActor)
	{
		CurrentOccupantComponents.Add(OtherComponent);
	}
}

void APressurePlate::HandleOccupancyEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex)
{
	if (!bPressurePlateInitialized || bIsApplyingWorldState
		|| CurrentOccupant.Get() != OtherActor || !OtherComponent)
	{
		return;
	}

	CurrentOccupantComponents.Remove(OtherComponent);
	for (auto Iterator = CurrentOccupantComponents.CreateIterator(); Iterator; ++Iterator)
	{
		if (!Iterator->IsValid())
		{
			Iterator.RemoveCurrent();
		}
	}

	if (CurrentOccupantComponents.IsEmpty())
	{
		ReconcileOccupancy(OtherComponent);
	}
}

bool APressurePlate::IsOccupantCandidateAccepted(
	AActor* OccupantActor,
	UPrimitiveComponent* OccupantComponent) const
{
	if (!IsValid(OccupantActor) || OccupantActor == this || !IsValid(OccupantComponent)
		|| OccupantComponent->GetOwner() != OccupantActor)
	{
		return false;
	}

	return HasRequiredOccupantActorTags(OccupantActor)
		&& CanOccupantActivatePlate(OccupantActor, OccupantComponent);
}

bool APressurePlate::HasRequiredOccupantActorTags(const AActor* OccupantActor) const
{
	if (RequiredOccupantActorTags.IsEmpty())
	{
		return true;
	}

	if (!IsValid(OccupantActor))
	{
		return false;
	}

	for (const FName RequiredTag : RequiredOccupantActorTags)
	{
		if (RequiredTag.IsNone() || !OccupantActor->ActorHasTag(RequiredTag))
		{
			return false;
		}
	}
	return true;
}

void APressurePlate::AcquireOccupant(
	AActor* OccupantActor,
	const TArray<TWeakObjectPtr<UPrimitiveComponent>>& OccupantComponents)
{
	if (!IsValid(OccupantActor) || OccupancyState != EPressurePlateOccupancyState::Free)
	{
		return;
	}

	CurrentOccupant = OccupantActor;
	CurrentOccupantComponents.Reset();
	for (const TWeakObjectPtr<UPrimitiveComponent>& Component : OccupantComponents)
	{
		if (Component.IsValid())
		{
			CurrentOccupantComponents.Add(Component);
		}
	}
	BindCurrentOccupantDestroyed();
	const bool bNotifyPresentationHooks = !bSuppressMovementFeedback && !bIsApplyingWorldState;
	SetOccupancyState(EPressurePlateOccupancyState::Occupied, bNotifyPresentationHooks);
	if (bNotifyPresentationHooks)
	{
		HandleOccupantAccepted(OccupantActor);
	}

	if (!IsInputPressed() && !Press())
	{
		PARADOX_LOG_WARNING(
			TEXT("Pressure Plate '%s' acquired occupant '%s', but inherited Press() rejected the physical edge."),
			*GetNameSafe(this),
			*GetNameSafe(OccupantActor));
	}
}

void APressurePlate::ReplaceOccupant(
	AActor* NewOccupant,
	const TArray<TWeakObjectPtr<UPrimitiveComponent>>& OccupantComponents)
{
	if (!IsValid(NewOccupant))
	{
		return;
	}

	AActor* PreviousOccupant = CurrentOccupant.Get();
	UnbindCurrentOccupantDestroyed();
	CurrentOccupant = NewOccupant;
	CurrentOccupantComponents.Reset();
	for (const TWeakObjectPtr<UPrimitiveComponent>& Component : OccupantComponents)
	{
		if (Component.IsValid())
		{
			CurrentOccupantComponents.Add(Component);
		}
	}
	BindCurrentOccupantDestroyed();

	const bool bNotifyPresentationHooks = !bSuppressMovementFeedback && !bIsApplyingWorldState;
	if (OccupancyState == EPressurePlateOccupancyState::Free)
	{
		SetOccupancyState(EPressurePlateOccupancyState::Occupied, bNotifyPresentationHooks);
	}
	if (bNotifyPresentationHooks)
	{
		HandleOccupantReplaced(PreviousOccupant, NewOccupant);
	}
}

void APressurePlate::ReleaseOccupant(
	bool bRequestInheritedRelease,
	bool bNotifyHooks,
	AActor* PreviousOccupantOverride)
{
	AActor* PreviousOccupant = PreviousOccupantOverride
		? PreviousOccupantOverride
		: CurrentOccupant.Get();
	const bool bWasOccupied = OccupancyState == EPressurePlateOccupancyState::Occupied;
	const bool bShouldNotifyHooks = bNotifyHooks
		&& !bSuppressMovementFeedback
		&& !bIsApplyingWorldState;

	UnbindCurrentOccupantDestroyed();
	CurrentOccupant.Reset();
	CurrentOccupantComponents.Reset();
	SetOccupancyState(EPressurePlateOccupancyState::Free, bShouldNotifyHooks);

	if (bShouldNotifyHooks && PreviousOccupant)
	{
		HandleOccupantReleased(PreviousOccupant);
	}

	if (bRequestInheritedRelease && bWasOccupied && IsInputPressed() && !Release())
	{
		PARADOX_LOG_WARNING(
			TEXT("Pressure Plate '%s' released occupant '%s', but inherited Release() rejected the physical edge."),
			*GetNameSafe(this),
			*GetNameSafe(PreviousOccupant));
	}
}

void APressurePlate::SetOccupancyState(
	EPressurePlateOccupancyState NewState,
	bool bNotifyHook)
{
	if (OccupancyState == NewState)
	{
		return;
	}

	const EPressurePlateOccupancyState PreviousState = OccupancyState;
	OccupancyState = NewState;
	if (bNotifyHook)
	{
		HandleOccupancyStateChanged(PreviousState, NewState);
	}
}

bool APressurePlate::FindOccupancyCandidate(
	UPrimitiveComponent* ExcludedComponent,
	AActor* ExcludedActor,
	AActor*& OutActor,
	TArray<TWeakObjectPtr<UPrimitiveComponent>>& OutComponents) const
{
	OutActor = nullptr;
	OutComponents.Reset();
	if (!OccupancyVolume)
	{
		return false;
	}

	TArray<UPrimitiveComponent*> OverlappingComponents;
	OccupancyVolume->GetOverlappingComponents(OverlappingComponents);

	AActor* PreferredActor = CurrentOccupant.Get();
	auto GatherForActor = [this, &OverlappingComponents, ExcludedComponent, ExcludedActor](
		AActor* CandidateActor,
		TArray<TWeakObjectPtr<UPrimitiveComponent>>& Components)
	{
		if (!CandidateActor || CandidateActor == ExcludedActor)
		{
			return;
		}
		for (UPrimitiveComponent* Component : OverlappingComponents)
		{
			if (Component && Component != ExcludedComponent && Component->GetOwner() == CandidateActor
				&& IsOccupantCandidateAccepted(CandidateActor, Component))
			{
				Components.Add(Component);
			}
		}
	};

	GatherForActor(PreferredActor, OutComponents);
	if (!OutComponents.IsEmpty())
	{
		OutActor = PreferredActor;
		return true;
	}

	for (UPrimitiveComponent* Component : OverlappingComponents)
	{
		AActor* CandidateActor = Component ? Component->GetOwner() : nullptr;
		if (Component == ExcludedComponent || CandidateActor == ExcludedActor
			|| !IsOccupantCandidateAccepted(CandidateActor, Component))
		{
			continue;
		}

		OutActor = CandidateActor;
		GatherForActor(CandidateActor, OutComponents);
		return !OutComponents.IsEmpty();
	}

	return false;
}

bool APressurePlate::ReconcileOccupancy(
	UPrimitiveComponent* ExcludedComponent,
	AActor* ExcludedActor)
{
	if (!OccupancyVolume)
	{
		return false;
	}

	AActor* CandidateActor = nullptr;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> CandidateComponents;
	const bool bHasCandidate = FindOccupancyCandidate(
		ExcludedComponent,
		ExcludedActor,
		CandidateActor,
		CandidateComponents);

	if (OccupancyState == EPressurePlateOccupancyState::Occupied)
	{
		if (bHasCandidate && CandidateActor == CurrentOccupant.Get())
		{
			CurrentOccupantComponents.Reset();
			CurrentOccupantComponents.Append(CandidateComponents);
			return true;
		}
		if (bHasCandidate)
		{
			ReplaceOccupant(CandidateActor, CandidateComponents);
			return true;
		}

		ReleaseOccupant(true, true);
		return true;
	}

	if (bHasCandidate)
	{
		AcquireOccupant(CandidateActor, CandidateComponents);
	}
	return true;
}

bool APressurePlate::SynchronizeSwitchInputWithOccupancy()
{
	const bool bShouldBePressed = OccupancyState == EPressurePlateOccupancyState::Occupied;
	if (bShouldBePressed == IsInputPressed())
	{
		return true;
	}

	const bool bRequestAccepted = bShouldBePressed ? Press() : Release();
	if (!bRequestAccepted)
	{
		PARADOX_LOG_WARNING(
			TEXT("Pressure Plate '%s' could not reconcile inherited input to physical occupancy '%s'."),
			*GetNameSafe(this),
			bShouldBePressed ? TEXT("Pressed") : TEXT("Released"));
	}
	return bRequestAccepted;
}

void APressurePlate::BindCurrentOccupantDestroyed()
{
	if (AActor* Occupant = CurrentOccupant.Get())
	{
		Occupant->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleCurrentOccupantDestroyed);
	}
}

void APressurePlate::UnbindCurrentOccupantDestroyed()
{
	if (AActor* Occupant = CurrentOccupant.Get(true))
	{
		Occupant->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleCurrentOccupantDestroyed);
	}
}

void APressurePlate::HandleCurrentOccupantDestroyed(AActor* DestroyedActor)
{
	if (!bPressurePlateInitialized || bIsApplyingWorldState
		|| !DestroyedActor || CurrentOccupant.Get(true) != DestroyedActor)
	{
		return;
	}

	UnbindCurrentOccupantDestroyed();
	CurrentOccupant.Reset();
	CurrentOccupantComponents.Reset();

	AActor* ReplacementActor = nullptr;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> ReplacementComponents;
	if (FindOccupancyCandidate(nullptr, DestroyedActor, ReplacementActor, ReplacementComponents))
	{
		CurrentOccupant = ReplacementActor;
		CurrentOccupantComponents.Append(ReplacementComponents);
		BindCurrentOccupantDestroyed();
		if (!bSuppressMovementFeedback)
		{
			HandleOccupantReplaced(DestroyedActor, ReplacementActor);
		}
		return;
	}

	const bool bNotifyPresentationHooks = !bSuppressMovementFeedback;
	SetOccupancyState(EPressurePlateOccupancyState::Free, bNotifyPresentationHooks);
	if (bNotifyPresentationHooks)
	{
		HandleOccupantReleased(DestroyedActor);
	}
	if (IsInputPressed() && !Release())
	{
		PARADOX_LOG_WARNING(
			TEXT("Pressure Plate '%s' could not release inherited input after occupant '%s' was destroyed."),
			*GetNameSafe(this),
			*GetNameSafe(DestroyedActor));
	}
}

void APressurePlate::StartPlateMovement(bool bMoveToPressed)
{
	if (!bPressurePlateInitialized || !bRaisedTransformCached || !PlateMesh)
	{
		return;
	}

	const float NewTargetAlpha = bMoveToPressed ? 1.0f : 0.0f;
	if (bSuppressMovementFeedback || bIsApplyingWorldState)
	{
		TargetPlateMovementAlpha = NewTargetAlpha;
		StopPlateMovement();
		ApplyPlateAlpha(NewTargetAlpha);
		return;
	}

	if (FMath::IsNearlyEqual(TargetPlateMovementAlpha, NewTargetAlpha)
		&& (bIsPlateMoving || FMath::IsNearlyEqual(PlateMovementAlpha, NewTargetAlpha)))
	{
		return;
	}

	TargetPlateMovementAlpha = NewTargetAlpha;
	const float RemainingAlpha = FMath::Abs(TargetPlateMovementAlpha - PlateMovementAlpha);
	if (RemainingAlpha <= KINDA_SMALL_NUMBER || PressDepth <= KINDA_SMALL_NUMBER)
	{
		StopPlateMovement();
		ApplyPlateAlpha(TargetPlateMovementAlpha);
		HandlePlateMovementCompleted(bMoveToPressed);
		return;
	}

	MovementStartAlpha = PlateMovementAlpha;
	MovementElapsedSeconds = 0.0f;
	const float FullDuration = bMoveToPressed ? PressDuration : ReleaseDuration;
	ActiveMovementDuration = FMath::Max(0.0f, FullDuration) * RemainingAlpha;
	bIsPlateMoving = true;
	HandlePlateMovementStarted(bMoveToPressed);
	StartMovementFeedback(bMoveToPressed);

	if (ActiveMovementDuration <= KINDA_SMALL_NUMBER)
	{
		CompletePlateMovement();
		return;
	}

	RefreshSwitchTickState();
}

void APressurePlate::UpdatePlateMovement(float DeltaSeconds)
{
	if (!bIsPlateMoving)
	{
		return;
	}

	MovementElapsedSeconds += FMath::Max(0.0f, DeltaSeconds);
	const float NormalizedTime = ActiveMovementDuration > KINDA_SMALL_NUMBER
		? FMath::Clamp(MovementElapsedSeconds / ActiveMovementDuration, 0.0f, 1.0f)
		: 1.0f;
	const float ShapedTime = MovementCurve
		? FMath::Clamp(MovementCurve->GetFloatValue(NormalizedTime), 0.0f, 1.0f)
		: FMath::SmoothStep(0.0f, 1.0f, NormalizedTime);
	ApplyPlateAlpha(FMath::Lerp(MovementStartAlpha, TargetPlateMovementAlpha, ShapedTime));

	if (NormalizedTime >= 1.0f)
	{
		CompletePlateMovement();
	}
}

void APressurePlate::CompletePlateMovement()
{
	const bool bCompletedPressed = TargetPlateMovementAlpha >= 0.5f;
	ApplyPlateAlpha(TargetPlateMovementAlpha);
	bIsPlateMoving = false;
	MovementElapsedSeconds = 0.0f;
	ActiveMovementDuration = 0.0f;
	if (MovementVFX)
	{
		MovementVFX->Deactivate();
	}
	HandlePlateMovementCompleted(bCompletedPressed);
	RefreshSwitchTickState();
}

void APressurePlate::ApplyPlateAlpha(float Alpha)
{
	PlateMovementAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (!bRaisedTransformCached || !PlateMesh)
	{
		return;
	}

	FTransform AppliedTransform = RaisedPlateRelativeTransform;
	AppliedTransform.SetLocation(
		RaisedPlateRelativeTransform.GetLocation()
		+ FVector(0.0f, 0.0f, -FMath::Max(0.0f, PressDepth) * PlateMovementAlpha));
	PlateMesh->SetRelativeTransform(AppliedTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

void APressurePlate::SnapPlateToAuthoritativeState()
{
	StopPlateMovement();
	TargetPlateMovementAlpha = IsSwitchActive() ? 1.0f : 0.0f;
	ApplyPlateAlpha(TargetPlateMovementAlpha);
	RefreshSwitchTickState();
}

void APressurePlate::StopPlateMovement()
{
	bIsPlateMoving = false;
	MovementElapsedSeconds = 0.0f;
	ActiveMovementDuration = 0.0f;
	RefreshSwitchTickState();
}

void APressurePlate::StartMovementFeedback(bool bMovingDown)
{
	if (bSuppressMovementFeedback || bIsApplyingWorldState)
	{
		return;
	}

	if (MovementAudio)
	{
		MovementAudio->Stop();
		USoundBase* SelectedSound = bMovingDown ? PressSound.Get() : ReleaseSound.Get();
		MovementAudio->SetSound(SelectedSound ? SelectedSound : DefaultMovementSound.Get());
		if (MovementAudio->GetSound())
		{
			MovementAudio->Play();
		}
	}

	if (MovementVFX)
	{
		MovementVFX->Deactivate();
		UNiagaraSystem* SelectedSystem = bMovingDown ? PressNiagaraSystem.Get() : ReleaseNiagaraSystem.Get();
		MovementVFX->SetAsset(SelectedSystem ? SelectedSystem : DefaultMovementNiagaraSystem.Get());
		if (MovementVFX->GetAsset())
		{
			MovementVFX->Activate(true);
		}
	}

	EmitMovementNoise(bMovingDown);
}

void APressurePlate::StopMovementFeedback()
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

void APressurePlate::EmitMovementNoise(bool bMovingDown)
{
	const bool bShouldEmit = bMovingDown
		? bEmitNoiseOnPressMovement
		: bEmitNoiseOnReleaseMovement;
	if (!bShouldEmit || !PerceptionSource || bSuppressMovementFeedback || bIsApplyingWorldState)
	{
		return;
	}

	FPerceptionKnowledgeNoiseRequest Request;
	Request.EventTag = bMovingDown ? PressNoiseEventTag : ReleaseNoiseEventTag;
	Request.Instigator = this;
	Request.WorldLocation = PlateMesh ? PlateMesh->GetComponentLocation() : GetActorLocation();
	Request.bUseSourceLocation = false;
	Request.Loudness = MovementNoiseLoudness;
	Request.MaxRange = MovementNoiseMaxRange;
	Request.Strength = MovementNoiseStrength;
	Request.CauseTag = MovementNoiseCauseTag;
	const FPerceptionKnowledgeOperationResult Result = PerceptionSource->EmitSemanticNoise(Request);
	if (!Result.IsSuccess())
	{
		PARADOX_LOG_WARNING(
			TEXT("Pressure Plate '%s' failed to emit movement noise '%s': %s"),
			*GetNameSafe(this),
			*Request.EventTag.ToString(),
			*Result.Message);
	}
}

void APressurePlate::EnforceNavigationSafety()
{
	if (BillboardRoot)
	{
		BillboardRoot->SetCanEverAffectNavigation(false);
	}
	if (PlateMesh)
	{
		PlateMesh->SetCanEverAffectNavigation(false);
	}
	if (OccupancyVolume)
	{
		OccupancyVolume->SetCanEverAffectNavigation(false);
		OccupancyVolume->bDynamicObstacle = false;
	}
}

void APressurePlate::SynchronizeInitialPressurePlateState()
{
	bStartActive = InitialInputState == EPuzzleSwitchInitialInputState::Pressed;
}

void APressurePlate::SynchronizeEmitterAfterWorldStateRestore()
{
	if (!PuzzleEmitterComponent || !OutputSignalTag.IsValid())
	{
		PARADOX_LOG_ERROR(
			TEXT("Pressure Plate '%s' could not synchronize restored switch output '%s'."),
			*GetNameSafe(this),
			*OutputSignalTag.ToString());
		return;
	}

	FPuzzleSignalState CurrentSignalState;
	if (PuzzleEmitterComponent->TryGetSignalState(OutputSignalTag, CurrentSignalState)
		&& CurrentSignalState.bIsActive == IsSwitchActive()
		&& CurrentSignalState.Payload == nullptr)
	{
		return;
	}

	if (!PuzzleEmitterComponent->SetSignalState(OutputSignalTag, IsSwitchActive(), nullptr))
	{
		PARADOX_LOG_ERROR(
			TEXT("Pressure Plate '%s' failed to publish restored switch output '%s'."),
			*GetNameSafe(this),
			*OutputSignalTag.ToString());
	}
}

void APressurePlate::HandleWorldStatePreRestore(FWorldStateParticipantId ParticipantId)
{
	bIsApplyingWorldState = true;
	bSuppressMovementFeedback = true;
	ResetSwitch();
}

void APressurePlate::HandleWorldStateRestored(FWorldStateParticipantId ParticipantId)
{
	FinishWorldStateRestore();
}

void APressurePlate::HandleWorldStateRestoreFailed(const FWorldStateParticipantResult& Result)
{
	PARADOX_LOG_WARNING(
		TEXT("Pressure Plate '%s' World State restore failed; applying safe local reconciliation."),
		*GetNameSafe(this));
	FinishWorldStateRestore();
}

void APressurePlate::FinishWorldStateRestore()
{
	SynchronizeEmitterAfterWorldStateRestore();
	SnapPlateToAuthoritativeState();
	if (bPressurePlateInitialized)
	{
		RefreshOccupantFromVolume();
		SynchronizeEmitterAfterWorldStateRestore();
		SnapPlateToAuthoritativeState();
	}
	bIsApplyingWorldState = false;
	bSuppressMovementFeedback = false;
	RefreshSwitchTickState();
}

void APressurePlate::DrawPressurePlateDebug() const
{
	UWorld* World = GetWorld();
	if (!World || !OccupancyVolume)
	{
		return;
	}

	const bool bOccupied = OccupancyState == EPressurePlateOccupancyState::Occupied;
	const FColor StateColor = bOccupied ? FColor::Orange : FColor::Blue;
	DrawDebugBox(
		World,
		OccupancyVolume->Bounds.Origin,
		OccupancyVolume->Bounds.BoxExtent,
		OccupancyVolume->GetComponentQuat(),
		StateColor,
		false,
		0.0f,
		0,
		2.0f);

	AActor* Occupant = CurrentOccupant.Get();
	if (Occupant)
	{
		DrawDebugLine(
			World,
			OccupancyVolume->Bounds.Origin,
			Occupant->GetActorLocation(),
			FColor::Yellow,
			false,
			0.0f,
			0,
			1.5f);
	}

	const UEnum* OccupancyEnum = StaticEnum<EPressurePlateOccupancyState>();
	const UEnum* SwitchModeEnum = StaticEnum<EPuzzleSwitchMode>();
	const UEnum* InputStateEnum = StaticEnum<EPuzzleSwitchInputState>();
	const UEnum* InitialInputStateEnum = StaticEnum<EPuzzleSwitchInitialInputState>();
	const FString OccupancyName = OccupancyEnum
		? OccupancyEnum->GetNameStringByValue(static_cast<int64>(OccupancyState))
		: TEXT("Unknown");
	const FString SwitchModeName = SwitchModeEnum
		? SwitchModeEnum->GetNameStringByValue(static_cast<int64>(SwitchMode))
		: TEXT("Unknown");
	const FString InputStateName = InputStateEnum
		? InputStateEnum->GetNameStringByValue(static_cast<int64>(GetInputState()))
		: TEXT("Unknown");
	const FString InitialInputStateName = InitialInputStateEnum
		? InitialInputStateEnum->GetNameStringByValue(static_cast<int64>(InitialInputState))
		: TEXT("Unknown");
	const float RemainingDuration = bIsPlateMoving
		? FMath::Max(0.0f, ActiveMovementDuration - MovementElapsedSeconds)
		: 0.0f;
	FString RequiredTagsText = TEXT("<Any>");
	if (!RequiredOccupantActorTags.IsEmpty())
	{
		TArray<FString> RequiredTagStrings;
		RequiredTagStrings.Reserve(RequiredOccupantActorTags.Num());
		for (const FName RequiredTag : RequiredOccupantActorTags)
		{
			RequiredTagStrings.Add(RequiredTag.ToString());
		}
		RequiredTagsText = FString::Join(RequiredTagStrings, TEXT(", "));
	}
	const FString Label = FString::Printf(
		TEXT("%s\nOccupancy: %s  Occupant: %s  Components: %d\nRequired Actor Tags: %s  Actor Tag Match: %s\nMode: %s  Input: %s  Initial: %s  Output: %s\nAlpha: %.2f -> %.2f  Moving: %s  Remaining: %.2fs\nWorldState Guard: %s  Noise Press/Release: %s/%s"),
		*GetNameSafe(this),
		*OccupancyName,
		*GetNameSafe(Occupant),
		CurrentOccupantComponents.Num(),
		*RequiredTagsText,
		Occupant && HasRequiredOccupantActorTags(Occupant) ? TEXT("true") : TEXT("false"),
		*SwitchModeName,
		*InputStateName,
		*InitialInputStateName,
		IsSwitchActive() ? TEXT("Active") : TEXT("Inactive"),
		PlateMovementAlpha,
		TargetPlateMovementAlpha,
		bIsPlateMoving ? (TargetPlateMovementAlpha > PlateMovementAlpha ? TEXT("Down") : TEXT("Up")) : TEXT("No"),
		RemainingDuration,
		bIsApplyingWorldState ? TEXT("true") : TEXT("false"),
		bEmitNoiseOnPressMovement ? TEXT("On") : TEXT("Off"),
		bEmitNoiseOnReleaseMovement ? TEXT("On") : TEXT("Off"));

	DrawDebugString(
		World,
		OccupancyVolume->Bounds.Origin + FVector(0.0f, 0.0f, DebugVerticalOffset + 80.0f),
		Label,
		nullptr,
		FColor::White,
		0.0f,
		true);
}

bool APressurePlate::ShouldDrawPressurePlateDebug() const
{
	return bEnableDebug && IsParadoxPressurePlateDebugEnabled();
}

#undef LOCTEXT_NAMESPACE
