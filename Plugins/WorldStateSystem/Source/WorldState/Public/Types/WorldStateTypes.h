#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "WorldStateTypes.generated.h"

/** Stable identity of one World State participant. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateParticipantId
{
	GENERATED_BODY()

	FWorldStateParticipantId() = default;
	explicit FWorldStateParticipantId(const FGuid& InValue) : Value(InValue) {}

	/** Creates a new non-zero identity for a runtime or authored participant instance. */
	static FWorldStateParticipantId NewId() { return FWorldStateParticipantId(FGuid::NewGuid()); }
	/** Returns whether this value can identify a participant. Templates intentionally return false. */
	bool IsValid() const { return Value.IsValid(); }
	/** Provides read-only C++ access to the underlying GUID. */
	const FGuid& GetGuid() const { return Value; }
	/** Formats the identity consistently for diagnostics and deterministic ordering. */
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	/** Returns the identity to its invalid state. */
	void Reset() { Value.Invalidate(); }

	friend bool operator==(const FWorldStateParticipantId& Left, const FWorldStateParticipantId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FWorldStateParticipantId& Left, const FWorldStateParticipantId& Right) { return !(Left == Right); }
	friend bool operator<(const FWorldStateParticipantId& Left, const FWorldStateParticipantId& Right) { return Left.Value < Right.Value; }
	friend uint32 GetTypeHash(const FWorldStateParticipantId& Id) { return GetTypeHash(Id.Value); }

private:
	/** Serialized identity storage; mutation is restricted to the value type and trusted subsystem/component code. */
	UPROPERTY(VisibleAnywhere, Category = "World State", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

/** Stable identity of one immutable in-memory snapshot. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateSnapshotId
{
	GENERATED_BODY()

	FWorldStateSnapshotId() = default;
	explicit FWorldStateSnapshotId(const FGuid& InValue) : Value(InValue) {}

	/** Creates an identity for a newly published in-memory snapshot. */
	static FWorldStateSnapshotId NewId() { return FWorldStateSnapshotId(FGuid::NewGuid()); }
	/** Returns whether this value identifies a snapshot. */
	bool IsValid() const { return Value.IsValid(); }
	/** Provides read-only C++ access to the underlying GUID. */
	const FGuid& GetGuid() const { return Value; }
	/** Formats the identity for logs and diagnostics. */
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }

	friend bool operator==(const FWorldStateSnapshotId& Left, const FWorldStateSnapshotId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FWorldStateSnapshotId& Left, const FWorldStateSnapshotId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FWorldStateSnapshotId& Id) { return GetTypeHash(Id.Value); }

private:
	/** Serialized GUID used as the public handle; snapshot contents remain private to the subsystem. */
	UPROPERTY(VisibleAnywhere, Category = "World State", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

/** Correlates every notification and result produced by one accepted restore request. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateRestoreSessionId
{
	GENERATED_BODY()

	FWorldStateRestoreSessionId() = default;
	explicit FWorldStateRestoreSessionId(const FGuid& InValue) : Value(InValue) {}

	/** Creates the correlation ID assigned when a restore request is accepted. */
	static FWorldStateRestoreSessionId NewId() { return FWorldStateRestoreSessionId(FGuid::NewGuid()); }
	/** Returns whether the restore session was accepted and assigned an ID. */
	bool IsValid() const { return Value.IsValid(); }
	/** Formats the correlation ID for structured diagnostics. */
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }

	friend bool operator==(const FWorldStateRestoreSessionId& Left, const FWorldStateRestoreSessionId& Right) { return Left.Value == Right.Value; }

private:
	/** Serialized correlation value shared by Started, ScopeResolved and the terminal result. */
	UPROPERTY(VisibleAnywhere, Category = "World State", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

/** Exclusive lifecycle state of the per-world subsystem. */
UENUM(BlueprintType)
enum class EWorldStateSubsystemState : uint8
{
	/** The subsystem object exists but has not initialized its runtime storage. */
	Initializing,
	/** Participants may register; registration must be finalized before baseline capture. */
	Registering,
	/** Registration is finalized but no valid baseline has been published. */
	ReadyWithoutBaseline,
	/** A synchronous capture transaction is active. */
	Capturing,
	/** A valid baseline exists and capture or restore requests may be accepted. */
	Ready,
	/** A synchronous restore session is active. */
	Restoring,
	/** A restore failed after mutation began; another restore may be attempted for recovery. */
	Failed,
	/** World teardown is in progress and no new work is accepted. */
	ShuttingDown
};

/** High-level outcome shared by capture, restore and registration operations. */
UENUM(BlueprintType)
enum class EWorldStateOperationStatus : uint8
{
	/** The operation completed without diagnostics. */
	Success,
	/** The operation completed and returned non-fatal warnings. */
	SuccessWithWarnings,
	/** The request was rejected before a mutable operation or session began. */
	RejectedInvalidRequest,
	/** Another capture or restore owns the subsystem synchronously. */
	RejectedBusy,
	/** Capture validation or serialization failed and no snapshot was published. */
	CaptureFailed,
	/** Restore validation, scope construction or ordering failed before mutation. */
	PreflightFailed,
	/** Restore failed after its session was accepted. */
	RestoreFailed,
	/** Reserved for an explicitly cancelled operation. */
	Cancelled,
	/** The request could not continue because its world is tearing down. */
	WorldTeardown
};

/** Severity of one machine-readable World State diagnostic. */
UENUM(BlueprintType)
enum class EWorldStateIssueSeverity : uint8
{
	/** Informational context that does not degrade the operation. */
	Info,
	/** Recoverable condition that callers may need to inspect. */
	Warning,
	/** Failure condition that invalidates the affected operation or participant. */
	Error
};

/** Kind of UObject that owns an explicitly selected root property. */
UENUM(BlueprintType)
enum class EWorldStateCaptureSourceKind : uint8
{
	/** The participant's owning Actor. */
	OwnerActor,
	/** An authored Actor Component identified by stable UObject name. */
	ActorComponent
};

/** Durable identity of the Actor or authored Actor Component that owns a selected property. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateCaptureSourceId
{
	GENERATED_BODY()

	/** Selects the owner Actor or an authored Actor Component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	EWorldStateCaptureSourceKind Kind = EWorldStateCaptureSourceKind::OwnerActor;

	/** Stable UObject name used only when Kind is ActorComponent; component array indices are never persisted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State", meta = (EditCondition = "Kind == EWorldStateCaptureSourceKind::ActorComponent"))
	FName ComponentName;

	/** Constructs the canonical owner-Actor source identity. */
	static FWorldStateCaptureSourceId OwnerActor() { return FWorldStateCaptureSourceId(); }
	/** Constructs a Component source identity from its stable authored name. */
	static FWorldStateCaptureSourceId Component(FName InName)
	{
		FWorldStateCaptureSourceId Id;
		Id.Kind = EWorldStateCaptureSourceKind::ActorComponent;
		Id.ComponentName = InName;
		return Id;
	}

	/** Returns whether the source contains enough identity to be resolved. */
	bool IsValid() const { return Kind == EWorldStateCaptureSourceKind::OwnerActor || !ComponentName.IsNone(); }
	/** Formats the source for diagnostics and deterministic selection keys. */
	FString ToString() const { return Kind == EWorldStateCaptureSourceKind::OwnerActor ? TEXT("OwnerActor") : FString::Printf(TEXT("Component:%s"), *ComponentName.ToString()); }

	friend bool operator==(const FWorldStateCaptureSourceId& Left, const FWorldStateCaptureSourceId& Right)
	{
		return Left.Kind == Right.Kind && Left.ComponentName == Right.ComponentName;
	}
	friend uint32 GetTypeHash(const FWorldStateCaptureSourceId& Id) { return HashCombine(GetTypeHash(Id.Kind), GetTypeHash(Id.ComponentName)); }
};

/** Coarse restore phase; phase boundaries form mandatory graph-ordering constraints. */
UENUM(BlueprintType)
enum class EWorldStateRestorePhase : uint8
{
	/** Restored before Default and Late participants. */
	Early,
	/** Normal restore phase used by most participants. */
	Default,
	/** Restored after Early and Default participants. */
	Late
};

/** Controls whether the subsystem may recreate or remove the participant Actor. */
UENUM(BlueprintType)
enum class EWorldStateExistencePolicy : uint8
{
	/** The Actor must already exist; a missing participant fails preflight. */
	ExistingOnly,
	/** Recreate the Actor when present in the snapshot but missing from the world. */
	RespawnIfMissing,
	/** Remove the Actor when it exists in the world but is absent from a complete snapshot. */
	DestroyIfAbsent,
	/** Combine RespawnIfMissing and DestroyIfAbsent behavior. */
	RespawnAndDestroy,
	/** Preserve lifetime externally while still allowing state restoration when the Actor exists. */
	Persistent,
	/** Delegate both creation and removal to a system outside World State. */
	ExternallyManaged
};

/** Determines whether a restored soft path must already resolve without synchronous loading. */
UENUM(BlueprintType)
enum class EWorldStateReferenceRequirement : uint8
{
	/** Keep unresolved paths and report a warning. */
	Optional,
	/** Fail the restore reference phase if the path does not resolve. */
	Required
};

/** Publication policy used when a runtime capture encounters invalid authored data. */
UENUM(BlueprintType)
enum class EWorldStateCaptureFailurePolicy : uint8
{
	/** Abort transactionally and publish no snapshot. Baseline capture always uses this behavior. */
	FailEntireSnapshot,
	/** Omit an invalid participant while preserving valid participants. */
	SkipInvalidParticipant,
	/** Omit invalid properties while preserving other selected values. */
	SkipInvalidProperty
};

/** Runtime behavior after a property payload fails to deserialize. */
UENUM(BlueprintType)
enum class EWorldStateRestoreFailurePolicy : uint8
{
	/** Stop at the first property failure. */
	FailFast,
	/** Continue independent property work, then return one observable terminal failure. */
	ContinueBestEffort
};

/** Policy for source, property or type-signature mismatches discovered during restore. */
UENUM(BlueprintType)
enum class EWorldStateMissingPropertyPolicy : uint8
{
	/** Reject the restore during preflight. */
	FailRestore,
	/** Skip the incompatible property and retain a warning in the result. */
	SkipWithWarning
};

/** Controls how partial restore scopes account for dependency graph edges. */
UENUM(BlueprintType)
enum class EWorldStateDependencyExpansionPolicy : uint8
{
	/** Use only requested participants and reject a scope that omits prerequisites. */
	ExactSelection,
	/** Add all transitive prerequisites required by requested participants. */
	IncludeRequiredDependencies,
	/** Add transitive prerequisites and dependents. */
	IncludeDependenciesAndDependents,
	/** Explicitly reject any incomplete scope instead of expanding it. */
	RejectIncompleteScope
};

/** Selects how a capture or restore request resolves its participant set. */
UENUM(BlueprintType)
enum class EWorldStateRestoreScopeKind : uint8
{
	/** Every participant contained by the source snapshot or current capture registry. */
	CompleteSnapshot,
	/** Only explicitly supplied participant IDs, subject to dependency policy. */
	ParticipantIds,
	/** Participants sharing at least one requested group name. */
	Groups,
	/** Participants explicitly marked dirty in this world. */
	DirtyParticipants
};

/** Observable stage of an accepted restore session. */
UENUM(BlueprintType)
enum class EWorldStateRestoreStage : uint8
{
	/** No restore stage has been entered. */
	None,
	/** Request, snapshot, sources, properties and spawn strategies are being validated. */
	Preflight,
	/** Partial scope expansion and deterministic graph ordering are being resolved. */
	ScopeConstruction,
	/** Missing or snapshot-absent Actors are being recreated or removed. */
	Existence,
	/** Actor transforms and Actor attachments are being restored. */
	Structure,
	/** Selected Scene Component attachments and relative transforms are being restored. */
	SceneComponents,
	/** Reflected property payloads are being deserialized. */
	Properties,
	/** Restored soft paths are being resolved without loading. */
	References,
	/** Participant callbacks are rebuilding derived state. */
	DerivedState,
	/** Final structural and participant results are being checked. */
	Validation,
	/** The restore reached its successful terminal event. */
	Completed,
	/** The restore reached its failed terminal event. */
	Failed
};

/** Detailed outcome produced by the shared runtime/editor property validator. */
UENUM(BlueprintType)
enum class EWorldStatePropertyValidationStatus : uint8
{
	/** The complete reflected value graph is supported. */
	Valid,
	/** The authored owner Actor or Component cannot be resolved. */
	MissingSource,
	/** The root property no longer exists on the resolved source class. */
	MissingProperty,
	/** The root property category is not supported. */
	UnsupportedPropertyType,
	/** A nested struct or container member is not supported. */
	UnsupportedNestedType,
	/** A hard UObject or interface reference was found. */
	HardObjectReferenceRejected,
	/** A weak or lazy UObject reference was found. */
	WeakObjectReferenceRejected,
	/** An editor-only or deprecated reflected property was found. */
	EditorOnlyPropertyRejected,
	/** The current canonical type differs from the authored signature. */
	TypeSignatureMismatch,
	/** A soft-reference policy is invalid for the selected property. */
	InvalidSoftPathPolicy
};

/** Result of resolving one soft path after existence and value restoration. */
UENUM(BlueprintType)
enum class EWorldStateReferenceResolutionStatus : uint8
{
	/** The serialized path value was restored but has not been classified further. */
	PathRestored,
	/** ResolveObject found the existing object or class without loading. */
	Resolved,
	/** The path is unresolved but the property policy allows it. */
	UnresolvedAllowed,
	/** The required path is unresolved and fails the restore. */
	UnresolvedRequired,
	/** The restored required path is null or otherwise invalid. */
	InvalidPath
};

/** One explicitly selected reflected root property. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStatePropertySelection
{
	GENERATED_BODY()

	/** Object that owns the selected root property. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	FWorldStateCaptureSourceId CaptureSourceId;

	/** Exact reflected root-property name; nested member paths are intentionally unsupported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	FName PropertyName;

	/** Authored source class used to reject a selection that resolves to a different class later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	FSoftClassPath ExpectedSourceClass;

	/** Canonical FPropertyTypeName signature captured by the editor picker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	FString ExpectedTypeSignature;

	/** Disables this selection without discarding its authored identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	bool bEnabled = true;

	/** Allows this value to use a different phase from its participant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	bool bOverrideRestorePhase = false;

	/** Phase used for this property when bOverrideRestorePhase is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State", meta = (EditCondition = "bOverrideRestorePhase"))
	EWorldStateRestorePhase RestorePhase = EWorldStateRestorePhase::Default;

	/** Determines whether every soft path contained by this value must resolve after restore. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	EWorldStateReferenceRequirement ReferenceRequirement = EWorldStateReferenceRequirement::Optional;
};

/** Explicit request to capture one authored Scene Component's complete relative transform. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateSceneComponentCaptureSelection
{
	GENERATED_BODY()

	/** Authored non-root Scene Component to resolve by stable source identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	FWorldStateCaptureSourceId CaptureSourceId;

	/** Disables this structural selection while retaining it for later use or diagnosis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	bool bEnabled = true;

	/** Captures and restores the Component's complete relative FTransform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	bool bCaptureRelativeTransform = true;

	/** Treats a changed or missing captured parent as an error instead of a warning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	bool bStrictParentValidation = false;
};

/** Authored participant-set selector shared by capture and restore requests. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateParticipantScope
{
	GENERATED_BODY()

	/** Selects which of the accompanying fields define the scope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	EWorldStateRestoreScopeKind Kind = EWorldStateRestoreScopeKind::CompleteSnapshot;

	/** Explicit participant set used when Kind is ParticipantIds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	TArray<FWorldStateParticipantId> ParticipantIds;

	/** Group names used when Kind is Groups; matching any group includes a participant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	TArray<FName> Groups;
};

/** Parameters for publishing an immutable baseline or runtime snapshot. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateCaptureRequest
{
	GENERATED_BODY()

	/** Optional human-readable label exposed by snapshot summaries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	FName Label;

	/** Participants whose current state should be captured. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	FWorldStateParticipantScope Scope;

	/** Runtime snapshot behavior for invalid participants or properties; baseline capture is always transactional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	EWorldStateCaptureFailurePolicy FailurePolicy = EWorldStateCaptureFailurePolicy::FailEntireSnapshot;

	/** Allows a snapshot containing observable warnings to be published. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	bool bAcceptWarnings = true;
};

/** Parameters controlling one synchronous restore request. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateRestoreRequest
{
	GENERATED_BODY()

	/** Source snapshot for RestoreSnapshot; RestoreBaseline replaces this with the immutable baseline ID. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	FWorldStateSnapshotId SnapshotId;

	/** Initially requested participant set before dependency expansion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	FWorldStateParticipantScope Scope;

	/** Determines whether partial scopes expand or reject omitted dependency edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	EWorldStateDependencyExpansionPolicy DependencyExpansion = EWorldStateDependencyExpansionPolicy::IncludeRequiredDependencies;

	/** Determines whether missing sources, properties or changed signatures fail preflight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	EWorldStateMissingPropertyPolicy MissingPropertyPolicy = EWorldStateMissingPropertyPolicy::FailRestore;

	/** Determines whether independent property work continues after deserialization failure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	EWorldStateRestoreFailurePolicy FailurePolicy = EWorldStateRestoreFailurePolicy::FailFast;

	/** Fails the terminal validation stage when any participant result remains invalid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	bool bStrictValidation = true;

	/** Optional caller context copied into diagnostics or higher-level orchestration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World State")
	FName Reason;
};

/** One structured diagnostic emitted by a public operation. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateIssue
{
	GENERATED_BODY()

	/** Operational significance of this issue. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	EWorldStateIssueSeverity Severity = EWorldStateIssueSeverity::Info;

	/** Stable machine-readable identifier suitable for branching or test assertions. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FName Code;

	/** Human-readable explanation with contextual details. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FString Message;

	/** Participant associated with the issue, or invalid when the issue is operation-wide. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateParticipantId ParticipantId;

	/** Capture source associated with the issue when applicable. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateCaptureSourceId CaptureSourceId;

	/** Root property associated with the issue, or None for structural/operation issues. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FName PropertyName;
};

/** Result returned by operations that do not create a snapshot or restore session. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateOperationResult
{
	GENERATED_BODY()

	/** High-level terminal status. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	EWorldStateOperationStatus Status = EWorldStateOperationStatus::RejectedInvalidRequest;

	/** Structured warnings and errors produced by the operation. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	TArray<FWorldStateIssue> Issues;

	/** Returns true for both warning-free and warning-bearing successful outcomes. */
	bool IsSuccess() const { return Status == EWorldStateOperationStatus::Success || Status == EWorldStateOperationStatus::SuccessWithWarnings; }
};

/** Per-property capture or restore outcome nested inside a participant result. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStatePropertyResult
{
	GENERATED_BODY()

	/** Participant that owns the selected property. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateParticipantId ParticipantId;

	/** Actor or Component source that contains the property. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateCaptureSourceId CaptureSourceId;

	/** Reflected root-property name. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FName PropertyName;

	/** Whether the payload operation completed successfully. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	bool bSucceeded = false;

	/** Property-specific serializer or validation diagnostic. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FString Message;
};

/** Aggregate result for one participant within a capture or restore. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateParticipantResult
{
	GENERATED_BODY()

	/** Stable identity of the affected participant. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateParticipantId ParticipantId;

	/** True only when all required work and final validation succeeded for this participant. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	bool bSucceeded = false;

	/** Results for selected reflected properties processed for this participant. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	TArray<FWorldStatePropertyResult> PropertyResults;

	/** Participant-level structural, reference and validation diagnostics. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	TArray<FWorldStateIssue> Issues;
};

/** Outcome of resolving one soft-object or soft-class path contained by a restored property. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateReferenceResolutionResult
{
	GENERATED_BODY()

	/** Participant whose property contained the path. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateParticipantId ParticipantId;

	/** Actor or Component source that owns the restored property. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateCaptureSourceId CaptureSourceId;

	/** Selected reflected root property. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FName PropertyName;

	/** Diagnostic path to the nested container or struct value that held the reference. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FString NestedValuePath;

	/** Exact serialized soft path; resolving this result never causes a synchronous load. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FSoftObjectPath SoftObjectPath;

	/** Resolution classification after the property value was restored. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	EWorldStateReferenceResolutionStatus Status = EWorldStateReferenceResolutionStatus::PathRestored;

	/** Human-readable context for unresolved or invalid paths. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FString Message;
};

/** Frozen public result of a capture attempt; only successful captures expose a valid snapshot ID. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateCaptureResult
{
	GENERATED_BODY()

	/** Terminal capture status. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	EWorldStateOperationStatus Status = EWorldStateOperationStatus::RejectedInvalidRequest;

	/** Published snapshot identity, or invalid when the transaction was rejected or failed. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateSnapshotId SnapshotId;

	/** Per-participant details accumulated according to the capture failure policy. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	TArray<FWorldStateParticipantResult> ParticipantResults;

	/** Operation-wide warnings and errors. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	TArray<FWorldStateIssue> Issues;

	/** Returns true for both warning-free and warning-bearing published captures. */
	bool IsSuccess() const { return Status == EWorldStateOperationStatus::Success || Status == EWorldStateOperationStatus::SuccessWithWarnings; }
};

/** Immutable event context shared by Started and ScopeResolved for one accepted restore session. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateRestoreLifecycleContext
{
	GENERATED_BODY()

	/** Correlation identity retained by every event emitted for the session. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateRestoreSessionId RestoreSessionId;

	/** Snapshot selected as the restore source. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateSnapshotId SnapshotId;

	/** Scope kind supplied by the caller before dependency expansion. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	EWorldStateRestoreScopeKind RequestedScope = EWorldStateRestoreScopeKind::CompleteSnapshot;

	/** Number of participants after scope expansion; zero until ScopeResolved. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	int32 ResolvedParticipantCount = 0;

	/** True when the immutable baseline, rather than a runtime snapshot, is being restored. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	bool bBaseline = false;

	/** Indicates whether any world state was changed before this notification. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	bool bMutationBegan = false;

	/** Current observable restore stage. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	EWorldStateRestoreStage Stage = EWorldStateRestoreStage::None;
};

/** Frozen terminal result emitted exactly once for an accepted restore session. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateRestoreResult
{
	GENERATED_BODY()

	/** Correlation identity shared with the lifecycle notifications. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateRestoreSessionId RestoreSessionId;

	/** Terminal operation status. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	EWorldStateOperationStatus Status = EWorldStateOperationStatus::RejectedInvalidRequest;

	/** Stage that failed, or None after a successful restore. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	EWorldStateRestoreStage FailureStage = EWorldStateRestoreStage::None;

	/** Number of participants named by the caller before dependency expansion. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	int32 RequestedParticipantCount = 0;

	/** Number of participants whose complete restore pipeline succeeded. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	int32 RestoredParticipantCount = 0;

	/** True once existence, structure or property state may have changed in the world. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	bool bMutationBegan = false;

	/** True when a best-effort failure left at least one participant restored. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	bool bPartiallyRestored = false;

	/** Per-participant property and validation outcomes. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	TArray<FWorldStateParticipantResult> ParticipantResults;

	/** Resolution outcome for every restored soft path. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	TArray<FWorldStateReferenceResolutionResult> ReferenceResults;

	/** Session-wide warnings and errors. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	TArray<FWorldStateIssue> Issues;

	/** Synchronous wall-clock duration measured from acceptance to the terminal event. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	double DurationSeconds = 0.0;

	/** Returns true for both warning-free and warning-bearing completed restores. */
	bool IsSuccess() const { return Status == EWorldStateOperationStatus::Success || Status == EWorldStateOperationStatus::SuccessWithWarnings; }
};

/** Read-only metadata for a private, value-owned snapshot. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateSnapshotSummary
{
	GENERATED_BODY()

	/** Public handle used to request a restore or a later summary. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateSnapshotId SnapshotId;

	/** Optional label supplied at capture time. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FName Label;

	/** Number of participant records owned by the snapshot. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	int32 ParticipantCount = 0;

	/** Total serialized property payload size, excluding structural metadata. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	int64 PayloadBytes = 0;

	/** True only for the subsystem's immutable baseline snapshot. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	bool bBaseline = false;
};

/** Read-only diagnostic view of one currently known participant identity. */
USTRUCT(BlueprintType)
struct WORLDSTATE_API FWorldStateParticipantSummary
{
	GENERATED_BODY()

	/** Stable participant identity. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FWorldStateParticipantId ParticipantId;

	/** Current Actor object path when a live instance can be resolved. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	FString ActorPath;

	/** Whether the participant currently belongs to the active registry. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	bool bRegistered = false;

	/** Whether the participant is eligible for DirtyParticipants scopes. */
	UPROPERTY(BlueprintReadOnly, Category = "World State")
	bool bDirty = false;
};

/** Runtime/editor result from validating one selected reflected property. */
struct WORLDSTATE_API FWorldStatePropertyValidationResult
{
	/** Machine-readable validation outcome. */
	EWorldStatePropertyValidationStatus Status = EWorldStatePropertyValidationStatus::Valid;
	/** Canonical UE 5.8 FPropertyTypeName signature of the resolved property. */
	FString TypeSignature;
	/** Precise member/container path of the first unsupported nested value. */
	FString NestedFailurePath;
	/** Human-readable validation explanation. */
	FString Message;

	/** Returns whether the complete recursively reflected value is supported. */
	bool IsValid() const { return Status == EWorldStatePropertyValidationStatus::Valid; }
};
