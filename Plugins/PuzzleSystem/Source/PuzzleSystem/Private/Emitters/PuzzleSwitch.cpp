#include "Emitters/PuzzleSwitch.h"

#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/World.h"
#include "PuzzleSystem.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "PuzzleSwitch"

const FName APuzzleSwitch::SceneRootComponentName(TEXT("SceneRoot"));

APuzzleSwitch::APuzzleSwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SceneRootComponent = CreateOptionalDefaultSubobject<USceneComponent>(SceneRootComponentName);
	if (SceneRootComponent)
	{
		SetRootComponent(SceneRootComponent);
	}

	PuzzleEmitterComponent = CreateDefaultSubobject<UPuzzleEmitterComponent>(TEXT("PuzzleEmitter"));

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void APuzzleSwitch::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	RefreshSwitchTickState();
}

void APuzzleSwitch::BeginPlay()
{
	Super::BeginPlay();
	InitializePuzzleSwitch();
	RefreshSwitchTickState();
}

void APuzzleSwitch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bIsSwitchInitialized = false;
	bConfigurationValid = false;
	InvalidateAllTimers();
	Super::EndPlay(EndPlayReason);
}

void APuzzleSwitch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (ShouldDrawSwitchDebug())
	{
		DrawSwitchDebug();
	}
}

bool APuzzleSwitch::ShouldTickIfViewportsOnly() const
{
	return bEnableDebug;
}

#if WITH_EDITOR
EDataValidationResult APuzzleSwitch::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	if (!IsValid(PuzzleEmitterComponent))
	{
		Context.AddError(LOCTEXT("MissingEmitter", "Puzzle Switch requires its native Puzzle Emitter component."));
		Result = EDataValidationResult::Invalid;
	}

	if (!OutputSignalTag.IsValid())
	{
		Context.AddError(LOCTEXT("InvalidOutputSignal", "Puzzle Switch requires a valid Output Signal Tag."));
		Result = EDataValidationResult::Invalid;
	}

	if (!FMath::IsFinite(PressDelay) || PressDelay < 0.0f)
	{
		Context.AddError(LOCTEXT("InvalidPressDelay", "Puzzle Switch Press Delay must be finite and non-negative."));
		Result = EDataValidationResult::Invalid;
	}

	if (!FMath::IsFinite(ReleaseDelay) || ReleaseDelay < 0.0f)
	{
		Context.AddError(LOCTEXT("InvalidReleaseDelay", "Puzzle Switch Release Delay must be finite and non-negative."));
		Result = EDataValidationResult::Invalid;
	}

	if (!FMath::IsFinite(PulseDuration))
	{
		Context.AddError(LOCTEXT("InvalidPulseDuration", "Puzzle Switch Pulse Duration must be finite."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

void APuzzleSwitch::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName ChangedPropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (ChangedPropertyName.IsNone() || ChangedPropertyName == GET_MEMBER_NAME_CHECKED(APuzzleSwitch, bEnableDebug))
	{
		RefreshSwitchTickState();
	}
}
#endif

bool APuzzleSwitch::Press()
{
	if (!CanProcessInputRequest(TEXT("Press")))
	{
		return false;
	}

	switch (InputState)
	{
	case EPuzzleSwitchInputState::Released:
		if (PressDelay <= 0.0f)
		{
			InputState = EPuzzleSwitchInputState::Pressed;
			HandleConfirmedPress();
			return true;
		}
		return StartPressDelay();

	case EPuzzleSwitchInputState::ReleasePending:
		return CancelPendingRelease();

	case EPuzzleSwitchInputState::PressPending:
	case EPuzzleSwitchInputState::Pressed:
	default:
		return false;
	}
}

bool APuzzleSwitch::Release()
{
	if (!CanProcessInputRequest(TEXT("Release")))
	{
		return false;
	}

	switch (InputState)
	{
	case EPuzzleSwitchInputState::Pressed:
		if (ReleaseDelay <= 0.0f)
		{
			InputState = EPuzzleSwitchInputState::Released;
			HandleConfirmedRelease();
			return true;
		}
		return StartReleaseDelay();

	case EPuzzleSwitchInputState::PressPending:
		return CancelPendingPress();

	case EPuzzleSwitchInputState::Released:
	case EPuzzleSwitchInputState::ReleasePending:
	default:
		return false;
	}
}

void APuzzleSwitch::ResetSwitch()
{
	InvalidateAllTimers();
	InputState = GetConfiguredInitialInputState();

	if (bIsSwitchInitialized && bConfigurationValid)
	{
		SetSwitchActive(bStartActive);
	}
	else
	{
		bIsActive = bStartActive;
	}

	HandleSwitchReset();
	OnSwitchReset.Broadcast(this);
}

EPuzzleSwitchInputState APuzzleSwitch::GetInputState() const
{
	return InputState;
}

bool APuzzleSwitch::IsInputPressed() const
{
	return InputState == EPuzzleSwitchInputState::PressPending || InputState == EPuzzleSwitchInputState::Pressed;
}

bool APuzzleSwitch::IsPressed() const
{
	return InputState == EPuzzleSwitchInputState::Pressed || InputState == EPuzzleSwitchInputState::ReleasePending;
}

bool APuzzleSwitch::IsSwitchActive() const
{
	return bIsActive;
}

bool APuzzleSwitch::RestartPressDelay()
{
	if (InputState != EPuzzleSwitchInputState::PressPending || PressDelay <= 0.0f || !FMath::IsFinite(PressDelay))
	{
		return false;
	}

	return SchedulePressDelay();
}

bool APuzzleSwitch::RestartReleaseDelay()
{
	if (InputState != EPuzzleSwitchInputState::ReleasePending || ReleaseDelay <= 0.0f || !FMath::IsFinite(ReleaseDelay))
	{
		return false;
	}

	return ScheduleReleaseDelay();
}

bool APuzzleSwitch::CancelPendingPress()
{
	if (InputState != EPuzzleSwitchInputState::PressPending)
	{
		return false;
	}

	InvalidatePressDelay();
	InputState = EPuzzleSwitchInputState::Released;
	HandlePressDelayCancelled();
	OnPressDelayCancelled.Broadcast(this);
	return true;
}

bool APuzzleSwitch::CancelPendingRelease()
{
	if (InputState != EPuzzleSwitchInputState::ReleasePending)
	{
		return false;
	}

	InvalidateReleaseDelay();
	InputState = EPuzzleSwitchInputState::Pressed;
	HandleReleaseDelayCancelled();
	OnReleaseDelayCancelled.Broadcast(this);
	return true;
}

bool APuzzleSwitch::IsPressDelayPending() const
{
	return InputState == EPuzzleSwitchInputState::PressPending;
}

bool APuzzleSwitch::IsReleaseDelayPending() const
{
	return InputState == EPuzzleSwitchInputState::ReleasePending;
}

float APuzzleSwitch::GetPressDelayRemaining() const
{
	const UWorld* World = GetWorld();
	if (!IsPressDelayPending() || !World)
	{
		return 0.0f;
	}

	return FMath::Max(0.0f, World->GetTimerManager().GetTimerRemaining(PressDelayTimerHandle));
}

float APuzzleSwitch::GetReleaseDelayRemaining() const
{
	const UWorld* World = GetWorld();
	if (!IsReleaseDelayPending() || !World)
	{
		return 0.0f;
	}

	return FMath::Max(0.0f, World->GetTimerManager().GetTimerRemaining(ReleaseDelayTimerHandle));
}

void APuzzleSwitch::SetSwitchDebugEnabled(bool bInEnableDebug)
{
	bEnableDebug = bInEnableDebug;
	RefreshSwitchTickState();
}

void APuzzleSwitch::HandleInputPressed_Implementation()
{
}

void APuzzleSwitch::HandleInputReleased_Implementation()
{
}

void APuzzleSwitch::HandlePressDelayStarted_Implementation()
{
}

void APuzzleSwitch::HandlePressDelayCancelled_Implementation()
{
}

void APuzzleSwitch::HandlePressDelayCompleted_Implementation()
{
}

void APuzzleSwitch::HandleReleaseDelayStarted_Implementation()
{
}

void APuzzleSwitch::HandleReleaseDelayCancelled_Implementation()
{
}

void APuzzleSwitch::HandleReleaseDelayCompleted_Implementation()
{
}

void APuzzleSwitch::HandleSwitchActivated_Implementation()
{
}

void APuzzleSwitch::HandleSwitchDeactivated_Implementation()
{
}

void APuzzleSwitch::HandleSwitchReset_Implementation()
{
}

bool APuzzleSwitch::ShouldEnableSwitchTick() const
{
	return bEnableDebug;
}

void APuzzleSwitch::RefreshSwitchTickState()
{
	SetActorTickEnabled(ShouldEnableSwitchTick());
}

void APuzzleSwitch::InitializePuzzleSwitch()
{
	InvalidateAllTimers();
	InputState = GetConfiguredInitialInputState();
	bIsActive = bStartActive;
	bIsSwitchInitialized = true;
	bConfigurationValid = ValidateSwitchConfiguration(true);

	if (bConfigurationValid)
	{
		PuzzleEmitterComponent->SetSignalState(OutputSignalTag, bIsActive, nullptr);
	}
}

EPuzzleSwitchInputState APuzzleSwitch::GetConfiguredInitialInputState() const
{
	return InitialInputState == EPuzzleSwitchInitialInputState::Pressed
		? EPuzzleSwitchInputState::Pressed
		: EPuzzleSwitchInputState::Released;
}

bool APuzzleSwitch::ValidateSwitchConfiguration(bool bLogErrors) const
{
	bool bIsValidConfiguration = true;

	if (!IsValid(PuzzleEmitterComponent))
	{
		if (bLogErrors)
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Switch '%s' has no valid Puzzle Emitter component.", *GetNameSafe(this));
		}
		bIsValidConfiguration = false;
	}

	if (!OutputSignalTag.IsValid())
	{
		if (bLogErrors)
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Switch '%s' has an invalid OutputSignalTag.", *GetNameSafe(this));
		}
		bIsValidConfiguration = false;
	}

	if (!FMath::IsFinite(PressDelay) || PressDelay < 0.0f)
	{
		if (bLogErrors)
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Switch '%s' has invalid PressDelay %.3f; it must be finite and non-negative.", *GetNameSafe(this), PressDelay);
		}
		bIsValidConfiguration = false;
	}

	if (!FMath::IsFinite(ReleaseDelay) || ReleaseDelay < 0.0f)
	{
		if (bLogErrors)
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Switch '%s' has invalid ReleaseDelay %.3f; it must be finite and non-negative.", *GetNameSafe(this), ReleaseDelay);
		}
		bIsValidConfiguration = false;
	}

	if (!FMath::IsFinite(PulseDuration))
	{
		if (bLogErrors)
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Switch '%s' has non-finite PulseDuration %.3f.", *GetNameSafe(this), PulseDuration);
		}
		bIsValidConfiguration = false;
	}

	return bIsValidConfiguration;
}

bool APuzzleSwitch::CanProcessInputRequest(const TCHAR* OperationName) const
{
	if (bIsSwitchInitialized && bConfigurationValid)
	{
		return true;
	}

	PUZZLESYSTEM_LOG_WARNING(
		"Puzzle Switch '%s' ignored %s because runtime initialization or configuration is invalid.",
		*GetNameSafe(this),
		OperationName);
	return false;
}

bool APuzzleSwitch::StartPressDelay()
{
	if (!SchedulePressDelay())
	{
		return false;
	}

	InputState = EPuzzleSwitchInputState::PressPending;
	HandlePressDelayStarted();
	OnPressDelayStarted.Broadcast(this);
	return true;
}

bool APuzzleSwitch::StartReleaseDelay()
{
	if (!ScheduleReleaseDelay())
	{
		return false;
	}

	InputState = EPuzzleSwitchInputState::ReleasePending;
	HandleReleaseDelayStarted();
	OnReleaseDelayStarted.Broadcast(this);
	return true;
}

bool APuzzleSwitch::SchedulePressDelay()
{
	UWorld* World = GetWorld();
	if (!World || PressDelay <= 0.0f || !FMath::IsFinite(PressDelay))
	{
		PUZZLESYSTEM_LOG_WARNING("Puzzle Switch '%s' could not schedule PressDelay.", *GetNameSafe(this));
		return false;
	}

	InvalidatePressDelay();
	const uint32 ExpectedGeneration = PressDelayGeneration;
	const FTimerDelegate TimerDelegate = FTimerDelegate::CreateUObject(
		this,
		&APuzzleSwitch::HandlePressDelayElapsed,
		ExpectedGeneration);
	World->GetTimerManager().SetTimer(PressDelayTimerHandle, TimerDelegate, PressDelay, false);
	return PressDelayTimerHandle.IsValid();
}

bool APuzzleSwitch::ScheduleReleaseDelay()
{
	UWorld* World = GetWorld();
	if (!World || ReleaseDelay <= 0.0f || !FMath::IsFinite(ReleaseDelay))
	{
		PUZZLESYSTEM_LOG_WARNING("Puzzle Switch '%s' could not schedule ReleaseDelay.", *GetNameSafe(this));
		return false;
	}

	InvalidateReleaseDelay();
	const uint32 ExpectedGeneration = ReleaseDelayGeneration;
	const FTimerDelegate TimerDelegate = FTimerDelegate::CreateUObject(
		this,
		&APuzzleSwitch::HandleReleaseDelayElapsed,
		ExpectedGeneration);
	World->GetTimerManager().SetTimer(ReleaseDelayTimerHandle, TimerDelegate, ReleaseDelay, false);
	return ReleaseDelayTimerHandle.IsValid();
}

void APuzzleSwitch::HandleConfirmedPress()
{
	switch (SwitchMode)
	{
	case EPuzzleSwitchMode::Hold:
		SetSwitchActive(true);
		break;

	case EPuzzleSwitchMode::Toggle:
		SetSwitchActive(!bIsActive);
		break;

	case EPuzzleSwitchMode::Latch:
		SetSwitchActive(true);
		break;

	case EPuzzleSwitchMode::Pulse:
		StartOrRetriggerPulse();
		break;

	default:
		break;
	}

	HandleInputPressed();
	OnInputPressed.Broadcast(this);
}

void APuzzleSwitch::HandleConfirmedRelease()
{
	if (SwitchMode == EPuzzleSwitchMode::Hold)
	{
		SetSwitchActive(false);
	}

	HandleInputReleased();
	OnInputReleased.Broadcast(this);
}

void APuzzleSwitch::HandlePressDelayElapsed(uint32 ExpectedGeneration)
{
	if (ExpectedGeneration != PressDelayGeneration || InputState != EPuzzleSwitchInputState::PressPending)
	{
		return;
	}

	PressDelayTimerHandle.Invalidate();
	InputState = EPuzzleSwitchInputState::Pressed;
	HandleConfirmedPress();
	HandlePressDelayCompleted();
	OnPressDelayCompleted.Broadcast(this);
}

void APuzzleSwitch::HandleReleaseDelayElapsed(uint32 ExpectedGeneration)
{
	if (ExpectedGeneration != ReleaseDelayGeneration || InputState != EPuzzleSwitchInputState::ReleasePending)
	{
		return;
	}

	ReleaseDelayTimerHandle.Invalidate();
	InputState = EPuzzleSwitchInputState::Released;
	HandleConfirmedRelease();
	HandleReleaseDelayCompleted();
	OnReleaseDelayCompleted.Broadcast(this);
}

void APuzzleSwitch::StartOrRetriggerPulse()
{
	if (bPulseCompletionPending && PulseRetriggerMode == EPuzzlePulseRetriggerMode::Ignore)
	{
		return;
	}

	if (!SchedulePulseCompletion())
	{
		PUZZLESYSTEM_LOG_ERROR("Puzzle Switch '%s' could not schedule Pulse completion.", *GetNameSafe(this));
		return;
	}

	SetSwitchActive(true);
}

bool APuzzleSwitch::SchedulePulseCompletion()
{
	UWorld* World = GetWorld();
	if (!World || !FMath::IsFinite(PulseDuration))
	{
		return false;
	}

	InvalidatePulse();
	const uint32 ExpectedGeneration = PulseGeneration;
	const FTimerDelegate TimerDelegate = FTimerDelegate::CreateUObject(
		this,
		&APuzzleSwitch::HandlePulseElapsed,
		ExpectedGeneration);

	if (PulseDuration <= 0.0f)
	{
		PulseTimerHandle = World->GetTimerManager().SetTimerForNextTick(TimerDelegate);
	}
	else
	{
		World->GetTimerManager().SetTimer(PulseTimerHandle, TimerDelegate, PulseDuration, false);
	}

	bPulseCompletionPending = PulseTimerHandle.IsValid();
	return bPulseCompletionPending;
}

void APuzzleSwitch::HandlePulseElapsed(uint32 ExpectedGeneration)
{
	if (ExpectedGeneration != PulseGeneration || !bPulseCompletionPending)
	{
		return;
	}

	PulseTimerHandle.Invalidate();
	bPulseCompletionPending = false;

	if (SwitchMode == EPuzzleSwitchMode::Pulse)
	{
		SetSwitchActive(false);
	}
}

bool APuzzleSwitch::SetSwitchActive(bool bNewActive)
{
	if (bIsActive == bNewActive)
	{
		return false;
	}

	bIsActive = bNewActive;
	if (bConfigurationValid && IsValid(PuzzleEmitterComponent) && OutputSignalTag.IsValid())
	{
		PuzzleEmitterComponent->SetSignalState(OutputSignalTag, bIsActive, nullptr);
	}

	if (bIsActive)
	{
		HandleSwitchActivated();
		OnSwitchActivated.Broadcast(this);
	}
	else
	{
		HandleSwitchDeactivated();
		OnSwitchDeactivated.Broadcast(this);
	}

	return true;
}

void APuzzleSwitch::InvalidatePressDelay()
{
	++PressDelayGeneration;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PressDelayTimerHandle);
	}
	else
	{
		PressDelayTimerHandle.Invalidate();
	}
}

void APuzzleSwitch::InvalidateReleaseDelay()
{
	++ReleaseDelayGeneration;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReleaseDelayTimerHandle);
	}
	else
	{
		ReleaseDelayTimerHandle.Invalidate();
	}
}

void APuzzleSwitch::InvalidatePulse()
{
	++PulseGeneration;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PulseTimerHandle);
	}
	else
	{
		PulseTimerHandle.Invalidate();
	}
	bPulseCompletionPending = false;
}

void APuzzleSwitch::InvalidateAllTimers()
{
	InvalidatePressDelay();
	InvalidateReleaseDelay();
	InvalidatePulse();
}

void APuzzleSwitch::DrawSwitchDebug() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UEnum* SwitchModeEnum = StaticEnum<EPuzzleSwitchMode>();
	const UEnum* InputStateEnum = StaticEnum<EPuzzleSwitchInputState>();
	const UEnum* InitialInputStateEnum = StaticEnum<EPuzzleSwitchInitialInputState>();
	const UEnum* RetriggerModeEnum = StaticEnum<EPuzzlePulseRetriggerMode>();
	const FString SwitchModeName = SwitchModeEnum
		? SwitchModeEnum->GetNameStringByValue(static_cast<int64>(SwitchMode))
		: TEXT("Unknown");
	const FString InputStateName = InputStateEnum
		? InputStateEnum->GetNameStringByValue(static_cast<int64>(InputState))
		: TEXT("Unknown");
	const FString InitialInputStateName = InitialInputStateEnum
		? InitialInputStateEnum->GetNameStringByValue(static_cast<int64>(InitialInputState))
		: TEXT("Unknown");
	const FString RetriggerModeName = RetriggerModeEnum
		? RetriggerModeEnum->GetNameStringByValue(static_cast<int64>(PulseRetriggerMode))
		: TEXT("Unknown");

	FString Label = FString::Printf(
		TEXT("%s\nMode: %s  Input: %s  Initial: %s\nRaw Pressed: %s  Logical Pressed: %s\nOutput: %s  Start Output: %s  Signal: %s\nPressDelay: %.2fs%s\nReleaseDelay: %.2fs%s"),
		*GetNameSafe(this),
		*SwitchModeName,
		*InputStateName,
		*InitialInputStateName,
		IsInputPressed() ? TEXT("true") : TEXT("false"),
		IsPressed() ? TEXT("true") : TEXT("false"),
		bIsActive ? TEXT("Active") : TEXT("Inactive"),
		bStartActive ? TEXT("Active") : TEXT("Inactive"),
		*OutputSignalTag.ToString(),
		PressDelay,
		IsPressDelayPending() ? *FString::Printf(TEXT(" (%.2fs remaining)"), GetPressDelayRemaining()) : TEXT(""),
		ReleaseDelay,
		IsReleaseDelayPending() ? *FString::Printf(TEXT(" (%.2fs remaining)"), GetReleaseDelayRemaining()) : TEXT(""));

	if (SwitchMode == EPuzzleSwitchMode::Pulse)
	{
		float PulseRemaining = 0.0f;
		if (bPulseCompletionPending)
		{
			PulseRemaining = FMath::Max(0.0f, World->GetTimerManager().GetTimerRemaining(PulseTimerHandle));
		}

		Label += FString::Printf(
			TEXT("\nPulse: %s  Remaining: %.2fs  Retrigger: %s"),
			bPulseCompletionPending ? TEXT("Pending") : TEXT("Inactive"),
			PulseRemaining,
			*RetriggerModeName);
	}

	const FVector ActorLocation = GetActorLocation();
	DrawDebugSphere(World, ActorLocation, 24.0f, 12, bIsActive ? FColor::Green : FColor::Red, false, 0.0f, 0, 2.0f);
	DrawDebugString(
		World,
		ActorLocation + FVector(0.0f, 0.0f, DebugVerticalOffset),
		Label,
		nullptr,
		FColor::White,
		0.0f,
		true);
}

bool APuzzleSwitch::ShouldDrawSwitchDebug() const
{
	return bEnableDebug && IsPuzzleSystemDebugVisualEnabled();
}

#undef LOCTEXT_NAMESPACE
