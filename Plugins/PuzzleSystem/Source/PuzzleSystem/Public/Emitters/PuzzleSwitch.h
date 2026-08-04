#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "PuzzleSwitch.generated.h"

class UPuzzleEmitterComponent;
class USceneComponent;
class APuzzleSwitch;

/** Defines how confirmed input edges change a puzzle switch's output state. */
UENUM(BlueprintType)
enum class EPuzzleSwitchMode : uint8
{
	/** Output remains active only while the logical input is pressed. */
	Hold,

	/** Every confirmed press edge inverts the output state. */
	Toggle,

	/** The first confirmed press activates the output until an explicit reset. */
	Latch,

	/** A confirmed press activates the output for the configured pulse duration. */
	Pulse
};

/** Authoritative state of the switch's raw and confirmed input transition. */
UENUM(BlueprintType)
enum class EPuzzleSwitchInputState : uint8
{
	/** Raw and logical input are released; no input delay is pending. */
	Released,

	/** Raw input is pressed while logical confirmation waits for PressDelay. */
	PressPending,

	/** Raw and logical input are pressed; no input delay is pending. */
	Pressed,

	/** Raw input is released while logical confirmation waits for ReleaseDelay. */
	ReleasePending
};

/** Stable logical input restored at initialization and by ResetSwitch. */
UENUM(BlueprintType)
enum class EPuzzleSwitchInitialInputState : uint8
{
	/** Initialize with raw and logical input released. */
	Released,

	/** Initialize with raw and logical input already pressed, without manufacturing a new edge. */
	Pressed
};

/** Defines how a confirmed press affects a pulse that is already running. */
UENUM(BlueprintType)
enum class EPuzzlePulseRetriggerMode : uint8
{
	/** Keep the original pulse completion time. */
	Ignore,

	/** Restart pulse completion from the full configured duration. */
	Restart
};

/** Blueprint-observable notification emitted by one puzzle switch instance. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPuzzleSwitchEventDelegate, APuzzleSwitch*, PuzzleSwitch);

/**
 * Abstract reusable base for two-edge puzzle controls such as buttons, levers, and pressure plates.
 *
 * Concrete children decide how raw Press and Release requests are detected. This class owns input
 * confirmation, switch-mode policy, timer lifetime, and publication through its emitter component.
 */
UCLASS(Abstract, Blueprintable)
class PUZZLESYSTEM_API APuzzleSwitch : public AActor
{
	GENERATED_BODY()

public:
	/** Stable name used by native subclasses that replace the default optional root component. */
	static const FName SceneRootComponentName;

	APuzzleSwitch(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Synchronizes optional debug ticking after components register. */
	virtual void PostRegisterAllComponents() override;

	/** Initializes authoritative input/output state and publishes the initial signal. */
	virtual void BeginPlay() override;

	/** Invalidates every switch-owned delayed callback before Actor shutdown. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Draws switch state only while local and global visual debug are enabled. */
	virtual void Tick(float DeltaSeconds) override;

	/** Allows explicitly enabled switch debug labels in editor viewports. */
	virtual bool ShouldTickIfViewportsOnly() const override;

#if WITH_EDITOR
	/** Validates signal, component, and timer configuration in editor validation passes. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

	/** Keeps debug tick state synchronized with Details-panel changes. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Default minimal root; specialized native children may replace this optional subobject. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Switch|Components")
	TObjectPtr<USceneComponent> SceneRootComponent = nullptr;

	/** Publishes the switch's single configured output signal. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Switch|Components")
	TObjectPtr<UPuzzleEmitterComponent> PuzzleEmitterComponent = nullptr;

	/** Policy applied to confirmed Press and Release edges. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Switch|Configuration")
	EPuzzleSwitchMode SwitchMode = EPuzzleSwitchMode::Hold;

	/** Raw/logical input condition established without delay or edge events at initialization and reset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Switch|Configuration")
	EPuzzleSwitchInitialInputState InitialInputState = EPuzzleSwitchInitialInputState::Released;

	/** Stateful signal channel published by the owned emitter component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Switch|Configuration")
	FGameplayTag OutputSignalTag;

	/** Independent output state established at runtime initialization and restored by ResetSwitch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Switch|Configuration")
	bool bStartActive = false;

	/** Continuous raw-press time required before producing a confirmed Press edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Switch|Input", meta = (ClampMin = "0.0", Units = "s"))
	float PressDelay = 0.0f;

	/** Continuous raw-release time required before producing a confirmed Release edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Switch|Input", meta = (ClampMin = "0.0", Units = "s"))
	float ReleaseDelay = 0.0f;

	/** Pulse lifetime; values less than or equal to zero complete on the next timer-manager tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Switch|Pulse", meta = (ClampMin = "0.0", Units = "s", EditCondition = "SwitchMode == EPuzzleSwitchMode::Pulse", EditConditionHides))
	float PulseDuration = 1.0f;

	/** Behavior when a new confirmed Press edge arrives while a pulse is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Switch|Pulse", meta = (EditCondition = "SwitchMode == EPuzzleSwitchMode::Pulse", EditConditionHides))
	EPuzzlePulseRetriggerMode PulseRetriggerMode = EPuzzlePulseRetriggerMode::Ignore;

	/** Enables local visual debug for this switch; the module global switch must also be enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Switch|Debug")
	bool bEnableDebug = false;

	/** Height above the Actor used for its switch-state debug label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Switch|Debug", meta = (ClampMin = "0.0"))
	float DebugVerticalOffset = 120.0f;

	/** Emitted after a confirmed logical Press edge is accepted. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Switch|Events")
	FPuzzleSwitchEventDelegate OnInputPressed;

	/** Emitted after a confirmed logical Release edge is accepted. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Switch|Events")
	FPuzzleSwitchEventDelegate OnInputReleased;

	/** Emitted after entering PressPending and starting its timer. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Switch|Events")
	FPuzzleSwitchEventDelegate OnPressDelayStarted;

	/** Emitted after PressPending is cancelled without a confirmed edge. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Switch|Events")
	FPuzzleSwitchEventDelegate OnPressDelayCancelled;

	/** Emitted after PressPending confirms and its Press edge has been processed. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Switch|Events")
	FPuzzleSwitchEventDelegate OnPressDelayCompleted;

	/** Emitted after entering ReleasePending and starting its timer. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Switch|Events")
	FPuzzleSwitchEventDelegate OnReleaseDelayStarted;

	/** Emitted after ReleasePending is cancelled without a confirmed edge. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Switch|Events")
	FPuzzleSwitchEventDelegate OnReleaseDelayCancelled;

	/** Emitted after ReleasePending confirms and its Release edge has been processed. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Switch|Events")
	FPuzzleSwitchEventDelegate OnReleaseDelayCompleted;

	/** Emitted after the output changes to active and the new signal state is cached. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Switch|Events")
	FPuzzleSwitchEventDelegate OnSwitchActivated;

	/** Emitted after the output changes to inactive and the new signal state is cached. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Switch|Events")
	FPuzzleSwitchEventDelegate OnSwitchDeactivated;

	/** Emitted after ResetSwitch restores input and output state. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Switch|Events")
	FPuzzleSwitchEventDelegate OnSwitchReset;

	/**
	 * Requests raw pressed input.
	 *
	 * @return True when the request starts, confirms, or cancels a real input transition.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Switch|Input")
	bool Press();

	/**
	 * Requests raw released input.
	 *
	 * @return True when the request starts, confirms, or cancels a real input transition.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Switch|Input")
	bool Release();

	/** Cancels all delayed work and restores the configured initial input and output states. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Switch")
	virtual void ResetSwitch();

	/** Returns the authoritative raw/logical input transition state. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Switch|State")
	EPuzzleSwitchInputState GetInputState() const;

	/** Returns true while raw input is represented as pressed. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Switch|State")
	bool IsInputPressed() const;

	/** Returns true while logical input is confirmed as pressed. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Switch|State")
	bool IsPressed() const;

	/** Returns the authoritative puzzle output state. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Switch|State")
	bool IsSwitchActive() const;

	/** Restarts an existing PressDelay from its full configured duration. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Switch|Input")
	bool RestartPressDelay();

	/** Restarts an existing ReleaseDelay from its full configured duration. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Switch|Input")
	bool RestartReleaseDelay();

	/** Cancels PressPending and restores Released without producing a confirmed edge. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Switch|Input")
	bool CancelPendingPress();

	/** Cancels ReleasePending and restores Pressed without producing a confirmed edge. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Switch|Input")
	bool CancelPendingRelease();

	/** Returns true exactly while PressPending is authoritative. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Switch|State")
	bool IsPressDelayPending() const;

	/** Returns true exactly while ReleasePending is authoritative. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Switch|State")
	bool IsReleaseDelayPending() const;

	/** Returns the pending press timer's remaining seconds, or zero when no press is pending. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Switch|State")
	float GetPressDelayRemaining() const;

	/** Returns the pending release timer's remaining seconds, or zero when no release is pending. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Switch|State")
	float GetReleaseDelayRemaining() const;

	/** Enables or disables local switch debug drawing and its otherwise-disabled Tick. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Switch|Debug")
	void SetSwitchDebugEnabled(bool bInEnableDebug);

protected:
	/** Internal extension point called after a logical Press is processed and before OnInputPressed. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Switch|Events")
	void HandleInputPressed();
	virtual void HandleInputPressed_Implementation();

	/** Internal extension point called after a logical Release is processed and before OnInputReleased. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Switch|Events")
	void HandleInputReleased();
	virtual void HandleInputReleased_Implementation();

	/** Internal extension point called after PressPending begins and before OnPressDelayStarted. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Switch|Events")
	void HandlePressDelayStarted();
	virtual void HandlePressDelayStarted_Implementation();

	/** Internal extension point called after PressPending is cancelled and before OnPressDelayCancelled. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Switch|Events")
	void HandlePressDelayCancelled();
	virtual void HandlePressDelayCancelled_Implementation();

	/** Internal extension point called after a delayed Press is processed and before OnPressDelayCompleted. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Switch|Events")
	void HandlePressDelayCompleted();
	virtual void HandlePressDelayCompleted_Implementation();

	/** Internal extension point called after ReleasePending begins and before OnReleaseDelayStarted. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Switch|Events")
	void HandleReleaseDelayStarted();
	virtual void HandleReleaseDelayStarted_Implementation();

	/** Internal extension point called after ReleasePending is cancelled and before OnReleaseDelayCancelled. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Switch|Events")
	void HandleReleaseDelayCancelled();
	virtual void HandleReleaseDelayCancelled_Implementation();

	/** Internal extension point called after a delayed Release is processed and before OnReleaseDelayCompleted. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Switch|Events")
	void HandleReleaseDelayCompleted();
	virtual void HandleReleaseDelayCompleted_Implementation();

	/** Internal extension point called after activation is published and before OnSwitchActivated. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Switch|Events")
	void HandleSwitchActivated();
	virtual void HandleSwitchActivated_Implementation();

	/** Internal extension point called after deactivation is published and before OnSwitchDeactivated. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Switch|Events")
	void HandleSwitchDeactivated();
	virtual void HandleSwitchDeactivated_Implementation();

	/** Internal extension point called after reset is applied and before OnSwitchReset. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Switch|Events")
	void HandleSwitchReset();
	virtual void HandleSwitchReset_Implementation();

	/** Lets native subclasses retain event-driven Tick work alongside optional switch debug drawing. */
	virtual bool ShouldEnableSwitchTick() const;

	/** Re-evaluates the native subclass Tick policy after its local runtime state changes. */
	void RefreshSwitchTickState();

	/** Establishes runtime state; protected so native test/specialization code can initialize controlled fixtures. */
	void InitializePuzzleSwitch();

private:
	/** Converts the designer-facing two-state initial setting into the runtime input-state enum. */
	EPuzzleSwitchInputState GetConfiguredInitialInputState() const;

	/** Single authoritative input-transition state. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Switch|Runtime", meta = (AllowPrivateAccess = "true"))
	EPuzzleSwitchInputState InputState = EPuzzleSwitchInputState::Released;

	/** Single authoritative output state, kept synchronized with the configured emitter signal. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Switch|Runtime", meta = (AllowPrivateAccess = "true"))
	bool bIsActive = false;

	/** True while pulse completion owns a live timer or next-tick callback. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Switch|Runtime", meta = (AllowPrivateAccess = "true"))
	bool bPulseCompletionPending = false;

	/** True after BeginPlay initialization establishes a callable runtime state. */
	bool bIsSwitchInitialized = false;

	/** Result of the latest runtime configuration validation. */
	bool bConfigurationValid = false;

	/** Handle for confirmed Press delay. */
	FTimerHandle PressDelayTimerHandle;

	/** Handle for confirmed Release delay. */
	FTimerHandle ReleaseDelayTimerHandle;

	/** Handle for normal or next-tick Pulse completion. */
	FTimerHandle PulseTimerHandle;

	/** Invalidates callbacks created by earlier Press delay generations. */
	uint32 PressDelayGeneration = 0;

	/** Invalidates callbacks created by earlier Release delay generations. */
	uint32 ReleaseDelayGeneration = 0;

	/** Invalidates callbacks created by earlier Pulse generations. */
	uint32 PulseGeneration = 0;

	/** Validates required component, tag, and duration configuration and optionally logs failures. */
	bool ValidateSwitchConfiguration(bool bLogErrors) const;

	/** Returns whether public state-changing requests may run. */
	bool CanProcessInputRequest(const TCHAR* OperationName) const;

	/** Starts the first PressDelay transition and emits its Started event. */
	bool StartPressDelay();

	/** Starts the first ReleaseDelay transition and emits its Started event. */
	bool StartReleaseDelay();

	/** Installs or replaces a positive-duration PressDelay callback. */
	bool SchedulePressDelay();

	/** Installs or replaces a positive-duration ReleaseDelay callback. */
	bool ScheduleReleaseDelay();

	/** Applies one confirmed Press edge to delegates and switch mode policy. */
	void HandleConfirmedPress();

	/** Applies one confirmed Release edge to delegates and switch mode policy. */
	void HandleConfirmedRelease();

	/** Completes PressPending only when its state and callback generation still match. */
	void HandlePressDelayElapsed(uint32 ExpectedGeneration);

	/** Completes ReleasePending only when its state and callback generation still match. */
	void HandleReleaseDelayElapsed(uint32 ExpectedGeneration);

	/** Starts or retriggers Pulse behavior after a confirmed Press edge. */
	void StartOrRetriggerPulse();

	/** Schedules normal or deferred Pulse completion and invalidates the previous generation. */
	bool SchedulePulseCompletion();

	/** Completes the Pulse only when its callback generation is current. */
	void HandlePulseElapsed(uint32 ExpectedGeneration);

	/** Deduplicates, publishes, and notifies one authoritative output transition. */
	bool SetSwitchActive(bool bNewActive);

	/** Clears and invalidates the PressDelay timer without emitting an event. */
	void InvalidatePressDelay();

	/** Clears and invalidates the ReleaseDelay timer without emitting an event. */
	void InvalidateReleaseDelay();

	/** Clears and invalidates Pulse completion without emitting an event. */
	void InvalidatePulse();

	/** Clears every switch-owned timer and stale callback generation. */
	void InvalidateAllTimers();

	/** Draws a compact world-space snapshot of input, output, delay, and pulse state. */
	void DrawSwitchDebug() const;

	/** Returns whether local and module-wide debug controls allow drawing. */
	bool ShouldDrawSwitchDebug() const;

};
