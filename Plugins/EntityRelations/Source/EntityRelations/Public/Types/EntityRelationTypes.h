#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "EntityRelationTypes.generated.h"

class AActor;
class UEntityIdentityComponent;

/** Opaque logical entity identity. UObject addresses and Actor names are never authoritative IDs. */
USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationId
{
	GENERATED_BODY()

public:
	FEntityRelationId() = default;
	explicit FEntityRelationId(const FGuid& InValue) : Value(InValue) {}

	static FEntityRelationId NewId() { return FEntityRelationId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	void Reset() { Value.Invalidate(); }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphens); }
	const FGuid& GetGuid() const { return Value; }

	friend bool operator==(const FEntityRelationId& Left, const FEntityRelationId& Right) { return Left.Value == Right.Value; }
	friend bool operator!=(const FEntityRelationId& Left, const FEntityRelationId& Right) { return !(Left == Right); }
	friend uint32 GetTypeHash(const FEntityRelationId& Id) { return GetTypeHash(Id.Value); }

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity Relations|Identity", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

UENUM(BlueprintType)
enum class EEntityRelationIdMode : uint8
{
	RuntimeGenerated,
	Explicit
};

UENUM(BlueprintType)
enum class EEntityRelationDecision : uint8
{
	NoOpinion,
	Allow,
	Deny
};

UENUM(BlueprintType)
enum class EEntityRelationQueryStatus : uint8
{
	Success,
	WrongThread,
	InvalidSource,
	InvalidTarget,
	InvalidContext,
	SourceNotRegistered,
	TargetNotRegistered,
	MissingPolicySet,
	UnsupportedDomain,
	EvaluationFailed
};

UENUM(BlueprintType)
enum class EEntityRelationRegistrationStatus : uint8
{
	Registered,
	AlreadyRegistered,
	Unregistered,
	InvalidComponent,
	InvalidId,
	DuplicateId,
	UnsupportedWorld,
	WrongThread,
	ShuttingDown
};

UENUM(BlueprintType)
enum class EEntityRelationStateMutationStatus : uint8
{
	Changed,
	Unchanged,
	InvalidSource,
	InvalidTarget,
	InvalidTag,
	InvalidValue,
	StateNotFound,
	SourceNotRegistered,
	WrongThread,
	ShuttingDown
};

UENUM(BlueprintType)
enum class EEntityRelationIssueSeverity : uint8
{
	Info,
	Warning,
	Error
};

UENUM(BlueprintType)
enum class EEntityRelationPolicyTraceStatus : uint8
{
	Evaluated,
	SkippedDisabled,
	SkippedUnsupportedDomain,
	InvalidPolicy,
	EvaluationFailed
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationRegistrationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Identity")
	EEntityRelationRegistrationStatus Status = EEntityRelationRegistrationStatus::InvalidComponent;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Identity")
	FEntityRelationId EntityId;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Identity")
	FString Message;

	bool IsSuccess() const
	{
		return Status == EEntityRelationRegistrationStatus::Registered || Status == EEntityRelationRegistrationStatus::AlreadyRegistered;
	}
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationStateMutationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Directed State")
	EEntityRelationStateMutationStatus Status = EEntityRelationStateMutationStatus::Unchanged;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Directed State")
	FEntityRelationId SourceId;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Directed State")
	FEntityRelationId TargetId;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Directed State")
	int64 Revision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Directed State")
	FString Message;

	bool WasChanged() const { return Status == EEntityRelationStateMutationStatus::Changed; }
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationQueryContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Query")
	FGameplayTag Domain;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Query")
	FGameplayTagContainer ContextTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Query")
	TMap<FGameplayTag, float> NumericContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Query")
	bool bRequestExplanation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Query")
	bool bAllowCache = true;
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationQuery
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Query")
	FEntityRelationId SourceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Query")
	FEntityRelationId TargetId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Query")
	FEntityRelationQueryContext Context;
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityDirectedRelationState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Entity Relations|Directed State")
	FGameplayTagContainer StateTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Entity Relations|Directed State")
	TMap<FGameplayTag, float> NumericValues;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Entity Relations|Directed State")
	int64 Revision = 0;

	bool IsEmpty() const { return StateTags.IsEmpty() && NumericValues.IsEmpty(); }
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationReason
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Result")
	FName PolicyId;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Result")
	FGameplayTag ReasonTag;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Result")
	FString DebugMessage;
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationContribution
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Policy")
	EEntityRelationDecision Decision = EEntityRelationDecision::NoOpinion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Policy")
	FGameplayTagContainer ClassificationTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Policy")
	FGameplayTagContainer OutcomeTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Policy")
	FGameplayTagContainer ReasonTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Policy")
	FString DebugMessage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity Relations|Policy")
	bool bStopEvaluation = false;

	bool HasContribution() const
	{
		return Decision != EEntityRelationDecision::NoOpinion || !ClassificationTags.IsEmpty() || !OutcomeTags.IsEmpty() || !ReasonTags.IsEmpty();
	}
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationEntityView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Policy")
	FEntityRelationId EntityId;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Policy")
	FName DebugName;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Policy")
	FGameplayTagContainer IdentityTags;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Policy")
	FGameplayTagContainer AffiliationTags;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Policy")
	int64 Revision = 0;

	/** Borrowed for the duration of one synchronous policy call. Policies must not retain or mutate it. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Policy")
	TObjectPtr<AActor> Actor = nullptr;
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationPolicyContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Policy")
	FEntityRelationEntityView Source;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Policy")
	FEntityRelationEntityView Target;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Policy")
	FEntityRelationQueryContext QueryContext;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Policy")
	bool bHasDirectedState = false;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Policy")
	FEntityDirectedRelationState DirectedState;
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationPolicyTrace
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	FName PolicyId;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int32 Priority = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int32 SerializedIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	EEntityRelationPolicyTraceStatus Status = EEntityRelationPolicyTraceStatus::Evaluated;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	FEntityRelationContribution Contribution;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	FString Message;
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationExplanation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	TArray<FEntityRelationPolicyTrace> PolicyTrace;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int64 SourceRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int64 TargetRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int64 PairRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int64 PolicySetRevision = 0;
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Result")
	EEntityRelationQueryStatus Status = EEntityRelationQueryStatus::EvaluationFailed;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Result")
	EEntityRelationDecision Decision = EEntityRelationDecision::NoOpinion;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Result")
	FGameplayTagContainer ClassificationTags;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Result")
	FGameplayTagContainer OutcomeTags;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Result")
	TArray<FEntityRelationReason> Reasons;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Result")
	FName WinningPolicyId;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Result")
	bool bWasCacheHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Result")
	FEntityRelationExplanation Explanation;

	bool IsSuccess() const { return Status == EEntityRelationQueryStatus::Success; }
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Validation")
	EEntityRelationIssueSeverity Severity = EEntityRelationIssueSeverity::Info;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Validation")
	FName Code;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Validation")
	FString Message;
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Validation")
	TArray<FEntityRelationIssue> Issues;

	bool IsValid() const
	{
		return !Issues.ContainsByPredicate([](const FEntityRelationIssue& Issue)
		{
			return Issue.Severity == EEntityRelationIssueSeverity::Error;
		});
	}
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationsRuntimeStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int64 QueryCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int64 BatchCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int64 CacheHits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int64 CacheMisses = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int64 PoliciesEvaluated = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int32 RegisteredEntities = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int32 DirectedStateEntries = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Debug")
	int32 CacheEntries = 0;
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationRegistryEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Events")
	FEntityRelationId EntityId;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Events")
	TObjectPtr<UEntityIdentityComponent> IdentityComponent = nullptr;
};

USTRUCT(BlueprintType)
struct ENTITYRELATIONS_API FEntityRelationInvalidationEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Events")
	FEntityRelationId SourceId;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Events")
	FEntityRelationId TargetId;

	UPROPERTY(BlueprintReadOnly, Category = "Entity Relations|Events")
	bool bAffectsAllRelationsForEntity = false;
};
