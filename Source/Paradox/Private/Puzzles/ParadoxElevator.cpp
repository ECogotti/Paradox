#include "Puzzles/ParadoxElevator.h"

#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/GridNavigationModifierComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Paradox.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Sound/SoundBase.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "TimerManager.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ParadoxElevator"

AParadoxElevator::AParadoxElevator()
{
	MovementMode = EPuzzleTransformMoverMode::FlipFlop;
	DeactivationBehavior = EPuzzleTransformMoverDeactivationBehavior::Continue;
	// Initialization must use the overridable Receiver policy even if an external Receiver is active.
	bAnimateInitialReceiverState = true;
	bWaitForClearPassage = false;
	bGenerateNavigationAtStableEndpoints = true;
	RequiredOccupantActorTags.Reset();
	if (GridNavigationModifier)
	{
		GridNavigationModifier->bBlockCells = false;
	}
	if (SelectableComponent)
	{
		SelectableComponent->bShowPuzzleConnectionsWhenSelected = true;
	}
	if (PuzzleReceiver)
	{
		PuzzleReceiver->ActivationMode = EPuzzleReceiverActivationMode::Manual;
		PuzzleReceiver->bActivateWhenUncontrolled = false;
		PuzzleReceiver->bAllowManualActivationWithoutController = true;
	}

	// The inherited volume acquires passengers anywhere on the platform, not only over the button.
	if (PassageOccupancyVolume)
	{
		PassageOccupancyVolume->InitBoxExtent(FVector(75.0f, 75.0f, 80.0f));
		PassageOccupancyVolume->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	}

	ButtonMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ButtonMesh"));
	ButtonMesh->SetupAttachment(BarrierMesh);
	ButtonMesh->SetMobility(EComponentMobility::Movable);
	ButtonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ButtonMesh->SetCanEverAffectNavigation(false);

	ButtonMovementAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("ButtonMovementAudio"));
	ButtonMovementAudio->SetupAttachment(ButtonMesh);
	ButtonMovementAudio->SetAutoActivate(false);

	ButtonMovementVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ButtonMovementVFX"));
	ButtonMovementVFX->SetupAttachment(ButtonMesh);
	ButtonMovementVFX->SetAutoActivate(false);

	ButtonOccupancyVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ButtonOccupancyVolume"));
	ButtonOccupancyVolume->SetupAttachment(BarrierMesh);
	ButtonOccupancyVolume->SetMobility(EComponentMobility::Movable);
	ButtonOccupancyVolume->InitBoxExtent(FVector(35.0f, 35.0f, 40.0f));
	ButtonOccupancyVolume->SetRelativeLocation(FVector(0.0f, 0.0f, 40.0f));
	ButtonOccupancyVolume->SetCollisionProfileName(TEXT("Trigger"));
	ButtonOccupancyVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ButtonOccupancyVolume->SetGenerateOverlapEvents(true);
	ButtonOccupancyVolume->SetSimulatePhysics(false);
	ButtonOccupancyVolume->SetCanEverAffectNavigation(false);
	ButtonOccupancyVolume->bDynamicObstacle = false;
}

void AParadoxElevator::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	// Blueprint component templates authored before this policy changed may retain the old values.
	if (SelectableComponent)
	{
		SelectableComponent->bShowPuzzleConnectionsWhenSelected = true;
	}
	if (PuzzleReceiver)
	{
		PuzzleReceiver->ActivationMode = EPuzzleReceiverActivationMode::Manual;
		PuzzleReceiver->bActivateWhenUncontrolled = false;
		PuzzleReceiver->bAllowManualActivationWithoutController = true;
	}
}

void AParadoxElevator::BeginPlay()
{
	Super::BeginPlay();
	RaisedButtonRelativeTransform = ButtonMesh ? ButtonMesh->GetRelativeTransform() : FTransform::Identity;
	DefaultButtonMovementSound = ButtonMovementAudio ? ButtonMovementAudio->GetSound() : nullptr;
	DefaultButtonMovementNiagaraSystem = ButtonMovementVFX ? ButtonMovementVFX->GetAsset() : nullptr;
	StopButtonFeedback();
	bButtonInitialized = true;
	bButtonArmed = true;
	if (ButtonOccupancyVolume)
	{
		ButtonOccupancyVolume->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::HandleButtonBeginOverlap);
		ButtonOccupancyVolume->OnComponentEndOverlap.AddUniqueDynamic(this, &ThisClass::HandleButtonEndOverlap);
	}
	if (PuzzleReceiver)
	{
		PuzzleReceiver->OnReceiverActivationPrerequisitesChangedNative.AddUObject(
			this, &ThisClass::HandleReceiverPrerequisitesChanged);
	}
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreRestore.AddUniqueDynamic(this, &ThisClass::HandleElevatorWorldStatePreRestore);
	}
	if (UWorld* World = GetWorld())
	{
		if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldStateRestoreCompletedHandle = WorldState->OnRestoreCompletedNative().AddUObject(
				this, &ThisClass::HandleWorldStateRestoreFinished);
			WorldStateRestoreFailedHandle = WorldState->OnRestoreFailedNative().AddUObject(
				this, &ThisClass::HandleWorldStateRestoreFinished);
		}
	}
	ApplyButtonAlpha(0.0f);
	LogButtonState(TEXT("BeginPlay"));
	if (bLogButtonDiagnostics && ButtonOccupancyVolume)
	{
		PARADOX_LOG_INFO(TEXT("[ElevatorButton] %s trigger=%s collision=%d overlapEvents=%d pawnResponse=%d center=%s extent=%s buttonMesh=%s pressDepth=%.1f pressDuration=%.3f"),
			*GetNameSafe(this), *GetNameSafe(ButtonOccupancyVolume.Get()),
			static_cast<int32>(ButtonOccupancyVolume->GetCollisionEnabled()),
			ButtonOccupancyVolume->GetGenerateOverlapEvents(),
			static_cast<int32>(ButtonOccupancyVolume->GetCollisionResponseToChannel(ECC_Pawn)),
			*ButtonOccupancyVolume->GetComponentLocation().ToCompactString(),
			*ButtonOccupancyVolume->GetScaledBoxExtent().ToCompactString(),
			*GetNameSafe(ButtonMesh.Get()), PressDepth, PressDuration);
	}
	RefreshButtonOccupancy();
	LogButtonState(TEXT("BeginPlayAfterRefresh"));
}

void AParadoxElevator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	LogButtonState(TEXT("EndPlay"));
	bButtonInitialized = false;
	bSuppressButtonActivation = true;
	bButtonPressPending = false;
	StopButtonAnimation();
	if (ButtonOccupancyVolume)
	{
		ButtonOccupancyVolume->OnComponentBeginOverlap.RemoveDynamic(this, &ThisClass::HandleButtonBeginOverlap);
		ButtonOccupancyVolume->OnComponentEndOverlap.RemoveDynamic(this, &ThisClass::HandleButtonEndOverlap);
	}
	if (PuzzleReceiver)
	{
		PuzzleReceiver->OnReceiverActivationPrerequisitesChangedNative.RemoveAll(this);
	}
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreRestore.RemoveDynamic(this, &ThisClass::HandleElevatorWorldStatePreRestore);
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
	for (const TWeakObjectPtr<AActor>& Occupant : ButtonOccupants)
	{
		if (AActor* Actor = Occupant.Get())
		{
			Actor->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleButtonOccupantDestroyed);
		}
	}
	ButtonOccupants.Reset();
	Super::EndPlay(EndPlayReason);
}

void AParadoxElevator::Tick(const float DeltaSeconds)
{
	const bool bWasButtonAnimating = bButtonAnimating;
	Super::Tick(DeltaSeconds);
	if (bWasButtonAnimating && bButtonAnimating)
	{
		AdvanceButtonAnimation(DeltaSeconds);
	}
	if (bEnableDebug && IsParadoxVerticalBarrierDebugEnabled())
	{
		RefreshButtonDebug();
	}
	
}

void AParadoxElevator::ResetMover()
{
	LogButtonState(TEXT("ResetBegin"));
	const bool bPreviouslySuppressed = bSuppressButtonActivation;
	bSuppressButtonActivation = true;
	bButtonPressPending = false;
	StopButtonAnimation();
	if (PuzzleReceiver && PuzzleReceiver->IsManualActivationRequested())
	{
		PuzzleReceiver->RequestManualDeactivation();
	}
	Super::ResetMover();
	RebuildButtonAfterAuthorityChange();
	bSuppressButtonActivation = bPreviouslySuppressed;
	LogButtonState(TEXT("ResetEnd"));
}

bool AParadoxElevator::IsPassageOpen() const
{
	return (IsAtStart() || IsAtEnd()) && !IsPassageBlockingNavigation();
}

#if WITH_EDITOR
EDataValidationResult AParadoxElevator::IsDataValid(FDataValidationContext& Context) const
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
	if (!ButtonMesh || ButtonMesh->GetAttachParent() != BarrierMesh.Get()
		|| ButtonMesh->Mobility != EComponentMobility::Movable
		|| ButtonMesh->CanEverAffectNavigation())
	{
		AddError(LOCTEXT("ButtonMesh", "Elevator requires a movable ButtonMesh attached to BarrierMesh with navigation influence disabled."));
	}
	if (!ButtonOccupancyVolume || ButtonOccupancyVolume->GetAttachParent() != BarrierMesh.Get()
		|| !ButtonOccupancyVolume->GetGenerateOverlapEvents()
		|| !CollisionEnabledHasQuery(ButtonOccupancyVolume->GetCollisionEnabled())
		|| ButtonOccupancyVolume->CanEverAffectNavigation())
	{
		AddError(LOCTEXT("ButtonVolume", "Elevator requires a query-enabled ButtonOccupancyVolume attached to BarrierMesh."));
	}
	if (bWaitForClearPassage || !bGenerateNavigationAtStableEndpoints
		|| MovementMode != EPuzzleTransformMoverMode::FlipFlop
		|| DeactivationBehavior != EPuzzleTransformMoverDeactivationBehavior::Continue
		|| !bAnimateInitialReceiverState || !RequiredOccupantActorTags.IsEmpty()
		|| !PuzzleReceiver || PuzzleReceiver->ActivationMode != EPuzzleReceiverActivationMode::Manual
		|| !PuzzleReceiver->bAllowManualActivationWithoutController)
	{
		AddError(LOCTEXT("ElevatorPolicy", "Elevator requires its native transport, navigation, and local-button movement policy."));
	}
	if (!FMath::IsFinite(PressDepth) || PressDepth < 0.0f
		|| !FMath::IsFinite(PressDuration) || PressDuration < 0.0f
		|| !FMath::IsFinite(ReleaseDuration) || ReleaseDuration < 0.0f)
	{
		AddError(LOCTEXT("ButtonTiming", "Elevator button depth and durations must be finite and non-negative."));
	}
	for (const FName Tag : RequiredButtonActorTags)
	{
		if (Tag.IsNone())
		{
			AddError(LOCTEXT("ButtonTag", "Elevator RequiredButtonActorTags cannot contain None."));
			break;
		}
	}
	return Result;
}

bool AParadoxElevator::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		const FName Name = InProperty->GetFName();
		if (Name == GET_MEMBER_NAME_CHECKED(AParadoxVerticalBarrier, bWaitForClearPassage)
			|| Name == GET_MEMBER_NAME_CHECKED(AParadoxVerticalBarrier, bGenerateNavigationAtStableEndpoints)
			|| Name == GET_MEMBER_NAME_CHECKED(AParadoxVerticalBarrier, RequiredOccupantActorTags)
			|| Name == GET_MEMBER_NAME_CHECKED(APuzzleTransformMover, MovementMode)
			|| Name == GET_MEMBER_NAME_CHECKED(APuzzleTransformMover, DeactivationBehavior)
			|| Name == GET_MEMBER_NAME_CHECKED(APuzzleTransformMover, bAnimateInitialReceiverState)
			|| Name == GET_MEMBER_NAME_CHECKED(APuzzleTransformMover, DefaultMovedComponent))
		{
			return false;
		}
	}
	return Super::CanEditChange(InProperty);
}
#endif

bool AParadoxElevator::RefreshButtonOccupancy()
{
	const bool bTraceRefresh = bLogButtonDiagnostics && !IsMoving();
	if (bTraceRefresh)
	{
		LogButtonState(TEXT("RefreshRequested"));
	}
	if (!bButtonInitialized || !ButtonOccupancyVolume)
	{
		PARADOX_LOG_WARNING(TEXT("Elevator '%s' cannot refresh its button before initialization or without ButtonOccupancyVolume."), *GetNameSafe(this));
		return false;
	}
	const bool bReconciled = ReconcileButtonOccupancy(nullptr, nullptr, true);
	if (bTraceRefresh)
	{
		LogButtonState(bReconciled ? TEXT("RefreshCompleted") : TEXT("RefreshFailed"));
	}
	return bReconciled;
}

bool AParadoxElevator::IsButtonEnabled() const
{
	return PuzzleReceiver
		&& PuzzleReceiver->GetActivationMode() == EPuzzleReceiverActivationMode::Manual
		&& PuzzleReceiver->AreActivationPrerequisitesSatisfied()
		&& !PuzzleReceiver->IsManualActivationRequested();
}

void AParadoxElevator::HandleButtonPressed_Implementation()
{
}

void AParadoxElevator::HandleButtonReleased_Implementation()
{
}

void AParadoxElevator::HandleButtonMovementCompleted_Implementation(const bool bIsPressed)
{
}

bool AParadoxElevator::CanActorActivateButton_Implementation(AActor* Candidate, UPrimitiveComponent* CandidateComponent) const
{
	return true;
}

bool AParadoxElevator::ShouldBlockPassageAtEndpoint(const EPuzzleTransformMoverTarget Endpoint) const
{
	return false;
}

bool AParadoxElevator::ShouldMoverTick() const
{
	return Super::ShouldMoverTick() || ShouldElevatorTick();
}

bool AParadoxElevator::ShouldElevatorTick() const
{
	return bButtonInitialized && bButtonAnimating && !bWorldStateRestoring;
}

bool AParadoxElevator::ShouldProcessReceiverStateNative(const bool bReceiverActive)
{
	const bool bProcess = bReceiverActive && bRequestFromButton && bButtonInitialized && !bWorldStateRestoring;
	if (bLogButtonDiagnostics)
	{
		PARADOX_LOG_INFO(TEXT("[ElevatorButton] %s ReceiverStateChanged active=%d processMovement=%d requestFromButton=%d"),
			*GetNameSafe(this), bReceiverActive, bProcess, bRequestFromButton);
	}
	return bProcess;
}

EPuzzleTransformMoverRequestDecision AParadoxElevator::EvaluateMovementRequestNative(
	const EPuzzleTransformMoverTarget RequestedTarget)
{
	if (!bRequestFromButton || !bButtonInitialized || bWorldStateRestoring
		|| !PuzzleReceiver || !PuzzleReceiver->IsReceiverActive()
		|| GetMovedComponent() != BarrierMesh.Get())
	{
		if (bLogButtonDiagnostics)
		{
			PARADOX_LOG_INFO(TEXT("[ElevatorButton] %s MovementRequest rejected target=%d fromButton=%d initialized=%d restoring=%d receiverActive=%d movedComponent=%s platform=%s"),
				*GetNameSafe(this), static_cast<int32>(RequestedTarget), bRequestFromButton,
				bButtonInitialized, bWorldStateRestoring,
				PuzzleReceiver && PuzzleReceiver->IsReceiverActive(),
				*GetNameSafe(GetMovedComponent()), *GetNameSafe(BarrierMesh.Get()));
		}
		return EPuzzleTransformMoverRequestDecision::Reject;
	}
	const EPuzzleTransformMoverRequestDecision Decision = Super::EvaluateMovementRequestNative(RequestedTarget);
	if (bLogButtonDiagnostics)
	{
		PARADOX_LOG_INFO(TEXT("[ElevatorButton] %s MovementRequest target=%d decision=%d"),
			*GetNameSafe(this), static_cast<int32>(RequestedTarget), static_cast<int32>(Decision));
	}
	return Decision;
}

void AParadoxElevator::OnMovementUpdatedNative(const float CurrentMovementAlpha, const float CurrentEasedAlpha)
{
	Super::OnMovementUpdatedNative(CurrentMovementAlpha, CurrentEasedAlpha);
	if (bButtonInitialized && !bWorldStateRestoring)
	{
		RefreshButtonOccupancy();
	}
}

void AParadoxElevator::OnReachedStartNative()
{
	LogButtonState(TEXT("ReachedStartBeforeRelease"));
	Super::OnReachedStartNative();
	if (PuzzleReceiver && PuzzleReceiver->IsManualActivationRequested())
	{
		PuzzleReceiver->RequestManualDeactivation();
	}
	RefreshButtonOccupancy();
	bButtonPressed = false;
	StartButtonAnimation(false);
	bButtonArmed = ButtonOccupants.IsEmpty();
	LogButtonState(TEXT("ReachedStartAfterRelease"));
}

void AParadoxElevator::OnReachedEndNative()
{
	LogButtonState(TEXT("ReachedEndBeforeRelease"));
	Super::OnReachedEndNative();
	if (PuzzleReceiver && PuzzleReceiver->IsManualActivationRequested())
	{
		PuzzleReceiver->RequestManualDeactivation();
	}
	RefreshButtonOccupancy();
	bButtonPressed = false;
	StartButtonAnimation(false);
	bButtonArmed = ButtonOccupants.IsEmpty();
	LogButtonState(TEXT("ReachedEndAfterRelease"));
}

void AParadoxElevator::HandleButtonBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	LogButtonState(TEXT("BeginOverlap"), OtherActor, OtherComponent);
	if (bButtonInitialized && !bWorldStateRestoring)
	{
		ReconcileButtonOccupancy(nullptr, OtherComponent, false);
	}
	else
	{
		LogButtonState(TEXT("BeginOverlapIgnored"), OtherActor, OtherComponent);
	}
}

void AParadoxElevator::HandleButtonEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex)
{
	LogButtonState(TEXT("EndOverlap"), OtherActor, OtherComponent);
	if (bButtonInitialized && !bWorldStateRestoring)
	{
		ReconcileButtonOccupancy(OtherComponent, nullptr, false);
	}
	else
	{
		LogButtonState(TEXT("EndOverlapIgnored"), OtherActor, OtherComponent);
	}
}

void AParadoxElevator::HandleReceiverPrerequisitesChanged(
	UPuzzleReceiverComponent* ChangedReceiver, const bool bPrerequisitesSatisfied)
{
	if (bLogButtonDiagnostics)
	{
		PARADOX_LOG_INFO(TEXT("[ElevatorButton] %s ReceiverPrerequisitesChanged receiver=%s satisfied=%d expectedReceiver=%s"),
			*GetNameSafe(this), *GetNameSafe(ChangedReceiver), bPrerequisitesSatisfied,
			*GetNameSafe(PuzzleReceiver.Get()));
	}
	LogButtonState(TEXT("PrerequisitesCallback"));
	if (ChangedReceiver != PuzzleReceiver || !bButtonInitialized || bWorldStateRestoring)
	{
		LogButtonState(TEXT("PrerequisitesCallbackIgnored"));
		return;
	}
	if (!bPrerequisitesSatisfied && bButtonPressPending)
	{
		CancelPendingButtonPress();
	}
	else if (bPrerequisitesSatisfied && bButtonArmed)
	{
		RefreshButtonOccupancy();
	}
	LogButtonState(TEXT("PrerequisitesCallbackHandled"));
}

bool AParadoxElevator::IsButtonCandidateAccepted(AActor* Actor, UPrimitiveComponent* Component) const
{
	if (!IsValid(Actor) || Actor == this || Actor->IsActorBeingDestroyed()
		|| !IsValid(Component) || Component->GetOwner() != Actor || Actor->IsOwnedBy(this))
	{
		return false;
	}
	if (!Actor->IsA<ACharacter>())
	{
		for (const FName Tag : RequiredButtonActorTags)
		{
			if (Tag.IsNone() || !Actor->ActorHasTag(Tag))
			{
				return false;
			}
		}
	}
	return CanActorActivateButton(Actor, Component);
}

bool AParadoxElevator::ReconcileButtonOccupancy(
	UPrimitiveComponent* ExcludedComponent,
	UPrimitiveComponent* IncludedComponent,
	const bool bUpdateOverlaps)
{
	if (!bButtonInitialized || !ButtonOccupancyVolume || bButtonReconciliationInProgress)
	{
		if (bLogButtonDiagnostics && (IncludedComponent || ExcludedComponent))
		{
			PARADOX_LOG_INFO(TEXT("[ElevatorButton] %s ReconcileSkipped initialized=%d trigger=%s inProgress=%d included=%s excluded=%s"),
				*GetNameSafe(this), bButtonInitialized, *GetNameSafe(ButtonOccupancyVolume.Get()),
				bButtonReconciliationInProgress, *GetNameSafe(IncludedComponent), *GetNameSafe(ExcludedComponent));
		}
		return false;
	}
	TGuardValue<bool> ReconciliationGuard(bButtonReconciliationInProgress, true);
	const int32 PreviousOccupantCount = ButtonOccupants.Num();
	const bool bPreviousValidOccupant = bLastReconciledHasValidOccupant;
	const bool bPreviouslyArmed = bButtonArmed;
	const bool bPreviouslyPressPending = bButtonPressPending;
	if (bUpdateOverlaps)
	{
		ButtonOccupancyVolume->UpdateOverlaps(nullptr, false);
	}
	TArray<UPrimitiveComponent*> Components;
	ButtonOccupancyVolume->GetOverlappingComponents(Components);
	if (IsValid(IncludedComponent))
	{
		Components.AddUnique(IncludedComponent);
	}
	TSet<TWeakObjectPtr<AActor>> NewOccupants;
	bool bHasValidOccupant = false;
	for (UPrimitiveComponent* Component : Components)
	{
		if (Component != ExcludedComponent)
		{
			AActor* Actor = Component ? Component->GetOwner() : nullptr;
			if (IsValid(Actor) && Actor != this && !Actor->IsActorBeingDestroyed()
				&& IsValid(Component) && Component->GetOwner() == Actor && !Actor->IsOwnedBy(this))
			{
				const bool bAccepted = IsButtonCandidateAccepted(Actor, Component);
				if (bLogButtonDiagnostics && !ButtonOccupants.Contains(TWeakObjectPtr<AActor>(Actor)))
				{
					FName FirstMissingTag = NAME_None;
					if (!Actor->IsA<ACharacter>())
					{
						for (const FName Tag : RequiredButtonActorTags)
						{
							if (Tag.IsNone() || !Actor->ActorHasTag(Tag))
							{
								FirstMissingTag = Tag;
								break;
							}
						}
					}
					PARADOX_LOG_INFO(TEXT("[ElevatorButton] %s Candidate actor=%s component=%s character=%d accepted=%d firstMissingActorTag=%s requiredTagCount=%d"),
						*GetNameSafe(this), *GetNameSafe(Actor), *GetNameSafe(Component), Actor->IsA<ACharacter>(),
						bAccepted, *FirstMissingTag.ToString(), RequiredButtonActorTags.Num());
				}
				NewOccupants.Add(Actor);
				bHasValidOccupant |= bAccepted;
			}
		}
	}
	for (const TWeakObjectPtr<AActor>& OldOccupant : ButtonOccupants)
	{
		if (!NewOccupants.Contains(OldOccupant))
		{
			if (AActor* Actor = OldOccupant.Get())
			{
				Actor->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleButtonOccupantDestroyed);
			}
		}
	}
	for (const TWeakObjectPtr<AActor>& NewOccupant : NewOccupants)
	{
		if (!ButtonOccupants.Contains(NewOccupant))
		{
			if (AActor* Actor = NewOccupant.Get())
			{
				Actor->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleButtonOccupantDestroyed);
			}
		}
	}
	ButtonOccupants = MoveTemp(NewOccupants);
	if (bButtonPressPending && (!bHasValidOccupant || !IsButtonEnabled()))
	{
		CancelPendingButtonPress();
	}
	if (ButtonOccupants.IsEmpty())
	{
		if (!IsMoving() && !bButtonPressPending && !bButtonPressed)
		{
			bButtonArmed = true;
		}
	}
	else if (bHasValidOccupant && bButtonArmed && IsButtonEnabled()
		&& !bSuppressButtonActivation && !bWorldStateRestoring)
	{
		BeginButtonPress();
	}
	bLastReconciledHasValidOccupant = bHasValidOccupant;
	if (bLogButtonDiagnostics && (IncludedComponent || ExcludedComponent
		|| PreviousOccupantCount != ButtonOccupants.Num()
		|| bPreviousValidOccupant != bHasValidOccupant
		|| bPreviouslyArmed != bButtonArmed
		|| bPreviouslyPressPending != bButtonPressPending))
	{
		PARADOX_LOG_INFO(TEXT("[ElevatorButton] %s Reconciled components=%d occupants=%d validOccupant=%d previouslyValid=%d included=%s excluded=%s"),
			*GetNameSafe(this), Components.Num(), ButtonOccupants.Num(), bHasValidOccupant,
			bPreviousValidOccupant, *GetNameSafe(IncludedComponent), *GetNameSafe(ExcludedComponent));
		LogButtonState(TEXT("Reconciled"));
	}
	return true;
}

void AParadoxElevator::BeginButtonPress()
{
	if (!IsAtStart() && !IsAtEnd())
	{
		LogButtonState(TEXT("PressRejectedNotAtEndpoint"));
		return;
	}
	LogButtonState(TEXT("PressBegin"));
	bButtonArmed = false;
	bButtonPressPending = true;
	bButtonPressed = true;
	StartButtonAnimation(true);
}

void AParadoxElevator::CancelPendingButtonPress()
{
	LogButtonState(TEXT("PressCancelled"));
	bButtonPressPending = false;
	bButtonPressed = false;
	StartButtonAnimation(false);
}

void AParadoxElevator::TryStartJourney()
{
	if (!bButtonPressPending || !IsButtonEnabled() || ButtonMovementAlpha < 1.0f - KINDA_SMALL_NUMBER
		|| (!IsAtStart() && !IsAtEnd()))
	{
		LogButtonState(TEXT("JourneyGuardRejected"));
		return;
	}
	LogButtonState(TEXT("ManualActivationRequest"));
	bRequestFromButton = true;
	const FPuzzleReceiverActivationCommandResult ActivationResult = PuzzleReceiver->RequestManualActivation();
	bRequestFromButton = false;
	if (bLogButtonDiagnostics)
	{
		PARADOX_LOG_INFO(TEXT("[ElevatorButton] %s ManualActivationResult status=%d accepted=%d prerequisites=%d manualRequested=%d receiverActive=%d moving=%d diagnostic=%s"),
			*GetNameSafe(this), static_cast<int32>(ActivationResult.Status), ActivationResult.WasAccepted(),
			ActivationResult.bPrerequisitesSatisfied, ActivationResult.bManualActivationRequested,
			ActivationResult.bReceiverActive, IsMoving(), *ActivationResult.DiagnosticMessage);
	}
	if (ActivationResult.Status == EPuzzleReceiverActivationCommandStatus::SupersededDuringNotification)
	{
		LogButtonState(TEXT("ManualActivationDeferred"));
		QueueButtonActivationRetry();
		return;
	}
	bButtonPressPending = false;
	if (!ActivationResult.WasAccepted() || !IsMoving())
	{
		PARADOX_LOG_WARNING(TEXT("Elevator '%s' could not start a trip from its occupied button: %s"),
			*GetNameSafe(this), *ActivationResult.DiagnosticMessage);
		if (PuzzleReceiver->IsManualActivationRequested())
		{
			PuzzleReceiver->RequestManualDeactivation();
		}
		bButtonPressed = false;
		StartButtonAnimation(false);
		return;
	}
	LogButtonState(TEXT("JourneyStarted"));
}

void AParadoxElevator::QueueButtonActivationRetry()
{
	if (bButtonActivationRetryQueued || !GetWorld())
	{
		LogButtonState(TEXT("ActivationRetryNotQueued"));
		return;
	}
	bButtonActivationRetryQueued = true;
	LogButtonState(TEXT("ActivationRetryQueued"));
	GetWorldTimerManager().SetTimerForNextTick([WeakThis = TWeakObjectPtr<AParadoxElevator>(this)]()
	{
		if (AParadoxElevator* Elevator = WeakThis.Get())
		{
			Elevator->bButtonActivationRetryQueued = false;
			Elevator->LogButtonState(TEXT("ActivationRetryFired"));
			if (Elevator->bButtonPressPending && !Elevator->bWorldStateRestoring)
			{
				Elevator->RefreshButtonOccupancy();
				Elevator->TryStartJourney();
			}
		}
	});
}

void AParadoxElevator::StartButtonAnimation(const bool bPressed)
{
	StopButtonAnimation();
	ButtonAnimationTargetAlpha = bPressed ? 1.0f : 0.0f;
	ButtonAnimationStartAlpha = ButtonMovementAlpha;
	ButtonAnimationElapsed = 0.0f;
	const float Remaining = FMath::Abs(ButtonAnimationTargetAlpha - ButtonAnimationStartAlpha);
	ButtonAnimationDuration = Remaining * FMath::Max(0.0f, bPressed ? PressDuration : ReleaseDuration);
	if (bLogButtonDiagnostics)
	{
		PARADOX_LOG_INFO(TEXT("[ElevatorButton] %s ButtonAnimation targetPressed=%d startAlpha=%.3f targetAlpha=%.3f duration=%.3f depth=%.1f"),
			*GetNameSafe(this), bPressed, ButtonAnimationStartAlpha, ButtonAnimationTargetAlpha,
			ButtonAnimationDuration, PressDepth);
	}
	bButtonAnimating = true;
	RefreshMovementTickState();
	if (bButtonInitialized && !bSuppressButtonActivation && !bWorldStateRestoring)
	{
		StartButtonFeedback(bPressed);
		if (bPressed)
		{
			HandleButtonPressed();
		}
		else
		{
			HandleButtonReleased();
		}
	}
	// A Blueprint presentation hook may reset the elevator or cancel this press synchronously.
	if (!bButtonAnimating || ButtonAnimationTargetAlpha != (bPressed ? 1.0f : 0.0f))
	{
		return;
	}
	if (Remaining <= KINDA_SMALL_NUMBER || PressDepth <= KINDA_SMALL_NUMBER
		|| ButtonAnimationDuration <= KINDA_SMALL_NUMBER || !GetWorld())
	{
		ApplyButtonAlpha(ButtonAnimationTargetAlpha);
		FinishButtonAnimation();
		return;
	}
}

void AParadoxElevator::AdvanceButtonAnimation(const float DeltaSeconds)
{
	if (!bButtonAnimating || !bButtonInitialized || bWorldStateRestoring)
	{
		return;
	}
	ButtonAnimationElapsed += FMath::Max(0.0f, DeltaSeconds);
	const float TimeAlpha = FMath::Clamp(ButtonAnimationElapsed / ButtonAnimationDuration, 0.0f, 1.0f);
	const float SmoothAlpha = FMath::SmoothStep(0.0f, 1.0f, TimeAlpha);
	ApplyButtonAlpha(FMath::Lerp(ButtonAnimationStartAlpha, ButtonAnimationTargetAlpha, SmoothAlpha));
	if (TimeAlpha >= 1.0f)
	{
		FinishButtonAnimation();
	}
}

void AParadoxElevator::StopButtonAnimation()
{
	bButtonAnimating = false;
	StopButtonFeedback();
	RefreshMovementTickState();
}

void AParadoxElevator::ApplyButtonAlpha(const float Alpha)
{
	ButtonMovementAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (!ButtonMesh)
	{
		return;
	}
	FTransform ButtonTransform = RaisedButtonRelativeTransform;
	ButtonTransform.SetLocation(
		RaisedButtonRelativeTransform.GetLocation()
			+ FVector(0.0f, 0.0f, -FMath::Max(0.0f, PressDepth) * ButtonMovementAlpha));
	ButtonMesh->SetRelativeTransform(ButtonTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

void AParadoxElevator::FinishButtonAnimation()
{
	const bool bCompletedPress = ButtonAnimationTargetAlpha >= 1.0f && bButtonPressPending;
	LogButtonState(bCompletedPress ? TEXT("PressAnimationComplete") : TEXT("ReleaseAnimationComplete"));
	bButtonAnimating = false;
	ButtonAnimationElapsed = 0.0f;
	ButtonAnimationDuration = 0.0f;
	if (ButtonMovementVFX)
	{
		ButtonMovementVFX->Deactivate();
	}
	RefreshMovementTickState();
	if (bButtonInitialized && !bSuppressButtonActivation && !bWorldStateRestoring)
	{
		HandleButtonMovementCompleted(ButtonAnimationTargetAlpha >= 1.0f);
	}
	if (!bButtonInitialized || bSuppressButtonActivation || bWorldStateRestoring)
	{
		return;
	}
	if (bCompletedPress)
	{
		RefreshButtonOccupancy();
		TryStartJourney();
		return;
	}
	if (!bButtonPressed && !IsMoving() && ButtonOccupants.IsEmpty())
	{
		bButtonArmed = true;
	}
}

void AParadoxElevator::StartButtonFeedback(const bool bPressed)
{
	if (!bButtonInitialized || bSuppressButtonActivation || bWorldStateRestoring)
	{
		return;
	}
	if (ButtonMovementAudio)
	{
		ButtonMovementAudio->Stop();
		USoundBase* SelectedSound = bPressed ? PressSound.Get() : ReleaseSound.Get();
		ButtonMovementAudio->SetSound(SelectedSound ? SelectedSound : DefaultButtonMovementSound.Get());
		if (ButtonMovementAudio->GetSound())
		{
			ButtonMovementAudio->Play();
		}
	}
	if (ButtonMovementVFX)
	{
		ButtonMovementVFX->Deactivate();
		UNiagaraSystem* SelectedSystem = bPressed ? PressNiagaraSystem.Get() : ReleaseNiagaraSystem.Get();
		ButtonMovementVFX->SetAsset(SelectedSystem ? SelectedSystem : DefaultButtonMovementNiagaraSystem.Get());
		if (ButtonMovementVFX->GetAsset())
		{
			ButtonMovementVFX->Activate(true);
		}
	}
}

void AParadoxElevator::StopButtonFeedback()
{
	if (ButtonMovementAudio)
	{
		ButtonMovementAudio->Stop();
	}
	if (ButtonMovementVFX)
	{
		ButtonMovementVFX->Deactivate();
	}
}

void AParadoxElevator::RebuildButtonAfterAuthorityChange()
{
	if (!bButtonInitialized)
	{
		return;
	}
	StopButtonAnimation();
	bButtonPressPending = false;
	if (!IsMoving() && PuzzleReceiver && PuzzleReceiver->IsManualActivationRequested())
	{
		PuzzleReceiver->RequestManualDeactivation();
	}
	bButtonPressed = IsMoving();
	bButtonArmed = false;
	ApplyButtonAlpha(bButtonPressed ? 1.0f : 0.0f);
	RefreshButtonOccupancy();
	bButtonArmed = !bButtonPressed && ButtonOccupants.IsEmpty();
	LogButtonState(TEXT("RebuiltAfterResetOrRestore"));
}

void AParadoxElevator::LogButtonState(
	const TCHAR* Stage, const AActor* OtherActor, const UPrimitiveComponent* OtherComponent) const
{
	if (!bLogButtonDiagnostics)
	{
		return;
	}
	PARADOX_LOG_INFO(TEXT("[ElevatorButton] world=%s elevator=%s stage=%s other=%s component=%s componentOwner=%s initialized=%d restoring=%d suppressed=%d armed=%d pending=%d pressed=%d buttonAlpha=%.3f animating=%d moving=%d atStart=%d atEnd=%d moverState=%d occupants=%d validOccupant=%d enabled=%d receiver=%s receiverMode=%d manualUncontrolled=%d registeredControllers=%d activeRequests=%d prerequisites=%d manualRequested=%d receiverActive=%d"),
		*GetNameSafe(GetWorld()), *GetNameSafe(this), Stage,
		*GetNameSafe(OtherActor), *GetNameSafe(OtherComponent),
		*GetNameSafe(OtherComponent ? OtherComponent->GetOwner() : nullptr),
		bButtonInitialized, bWorldStateRestoring, bSuppressButtonActivation,
		bButtonArmed, bButtonPressPending, bButtonPressed, ButtonMovementAlpha,
		bButtonAnimating, IsMoving(), IsAtStart(), IsAtEnd(), static_cast<int32>(GetMoverState()),
		ButtonOccupants.Num(), bLastReconciledHasValidOccupant, IsButtonEnabled(),
		*GetNameSafe(PuzzleReceiver.Get()),
		PuzzleReceiver ? static_cast<int32>(PuzzleReceiver->GetActivationMode()) : -1,
		PuzzleReceiver && PuzzleReceiver->bAllowManualActivationWithoutController,
		PuzzleReceiver ? PuzzleReceiver->GetRegisteredControllerCount() : 0,
		PuzzleReceiver ? PuzzleReceiver->GetActiveRequestCount() : 0,
		PuzzleReceiver && PuzzleReceiver->AreActivationPrerequisitesSatisfied(),
		PuzzleReceiver && PuzzleReceiver->IsManualActivationRequested(),
		PuzzleReceiver && PuzzleReceiver->IsReceiverActive());
}

void AParadoxElevator::RefreshButtonDebug() const
{
	if (!GetWorld() || !ButtonOccupancyVolume)
	{
		return;
	}
	DrawDebugBox(
		GetWorld(),
		ButtonOccupancyVolume->Bounds.Origin,
		ButtonOccupancyVolume->Bounds.BoxExtent,
		IsButtonOccupied() ? FColor::Orange : (bButtonArmed ? FColor::Green : FColor::Red),
		false,
		0.0f,
		0,
		2.0f);
}

void AParadoxElevator::HandleButtonOccupantDestroyed(AActor* DestroyedActor)
{
	LogButtonState(TEXT("OccupantDestroyed"), DestroyedActor);
	if (bButtonInitialized && !bWorldStateRestoring)
	{
		RefreshButtonOccupancy();
	}
}

void AParadoxElevator::HandleElevatorWorldStatePreRestore(FWorldStateParticipantId ParticipantId)
{
	LogButtonState(TEXT("WorldStatePreRestore"));
	bWorldStateRestoring = true;
	bSuppressButtonActivation = true;
	bButtonPressPending = false;
	StopButtonAnimation();
	bButtonArmed = false;
}

void AParadoxElevator::HandleWorldStateRestoreFinished(const FWorldStateRestoreResult& Result)
{
	LogButtonState(TEXT("WorldStateRestoreFinished"));
	if (!bWorldStateRestoring)
	{
		return;
	}
	RebuildButtonAfterAuthorityChange();
	bWorldStateRestoring = false;
	bSuppressButtonActivation = false;
}

#undef LOCTEXT_NAMESPACE
