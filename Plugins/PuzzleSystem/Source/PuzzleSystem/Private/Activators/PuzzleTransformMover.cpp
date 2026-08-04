#include "Activators/PuzzleTransformMover.h"

#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Curves/CurveFloat.h"
#include "DrawDebugHelpers.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "PuzzleSystem.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "PuzzleTransformMover"

namespace
{
	const TCHAR* PuzzleMoverIconPath = TEXT("/PuzzleSystem/Textures/T_PuzzleControllerIcon.T_PuzzleControllerIcon");
	constexpr float DefaultEndpointDistance = 200.0f;
	constexpr float TransformComparisonTolerance = 0.001f;

	const TCHAR* GetEndpointName(const EPuzzleTransformMoverState State)
	{
		switch (State)
		{
		case EPuzzleTransformMoverState::AtEnd:
		case EPuzzleTransformMoverState::MovingTowardEnd:
			return TEXT("End");

		case EPuzzleTransformMoverState::AtStart:
		case EPuzzleTransformMoverState::MovingTowardStart:
		default:
			return TEXT("Start");
		}
	}
}

APuzzleTransformMover::APuzzleTransformMover()
{
	BillboardRoot = CreateDefaultSubobject<UBillboardComponent>(TEXT("BillboardRoot"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> PuzzleMoverIcon(PuzzleMoverIconPath);
	if (PuzzleMoverIcon.Succeeded() && IsValid(PuzzleMoverIcon.Object))
	{
		BillboardRoot->SetSprite(PuzzleMoverIcon.Object);
	}
	BillboardRoot->SetHiddenInGame(true);
	BillboardRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BillboardRoot->SetCanEverAffectNavigation(false);
	BillboardRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(BillboardRoot);

	StartArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("StartArrow"));
	StartArrow->SetupAttachment(BillboardRoot);
	StartArrow->SetArrowFColor(FColor::Green);
	StartArrow->SetHiddenInGame(true);
	StartArrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StartArrow->SetGenerateOverlapEvents(false);
	StartArrow->SetCanEverAffectNavigation(false);
	StartArrow->SetMobility(EComponentMobility::Movable);

	EndArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("EndArrow"));
	EndArrow->SetupAttachment(BillboardRoot);
	EndArrow->SetRelativeLocation(FVector(DefaultEndpointDistance, 0.0, 0.0));
	EndArrow->SetArrowFColor(FColor::Red);
	EndArrow->SetHiddenInGame(true);
	EndArrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EndArrow->SetGenerateOverlapEvents(false);
	EndArrow->SetCanEverAffectNavigation(false);
	EndArrow->SetMobility(EComponentMobility::Movable);

	PuzzleReceiver = CreateDefaultSubobject<UPuzzleReceiverComponent>(TEXT("PuzzleReceiver"));

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void APuzzleTransformMover::BeginPlay()
{
	Super::BeginPlay();
	InitializePuzzleTransformMover();
}

void APuzzleTransformMover::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PuzzleReceiver)
	{
		PuzzleReceiver->OnReceiverStateChangedNative.Remove(ReceiverStateChangedHandle);
		ReceiverStateChangedHandle.Reset();
	}

	bIsRuntimeInitialized = false;
	bIsInitializingFromReceiver = false;
	bConfigurationValid = false;
	SetActorTickEnabled(false);
	Super::EndPlay(EndPlayReason);
}

void APuzzleTransformMover::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AdvanceMovement(DeltaSeconds);
	if (ShouldDrawMovementDebug())
	{
		DrawMovementDebug();
	}
}

#if WITH_EDITOR
EDataValidationResult APuzzleTransformMover::IsDataValid(FDataValidationContext& Context) const
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

	if (!IsValid(BillboardRoot))
	{
		AddError(LOCTEXT("MissingBillboardRoot", "Puzzle Transform Mover requires its native Billboard Root component."));
	}
	if (!IsValid(StartArrow))
	{
		AddError(LOCTEXT("MissingStartArrow", "Puzzle Transform Mover requires its native Start Arrow component."));
	}
	if (!IsValid(EndArrow))
	{
		AddError(LOCTEXT("MissingEndArrow", "Puzzle Transform Mover requires its native End Arrow component."));
	}
	if (StartArrow == EndArrow)
	{
		AddError(LOCTEXT("DuplicateEndpointMarkers", "Puzzle Transform Mover Start and End arrows must be distinct components."));
	}
	if (!IsValid(PuzzleReceiver))
	{
		AddError(LOCTEXT("MissingReceiver", "Puzzle Transform Mover requires its native Puzzle Receiver component."));
	}

	const USceneComponent* ConfiguredMovedComponent = ResolveDefaultMovedComponent();
	if (!ValidateMovedComponent(ConfiguredMovedComponent, false, false))
	{
		AddError(LOCTEXT("InvalidMovedComponent", "Default Moved Component must be a movable, non-physics scene component owned by this Actor and cannot be an internal marker."));
	}

	if (IsValid(StartArrow) && IsValid(EndArrow))
	{
		const FTransform StartRelativeTransform = StartArrow->GetRelativeTransform();
		const FTransform EndRelativeTransform = EndArrow->GetRelativeTransform();
		if (StartRelativeTransform.ContainsNaN() || EndRelativeTransform.ContainsNaN())
		{
			AddError(LOCTEXT("InvalidEndpointTransform", "Puzzle Transform Mover endpoint transforms must contain only finite values."));
		}
		else if (StartRelativeTransform.Equals(EndRelativeTransform, TransformComparisonTolerance))
		{
			AddError(LOCTEXT("IdenticalEndpoints", "Puzzle Transform Mover Start and End transforms must be different."));
		}
		else if (TimingMode == EPuzzleTransformMoverTimingMode::Speed
			&& StartRelativeTransform.GetLocation().Equals(EndRelativeTransform.GetLocation(), TransformComparisonTolerance))
		{
			AddError(LOCTEXT("SpeedWithoutTranslation", "Speed timing requires non-zero endpoint translation. Use Movement Time for pure rotation or scale movement."));
		}
	}

	if (TimingMode == EPuzzleTransformMoverTimingMode::Speed
		&& (!FMath::IsFinite(ForwardSpeed) || ForwardSpeed <= 0.0f
			|| (bUseSeparateReturnTiming && (!FMath::IsFinite(ReturnSpeed) || ReturnSpeed <= 0.0f))))
	{
		AddError(LOCTEXT("InvalidSpeed", "Puzzle Transform Mover active speed values must be finite and greater than zero."));
	}

	if (TimingMode == EPuzzleTransformMoverTimingMode::MovementTime
		&& (!FMath::IsFinite(ForwardMovementTime) || ForwardMovementTime <= 0.0f
			|| (bUseSeparateReturnTiming && (!FMath::IsFinite(ReturnMovementTime) || ReturnMovementTime <= 0.0f))))
	{
		AddError(LOCTEXT("InvalidMovementTime", "Puzzle Transform Mover active movement-time values must be finite and greater than zero."));
	}

	if (InterpolationSource == EPuzzleTransformMoverInterpolationSource::BuiltInEasing
		&& (!FMath::IsFinite(EasingExponent) || EasingExponent <= 0.0f || EasingSteps < 2))
	{
		AddError(LOCTEXT("InvalidBuiltInEasing", "Puzzle Transform Mover easing exponent must be positive and Step easing requires at least two steps."));
	}

	if (InterpolationSource == EPuzzleTransformMoverInterpolationSource::CustomCurve && !IsValid(MovementCurve))
	{
		AddError(LOCTEXT("MissingMovementCurve", "Custom Curve interpolation requires a Movement Curve."));
	}

	return Result;
}
#endif

bool APuzzleTransformMover::SetMovedComponent(USceneComponent* NewComponent)
{
	if (!ValidateMovedComponent(NewComponent, bIsRuntimeInitialized, true))
	{
		return false;
	}

	if (MovedComponent == NewComponent)
	{
		return SynchronizeMovedComponent(true);
	}

	USceneComponent* PreviousComponent = MovedComponent;
	MovedComponent = NewComponent;
	if (!UpdateEasedAlpha(true) || !SynchronizeMovedComponent(true))
	{
		MovedComponent = PreviousComponent;
		return false;
	}

	bInvalidMovedComponentWarningEmitted = false;
	bConfigurationValid = ValidateMoverConfiguration(true);
	UpdateMovementTickState();

	HandleMovedComponentChanged();
	OnMovedComponentChanged.Broadcast(this);
	return true;
}

bool APuzzleTransformMover::RestoreDefaultMovedComponent()
{
	USceneComponent* DefaultComponent = ResolveDefaultMovedComponent();
	if (!DefaultComponent)
	{
		PUZZLESYSTEM_LOG_WARNING("Puzzle Transform Mover '%s' could not resolve DefaultMovedComponent.", *GetNameSafe(this));
		return false;
	}

	return SetMovedComponent(DefaultComponent);
}

USceneComponent* APuzzleTransformMover::GetMovedComponent() const
{
	return MovedComponent.Get();
}

bool APuzzleTransformMover::HasValidMovedComponent() const
{
	return ValidateMovedComponent(MovedComponent, bIsRuntimeInitialized, false);
}

void APuzzleTransformMover::ResetMover()
{
	const bool bRestoredTransform = RestoreInitialPosition(true);
	bInvalidMovedComponentWarningEmitted = false;
	bConfigurationValid = ValidateMoverConfiguration(true);
	UpdateMovementTickState();

	if (!bRestoredTransform)
	{
		PUZZLESYSTEM_LOG_WARNING(
			"Puzzle Transform Mover '%s' reset its local state but could not synchronize the moved component.",
			*GetNameSafe(this));
	}

	HandleMoverReset();
	OnMoverReset.Broadcast(this);
}

bool APuzzleTransformMover::SynchronizeWithCurrentReceiverState(bool bAnimate)
{
	if (!bIsRuntimeInitialized || !IsValid(PuzzleReceiver))
	{
		PUZZLESYSTEM_LOG_WARNING(
			"Puzzle Transform Mover '%s' cannot synchronize before runtime initialization or without its Receiver.",
			*GetNameSafe(this));
		return false;
	}

	bConfigurationValid = ValidateMoverConfiguration(true);
	if (!bConfigurationValid)
	{
		UpdateMovementTickState();
		return false;
	}

	const bool bReceiverActive = PuzzleReceiver->IsReceiverActive();
	if (!bAnimate)
	{
		return ApplyReceiverStateWithoutAnimation(bReceiverActive);
	}

	if (bReceiverActive)
	{
		ProcessReceiverActivated();
	}
	else
	{
		ProcessReceiverDeactivated();
	}
	return true;
}

EPuzzleTransformMoverState APuzzleTransformMover::GetMoverState() const
{
	return MoverState;
}

EPuzzleTransformMoverMode APuzzleTransformMover::GetMovementMode() const
{
	return MovementMode;
}

EPuzzleTransformMoverDeactivationBehavior APuzzleTransformMover::GetDeactivationBehavior() const
{
	return DeactivationBehavior;
}

float APuzzleTransformMover::GetMovementAlpha() const
{
	return MovementAlpha;
}

float APuzzleTransformMover::GetEasedAlpha() const
{
	return EasedAlpha;
}

bool APuzzleTransformMover::IsMoving() const
{
	return MoverState == EPuzzleTransformMoverState::MovingTowardEnd
		|| MoverState == EPuzzleTransformMoverState::MovingTowardStart;
}

bool APuzzleTransformMover::IsMovementPaused() const
{
	return bIsMovementPaused;
}

bool APuzzleTransformMover::IsAtStart() const
{
	return MoverState == EPuzzleTransformMoverState::AtStart;
}

bool APuzzleTransformMover::IsAtEnd() const
{
	return MoverState == EPuzzleTransformMoverState::AtEnd;
}

bool APuzzleTransformMover::IsLatchCompleted() const
{
	return bLatchCompleted;
}

FTransform APuzzleTransformMover::GetStartTransform() const
{
	return IsValid(StartArrow) ? StartArrow->GetComponentTransform() : FTransform::Identity;
}

FTransform APuzzleTransformMover::GetEndTransform() const
{
	return IsValid(EndArrow) ? EndArrow->GetComponentTransform() : FTransform::Identity;
}

FTransform APuzzleTransformMover::GetCurrentTargetTransform() const
{
	return MoverState == EPuzzleTransformMoverState::MovingTowardEnd || MoverState == EPuzzleTransformMoverState::AtEnd
		? GetEndTransform()
		: GetStartTransform();
}

bool APuzzleTransformMover::GetRemainingMovementTime(float& OutRemainingSeconds) const
{
	OutRemainingSeconds = 0.0f;
	if (TimingMode == EPuzzleTransformMoverTimingMode::Speed)
	{
		const float TranslationDistance = FVector::Distance(GetStartTransform().GetLocation(), GetEndTransform().GetLocation());
		if (!FMath::IsFinite(TranslationDistance) || TranslationDistance <= UE_KINDA_SMALL_NUMBER
			|| !FMath::IsFinite(ForwardSpeed) || ForwardSpeed <= 0.0f
			|| (bUseSeparateReturnTiming && (!FMath::IsFinite(ReturnSpeed) || ReturnSpeed <= 0.0f)))
		{
			return false;
		}
	}
	else if (!FMath::IsFinite(ForwardMovementTime) || ForwardMovementTime <= 0.0f
		|| (bUseSeparateReturnTiming && (!FMath::IsFinite(ReturnMovementTime) || ReturnMovementTime <= 0.0f)))
	{
		return false;
	}

	if (!IsMoving())
	{
		return true;
	}

	float AlphaPerSecond = 0.0f;
	if (!GetActiveAlphaPerSecond(AlphaPerSecond) || AlphaPerSecond <= 0.0f)
	{
		return false;
	}

	const float RemainingAlpha = MoverState == EPuzzleTransformMoverState::MovingTowardEnd
		? 1.0f - MovementAlpha
		: MovementAlpha;
	OutRemainingSeconds = FMath::Max(0.0f, RemainingAlpha / AlphaPerSecond);
	return FMath::IsFinite(OutRemainingSeconds);
}

void APuzzleTransformMover::SetMoverDebugEnabled(bool bInEnableDebug)
{
	bEnableDebug = bInEnableDebug;
}

bool APuzzleTransformMover::RequestMoveTowardStart()
{
	if (!CanProcessMovementRequest(TEXT("RequestMoveTowardStart")))
	{
		return false;
	}

	if (MoverState == EPuzzleTransformMoverState::AtStart)
	{
		return false;
	}
	if (MoverState == EPuzzleTransformMoverState::MovingTowardStart)
	{
		return bIsMovementPaused ? ResumeMovement() : false;
	}
	if (!ValidateMovedComponent(MovedComponent, true, true))
	{
		HandleMovedComponentInvalidation();
		return false;
	}

	const bool bWasMoving = IsMoving();
	MoverState = EPuzzleTransformMoverState::MovingTowardStart;
	bIsMovementPaused = false;
	bInvalidMovedComponentWarningEmitted = false;
	UpdateMovementTickState();

	if (bWasMoving)
	{
		HandleMovementReversed();
		OnMovementReversed.Broadcast(this);
	}
	else
	{
		HandleMovementStarted();
		OnMovementStarted.Broadcast(this);
	}
	return true;
}

bool APuzzleTransformMover::RequestMoveTowardEnd()
{
	if (!CanProcessMovementRequest(TEXT("RequestMoveTowardEnd")))
	{
		return false;
	}

	if (MoverState == EPuzzleTransformMoverState::AtEnd)
	{
		return false;
	}
	if (MoverState == EPuzzleTransformMoverState::MovingTowardEnd)
	{
		return bIsMovementPaused ? ResumeMovement() : false;
	}
	if (!ValidateMovedComponent(MovedComponent, true, true))
	{
		HandleMovedComponentInvalidation();
		return false;
	}

	const bool bWasMoving = IsMoving();
	MoverState = EPuzzleTransformMoverState::MovingTowardEnd;
	bIsMovementPaused = false;
	bInvalidMovedComponentWarningEmitted = false;
	UpdateMovementTickState();

	if (bWasMoving)
	{
		HandleMovementReversed();
		OnMovementReversed.Broadcast(this);
	}
	else
	{
		HandleMovementStarted();
		OnMovementStarted.Broadcast(this);
	}
	return true;
}

bool APuzzleTransformMover::PauseMovement()
{
	if (!IsMoving() || bIsMovementPaused)
	{
		return false;
	}

	bIsMovementPaused = true;
	UpdateMovementTickState();
	HandleMovementPaused();
	OnMovementPaused.Broadcast(this);
	return true;
}

bool APuzzleTransformMover::ResumeMovement()
{
	if (!IsMoving() || !bIsMovementPaused)
	{
		return false;
	}
	if (!CanProcessMovementRequest(TEXT("ResumeMovement")) || !ValidateMovedComponent(MovedComponent, true, true))
	{
		return false;
	}

	float AlphaPerSecond = 0.0f;
	if (!GetActiveAlphaPerSecond(AlphaPerSecond))
	{
		return false;
	}

	bIsMovementPaused = false;
	bInvalidMovedComponentWarningEmitted = false;
	UpdateMovementTickState();
	HandleMovementResumed();
	OnMovementResumed.Broadcast(this);
	return true;
}

void APuzzleTransformMover::HandleMovementStarted_Implementation()
{
}

void APuzzleTransformMover::HandleMovementResumed_Implementation()
{
}

void APuzzleTransformMover::HandleMovementReversed_Implementation()
{
}

void APuzzleTransformMover::HandleMovementPaused_Implementation()
{
}

void APuzzleTransformMover::HandleMovementUpdated_Implementation(float CurrentMovementAlpha, float CurrentEasedAlpha)
{
}

void APuzzleTransformMover::HandleReachedStart_Implementation()
{
}

void APuzzleTransformMover::HandleReachedEnd_Implementation()
{
}

void APuzzleTransformMover::HandleMovedComponentChanged_Implementation()
{
}

void APuzzleTransformMover::HandleMoverReset_Implementation()
{
}

bool APuzzleTransformMover::InitializePuzzleTransformMover()
{
	if (PuzzleReceiver)
	{
		PuzzleReceiver->OnReceiverStateChangedNative.Remove(ReceiverStateChangedHandle);
		ReceiverStateChangedHandle.Reset();
	}

	bIsRuntimeInitialized = false;
	bIsInitializingFromReceiver = true;
	bConfigurationValid = false;
	bInvalidMovedComponentWarningEmitted = false;
	SetActorTickEnabled(false);

	MovedComponent = ResolveDefaultMovedComponent();
	RestoreInitialPosition(false);

	bIsRuntimeInitialized = true;
	bConfigurationValid = ValidateMoverConfiguration(true);
	if (bConfigurationValid && !SynchronizeMovedComponent(true))
	{
		bConfigurationValid = false;
	}

	if (IsValid(PuzzleReceiver))
	{
		ReceiverStateChangedHandle = PuzzleReceiver->OnReceiverStateChangedNative.AddUObject(
			this,
			&APuzzleTransformMover::HandleOwnedReceiverStateChanged);
	}

	bool bSynchronizedReceiver = false;
	if (bConfigurationValid)
	{
		bSynchronizedReceiver = SynchronizeWithCurrentReceiverState(bAnimateInitialReceiverState);
	}

	bIsInitializingFromReceiver = false;
	UpdateMovementTickState();
	return bConfigurationValid && bSynchronizedReceiver;
}

void APuzzleTransformMover::HandleOwnedReceiverStateChanged(UPuzzleReceiverComponent* Receiver, bool bIsActive)
{
	if (Receiver != PuzzleReceiver || !bIsRuntimeInitialized || bIsInitializingFromReceiver)
	{
		return;
	}

	if (bIsActive)
	{
		ProcessReceiverActivated();
	}
	else
	{
		ProcessReceiverDeactivated();
	}
}

USceneComponent* APuzzleTransformMover::ResolveDefaultMovedComponent() const
{
	UActorComponent* ResolvedComponent = DefaultMovedComponent.GetComponent(const_cast<APuzzleTransformMover*>(this));
	return Cast<USceneComponent>(ResolvedComponent);
}

bool APuzzleTransformMover::ValidateMovedComponent(const USceneComponent* Candidate, bool bRequireRegistered, bool bLogErrors) const
{
	auto Fail = [this, bLogErrors](const TCHAR* Reason)
	{
		if (bLogErrors)
		{
			PUZZLESYSTEM_LOG_ERROR(
				"Puzzle Transform Mover '%s' rejected moved component: %s",
				*GetNameSafe(this),
				Reason);
		}
		return false;
	};

	if (!IsValid(Candidate))
	{
		return Fail(TEXT("component is null or invalid."));
	}
	if (Candidate == BillboardRoot || Candidate == StartArrow || Candidate == EndArrow)
	{
		return Fail(TEXT("internal billboard and endpoint markers cannot be moved."));
	}
	if (Candidate->GetOwner() != this)
	{
		return Fail(TEXT("component must belong to the mover Actor."));
	}
	if (Candidate->Mobility != EComponentMobility::Movable)
	{
		return Fail(TEXT("component mobility must be Movable."));
	}
	if (bRequireRegistered && !Candidate->IsRegistered())
	{
		return Fail(TEXT("component must be registered before runtime movement."));
	}

	const UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Candidate);
	if (PrimitiveComponent && PrimitiveComponent->IsSimulatingPhysics())
	{
		return Fail(TEXT("physics-simulating components are unsupported."));
	}

	return true;
}

bool APuzzleTransformMover::ValidateMoverConfiguration(bool bLogErrors) const
{
	bool bValid = true;
	auto Fail = [this, bLogErrors, &bValid](const TCHAR* Reason)
	{
		if (bLogErrors)
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Transform Mover '%s' has invalid configuration: %s", *GetNameSafe(this), Reason);
		}
		bValid = false;
	};

	if (!IsValid(BillboardRoot) || !IsValid(StartArrow) || !IsValid(EndArrow) || StartArrow == EndArrow || !IsValid(PuzzleReceiver))
	{
		Fail(TEXT("one or more required native components are missing or duplicated."));
	}
	if (!ValidateMovedComponent(MovedComponent, bIsRuntimeInitialized, bLogErrors))
	{
		bValid = false;
	}

	if (IsValid(StartArrow) && IsValid(EndArrow))
	{
		const FTransform StartTransform = GetStartTransform();
		const FTransform EndTransform = GetEndTransform();
		if (StartTransform.ContainsNaN() || EndTransform.ContainsNaN())
		{
			Fail(TEXT("endpoint transforms contain non-finite values."));
		}
		else if (StartTransform.Equals(EndTransform, TransformComparisonTolerance))
		{
			Fail(TEXT("Start and End transforms are identical."));
		}
		else if (TimingMode == EPuzzleTransformMoverTimingMode::Speed
			&& StartTransform.GetLocation().Equals(EndTransform.GetLocation(), TransformComparisonTolerance))
		{
			Fail(TEXT("Speed timing cannot drive a zero-translation rotation/scale-only path; use MovementTime."));
		}
	}

	if (TimingMode == EPuzzleTransformMoverTimingMode::Speed)
	{
		if (!FMath::IsFinite(ForwardSpeed) || ForwardSpeed <= 0.0f)
		{
			Fail(TEXT("ForwardSpeed must be finite and greater than zero."));
		}
		if (bUseSeparateReturnTiming && (!FMath::IsFinite(ReturnSpeed) || ReturnSpeed <= 0.0f))
		{
			Fail(TEXT("ReturnSpeed must be finite and greater than zero while separate return timing is enabled."));
		}
	}
	else
	{
		if (!FMath::IsFinite(ForwardMovementTime) || ForwardMovementTime <= 0.0f)
		{
			Fail(TEXT("ForwardMovementTime must be finite and greater than zero."));
		}
		if (bUseSeparateReturnTiming && (!FMath::IsFinite(ReturnMovementTime) || ReturnMovementTime <= 0.0f))
		{
			Fail(TEXT("ReturnMovementTime must be finite and greater than zero while separate return timing is enabled."));
		}
	}

	if (InterpolationSource == EPuzzleTransformMoverInterpolationSource::BuiltInEasing)
	{
		if (!FMath::IsFinite(EasingExponent) || EasingExponent <= 0.0f || EasingSteps < 2)
		{
			Fail(TEXT("built-in easing requires a positive finite exponent and at least two steps."));
		}
	}
	else if (!IsValid(MovementCurve))
	{
		Fail(TEXT("CustomCurve interpolation requires MovementCurve."));
	}

	return bValid;
}

bool APuzzleTransformMover::CanProcessMovementRequest(const TCHAR* OperationName) const
{
	if (bIsRuntimeInitialized && bConfigurationValid)
	{
		return true;
	}

	PUZZLESYSTEM_LOG_WARNING(
		"Puzzle Transform Mover '%s' ignored %s because runtime initialization or configuration is invalid.",
		*GetNameSafe(this),
		OperationName);
	return false;
}

void APuzzleTransformMover::ProcessReceiverActivated()
{
	if (bIsMovementPaused)
	{
		ResumeMovement();
		return;
	}

	switch (MovementMode)
	{
	case EPuzzleTransformMoverMode::Latch:
		if (!bLatchCompleted)
		{
			RequestMoveTowardEnd();
		}
		break;

	case EPuzzleTransformMoverMode::FlipFlop:
		if (ShouldFlipFlopTargetEnd())
		{
			RequestMoveTowardEnd();
		}
		else
		{
			RequestMoveTowardStart();
		}
		break;

	case EPuzzleTransformMoverMode::PingPong:
	default:
		RequestMoveTowardEnd();
		break;
	}
}

void APuzzleTransformMover::ProcessReceiverDeactivated()
{
	if (IsMoving())
	{
		switch (DeactivationBehavior)
		{
		case EPuzzleTransformMoverDeactivationBehavior::Stop:
			PauseMovement();
			break;

		case EPuzzleTransformMoverDeactivationBehavior::Return:
			if (MoverState == EPuzzleTransformMoverState::MovingTowardEnd)
			{
				RequestMoveTowardStart();
			}
			else
			{
				RequestMoveTowardEnd();
			}
			break;

		case EPuzzleTransformMoverDeactivationBehavior::Continue:
		default:
			break;
		}
		return;
	}

	if (MovementMode == EPuzzleTransformMoverMode::PingPong && MoverState == EPuzzleTransformMoverState::AtEnd)
	{
		RequestMoveTowardStart();
	}
}

bool APuzzleTransformMover::ShouldFlipFlopTargetEnd() const
{
	return MoverState == EPuzzleTransformMoverState::AtStart
		|| MoverState == EPuzzleTransformMoverState::MovingTowardStart;
}

bool APuzzleTransformMover::ApplyReceiverStateWithoutAnimation(bool bReceiverActive)
{
	switch (MovementMode)
	{
	case EPuzzleTransformMoverMode::Latch:
		return !bReceiverActive || bLatchCompleted || SnapToEndpoint(true);

	case EPuzzleTransformMoverMode::FlipFlop:
		return !bReceiverActive || SnapToEndpoint(ShouldFlipFlopTargetEnd());

	case EPuzzleTransformMoverMode::PingPong:
	default:
		return SnapToEndpoint(bReceiverActive);
	}
}

bool APuzzleTransformMover::RestoreInitialPosition(bool bSynchronizeComponent)
{
	bIsMovementPaused = false;
	if (InitialPosition == EPuzzleTransformMoverInitialPosition::End)
	{
		MovementAlpha = 1.0f;
		MoverState = EPuzzleTransformMoverState::AtEnd;
	}
	else
	{
		MovementAlpha = 0.0f;
		MoverState = EPuzzleTransformMoverState::AtStart;
	}
	bLatchCompleted = MovementMode == EPuzzleTransformMoverMode::Latch
		&& InitialPosition == EPuzzleTransformMoverInitialPosition::End;

	const bool bEasingValid = UpdateEasedAlpha(bSynchronizeComponent);
	SetActorTickEnabled(false);
	return !bSynchronizeComponent || (bEasingValid && SynchronizeMovedComponent(true));
}

bool APuzzleTransformMover::SnapToEndpoint(bool bToEnd)
{
	if (!ValidateMovedComponent(MovedComponent, bIsRuntimeInitialized, true))
	{
		return false;
	}

	MovementAlpha = bToEnd ? 1.0f : 0.0f;
	MoverState = bToEnd ? EPuzzleTransformMoverState::AtEnd : EPuzzleTransformMoverState::AtStart;
	bIsMovementPaused = false;
	if (bToEnd && MovementMode == EPuzzleTransformMoverMode::Latch)
	{
		bLatchCompleted = true;
	}

	if (!UpdateEasedAlpha(true) || !SynchronizeMovedComponent(true))
	{
		return false;
	}

	UpdateMovementTickState();
	return true;
}

bool APuzzleTransformMover::UpdateEasedAlpha(bool bLogErrors)
{
	const float ClampedMovementAlpha = FMath::Clamp(MovementAlpha, 0.0f, 1.0f);
	float NewEasedAlpha = ClampedMovementAlpha;

	if (InterpolationSource == EPuzzleTransformMoverInterpolationSource::BuiltInEasing)
	{
		NewEasedAlpha = static_cast<float>(UKismetMathLibrary::Ease(
			0.0,
			1.0,
			ClampedMovementAlpha,
			BuiltInEasingType,
			EasingExponent,
			EasingSteps));
	}
	else
	{
		if (!IsValid(MovementCurve))
		{
			if (bLogErrors)
			{
				PUZZLESYSTEM_LOG_ERROR("Puzzle Transform Mover '%s' cannot evaluate a missing MovementCurve.", *GetNameSafe(this));
			}
			return false;
		}
		NewEasedAlpha = MovementCurve->GetFloatValue(ClampedMovementAlpha);
	}

	if (!FMath::IsFinite(NewEasedAlpha))
	{
		if (bLogErrors)
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Transform Mover '%s' produced a non-finite eased alpha.", *GetNameSafe(this));
		}
		return false;
	}

	EasedAlpha = FMath::Clamp(NewEasedAlpha, 0.0f, 1.0f);
	return true;
}

bool APuzzleTransformMover::SynchronizeMovedComponent(bool bLogErrors)
{
	if (!ValidateMovedComponent(MovedComponent, bIsRuntimeInitialized, bLogErrors))
	{
		return false;
	}
	if (!IsValid(StartArrow) || !IsValid(EndArrow))
	{
		return false;
	}

	const FTransform DesiredTransform = UKismetMathLibrary::TLerp(
		GetStartTransform(),
		GetEndTransform(),
		EasedAlpha,
		ELerpInterpolationMode::QuatInterp);
	MovedComponent->SetWorldTransform(DesiredTransform, false, nullptr, ETeleportType::None);
	return true;
}

void APuzzleTransformMover::AdvanceMovement(float DeltaSeconds)
{
	if (!bIsRuntimeInitialized || !bConfigurationValid || !IsMoving() || bIsMovementPaused)
	{
		UpdateMovementTickState();
		return;
	}
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
	{
		return;
	}
	if (!ValidateMovedComponent(MovedComponent, true, false))
	{
		HandleMovedComponentInvalidation();
		return;
	}

	float AlphaPerSecond = 0.0f;
	if (!GetActiveAlphaPerSecond(AlphaPerSecond))
	{
		PUZZLESYSTEM_LOG_ERROR("Puzzle Transform Mover '%s' stopped because active timing is invalid.", *GetNameSafe(this));
		bConfigurationValid = false;
		UpdateMovementTickState();
		return;
	}

	const float AlphaDelta = AlphaPerSecond * DeltaSeconds;
	MovementAlpha = MoverState == EPuzzleTransformMoverState::MovingTowardEnd
		? FMath::Min(1.0f, MovementAlpha + AlphaDelta)
		: FMath::Max(0.0f, MovementAlpha - AlphaDelta);

	if (!UpdateEasedAlpha(true) || !SynchronizeMovedComponent(true))
	{
		bConfigurationValid = false;
		UpdateMovementTickState();
		return;
	}

	HandleMovementUpdated(MovementAlpha, EasedAlpha);

	if (MovementAlpha >= 1.0f)
	{
		CompleteMovementAtEndpoint(true);
	}
	else if (MovementAlpha <= 0.0f)
	{
		CompleteMovementAtEndpoint(false);
	}
}

bool APuzzleTransformMover::GetActiveAlphaPerSecond(float& OutAlphaPerSecond) const
{
	OutAlphaPerSecond = 0.0f;
	if (!IsMoving())
	{
		return false;
	}

	const bool bReturning = MoverState == EPuzzleTransformMoverState::MovingTowardStart;
	if (TimingMode == EPuzzleTransformMoverTimingMode::MovementTime)
	{
		const float FullMovementTime = bReturning && bUseSeparateReturnTiming
			? ReturnMovementTime
			: ForwardMovementTime;
		if (!FMath::IsFinite(FullMovementTime) || FullMovementTime <= 0.0f)
		{
			return false;
		}
		OutAlphaPerSecond = 1.0f / FullMovementTime;
		return FMath::IsFinite(OutAlphaPerSecond);
	}

	const float TranslationDistance = FVector::Distance(GetStartTransform().GetLocation(), GetEndTransform().GetLocation());
	const float Speed = bReturning && bUseSeparateReturnTiming ? ReturnSpeed : ForwardSpeed;
	if (!FMath::IsFinite(TranslationDistance) || TranslationDistance <= UE_KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(Speed) || Speed <= 0.0f)
	{
		return false;
	}

	OutAlphaPerSecond = Speed / TranslationDistance;
	return FMath::IsFinite(OutAlphaPerSecond);
}

void APuzzleTransformMover::UpdateMovementTickState()
{
	const bool bShouldTick = bIsRuntimeInitialized
		&& bConfigurationValid
		&& IsMoving()
		&& !bIsMovementPaused
		&& ValidateMovedComponent(MovedComponent, true, false);
	SetActorTickEnabled(bShouldTick);
}

void APuzzleTransformMover::HandleMovedComponentInvalidation()
{
	SetActorTickEnabled(false);
	if (!bInvalidMovedComponentWarningEmitted)
	{
		PUZZLESYSTEM_LOG_WARNING(
			"Puzzle Transform Mover '%s' stopped at alpha %.3f because moved component '%s' became invalid. Assign a valid replacement to continue.",
			*GetNameSafe(this),
			MovementAlpha,
			*GetNameSafe(MovedComponent));
		bInvalidMovedComponentWarningEmitted = true;
	}
}

void APuzzleTransformMover::CompleteMovementAtEndpoint(bool bReachedEnd)
{
	MovementAlpha = bReachedEnd ? 1.0f : 0.0f;
	EasedAlpha = bReachedEnd ? 1.0f : 0.0f;
	MoverState = bReachedEnd ? EPuzzleTransformMoverState::AtEnd : EPuzzleTransformMoverState::AtStart;
	bIsMovementPaused = false;

	if (bReachedEnd && MovementMode == EPuzzleTransformMoverMode::Latch)
	{
		bLatchCompleted = true;
	}

	UpdateMovementTickState();
	if (bReachedEnd)
	{
		HandleReachedEnd();
		OnReachedEnd.Broadcast(this);
	}
	else
	{
		HandleReachedStart();
		OnReachedStart.Broadcast(this);
	}
}

void APuzzleTransformMover::DrawMovementDebug() const
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(StartArrow) || !IsValid(EndArrow))
	{
		return;
	}

	const FVector StartLocation = GetStartTransform().GetLocation();
	const FVector EndLocation = GetEndTransform().GetLocation();
	const FVector CurrentLocation = IsValid(MovedComponent)
		? MovedComponent->GetComponentLocation()
		: FMath::Lerp(StartLocation, EndLocation, EasedAlpha);

	DrawDebugSphere(World, StartLocation, 18.0f, 12, FColor::Green, false, 0.0f, 0, 2.0f);
	DrawDebugSphere(World, EndLocation, 18.0f, 12, FColor::Red, false, 0.0f, 0, 2.0f);
	DrawDebugLine(World, StartLocation, EndLocation, FColor::Silver, false, 0.0f, 0, 1.5f);
	DrawDebugSphere(World, CurrentLocation, 12.0f, 10, FColor::Yellow, false, 0.0f, 0, 2.0f);

	const FVector DirectionStart = MoverState == EPuzzleTransformMoverState::MovingTowardEnd ? StartLocation : EndLocation;
	const FVector DirectionEnd = MoverState == EPuzzleTransformMoverState::MovingTowardEnd ? EndLocation : StartLocation;
	DrawDebugDirectionalArrow(World, DirectionStart, DirectionEnd, 24.0f, FColor::Cyan, false, 0.0f, 0, 2.0f);

	float RemainingTime = 0.0f;
	const bool bHasRemainingTime = GetRemainingMovementTime(RemainingTime);
	const FString InterpolationName = InterpolationSource == EPuzzleTransformMoverInterpolationSource::BuiltInEasing
		? UEnum::GetValueAsString(BuiltInEasingType.GetValue())
		: GetNameSafe(MovementCurve);
	const FString Label = FString::Printf(
		TEXT("%s\nComponent: %s\nMode: %s / %s\nState: %s%s\nAlpha: %.3f Eased: %.3f\nTarget: %s Receiver: %s\nLatch: %s Timing: %s Remaining: %s\nInterpolation: %s"),
		*GetNameSafe(this),
		*GetNameSafe(MovedComponent),
		*UEnum::GetValueAsString(MovementMode),
		*UEnum::GetValueAsString(DeactivationBehavior),
		*UEnum::GetValueAsString(MoverState),
		bIsMovementPaused ? TEXT(" (Paused)") : TEXT(""),
		MovementAlpha,
		EasedAlpha,
		GetEndpointName(MoverState),
		PuzzleReceiver && PuzzleReceiver->IsReceiverActive() ? TEXT("Active") : TEXT("Inactive"),
		bLatchCompleted ? TEXT("Completed") : TEXT("Open"),
		*UEnum::GetValueAsString(TimingMode),
		bHasRemainingTime ? *FString::Printf(TEXT("%.2fs"), RemainingTime) : TEXT("Invalid"),
		*InterpolationName);
	DrawDebugString(World, CurrentLocation + FVector(0.0f, 0.0f, DebugVerticalOffset), Label, nullptr, FColor::White, 0.0f, true);
}

bool APuzzleTransformMover::ShouldDrawMovementDebug() const
{
	return bEnableDebug && IsPuzzleSystemDebugVisualEnabled();
}

#undef LOCTEXT_NAMESPACE
