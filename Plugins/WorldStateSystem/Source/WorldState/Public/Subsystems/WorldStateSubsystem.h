#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Spawning/WorldStateSpawnStrategy.h"
#include "Types/WorldStateTypes.h"
#include "WorldStateSubsystem.generated.h"

class UWorldStateParticipantComponent;
struct FWorldStateSubsystemRuntime;

/** Blueprint lifecycle notification for an accepted restore session. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWorldStateRestoreLifecycleEvent, const FWorldStateRestoreLifecycleContext&, Context);
/** Blueprint terminal notification carrying the frozen restore result. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWorldStateRestoreTerminalEvent, const FWorldStateRestoreResult&, Result);
/** Native counterpart to the Blueprint lifecycle notification. */
DECLARE_MULTICAST_DELEGATE_OneParam(FWorldStateRestoreLifecycleNativeEvent, const FWorldStateRestoreLifecycleContext&);
/** Native counterpart to the Blueprint terminal notification. */
DECLARE_MULTICAST_DELEGATE_OneParam(FWorldStateRestoreTerminalNativeEvent, const FWorldStateRestoreResult&);

/** Per-world authority for World State registration, snapshots and restore-session transitions. */
UCLASS()
class WORLDSTATE_API UWorldStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UWorldStateSubsystem();
	virtual ~UWorldStateSubsystem() override;

	/** Broadcast immediately after a restore request is accepted and assigned a session ID. */
	UPROPERTY(BlueprintAssignable, Category = "World State|Restore")
	FWorldStateRestoreLifecycleEvent OnWorldStateRestoreStarted;

	/** Broadcast after preflight produces the final dependency-expanded restore order. */
	UPROPERTY(BlueprintAssignable, Category = "World State|Restore")
	FWorldStateRestoreLifecycleEvent OnWorldStateRestoreScopeResolved;

	/** Broadcast exactly once when an accepted restore succeeds, including warning-bearing success. */
	UPROPERTY(BlueprintAssignable, Category = "World State|Restore")
	FWorldStateRestoreTerminalEvent OnWorldStateRestoreCompleted;

	/** Broadcast exactly once when an accepted restore terminates unsuccessfully. */
	UPROPERTY(BlueprintAssignable, Category = "World State|Restore")
	FWorldStateRestoreTerminalEvent OnWorldStateRestoreFailed;

	/** Returns the subsystem's exclusive capture/restore lifecycle state. */
	UFUNCTION(BlueprintPure, Category = "World State")
	EWorldStateSubsystemState GetWorldStateSubsystemState() const { return State; }

	/** Freezes initial registration and makes baseline capture eligible. */
	UFUNCTION(BlueprintCallable, Category = "World State")
	FWorldStateOperationResult FinalizeWorldStateRegistration();

	/** Publishes the one immutable baseline; a valid baseline can never be overwritten. */
	UFUNCTION(BlueprintCallable, Category = "World State")
	FWorldStateCaptureResult CaptureBaseline(const FWorldStateCaptureRequest& Request);

	/** Publishes a value-owned runtime snapshot without replacing earlier valid snapshots. */
	UFUNCTION(BlueprintCallable, Category = "World State")
	FWorldStateCaptureResult CaptureRuntimeSnapshot(const FWorldStateCaptureRequest& Request);

	/** Restores the immutable baseline using the supplied scope and failure policies. */
	UFUNCTION(BlueprintCallable, Category = "World State")
	FWorldStateRestoreResult RestoreBaseline(const FWorldStateRestoreRequest& Request);

	/** Restores Request.SnapshotId synchronously on the Game Thread. */
	UFUNCTION(BlueprintCallable, Category = "World State")
	FWorldStateRestoreResult RestoreSnapshot(const FWorldStateRestoreRequest& Request);

	/** Convenience partial restore that forces a ParticipantIds scope for SnapshotId. */
	UFUNCTION(BlueprintCallable, Category = "World State")
	FWorldStateRestoreResult RestoreParticipants(FWorldStateSnapshotId SnapshotId, const TArray<FWorldStateParticipantId>& ParticipantIds, FWorldStateRestoreRequest Request);

	/** Returns whether the world owns a valid immutable baseline. */
	UFUNCTION(BlueprintPure, Category = "World State")
	bool HasBaseline() const;

	/** Copies read-only metadata for SnapshotId without exposing its private payloads. */
	UFUNCTION(BlueprintPure, Category = "World State")
	bool GetSnapshotSummary(FWorldStateSnapshotId SnapshotId, FWorldStateSnapshotSummary& OutSummary) const;

	/** Returns value copies describing registered and known participant identities. */
	UFUNCTION(BlueprintPure, Category = "World State")
	TArray<FWorldStateParticipantSummary> GetParticipantStateSummaries() const;

	/** Emits a structured, non-mutating registry and snapshot summary through LogWorldState. */
	UFUNCTION(BlueprintCallable, Category = "World State|Debug")
	void DumpWorldStateToLog() const;

	/** Registers a Game-Thread native spawn policy under a stable identifier; duplicate IDs are rejected. */
	bool RegisterSpawnStrategy(FName StrategyId, TSharedRef<IWorldStateSpawnStrategy> Strategy);
	/** Removes a custom spawn policy; active restore sessions reject registry changes. */
	bool UnregisterSpawnStrategy(FName StrategyId);

	/** Native Started observer; payload and timing match OnWorldStateRestoreStarted. */
	FWorldStateRestoreLifecycleNativeEvent& OnRestoreStartedNative();
	/** Native ScopeResolved observer; payload and timing match the Blueprint event. */
	FWorldStateRestoreLifecycleNativeEvent& OnRestoreScopeResolvedNative();
	/** Native successful terminal observer. */
	FWorldStateRestoreTerminalNativeEvent& OnRestoreCompletedNative();
	/** Native failed terminal observer. */
	FWorldStateRestoreTerminalNativeEvent& OnRestoreFailedNative();

	/** Allocates private runtime storage and installs the default spawn strategy. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	/** Clears sessions, deferred mutations, snapshots and weak references during world teardown. */
	virtual void Deinitialize() override;

protected:
	/** Restricts the subsystem to gameplay, PIE and GamePreview worlds. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Adds a live participant or queues the mutation while a registry iteration is protected. */
	bool RegisterParticipant(UWorldStateParticipantComponent* Participant);
	/** Removes a live participant or queues the mutation while a registry iteration is protected. */
	void UnregisterParticipant(UWorldStateParticipantComponent* Participant);
	/** Adds a valid known identity to dirty restore scopes. */
	void MarkParticipantDirty(const FWorldStateParticipantId& ParticipantId);
	/** Verifies identity and live instance ownership against the private registry. */
	bool IsParticipantRegistered(const UWorldStateParticipantComponent* Participant) const;
	/** Transfers an identity staged before deferred spawn into the new participant Component. */
	bool ClaimPendingRespawnIdentity(UWorldStateParticipantComponent* Participant);
	/** Protects active iterations by queueing registration changes caused by callbacks or spawn/destroy. */
	void BeginRegistryMutationDeferral();
	/** Leaves one nested deferral scope and flushes queued changes at the outermost boundary. */
	void EndRegistryMutationDeferral();
	/** Applies queued weak-reference mutations after protected iterations complete. */
	void FlushPendingRegistryMutations();

	/** Runs the transactional validate-to-publication capture pipeline. */
	FWorldStateCaptureResult CaptureSnapshotInternal(const FWorldStateCaptureRequest& Request, bool bBaseline);
	/** Runs acceptance, preflight, ordered mutation and exactly-once terminal notification. */
	FWorldStateRestoreResult RestoreSnapshotInternal(const FWorldStateRestoreRequest& Request, bool bBaselineRequest);

#if WITH_DEV_AUTOMATION_TESTS
	/** Test seam that produces a deterministic deserialize failure without exposing snapshot internals publicly. */
	bool CorruptSnapshotPropertyPayloadForTests(FWorldStateSnapshotId SnapshotId, FWorldStateParticipantId ParticipantId, FName PropertyName);
#endif

	/** Exclusive lifecycle state; all transitions occur synchronously on the Game Thread. */
	EWorldStateSubsystemState State = EWorldStateSubsystemState::Initializing;
	/** Heap-owned non-UObject pimpl containing private snapshot/session storage; explicitly deleted at teardown. */
	FWorldStateSubsystemRuntime* Runtime = nullptr;

	friend class UWorldStateParticipantComponent;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FWorldStateRestoreFailurePolicyTest;
#endif
};
