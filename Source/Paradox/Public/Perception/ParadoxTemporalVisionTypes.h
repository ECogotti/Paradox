#pragma once

#include "CoreMinimal.h"
#include "ParadoxTemporalVisionTypes.generated.h"

class AActor;
class UPrimitiveComponent;

/** Filtered temporal-cone candidate before Entity Relations evaluates temporal order. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxTemporalOverlapSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	TObjectPtr<AActor> Observer = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	TObjectPtr<UPrimitiveComponent> ObserverComponent = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	TObjectPtr<AActor> Target = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	TObjectPtr<UPrimitiveComponent> TargetComponent = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	int32 ObserverTemporalIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	int32 TargetTemporalIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	int32 OverlappingComponentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	int32 DetectionSessionId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	bool bDetectionAuthoritative = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	FVector ObserverLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision")
	FVector TargetLocation = FVector::ZeroVector;
};

/** Read-only diagnostic copy for one clone-owned temporal vision participant. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxTemporalVisionDebugSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision|Debug")
	TObjectPtr<AActor> Observer = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision|Debug")
	int32 ObserverTemporalIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision|Debug")
	int32 DetectionSessionId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision|Debug")
	int32 DeduplicatedActorPairCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision|Debug")
	int32 OverlappingPrimitiveCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision|Debug")
	bool bDetectionAuthoritative = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision|Debug")
	bool bLocalDebugEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Temporal Vision|Debug")
	bool bGlobalDebugEnabled = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxTemporalOverlapEvent,
	const FParadoxTemporalOverlapSnapshot&,
	Snapshot);
