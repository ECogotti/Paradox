#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Signals/PuzzleSignalTypes.h"
#include "PuzzleController.generated.h"

class UPuzzleCondition;
class UPuzzleEmitterComponent;
class UPuzzleReceiverComponent;
class UPuzzleSignalPayload;
class UBillboardComponent;

/** Designer-facing gate input resolved inside one primary input binding only. */
USTRUCT(BlueprintType)
struct PUZZLESYSTEM_API FPuzzleEmitterGateBinding
{
	GENERATED_BODY()

	/** Local alias used by this primary binding's gate conditions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Gate")
	FName InputId;

	/** Actor that owns the emitter component publishing this gate input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Gate")
	TObjectPtr<AActor> EmitterActor = nullptr;

	/** When enabled, resolves the emitter by `EmitterComponentName`; otherwise uses the first available emitter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Gate")
	bool bSpecifyEmitterComponent = false;

	/** Emitter component name used only when `bSpecifyEmitterComponent` is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Gate", meta = (EditCondition = "bSpecifyEmitterComponent", EditConditionHides))
	FName EmitterComponentName;

	/** Gameplay tag identifying the signal channel on the resolved emitter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Gate")
	FGameplayTag SignalTag;
};

/** Designer-facing binding from one external emitter signal to one controller-local input ID. */
USTRUCT(BlueprintType)
struct PUZZLESYSTEM_API FPuzzleInputBinding
{
	GENERATED_BODY()

	/** Local alias used by conditions inside this controller. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle")
	FName InputId;

	/** Actor that owns the emitter component publishing this input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle")
	TObjectPtr<AActor> EmitterActor = nullptr;

	/** When enabled, resolves the emitter by `EmitterComponentName`; otherwise uses the first available emitter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle")
	bool bSpecifyEmitterComponent = false;

	/** Emitter component name used only when `bSpecifyEmitterComponent` is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle", meta = (EditCondition = "bSpecifyEmitterComponent", EditConditionHides))
	FName EmitterComponentName;

	/** Gameplay tag identifying the signal channel on the resolved emitter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle")
	FGameplayTag SignalTag;

	/** Gate-local emitter inputs. Ignored unless at least one Gate Condition is also configured. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Gate", meta = (TitleProperty = "InputId"))
	TArray<FPuzzleEmitterGateBinding> EmitterGates;

	/** Conditions evaluated with AND semantics against this binding's gate-local inputs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Puzzle|Gate", meta = (EditInline, AllowEditInlineCustomization, MaxPropertyDepth = "8"))
	TArray<TObjectPtr<UPuzzleCondition>> GateConditions;
};

/** Designer-facing binding to one receiver component controlled by this controller. */
USTRUCT(BlueprintType)
struct PUZZLESYSTEM_API FPuzzleReceiverBinding
{
	GENERATED_BODY()

	/** Actor that owns the receiver component to control. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle")
	TObjectPtr<AActor> ReceiverActor = nullptr;

	/** When enabled, resolves the receiver by `ReceiverComponentName`; otherwise uses the first available receiver. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle")
	bool bSpecifyReceiverComponent = false;

	/** Receiver component name used only when `bSpecifyReceiverComponent` is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle", meta = (EditCondition = "bSpecifyReceiverComponent", EditConditionHides))
	FName ReceiverComponentName;
};

/** Actor that observes emitter signals, evaluates a condition tree, and requests receiver activation. */
UCLASS(Blueprintable)
class PUZZLESYSTEM_API APuzzleController : public AActor
{
	GENERATED_BODY()

public:
	APuzzleController();

	/** Synchronizes debug ticking after components are registered in editor or runtime worlds. */
	virtual void PostRegisterAllComponents() override;

	/** Initializes bindings and applies the initial receiver request after all actors have begun play. */
	virtual void BeginPlay() override;

	/**
	 * Releases emitter bindings and receiver requests during shutdown.
	 *
	 * @param EndPlayReason Unreal reason for the actor ending play.
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Draws local debug visualization when enabled.
	 *
	 * @param DeltaSeconds Frame delta provided by Unreal; unused because puzzle evaluation is event-driven.
	 */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Allows debug drawing in editor viewports when the local debug flag is enabled.
	 *
	 * @return True when this actor should tick in viewport-only editor worlds.
	 */
	virtual bool ShouldTickIfViewportsOnly() const override;

#if WITH_EDITOR
	/** Validates primary bindings, enabled gates, condition ownership, and Receiver wiring. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;

	/**
	 * Keeps debug tick state in sync when designers toggle properties in the Details panel.
	 *
	 * @param PropertyChangedEvent Unreal editor property-change description.
	 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Billboard root used to make puzzle controllers visible and selectable in the level. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Controller", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBillboardComponent> PuzzleBillboardComponent = nullptr;

	/** Input wiring from external emitter signals to local condition input IDs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Inputs", meta = (TitleProperty = "InputId"))
	TArray<FPuzzleInputBinding> InputBindings;

	/** Root condition object evaluated to decide whether this controller requests activation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Puzzle|Logic", meta = (EditInline, AllowEditInlineCustomization, MaxPropertyDepth = "8"))
	TObjectPtr<UPuzzleCondition> RootCondition = nullptr;

	/** Receiver components that receive this controller's single boolean result. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Receivers")
	TArray<FPuzzleReceiverBinding> ReceiverBindings;

	/** Enables local visual debug drawing for this controller instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Debug")
	bool bEnableDebug = false;

	/** Height above the controller used for the debug text label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Puzzle|Debug", meta = (ClampMin = "0.0"))
	float DebugVerticalOffset = 120.0f;

	/**
	 * Validates configuration, resolves bindings, subscribes to emitters, and evaluates initial state.
	 *
	 * @return True when configuration is valid and runtime subscriptions were created.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Controller")
	bool InitializePuzzleController();

	/** Unsubscribes from emitters and removes this controller's requests from resolved receivers. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Controller")
	void ShutdownPuzzleController();

	/** Re-evaluates the root condition and applies the result when it changed. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Controller")
	void EvaluateController();

	/**
	 * Reads the latest cached state for a local input.
	 *
	 * @param InputId Local input alias configured in `InputBindings`.
	 * @param OutInputState Receives the cached state or an invalid default when not found.
	 * @return True when the input exists and is currently valid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Controller")
	bool TryGetInputState(FName InputId, FPuzzleSignalState& OutInputState) const;

	/** Explicitly reads the effective main-input state, independent of condition evaluation scope. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Controller")
	bool TryGetEffectiveInputState(FName InputId, FPuzzleSignalState& OutInputState) const;

	/** Reads the source state before gate admission. Intentionally C++-only to protect Blueprint condition invariants. */
	bool TryGetRawInputState(FName InputId, FPuzzleSignalState& OutInputState) const;

	/** Reads one gate-local signal state for diagnostics and Blueprint tooling. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Controller|Gate")
	bool TryGetGateInputState(FName PrimaryInputId, FName GateInputId, FPuzzleSignalState& OutInputState) const;

	/** Returns true when either gate array is empty and the primary signal therefore bypasses gating. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Controller|Gate")
	bool IsInputGateBypassed(FName PrimaryInputId) const;

	/** Returns true for a bypassed gate or for an enabled gate with valid runtime inputs. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Controller|Gate")
	bool IsInputGateValid(FName PrimaryInputId) const;

	/** Returns true for a bypassed gate or when every enabled top-level gate condition passes. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Controller|Gate")
	bool DoesInputGateAllowSignal(FName PrimaryInputId) const;

	/**
	 * Checks whether a local input currently has valid data.
	 *
	 * @param InputId Local input alias to query.
	 * @return True when the cached input exists and is valid.
	 */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Controller")
	bool IsInputValid(FName InputId) const;

	/**
	 * Checks whether a local input is currently valid and active.
	 *
	 * @param InputId Local input alias to query.
	 * @return True when the input is valid and active.
	 */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Controller")
	bool IsInputActive(FName InputId) const;

	/**
	 * Reads the payload attached to a local input.
	 *
	 * @param InputId Local input alias to query.
	 * @return Payload object when the input is valid; otherwise null.
	 */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Controller")
	UPuzzleSignalPayload* GetInputPayload(FName InputId) const;

	/**
	 * Reads the revision counter for a local input.
	 *
	 * @param InputId Local input alias to query.
	 * @return Input revision when valid; otherwise zero.
	 */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Controller")
	int64 GetInputRevision(FName InputId) const;

	/**
	 * Returns the last applied controller result.
	 *
	 * @return True when the controller has evaluated active.
	 */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Controller")
	bool IsControllerActive() const;

	/**
	 * Returns current runtime configuration validity.
	 *
	 * @return True after successful initialization validation.
	 */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Controller")
	bool IsPuzzleControllerConfigurationValid() const;

	/**
	 * Enables or disables local visual debug drawing and updates actor ticking.
	 *
	 * @param bInEnableDebug New local debug state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Debug")
	void SetPuzzleDebugEnabled(bool bInEnableDebug);

	/**
	 * Checks whether an input ID exists in this controller's validated configuration.
	 *
	 * @param InputId Local input alias to query.
	 * @return True when the input ID was configured.
	 */
	bool HasConfiguredInput(FName InputId) const;

private:
	/** Runtime-resolved form of one gate-local emitter input. */
	struct FResolvedGateInputBinding
	{
		FName InputId;
		TWeakObjectPtr<UPuzzleEmitterComponent> Emitter;
		FGameplayTag SignalTag;
		FPuzzleSignalState State;
	};

	/** Runtime-resolved form of an input binding after component lookup succeeds. */
	struct FResolvedInputBinding
	{
		/** Index into the serialized `InputBindings` array. */
		int32 ConfigurationIndex = INDEX_NONE;

		/** Local input alias written to the runtime input cache. */
		FName InputId;

		/** Emitter component source; weak to tolerate actor/component teardown. */
		TWeakObjectPtr<UPuzzleEmitterComponent> Emitter;

		/** Signal channel observed on the emitter. */
		FGameplayTag SignalTag;

		/** Latest source state before gate admission. */
		FPuzzleSignalState RawState;

		/** Resolved and cached gate-local inputs. Empty when gate evaluation is bypassed. */
		TArray<FResolvedGateInputBinding> GateInputs;

		/** Gate-local IDs available to conditions in this binding's evaluation scope. */
		TSet<FName> ConfiguredGateInputIds;

		/** Last result of each top-level gate condition, in authored order. */
		TArray<bool> LastGateConditionResults;

		/** True only when both gate arrays were configured. */
		bool bGateEnabled = false;

		/** Runtime validity of every required gate input. Bypassed gates are valid. */
		bool bGateValid = true;

		/** Aggregated AND result. Bypassed gates always allow the primary signal. */
		bool bGateAllowsSignal = true;

		/** True after the first gated effective-state signature has been built. */
		bool bHasEffectiveSignature = false;

		/** Gate-local revision snapshot used to identify meaningful effective updates. */
		TArray<int64> LastGateRevisions;

		/** Raw revision last admitted through an open gate. */
		int64 LastAdmittedRawRevision = 0;

		/** Monotonic Controller-local revision used only by enabled gates. */
		int64 EffectiveRevision = 0;
	};

	/** One cached destination updated by an emitter signal route. */
	struct FSignalRouteDestination
	{
		int32 PrimaryBindingIndex = INDEX_NONE;
		int32 GateBindingIndex = INDEX_NONE;

		bool IsPrimary() const { return GateBindingIndex == INDEX_NONE; }
	};

	/** Routes signal tags from one unique emitter to every affected primary or gate cache. */
	struct FEmitterSignalRoutes
	{
		TMap<FGameplayTag, TArray<FSignalRouteDestination>> DestinationsBySignal;
	};

	/**
	 * Handles signal updates from any bound emitter.
	 *
	 * @param Emitter Emitter component that published the update.
	 * @param SignalTag Signal channel that changed.
	 * @param SignalState New signal state to cache.
	 */
	UFUNCTION()
	void HandleEmitterSignalChanged(UPuzzleEmitterComponent* Emitter, FGameplayTag SignalTag, FPuzzleSignalState SignalState);

	/**
	 * Handles emitter destruction/shutdown by invalidating affected inputs.
	 *
	 * @param Emitter Emitter component that can no longer provide valid state.
	 */
	UFUNCTION()
	void HandleEmitterInvalidated(UPuzzleEmitterComponent* Emitter);

	/** Valid input bindings resolved to concrete emitter components. */
	TArray<FResolvedInputBinding> ResolvedInputBindings;

	/** Unique emitters this controller subscribed to. */
	TArray<TWeakObjectPtr<UPuzzleEmitterComponent>> BoundEmitters;

	/** Receiver components that receive this controller's output request. */
	TArray<TWeakObjectPtr<UPuzzleReceiverComponent>> ResolvedReceivers;

	/** Event routing shared by primary and gate inputs, keyed by unique emitter component. */
	TMap<TWeakObjectPtr<UPuzzleEmitterComponent>, FEmitterSignalRoutes> SignalRoutes;

	/** Fast lookup from a validated primary InputId to its resolved runtime binding. */
	TMap<FName, int32> ResolvedInputIndices;

	/** Latest known state for each configured local input ID. */
	TMap<FName, FPuzzleSignalState> RuntimeInputCache;

	/** Input IDs accepted during validation; used by conditions to fail unknown IDs early. */
	TSet<FName> ConfiguredInputIds;

	/** True while runtime bindings are active. */
	bool bIsInitialized = false;

	/** True when the latest validation pass found usable configuration. */
	bool bConfigurationValid = false;

	/** True after at least one condition evaluation has produced a result. */
	bool bHasEvaluationResult = false;

	/** Last result applied to receiver requests. */
	bool bLastEvaluationResult = false;

	/** Prevents recursive condition evaluation during synchronous puzzle chains. */
	bool bIsEvaluating = false;

	/** Requests one collapsed follow-up evaluation after a reentrant signal update. */
	bool bReevaluationRequested = false;

	/** Resolved primary binding whose gate-local cache is currently visible to a condition. */
	int32 ActiveGateEvaluationBindingIndex = INDEX_NONE;

	/**
	 * Validates designer configuration and resolves actor bindings to components.
	 *
	 * @return True when all required inputs, condition, and receivers are valid.
	 */
	bool ValidateAndResolveConfiguration();

	/**
	 * Resolves an Actor/component selection to the first emitter or the explicitly named emitter.
	 *
	 * @param EmitterActor Actor expected to own the component.
	 * @param bSpecifyEmitterComponent Whether component-name selection is enabled.
	 * @param EmitterComponentName Explicit component name when selection is enabled.
	 * @param BindingContext Human-readable binding identity used in diagnostics.
	 * @param OutEmitter Receives the resolved component on success.
	 * @return True when a valid emitter was resolved.
	 */
	bool ResolveEmitterComponent(
		AActor* EmitterActor,
		bool bSpecifyEmitterComponent,
		FName EmitterComponentName,
		const FString& BindingContext,
		UPuzzleEmitterComponent*& OutEmitter) const;

	/**
	 * Resolves one receiver binding to the first receiver or the explicitly named receiver.
	 *
	 * @param Binding Designer-facing binding to resolve.
	 * @param OutReceiver Receives the resolved component on success.
	 * @return True when a valid receiver was resolved.
	 */
	bool ResolveReceiverComponent(const FPuzzleReceiverBinding& Binding, UPuzzleReceiverComponent*& OutReceiver) const;

	/** Builds emitter/signal routes and subscribes once to every unique resolved emitter. */
	void BindEmitters();

	/** Removes this controller's native delegate bindings from every bound emitter. */
	void UnbindEmitters();

	/** Initializes raw primary and gate caches, then builds every effective input state. */
	void InitializeInputCache();

	/** Re-evaluates one enabled gate and rebuilds its primary input's effective cached state. */
	void RebuildEffectiveInput(int32 PrimaryBindingIndex);

	/** Evaluates all top-level conditions for one valid enabled gate under a scoped local namespace. */
	bool EvaluateGateConditions(int32 PrimaryBindingIndex);

	/** Returns the resolved runtime binding for one primary ID, or null when unavailable. */
	const FResolvedInputBinding* FindResolvedInput(FName PrimaryInputId) const;

	/** Returns the input cache currently visible to conditions according to evaluation scope. */
	const FPuzzleSignalState* FindConditionInputState(FName InputId) const;

	/**
	 * Sends the controller's result to every resolved receiver.
	 *
	 * @param bResult New boolean result requested by this controller.
	 */
	void ApplyEvaluationResultToReceivers(bool bResult);

	/**
	 * Marks every routed primary and gate destination from one emitter invalid.
	 *
	 * @param Emitter Emitter whose bound inputs should fail closed.
	 */
	void MarkInputsFromEmitterInvalid(UPuzzleEmitterComponent* Emitter);

	/** Draws controller-to-emitter and controller-to-receiver debug geometry plus a text label. */
	void DrawPuzzleDebug() const;

	/**
	 * Checks whether local and global visual debug switches currently allow drawing.
	 *
	 * @return True when drawing should happen this tick.
	 */
	bool ShouldDrawPuzzleDebug() const;

	/** Enables actor ticking while local debug is enabled so Details-panel toggles take effect. */
	void UpdateDebugTickState();
};
