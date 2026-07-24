#pragma once

#include "Components/ActorComponent.h"
#include "Types/WorldStateTypes.h"
#include "WorldStateParticipantComponent.generated.h"

class UWorldStateSubsystem;

/** Participant lifecycle notification carrying the stable participant identity. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWorldStateParticipantEvent, FWorldStateParticipantId, ParticipantId);
/** Participant-specific terminal failure notification with frozen diagnostics. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWorldStateParticipantRestoreResultEvent, const FWorldStateParticipantResult&, Result);

/** Designer-facing configuration and runtime bridge for one World State participant Actor. */
UCLASS(ClassGroup = (WorldState), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class WORLDSTATE_API UWorldStateParticipantComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWorldStateParticipantComponent();

	/** Per-instance stable identity. Templates intentionally keep this invalid. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "World State|Identity")
	FWorldStateParticipantId ParticipantId;

	/** Captures whether this Actor exists so complete snapshots can apply the configured existence policy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Structural State")
	bool bCaptureExistence = true;

	/** Captures the Actor world transform; when enabled, the root Component relative transform is not selectable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Structural State")
	bool bCaptureActorTransform = true;

	/** Captures the Actor's parent Actor and attachment socket using stable participant identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Structural State")
	bool bCaptureAttachment = false;

	/** Authored non-root Scene Components whose relative transforms are restored parent-first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Structural State")
	TArray<FWorldStateSceneComponentCaptureSelection> SceneComponentCaptureSelections;

	/** Explicit reflected root properties selected on the owner Actor or authored Components. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Properties")
	TArray<FWorldStatePropertySelection> CapturedProperties;

	/** Coarse phase used to create mandatory ordering edges around this participant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Ordering")
	EWorldStateRestorePhase RestorePhase = EWorldStateRestorePhase::Default;

	/** Participants that must complete before this participant; cycles fail restore preflight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Ordering")
	TArray<FWorldStateParticipantId> RestoreAfter;

	/** Participants that must restore after this participant; cycles fail restore preflight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Ordering")
	TArray<FWorldStateParticipantId> RestoreBefore;

	/** Designer-defined names used by group capture and restore scopes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Scope")
	TArray<FName> Groups;

	/** Controls whether World State may respawn or destroy the owning Actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Existence")
	EWorldStateExistencePolicy ExistencePolicy = EWorldStateExistencePolicy::ExistingOnly;

	/** Registered native strategy used for eligible runtime Actor respawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Existence")
	FName SpawnStrategyId = TEXT("WorldState.DefaultActor");

	/** Enables short-lived participant labels when the global WorldState.Debug.Visual console variable is also enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State|Debug")
	bool bEnableDebug = false;

	/** Broadcast before any structural or property value is read for this participant. */
	UPROPERTY(BlueprintAssignable, Category = "World State|Events")
	FWorldStateParticipantEvent OnWorldStatePreCapture;

	/** Broadcast after this participant has been committed into a published snapshot. */
	UPROPERTY(BlueprintAssignable, Category = "World State|Events")
	FWorldStateParticipantEvent OnWorldStateCaptured;

	/** Broadcast after existence is resolved but before structure and properties are mutated. */
	UPROPERTY(BlueprintAssignable, Category = "World State|Events")
	FWorldStateParticipantEvent OnWorldStatePreRestore;

	/** Broadcast after selected property values and soft-reference checks have completed. */
	UPROPERTY(BlueprintAssignable, Category = "World State|Events")
	FWorldStateParticipantEvent OnWorldStatePropertiesRestored;

	/** Broadcast after derived-state reconstruction and final validation succeed. */
	UPROPERTY(BlueprintAssignable, Category = "World State|Events")
	FWorldStateParticipantEvent OnWorldStateRestored;

	/** Broadcast once when this participant's accepted restore work terminates with failure. */
	UPROPERTY(BlueprintAssignable, Category = "World State|Events")
	FWorldStateParticipantRestoreResultEvent OnWorldStateRestoreFailed;

	/** Returns the stable instance identity; templates and CDOs intentionally return an invalid value. */
	UFUNCTION(BlueprintPure, Category = "World State")
	FWorldStateParticipantId GetParticipantId() const { return ParticipantId; }

	/** Adds this participant to DirtyParticipants scopes without mutating captured data. */
	UFUNCTION(BlueprintCallable, Category = "World State")
	void MarkParticipantDirty();

	/** Validates every enabled property selection against its current source and recursive reflected type. */
	UFUNCTION(BlueprintCallable, Category = "World State")
	FWorldStateOperationResult ValidateCapturedProperties() const;

	/** Regenerates this instance's ID. Rejected while registered at runtime. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "World State")
	bool RegenerateParticipantId();

	/**
	 * Resolves an owner or authored Component source without using Component indices.
	 * @param SourceId Stable authored source identity.
	 * @return The current source object, or nullptr when its name/class cannot be resolved.
	 */
	UObject* ResolveCaptureSource(const FWorldStateCaptureSourceId& SourceId) const;

	/** Adopts a pending respawn identity and registers symmetrically with EndPlay. */
	virtual void BeginPlay() override;
	/** Removes the live weak reference even during world teardown. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Repairs legacy instance identities after serialized data is loaded. */
	virtual void PostLoad() override;
	/** Assigns an identity to newly created instances while leaving templates invalid. */
	virtual void OnComponentCreated() override;
	/** Preserves PIE duplication identity but regenerates ordinary duplicate identities. */
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
#if WITH_EDITOR
	/** Regenerates identity for editor copy/paste imports. */
	virtual void PostEditImport() override;
#endif

private:
	/** Enforces the template/instance identity invariant and optionally creates a replacement GUID. */
	void EnsureStableId(bool bForceNewId = false);

	friend class UWorldStateSubsystem;
};
