#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PerceptionKnowledgeTypes.generated.h"

class AActor;
class UPerceptionKnowledgeSourceComponent;

/** Stable semantic identity used only by PerceptionKnowledge. */
USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeEntityId
{
	GENERATED_BODY()

public:
	FPerceptionKnowledgeEntityId() = default;
	explicit FPerceptionKnowledgeEntityId(const FGuid& InValue) : Value(InValue) {}

	static FPerceptionKnowledgeEntityId NewId() { return FPerceptionKnowledgeEntityId(FGuid::NewGuid()); }
	bool IsValid() const { return Value.IsValid(); }
	void Reset() { Value.Invalidate(); }
	const FGuid& GetGuid() const { return Value; }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphens); }
	FString ToShortString() const { return Value.ToString(EGuidFormats::Short).Left(8); }

	friend bool operator==(const FPerceptionKnowledgeEntityId& Left, const FPerceptionKnowledgeEntityId& Right)
	{
		return Left.Value == Right.Value;
	}
	friend bool operator!=(const FPerceptionKnowledgeEntityId& Left, const FPerceptionKnowledgeEntityId& Right)
	{
		return !(Left == Right);
	}
	friend uint32 GetTypeHash(const FPerceptionKnowledgeEntityId& Id) { return GetTypeHash(Id.Value); }

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Identity", meta = (AllowPrivateAccess = "true"))
	FGuid Value;
};

UENUM(BlueprintType)
enum class EPerceptionKnowledgeValueType : uint8
{
	None,
	Bool,
	Integer,
	Float,
	Name,
	GameplayTag,
	EntityId,
	Vector
};

/** Closed, serializable semantic value with deterministic type-aware equality. */
USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeValue
{
	GENERATED_BODY()

public:
	static FPerceptionKnowledgeValue MakeBool(bool Value);
	static FPerceptionKnowledgeValue MakeInteger(int64 Value);
	static FPerceptionKnowledgeValue MakeFloat(double Value);
	static FPerceptionKnowledgeValue MakeName(FName Value);
	static FPerceptionKnowledgeValue MakeGameplayTag(FGameplayTag Value);
	static FPerceptionKnowledgeValue MakeEntityId(FPerceptionKnowledgeEntityId Value);
	static FPerceptionKnowledgeValue MakeVector(FVector Value);

	EPerceptionKnowledgeValueType GetType() const { return Type; }
	bool IsValid() const;
	FString ToString() const;

	bool GetBool(bool& OutValue) const;
	bool GetInteger(int64& OutValue) const;
	bool GetFloat(double& OutValue) const;
	bool GetName(FName& OutValue) const;
	bool GetGameplayTag(FGameplayTag& OutValue) const;
	bool GetEntityId(FPerceptionKnowledgeEntityId& OutValue) const;
	bool GetVector(FVector& OutValue) const;

	friend PERCEPTIONKNOWLEDGE_API bool operator==(
		const FPerceptionKnowledgeValue& Left,
		const FPerceptionKnowledgeValue& Right);
	friend bool operator!=(const FPerceptionKnowledgeValue& Left, const FPerceptionKnowledgeValue& Right)
	{
		return !(Left == Right);
	}

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Value", meta = (AllowPrivateAccess = "true"))
	EPerceptionKnowledgeValueType Type = EPerceptionKnowledgeValueType::None;

	UPROPERTY()
	bool BoolValue = false;

	UPROPERTY()
	int64 IntegerValue = 0;

	UPROPERTY()
	double FloatValue = 0.0;

	UPROPERTY()
	FName NameValue;

	UPROPERTY()
	FGameplayTag GameplayTagValue;

	UPROPERTY()
	FPerceptionKnowledgeEntityId EntityIdValue;

	UPROPERTY()
	FVector VectorValue = FVector::ZeroVector;
};

UENUM(BlueprintType)
enum class EPerceptionKnowledgeFactStatus : uint8
{
	Known,
	Unknown,
	Invalidated
};

/** One state exposed by a Source or state-provider. */
USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeExposedState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|State")
	FGameplayTag StateTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|State")
	FPerceptionKnowledgeValue Value;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|State")
	EPerceptionKnowledgeFactStatus Status = EPerceptionKnowledgeFactStatus::Known;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|State")
	FGameplayTagContainer ObservableThroughSenses;

	bool IsValid() const;
	bool IsObservableThrough(FGameplayTag SenseTag) const;
};

/** Stable lookup key for one fact in an observer's knowledge. */
USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeStateKey
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|State")
	FPerceptionKnowledgeEntityId EntityId;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|State")
	FGameplayTag StateTag;

	friend bool operator==(const FPerceptionKnowledgeStateKey& Left, const FPerceptionKnowledgeStateKey& Right)
	{
		return Left.EntityId == Right.EntityId && Left.StateTag == Right.StateTag;
	}
	friend uint32 GetTypeHash(const FPerceptionKnowledgeStateKey& Key)
	{
		return HashCombine(GetTypeHash(Key.EntityId), GetTypeHash(Key.StateTag));
	}
};

USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeStateObservation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FPerceptionKnowledgeStateKey Key;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FPerceptionKnowledgeValue Value;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	EPerceptionKnowledgeFactStatus Status = EPerceptionKnowledgeFactStatus::Known;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FGameplayTag SenseTag;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	float Confidence = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	double WorldTimestamp = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FVector ObservationLocation = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeEventObservation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FGuid ObservationId;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FGameplayTag EventTag;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FGameplayTag SenseTag;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FPerceptionKnowledgeEntityId SourceEntityId;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FPerceptionKnowledgeEntityId InstigatorEntityId;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	float Loudness = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	float Strength = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	float Confidence = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	double WorldTimestamp = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FGameplayTag CauseTag;
};

UENUM(BlueprintType)
enum class EPerceptionKnowledgeObservationType : uint8
{
	State,
	Event
};

/** Type-safe public envelope used by generic consumers such as future adapters. */
USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeObservation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	EPerceptionKnowledgeObservationType Type = EPerceptionKnowledgeObservationType::State;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FPerceptionKnowledgeStateObservation State;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Observation")
	FPerceptionKnowledgeEventObservation Event;

	static FPerceptionKnowledgeObservation FromState(const FPerceptionKnowledgeStateObservation& InState);
	static FPerceptionKnowledgeObservation FromEvent(const FPerceptionKnowledgeEventObservation& InEvent);
};

USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeKnownState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Knowledge")
	FPerceptionKnowledgeStateKey Key;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Knowledge")
	FPerceptionKnowledgeValue Value;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Knowledge")
	FGameplayTag SourceSenseTag;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Knowledge")
	float Confidence = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Knowledge")
	double LastObservedWorldTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Knowledge")
	FVector LastObservationLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Knowledge")
	EPerceptionKnowledgeFactStatus Status = EPerceptionKnowledgeFactStatus::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Knowledge")
	int64 FactRevision = 0;

	/** Listener-wide revision at which this copy was last observed. */
	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Knowledge")
	int64 KnowledgeRevision = 0;
};

USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeSnapshotFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Snapshot")
	TArray<FPerceptionKnowledgeEntityId> EntityIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Snapshot")
	FGameplayTagContainer StateTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Snapshot")
	FGameplayTagContainer SenseTags;

	/** Negative values disable age filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Snapshot", meta = (ClampMin = "-1.0", Units = "Seconds"))
	double MaxAgeSeconds = -1.0;
};

/** Disconnected value snapshot; it never aliases the Listener's internal maps. */
USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Snapshot")
	int64 KnowledgeRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Snapshot")
	double BuiltAtWorldTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Snapshot")
	TArray<FPerceptionKnowledgeKnownState> States;
};

UENUM(BlueprintType)
enum class EPerceptionKnowledgeOperationStatus : uint8
{
	Success,
	Unchanged,
	NoObservers,
	InvalidArgument,
	InvalidOwner,
	InvalidEntityId,
	DuplicateEntityId,
	InvalidTag,
	InvalidValue,
	TypeMismatch,
	NotRegistered,
	Disabled,
	MissingProfile,
	UnsupportedSense,
	CorrelationFailed,
	CapacityExceeded,
	WrongThread,
	ShuttingDown
};

USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeOperationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Result")
	EPerceptionKnowledgeOperationStatus Status = EPerceptionKnowledgeOperationStatus::InvalidArgument;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Result")
	FPerceptionKnowledgeEntityId EntityId;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Result")
	FGuid ObservationId;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Result")
	FString Message;

	bool IsSuccess() const
	{
		return Status == EPerceptionKnowledgeOperationStatus::Success
			|| Status == EPerceptionKnowledgeOperationStatus::Unchanged
			|| Status == EPerceptionKnowledgeOperationStatus::NoObservers;
	}
};

/** Non-hearing event delivered only through an already-active perception relationship. */
USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeEventRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Event")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Event")
	FGameplayTag SenseTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Event")
	TObjectPtr<AActor> Instigator = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Event")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Event")
	bool bUseSourceLocation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Event", meta = (ClampMin = "0.0"))
	float Strength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Event")
	FGameplayTag CauseTag;
};

/** Semantic metadata registered before the corresponding native Hearing event is emitted. */
USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeNoiseRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Hearing")
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Hearing")
	TObjectPtr<AActor> Instigator = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Hearing")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Hearing")
	bool bUseSourceLocation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Hearing", meta = (ClampMin = "0.0"))
	float Loudness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Hearing", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float MaxRange = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Hearing", meta = (ClampMin = "0.0"))
	float Strength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Hearing")
	FGameplayTag CauseTag;
};

USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeDebugFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Debug")
	bool bDrawSight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Debug")
	bool bDrawHearing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Debug")
	bool bDrawStates = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Debug")
	bool bDrawEvents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Debug")
	bool bDrawKnownNotPerceived = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Debug")
	bool bDrawLabels = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Debug")
	bool bDrawBounds = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Debug")
	bool bDrawLines = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Debug", meta = (ClampMin = "0"))
	int32 MaxStatesPerSource = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception Knowledge|Debug")
	FPerceptionKnowledgeEntityId SourceFilter;
};

USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeRuntimeStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	int32 RegisteredSources = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	int32 RegisteredListeners = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	int32 PendingSemanticNoises = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	int64 ProducedObservations = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	int64 DuplicateObservationsDiscarded = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	int64 VisibleSourceRefreshes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	int64 DebugFramesBuilt = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	double LastVisibleRefreshMilliseconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	double LastDebugDrawMilliseconds = 0.0;
};

/** Value-only source data prepared before any runtime debug drawing occurs. */
USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeDebugSourceFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	FPerceptionKnowledgeEntityId EntityId;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	FVector BoundsOrigin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	FVector BoundsExtent = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	FColor Color = FColor::Blue;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	bool bCurrentlySeen = false;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	bool bCurrentlyHeard = false;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	bool bKnownNotPerceived = false;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	int32 ExposedStateCount = 0;
};

/**
 * Disconnected debug frame. A disabled Global AND Local gate returns immediately with
 * bExpensiveDataBuilt=false, which makes the disabled-cost contract automation-testable.
 */
USTRUCT(BlueprintType)
struct PERCEPTIONKNOWLEDGE_API FPerceptionKnowledgeDebugFrame
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	bool bShouldDraw = false;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	bool bExpensiveDataBuilt = false;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	bool bHasValidViewpoint = false;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	FVector ViewLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	FVector ViewDirection = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	FVector BodyLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	FColor ListenerColor = FColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	TArray<FPerceptionKnowledgeDebugSourceFrame> Sources;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	TArray<FPerceptionKnowledgeEventObservation> RecentEvents;

	UPROPERTY(BlueprintReadOnly, Category = "Perception Knowledge|Debug")
	TArray<FVector> CorrelationFailureLocations;
};
