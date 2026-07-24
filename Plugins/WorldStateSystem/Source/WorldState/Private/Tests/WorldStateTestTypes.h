#pragma once

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Types/WorldStateTypes.h"
#include "WorldStateTestTypes.generated.h"

class USceneComponent;
class UWorldStateParticipantComponent;
class UWorldStateSubsystem;

/** Reflected enum fixture used to verify byte-stable enum serialization. */
UENUM()
enum class EWorldStateTestEnum : uint8
{
	/** Distinct values exercise non-default round trips. */
	First,
	Second,
	Third
};

/** Nested reflected fixture used inside native structs and all supported container kinds. */
USTRUCT()
struct FWorldStateTestNestedValue
{
	GENERATED_BODY()

	/** Scalar, native-struct and dynamic-container members cover recursive traversal. */
	UPROPERTY()
	int32 Count = 0;

	/** Native struct member. */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Nested dynamic-array member. */
	UPROPERTY()
	TArray<FName> Tags;

	/** Compares all reflected fields after a serialization round trip. */
	bool operator==(const FWorldStateTestNestedValue& Other) const
	{
		return Count == Other.Count && Location == Other.Location && Tags == Other.Tags;
	}
};

/** Supported native aggregate used to verify nested array, set and map serialization. */
USTRUCT()
struct FWorldStateTestNativeValue
{
	GENERATED_BODY()

	/** Nested struct value. */
	UPROPERTY()
	FWorldStateTestNestedValue Nested;

	/** Representative homogeneous containers. */
	UPROPERTY()
	TArray<int32> Numbers;

	/** Reflected set fixture. */
	UPROPERTY()
	TSet<FName> Names;

	/** Reflected map fixture. */
	UPROPERTY()
	TMap<FName, FString> Labels;

	/** Uses order-independent comparisons for hashed containers. */
	bool operator==(const FWorldStateTestNativeValue& Other) const
	{
		return Nested == Other.Nested && Numbers == Other.Numbers && Names.Includes(Other.Names) && Other.Names.Includes(Names) && Labels.OrderIndependentCompareEqual(Other.Labels);
	}
};

/** Deliberately invalid nested fixture proving hard references are rejected recursively. */
USTRUCT()
struct FWorldStateTestForbiddenValue
{
	GENERATED_BODY()

	/** Unsupported hard UObject edge. */
	UPROPERTY()
	TObjectPtr<UObject> HardReference;
};

/** Comprehensive reflected value source shared by runtime serialization and restore tests. */
UCLASS()
class UWorldStateTestDataComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Disables Tick so the fixture follows the production component lifecycle contract. */
	UWorldStateTestDataComponent();

	/** Primitive, static-array, enum and text-like values. */
	UPROPERTY()
	bool bBoolValue = false;

	/** Signed integer fixture. */
	UPROPERTY()
	int32 IntegerValue = 0;

	/** Root static-array fixture exercising ArrayDim. */
	UPROPERTY()
	int32 FixedIntegers[3] = { 0, 0, 0 };

	/** Floating-point fixture. */
	UPROPERTY()
	double FloatingValue = 0.0;

	/** Reflected enum fixture. */
	UPROPERTY()
	EWorldStateTestEnum EnumValue = EWorldStateTestEnum::First;

	/** Name serialization fixture. */
	UPROPERTY()
	FName NameValue;

	/** String serialization fixture. */
	UPROPERTY()
	FString StringValue;

	/** Localized text serialization fixture. */
	UPROPERTY()
	FText TextValue;

	/** Common native math structs. */
	UPROPERTY()
	FVector VectorValue = FVector::ZeroVector;

	/** Rotator serialization fixture. */
	UPROPERTY()
	FRotator RotatorValue = FRotator::ZeroRotator;

	/** Transform serialization fixture. */
	UPROPERTY()
	FTransform TransformValue = FTransform::Identity;

	/** Nested native aggregate and containers of reflected values. */
	UPROPERTY()
	FWorldStateTestNativeValue NativeValue;

	/** Array-of-struct recursive fixture. */
	UPROPERTY()
	TArray<FWorldStateTestNestedValue> StructArray;

	/** Integer set fixture. */
	UPROPERTY()
	TSet<int32> IntegerSet;

	/** Map-to-struct recursive fixture. */
	UPROPERTY()
	TMap<FName, FWorldStateTestNestedValue> StructMap;

	/** Supported soft object and class references. */
	UPROPERTY()
	TSoftObjectPtr<AActor> SoftActor;

	/** Supported soft-class fixture. */
	UPROPERTY()
	TSoftClassPtr<AActor> SoftActorClass;

	/** Direct and nested reference categories deliberately rejected by validation. */
	UPROPERTY()
	TObjectPtr<UObject> HardObject;

	/** Unsupported weak-reference fixture. */
	UPROPERTY()
	TWeakObjectPtr<UObject> WeakObject;

	/** Unsupported hard reference nested in a struct. */
	UPROPERTY()
	FWorldStateTestForbiddenValue ForbiddenNested;

	/** Unsupported hard reference nested in an array element. */
	UPROPERTY()
	TArray<FWorldStateTestForbiddenValue> ForbiddenArray;

	/** Control value proving unselected reflected properties remain untouched. */
	UPROPERTY()
	int32 UnselectedValue = 0;
};

/** Minimal participant Actor with an authored Scene hierarchy and Component property source. */
UCLASS()
class AWorldStateTestActor : public AActor
{
	GENERATED_BODY()

public:
	/** Creates deterministic default subobjects used by structural restore tests. */
	AWorldStateTestActor();

	/** Owner-Actor property selection fixture. */
	UPROPERTY()
	int32 OwnerValue = 0;

	/** Authored root, pivot and alternate-parent Scene Components. */
	UPROPERTY()
	TObjectPtr<USceneComponent> TestRoot;

	/** Child Scene Component used for relative-transform capture. */
	UPROPERTY()
	TObjectPtr<USceneComponent> TestPivot;

	/** Alternate parent used to exercise attachment mismatch handling. */
	UPROPERTY()
	TObjectPtr<USceneComponent> TestAlternateParent;

	/** Reflected Component property source. */
	UPROPERTY()
	TObjectPtr<UWorldStateTestDataComponent> DataComponent;

	/** Participant bridge under test. */
	UPROPERTY()
	TObjectPtr<UWorldStateParticipantComponent> Participant;
};

/** GC-aware callback observer used to assert ordering, values and reentrancy outcomes. */
UCLASS()
class UWorldStateTestObserver : public UObject
{
	GENERATED_BODY()

public:
	/** Strong test references keep the observed subsystem and Actor alive while callbacks execute. */
	UPROPERTY()
	TObjectPtr<UWorldStateSubsystem> Subsystem;

	/** Actor whose values are sampled by callback handlers. */
	UPROPERTY()
	TObjectPtr<AWorldStateTestActor> WatchedActor;

	/** Exactly-once callback counters and state sampled at each lifecycle boundary. */
	int32 PreCaptureCount = 0;
	int32 CapturedCount = 0;
	int32 PreRestoreCount = 0;
	int32 PropertiesRestoredCount = 0;
	int32 RestoredCount = 0;
	int32 FailedCount = 0;
	int32 IntegerSeenAtPreRestore = 0;
	int32 IntegerSeenAtPropertiesRestored = 0;
	int32 IntegerSeenAtRestored = 0;
	FVector LocationSeenAtPreRestore = FVector::ZeroVector;
	FVector LocationSeenAtRestored = FVector::ZeroVector;
	/** Reentrancy result, terminal diagnostic and optional deterministic-order sink. */
	EWorldStateOperationStatus NestedRestoreStatus = EWorldStateOperationStatus::Success;
	FName LastFailureCode;
	bool bRequestNestedRestore = false;
	bool bSetIntegerDuringPreCapture = false;
	int32 PreCaptureIntegerValue = 0;
	TArray<FWorldStateParticipantId>* RestoreOrderLog = nullptr;

	/** Dynamic delegate handlers mirror every participant callback stage. */
	UFUNCTION()
	void HandlePreCapture(FWorldStateParticipantId ParticipantId);

	/** Records successful capture publication. */
	UFUNCTION()
	void HandleCaptured(FWorldStateParticipantId ParticipantId);

	/** Samples pre-mutation restore state and optionally attempts nested restore. */
	UFUNCTION()
	void HandlePreRestore(FWorldStateParticipantId ParticipantId);

	/** Samples values immediately after property restoration. */
	UFUNCTION()
	void HandlePropertiesRestored(FWorldStateParticipantId ParticipantId);

	/** Records successful final validation and deterministic callback order. */
	UFUNCTION()
	void HandleRestored(FWorldStateParticipantId ParticipantId);

	/** Records the first participant failure diagnostic. */
	UFUNCTION()
	void HandleRestoreFailed(const FWorldStateParticipantResult& Result);
};
