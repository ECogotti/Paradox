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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Puzzle|Inputs")
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
	/** Runtime-resolved form of an input binding after component lookup succeeds. */
	struct FResolvedInputBinding
	{
		/** Local input alias written to the runtime input cache. */
		FName InputId;

		/** Emitter component source; weak to tolerate actor/component teardown. */
		TWeakObjectPtr<UPuzzleEmitterComponent> Emitter;

		/** Signal channel observed on the emitter. */
		FGameplayTag SignalTag;
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

	/**
	 * Validates designer configuration and resolves actor bindings to components.
	 *
	 * @return True when all required inputs, condition, and receivers are valid.
	 */
	bool ValidateAndResolveConfiguration();

	/**
	 * Resolves one input binding to the first emitter or the explicitly named emitter.
	 *
	 * @param Binding Designer-facing binding to resolve.
	 * @param OutEmitter Receives the resolved component on success.
	 * @return True when a valid emitter was resolved.
	 */
	bool ResolveEmitterComponent(const FPuzzleInputBinding& Binding, UPuzzleEmitterComponent*& OutEmitter) const;

	/**
	 * Resolves one receiver binding to the first receiver or the explicitly named receiver.
	 *
	 * @param Binding Designer-facing binding to resolve.
	 * @param OutReceiver Receives the resolved component on success.
	 * @return True when a valid receiver was resolved.
	 */
	bool ResolveReceiverComponent(const FPuzzleReceiverBinding& Binding, UPuzzleReceiverComponent*& OutReceiver) const;

	/** Subscribes to every unique resolved emitter. */
	void BindEmitters();

	/** Removes this controller's native delegate bindings from every bound emitter. */
	void UnbindEmitters();

	/** Initializes the runtime input cache from each emitter's current state. */
	void InitializeInputCache();

	/**
	 * Sends the controller's result to every resolved receiver.
	 *
	 * @param bResult New boolean result requested by this controller.
	 */
	void ApplyEvaluationResultToReceivers(bool bResult);

	/**
	 * Marks cached inputs from one emitter invalid.
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
