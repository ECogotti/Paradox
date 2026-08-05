#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"
#include "PuzzleTransformMover.generated.h"

class APuzzleTransformMover;
class UArrowComponent;
class UBillboardComponent;
class UCurveFloat;
class UPuzzleReceiverComponent;
class USceneComponent;

/** Authoritative direction or stable endpoint of a puzzle transform mover. */
UENUM(BlueprintType)
enum class EPuzzleTransformMoverState : uint8
{
	/** The moved component is exactly at StartArrow. */
	AtStart,

	/** Linear progress is increasing toward EndArrow. */
	MovingTowardEnd,

	/** The moved component is exactly at EndArrow. */
	AtEnd,

	/** Linear progress is decreasing toward StartArrow. */
	MovingTowardStart
};

/** Defines how effective receiver edges select movement targets. */
UENUM(BlueprintType)
enum class EPuzzleTransformMoverMode : uint8
{
	/** The first completed activation traversal latches the mover at End until ResetMover. */
	Latch,

	/** Every accepted activation selects the endpoint opposite the current direction or endpoint. */
	FlipFlop,

	/** Active commands End and inactive commands Start. */
	PingPong
};

/** Defines what receiver deactivation does while interpolation is in progress. */
UENUM(BlueprintType)
enum class EPuzzleTransformMoverDeactivationBehavior : uint8
{
	/** Pause at the current progress and resume the same direction on the next activation. */
	Stop,

	/** Reverse immediately toward the logical origin endpoint. */
	Return,

	/** Ignore this deactivation and finish the current traversal. */
	Continue
};

/** Selects the authoritative endpoint-to-endpoint timing model. */
UENUM(BlueprintType)
enum class EPuzzleTransformMoverTimingMode : uint8
{
	/** Translation speed in Unreal units per second. */
	Speed,

	/** Duration of one complete endpoint-to-endpoint traversal. */
	MovementTime
};

/** Selects how linear movement progress becomes visible interpolation progress. */
UENUM(BlueprintType)
enum class EPuzzleTransformMoverInterpolationSource : uint8
{
	/** Use Unreal's built-in EEasingFunc implementation. */
	BuiltInEasing,

	/** Evaluate MovementCurve over the normalized [0, 1] domain. */
	CustomCurve
};

/** Stable endpoint restored during initialization and ResetMover. */
UENUM(BlueprintType)
enum class EPuzzleTransformMoverInitialPosition : uint8
{
	/** Initialize at StartArrow with MovementAlpha equal to zero. */
	Start,

	/** Initialize at EndArrow with MovementAlpha equal to one. */
	End
};

/** Endpoint selected by one semantic movement request. */
UENUM(BlueprintType)
enum class EPuzzleTransformMoverTarget : uint8
{
	Start,
	End
};

/** Result returned by a native specialization before a request changes mover state. */
UENUM(BlueprintType)
enum class EPuzzleTransformMoverRequestDecision : uint8
{
	Accept,
	Defer,
	Reject
};

/** Serializable authoritative runtime state; visible transforms are rebuilt from this data. */
USTRUCT(BlueprintType)
struct PUZZLESYSTEM_API FPuzzleTransformMoverRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Mover|State")
	EPuzzleTransformMoverState MoverState = EPuzzleTransformMoverState::AtStart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Mover|State")
	bool bIsMovementPaused = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Mover|State")
	bool bLatchCompleted = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Mover|State", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MovementAlpha = 0.0f;
};

/** Blueprint-observable notification emitted by one transform mover. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPuzzleTransformMoverEventDelegate, APuzzleTransformMover*, PuzzleTransformMover);

/**
 * Abstract Receiver-driven Actor that moves a selected scene component between two authored transforms.
 *
 * Native code owns movement state, timing, easing, pause/reversal policy, and Receiver synchronization.
 * Concrete native or Blueprint children supply the visual component that should be moved.
 */
UCLASS(Abstract, Blueprintable)
class PUZZLESYSTEM_API APuzzleTransformMover : public AActor
{
	GENERATED_BODY()

public:
	APuzzleTransformMover();

	/** Initializes component selection, movement state, Receiver binding, and current Receiver synchronization. */
	virtual void BeginPlay() override;

	/** Removes Receiver bindings and disables interpolation before Actor shutdown. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Advances active interpolation and optional movement debug drawing. */
	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	/** Validates component selection, endpoint, timing, and interpolation configuration. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	/** Viewport-selection root for the abstract mover template. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Mover|Components")
	TObjectPtr<UBillboardComponent> BillboardRoot = nullptr;

	/** Green authoring marker for MovementAlpha zero. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Mover|Components")
	TObjectPtr<UArrowComponent> StartArrow = nullptr;

	/** Red authoring marker for MovementAlpha one. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Mover|Components")
	TObjectPtr<UArrowComponent> EndArrow = nullptr;

	/** Receiver whose effective state transitions drive this mover. */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Mover|Components")
	TObjectPtr<UPuzzleReceiverComponent> PuzzleReceiver = nullptr;

	/** Component supplied by this Actor or a Blueprint child and resolved during controlled initialization. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Puzzle|Mover|Configuration", meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.SceneComponent"))
	FComponentReference DefaultMovedComponent;

	/** Receiver-edge policy used to select movement targets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Configuration")
	EPuzzleTransformMoverMode MovementMode = EPuzzleTransformMoverMode::PingPong;

	/** Policy applied only when the Receiver deactivates during an active traversal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Configuration")
	EPuzzleTransformMoverDeactivationBehavior DeactivationBehavior = EPuzzleTransformMoverDeactivationBehavior::Return;

	/** Endpoint established before the current Receiver state is synchronized. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Configuration")
	EPuzzleTransformMoverInitialPosition InitialPosition = EPuzzleTransformMoverInitialPosition::Start;

	/** When enabled, initial Receiver synchronization uses normal runtime movement instead of an event-free snap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Configuration")
	bool bAnimateInitialReceiverState = false;

	/** Selects speed-based or full-duration movement timing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Timing")
	EPuzzleTransformMoverTimingMode TimingMode = EPuzzleTransformMoverTimingMode::MovementTime;

	/** Enables an independent speed or duration while moving back toward Start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Timing")
	bool bUseSeparateReturnTiming = false;

	/** Start-to-End translation speed in Unreal units per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Timing", meta = (ClampMin = "0.001", Units = "cm/s", EditCondition = "TimingMode == EPuzzleTransformMoverTimingMode::Speed", EditConditionHides))
	float ForwardSpeed = 100.0f;

	/** End-to-Start translation speed when separate return timing is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Timing", meta = (ClampMin = "0.001", Units = "cm/s", EditCondition = "TimingMode == EPuzzleTransformMoverTimingMode::Speed && bUseSeparateReturnTiming", EditConditionHides))
	float ReturnSpeed = 100.0f;

	/** Seconds required for one complete Start-to-End traversal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Timing", meta = (ClampMin = "0.001", Units = "s", EditCondition = "TimingMode == EPuzzleTransformMoverTimingMode::MovementTime", EditConditionHides))
	float ForwardMovementTime = 1.0f;

	/** Seconds required for one complete End-to-Start traversal when separate return timing is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Timing", meta = (ClampMin = "0.001", Units = "s", EditCondition = "TimingMode == EPuzzleTransformMoverTimingMode::MovementTime && bUseSeparateReturnTiming", EditConditionHides))
	float ReturnMovementTime = 1.0f;

	/** Selects Unreal built-in easing or a normalized custom float curve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Interpolation")
	EPuzzleTransformMoverInterpolationSource InterpolationSource = EPuzzleTransformMoverInterpolationSource::BuiltInEasing;

	/** Verified Unreal easing function applied to linear MovementAlpha. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Interpolation", meta = (EditCondition = "InterpolationSource == EPuzzleTransformMoverInterpolationSource::BuiltInEasing", EditConditionHides))
	TEnumAsByte<EEasingFunc::Type> BuiltInEasingType = EEasingFunc::Linear;

	/** Blend exponent used by Unreal's EaseIn, EaseOut, and EaseInOut modes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Interpolation", meta = (ClampMin = "0.001", EditCondition = "InterpolationSource == EPuzzleTransformMoverInterpolationSource::BuiltInEasing", EditConditionHides))
	float EasingExponent = 2.0f;

	/** Step count used by Unreal's Step easing mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Interpolation", meta = (ClampMin = "2", EditCondition = "InterpolationSource == EPuzzleTransformMoverInterpolationSource::BuiltInEasing", EditConditionHides))
	int32 EasingSteps = 2;

	/** Normalized curve where X is MovementAlpha and Y is clamped visible interpolation alpha. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Mover|Interpolation", meta = (EditCondition = "InterpolationSource == EPuzzleTransformMoverInterpolationSource::CustomCurve", EditConditionHides))
	TObjectPtr<UCurveFloat> MovementCurve = nullptr;

	/** Enables local movement visualization while the mover is actively interpolating. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Mover|Debug")
	bool bEnableDebug = false;

	/** Height above the current path position used by the runtime debug label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Mover|Debug", meta = (ClampMin = "0.0"))
	float DebugVerticalOffset = 100.0f;

	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Mover|Events")
	FPuzzleTransformMoverEventDelegate OnMovementStarted;

	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Mover|Events")
	FPuzzleTransformMoverEventDelegate OnMovementResumed;

	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Mover|Events")
	FPuzzleTransformMoverEventDelegate OnMovementReversed;

	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Mover|Events")
	FPuzzleTransformMoverEventDelegate OnMovementPaused;

	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Mover|Events")
	FPuzzleTransformMoverEventDelegate OnReachedStart;

	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Mover|Events")
	FPuzzleTransformMoverEventDelegate OnReachedEnd;

	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Mover|Events")
	FPuzzleTransformMoverEventDelegate OnMovedComponentChanged;

	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Mover|Events")
	FPuzzleTransformMoverEventDelegate OnMoverReset;

	/** Replaces the controlled component and synchronizes it to current movement progress. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Mover")
	bool SetMovedComponent(USceneComponent* NewComponent);

	/** Restores the component selected by DefaultMovedComponent. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Mover")
	bool RestoreDefaultMovedComponent();

	/** Returns the currently controlled component without granting mutable state ownership. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	USceneComponent* GetMovedComponent() const;

	/** Returns whether the current moved component satisfies ownership, mobility, registration, and physics rules. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	bool HasValidMovedComponent() const;

	/** Restores the configured initial endpoint without replaying current Receiver state. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Mover")
	virtual void ResetMover();

	/** Explicitly reapplies the Receiver's current state, either with movement or an event-free snap. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Mover")
	bool SynchronizeWithCurrentReceiverState(bool bAnimate);

	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	EPuzzleTransformMoverState GetMoverState() const;

	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	EPuzzleTransformMoverMode GetMovementMode() const;

	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	EPuzzleTransformMoverDeactivationBehavior GetDeactivationBehavior() const;

	/** Returns authoritative linear progress in the inclusive [0, 1] range. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	float GetMovementAlpha() const;

	/** Returns the last easing/curve result used for the visible transform. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	float GetEasedAlpha() const;

	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	bool IsMoving() const;

	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	bool IsMovementPaused() const;

	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	bool IsAtStart() const;

	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	bool IsAtEnd() const;

	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	bool IsLatchCompleted() const;

	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	FTransform GetStartTransform() const;

	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	FTransform GetEndTransform() const;

	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	FTransform GetCurrentTargetTransform() const;

	/** Calculates time remaining in the current direction; returns false for invalid timing configuration. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	bool GetRemainingMovementTime(float& OutRemainingSeconds) const;

	/** Captures only authoritative mover state; component transforms and presentation are derived. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Mover|State")
	FPuzzleTransformMoverRuntimeState CaptureRuntimeState() const;

	/** Restores authoritative mover state and rebuilds the moved component without emitting presentation events. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Mover|State")
	bool RestoreRuntimeState(const FPuzzleTransformMoverRuntimeState& RuntimeState);

	/** Changes only the local debug flag; Tick remains movement-driven and is never enabled solely for debug. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Mover|Debug")
	void SetMoverDebugEnabled(bool bInEnableDebug);

protected:
#if WITH_EDITOR
	/** Excludes transient Blueprint compilation/reinstancing objects from asset validation. */
	bool ShouldValidateMoverData() const;
#endif

	/** Called for every semantic target request, including an otherwise deduplicated request. */
	virtual void OnMovementTargetRequestedNative(EPuzzleTransformMoverTarget RequestedTarget);

	/** Dependency-free request gate evaluated before mover state, alpha, Tick, latch, or events change. */
	virtual EPuzzleTransformMoverRequestDecision EvaluateMovementRequestNative(
		EPuzzleTransformMoverTarget RequestedTarget);

	/** Lets a specialization suppress Receiver-driven policy during an authoritative lifecycle operation. */
	virtual bool ShouldProcessReceiverStateNative(bool bReceiverActive);

	/** Native correctness hooks always execute before their Blueprint hook and public delegate. */
	virtual void OnMovementStartedNative();
	virtual void OnMovementResumedNative();
	virtual void OnMovementReversedNative();
	virtual void OnMovementPausedNative();
	virtual void OnMovementUpdatedNative(float CurrentMovementAlpha, float CurrentEasedAlpha);
	virtual void OnReachedStartNative();
	virtual void OnReachedEndNative();
	virtual void OnMovedComponentChangingNative(USceneComponent* PreviousComponent, USceneComponent* NewComponent);
	virtual void OnMovedComponentChangedNative(USceneComponent* PreviousComponent, USceneComponent* NewComponent);
	virtual void OnMoverResetNative();

	/** Re-evaluates movement-driven Tick after a native specialization changes relevant state. */
	void RefreshMovementTickState();

	/** Starts or deduplicates movement toward Start while preserving reversal progress. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Mover|Control", meta = (BlueprintProtected = "true"))
	bool RequestMoveTowardStart();

	/** Starts or deduplicates movement toward End while preserving reversal progress. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Mover|Control", meta = (BlueprintProtected = "true"))
	bool RequestMoveTowardEnd();

	/** Applies Stop semantics to an active traversal. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Mover|Control", meta = (BlueprintProtected = "true"))
	bool PauseMovement();

	/** Resumes a Stop-paused traversal in its preserved direction. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Mover|Control", meta = (BlueprintProtected = "true"))
	bool ResumeMovement();

	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Mover|Events")
	void HandleMovementStarted();
	virtual void HandleMovementStarted_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Mover|Events")
	void HandleMovementResumed();
	virtual void HandleMovementResumed_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Mover|Events")
	void HandleMovementReversed();
	virtual void HandleMovementReversed_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Mover|Events")
	void HandleMovementPaused();
	virtual void HandleMovementPaused_Implementation();

	/** Per-frame subclass hook; avoid expensive Blueprint work because it runs during active interpolation. */
	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Mover|Events")
	void HandleMovementUpdated(float CurrentMovementAlpha, float CurrentEasedAlpha);
	virtual void HandleMovementUpdated_Implementation(float CurrentMovementAlpha, float CurrentEasedAlpha);

	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Mover|Events")
	void HandleReachedStart();
	virtual void HandleReachedStart_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Mover|Events")
	void HandleReachedEnd();
	virtual void HandleReachedEnd_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Mover|Events")
	void HandleMovedComponentChanged();
	virtual void HandleMovedComponentChanged_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "Puzzle|Mover|Events")
	void HandleMoverReset();
	virtual void HandleMoverReset_Implementation();

	/** Controlled initialization extension used by native specializations and automation fixtures. */
	bool InitializePuzzleTransformMover();

private:
	/** Receives effective state edges only from the owned Receiver component. */
	void HandleOwnedReceiverStateChanged(UPuzzleReceiverComponent* Receiver, bool bIsActive);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Mover|Runtime", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> MovedComponent = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Mover|Runtime", meta = (AllowPrivateAccess = "true"))
	EPuzzleTransformMoverState MoverState = EPuzzleTransformMoverState::AtStart;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Mover|Runtime", meta = (AllowPrivateAccess = "true"))
	bool bIsMovementPaused = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Mover|Runtime", meta = (AllowPrivateAccess = "true"))
	bool bLatchCompleted = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Mover|Runtime", meta = (AllowPrivateAccess = "true"))
	float MovementAlpha = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Puzzle|Mover|Runtime", meta = (AllowPrivateAccess = "true"))
	float EasedAlpha = 0.0f;

	bool bIsRuntimeInitialized = false;
	bool bIsInitializingFromReceiver = false;
	bool bConfigurationValid = false;
	bool bInvalidMovedComponentWarningEmitted = false;
	FDelegateHandle ReceiverStateChangedHandle;

	/** Resolves the designer-authored default component without searching the world. */
	USceneComponent* ResolveDefaultMovedComponent() const;

	/** Validates one candidate against the mover's ownership and transform-control contract. */
	bool ValidateMovedComponent(const USceneComponent* Candidate, bool bRequireRegistered, bool bLogErrors) const;

	/** Validates components, endpoints, timing, and interpolation configuration. */
	bool ValidateMoverConfiguration(bool bLogErrors) const;

	/** Returns whether a new movement request may safely change authoritative direction. */
	bool CanProcessMovementRequest(const TCHAR* OperationName) const;

	/** Processes one effective Receiver activation according to MovementMode. */
	void ProcessReceiverActivated();

	/** Processes one effective Receiver deactivation and its in-progress policy. */
	void ProcessReceiverDeactivated();

	/** Returns the endpoint selected by a FlipFlop activation. */
	bool ShouldFlipFlopTargetEnd() const;

	/** Applies current Receiver state without emitting movement presentation events. */
	bool ApplyReceiverStateWithoutAnimation(bool bReceiverActive);

	/** Restores state from InitialPosition and optionally synchronizes the component transform. */
	bool RestoreInitialPosition(bool bSynchronizeComponent);

	/** Snaps to one exact endpoint without movement presentation events. */
	bool SnapToEndpoint(bool bToEnd);

	/** Calculates and stores EasedAlpha from authoritative MovementAlpha. */
	bool UpdateEasedAlpha(bool bLogErrors);

	/** Applies the transform derived from endpoint markers and EasedAlpha. */
	bool SynchronizeMovedComponent(bool bLogErrors);

	/** Advances MovementAlpha once and performs exact endpoint completion. */
	void AdvanceMovement(float DeltaSeconds);

	/** Returns normalized alpha advance per second for the current direction. */
	bool GetActiveAlphaPerSecond(float& OutAlphaPerSecond) const;

	/** Updates Actor Tick so it runs only during valid, unpaused interpolation. */
	void UpdateMovementTickState();

	/** Stops movement safely when the selected component becomes invalid mid-traversal. */
	void HandleMovedComponentInvalidation();

	/** Emits an endpoint transition after exact state and latch updates. */
	void CompleteMovementAtEndpoint(bool bReachedEnd);

	/** Draws active-movement path, direction, progress, and state. */
	void DrawMovementDebug() const;

	/** Returns whether local and global visual debug controls permit drawing. */
	bool ShouldDrawMovementDebug() const;
};
