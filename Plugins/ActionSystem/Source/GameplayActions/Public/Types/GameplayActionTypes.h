#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/PropertyBag.h"
#include "GameplayActionTypes.generated.h"

class UGameplayActionComponent;
class UGameplayActionDefinition;
class UGameplayActionInstance;
class UGameplayActionBlueprintLibrary;

/** Authoritative lifecycle state. Only UGameplayActionComponent is allowed to change it. */
UENUM(BlueprintType)
enum class EGameplayActionState : uint8
{
	Created,
	Queued,
	Starting,
	Running,
	Paused,
	Ending,
	Succeeded,
	Failed,
	Cancelled,
	Interrupted,
	Aborted
};

/** Structured notifications emitted by the component; Init intentionally remains an instance-only hook. */
UENUM(BlueprintType)
enum class EGameplayActionEventType : uint8
{
	Accepted,
	Rejected,
	Started,
	Paused,
	Resumed,
	Ended
};

/** Scheduling choice used when an accepted request cannot acquire its full exact lock set. */
UENUM(BlueprintType)
enum class EGameplayActionBlockedPolicy : uint8
{
	Queue,
	Reject
};

/** Controls whether the synchronous journal participates in acceptance of a request. */
UENUM(BlueprintType)
enum class EGameplayActionJournalRequirement : uint8
{
	Disabled,
	Optional,
	Required
};

/** Complete observable outcome of SubmitAction or PreflightAction. */
UENUM(BlueprintType)
enum class EGameplayActionSubmissionStatus : uint8
{
	AcceptedStarted,
	AcceptedQueued,
	RejectedInvalidRequest,
	RejectedBlocked,
	RejectedValidation,
	RejectedJournal,
	RejectedReentrant,
	RejectedNotAccepting
};

/** Result of the only supported request-construction path. */
UENUM(BlueprintType)
enum class EGameplayActionRequestCreationStatus : uint8
{
	Created,
	InvalidDefinition
};

/** Type-safe wildcard Property Bag access result. Failures never mutate schema or output values. */
UENUM(BlueprintType)
enum class EGameplayActionParameterAccessResult : uint8
{
	Success,
	RequestNotInitialized,
	ParameterNotFound,
	TypeMismatch,
	InvalidValue
};

/** Decision returned synchronously by the single registered journal sink. */
UENUM(BlueprintType)
enum class EGameplayActionJournalWriteStatus : uint8
{
	Accepted,
	Rejected
};

/** Result for component commands that operate on an existing handle rather than submitting a request. */
UENUM(BlueprintType)
enum class EGameplayActionOperationResult : uint8
{
	Succeeded,
	HandleNotFound,
	InvalidState,
	RejectedReentrant,
	NotAccepting
};

/**
 * Component-local identity for an accepted execution.
 *
 * Values are positive, monotonic int64 identifiers and are never reused during the component's
 * lifetime. A default-constructed handle is intentionally invalid.
 */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONS_API FGameplayActionHandle
{
	GENERATED_BODY()

public:
	FGameplayActionHandle() = default;
	explicit FGameplayActionHandle(const int64 InValue) : Value(InValue) {}

	bool IsValid() const { return Value > 0; }

	int64 GetValue() const { return Value; }

	friend bool operator==(const FGameplayActionHandle& Left, const FGameplayActionHandle& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FGameplayActionHandle& Left, const FGameplayActionHandle& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FGameplayActionHandle& Handle) { return GetTypeHash(Handle.Value); }

private:
	UPROPERTY(VisibleAnywhere, Category = "Gameplay Actions", meta = (AllowPrivateAccess = "true"))
	int64 Value = 0;
};

/** Generic origin correlation reserved for consumers such as the future IntentReplay plugin. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONS_API FGameplayActionCorrelationData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Actions")
	FGameplayTag Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Actions")
	FGuid Id;
};

/** Execution-time timeout configuration copied to snapshots but not enforced in the first milestone. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONS_API FGameplayActionTimeout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Actions")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Actions", meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = "s"))
	double DurationSeconds = 0.0;
};

/** Lightweight terminal data retained after the transient action instance has been released. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONS_API FGameplayActionResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	EGameplayActionState TerminalState = EGameplayActionState::Failed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FGameplayTag ReasonTag;

	/**
	 * Runtime handle of the action that directly caused this terminal transition.
	 * Valid for scheduler preemption; invalid for self-completion, cancellation, failure, and abort.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FGameplayActionHandle CausingActionHandle;

	/** Human-readable diagnostics only. Never use this string as authoritative gameplay data. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FString DiagnosticMessage;

	bool IsTerminal() const;
};

/** Submission decision. Handle is valid only for AcceptedStarted and AcceptedQueued. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONS_API FGameplayActionSubmissionResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	EGameplayActionSubmissionStatus Status = EGameplayActionSubmissionStatus::RejectedInvalidRequest;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FGameplayActionHandle Handle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FGameplayTag ReasonTag;

	/** Human-readable diagnostics only. Never use this string as authoritative gameplay data. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FString DiagnosticMessage;

	bool IsAccepted() const;
};

/**
 * Isolated request value created by UGameplayActionBlueprintLibrary.
 *
 * Construction is deliberately private: a Blueprint Make Struct node may produce the shape, but
 * SubmitAction rejects it because bInitialized and the deep-copied parameter bag were not prepared
 * by the authoritative factory.
 */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONS_API FGameplayActionRequest
{
	GENERATED_BODY()

public:
	bool IsInitialized() const { return bInitialized; }
	const UGameplayActionDefinition* GetDefinition() const { return Definition; }
	const FInstancedPropertyBag& GetParameters() const { return Parameters; }
	bool HasPriorityOverride() const { return bOverridePriority; }
	int32 GetPriorityOverride() const { return PriorityOverride; }
	bool HasBlockedPolicyOverride() const { return bOverrideBlockedPolicy; }
	EGameplayActionBlockedPolicy GetBlockedPolicyOverride() const { return BlockedPolicyOverride; }
	FGameplayTag GetOriginTag() const { return OriginTag; }
	UObject* GetRequester() const { return Requester.Get(); }
	const FGameplayActionCorrelationData& GetCorrelation() const { return Correlation; }

private:
	UPROPERTY()
	TObjectPtr<UGameplayActionDefinition> Definition;

	UPROPERTY()
	FInstancedPropertyBag Parameters;

	UPROPERTY()
	bool bInitialized = false;

	UPROPERTY()
	bool bOverridePriority = false;

	UPROPERTY()
	int32 PriorityOverride = 0;

	UPROPERTY()
	bool bOverrideBlockedPolicy = false;

	UPROPERTY()
	EGameplayActionBlockedPolicy BlockedPolicyOverride = EGameplayActionBlockedPolicy::Queue;

	UPROPERTY()
	FGameplayTag OriginTag;

	UPROPERTY()
	TWeakObjectPtr<UObject> Requester;

	UPROPERTY()
	FGameplayActionCorrelationData Correlation;

	friend class UGameplayActionBlueprintLibrary;
	friend class UGameplayActionComponent;
	friend class UGameplayActionInstance;
};

/** Factory result that keeps invalid Definition failures observable without producing a usable request. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONS_API FGameplayActionRequestCreationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	EGameplayActionRequestCreationStatus Status = EGameplayActionRequestCreationStatus::InvalidDefinition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FGameplayActionRequest Request;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FString DiagnosticMessage;

	bool WasCreated() const { return Status == EGameplayActionRequestCreationStatus::Created; }
};

/**
 * Immutable-by-contract event snapshot delivered FIFO to the journal and observers.
 *
 * Parameters, scheduling configuration, correlation, and result data are copied so listeners do
 * not depend on the lifetime of the transient action instance.
 */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONS_API FGameplayActionEvent
{
	GENERATED_BODY()

public:
	const FInstancedPropertyBag& GetParameters() const { return Parameters; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	EGameplayActionEventType EventType = EGameplayActionEventType::Accepted;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FGameplayActionHandle Handle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FPrimaryAssetId DefinitionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	TSoftObjectPtr<UGameplayActionDefinition> Definition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FGameplayTag ActionTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	int32 Priority = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	EGameplayActionBlockedPolicy BlockedPolicy = EGameplayActionBlockedPolicy::Queue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FGameplayTagContainer ExecutionLocks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	bool bInterruptible = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FGameplayActionTimeout OptionalTimeout;

	/**
	 * Maximum gameplay-scaled time that this action may remain queued.
	 * Zero means that queue residence is unlimited.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions", meta = (ClampMin = "0.0", Units = "s"))
	double MaxQueueTimeSeconds = 0.0;

	/**
	 * Queue time accumulated by the component when this snapshot was created.
	 * The value does not advance during component pause or world pause.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions", meta = (Units = "s"))
	double QueueElapsedSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	EGameplayActionJournalRequirement JournalRequirement = EGameplayActionJournalRequirement::Disabled;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FGameplayTag OriginTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	FGameplayActionCorrelationData Correlation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	TWeakObjectPtr<UObject> Requester;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	TWeakObjectPtr<UObject> Owner;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	int64 SubmissionSequence = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	bool bHasResult = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions", meta = (EditCondition = "bHasResult"))
	FGameplayActionResult Result;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions")
	bool bHasSubmissionResult = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions", meta = (EditCondition = "bHasSubmissionResult"))
	FGameplayActionSubmissionResult SubmissionResult;

private:
	UPROPERTY()
	FInstancedPropertyBag Parameters;

	friend class UGameplayActionComponent;
};

/** Synchronous journal response. DiagnosticMessage is informative and never authoritative. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONS_API FGameplayActionJournalResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Actions")
	EGameplayActionJournalWriteStatus Status = EGameplayActionJournalWriteStatus::Accepted;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Actions")
	FGameplayTag ReasonTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Actions")
	FString DiagnosticMessage;

	bool IsAccepted() const { return Status == EGameplayActionJournalWriteStatus::Accepted; }
};

/** Per-action scheduler snapshot intended for read-only Blueprint and C++ debugging UIs. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONS_API FGameplayActionDebugEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	FGameplayActionHandle Handle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	FGameplayTag ActionTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	EGameplayActionState State = EGameplayActionState::Created;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	int32 Priority = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	FGameplayTagContainer ExecutionLocks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	int64 SubmissionSequence = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	double ElapsedSeconds = 0.0;

	/** Authored queue limit copied into the accepted instance. Zero means unlimited. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug", meta = (Units = "s"))
	double MaxQueueTimeSeconds = 0.0;

	/** Gameplay-scaled time accumulated while the action was actually in Queued state. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug", meta = (Units = "s"))
	double QueueElapsedSeconds = 0.0;

	/** Remaining queue time clamped to zero. It is zero for unlimited actions. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug", meta = (Units = "s"))
	double QueueRemainingSeconds = 0.0;

	/** True when MaxQueueTimeSeconds is greater than zero and queue expiration is enabled. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	bool bHasQueueTimeout = false;

	/** Convenience inverse of bHasQueueTimeout for Blueprint debug displays. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	bool bQueueTimeUnlimited = true;
};

/** Component-level diagnostic snapshot with active/queued entries and the last scheduler decision. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONS_API FGameplayActionDebugSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	TWeakObjectPtr<UObject> Owner;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	bool bPaused = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	bool bAcceptingSubmissions = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	TArray<FGameplayActionDebugEntry> ActiveActions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	TArray<FGameplayActionDebugEntry> QueuedActions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	bool bHasLastResult = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	FGameplayActionResult LastResult;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Actions|Debug")
	FString LastSchedulerDecision;
};
