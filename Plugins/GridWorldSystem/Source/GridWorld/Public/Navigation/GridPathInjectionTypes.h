// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavigationTypes.h"
#include "GridWorldTypes.h"
#include "GridPathInjectionTypes.generated.h"

class UNavigationQueryFilter;

/** Identifies how a GridWorld movement request obtains its initial logical path. */
UENUM(BlueprintType)
enum class EGridMovePathSource : uint8
{
	Destination,
	ExactInjectedPath UMETA(DisplayName = "Exact Injected Path")
};

/** Runtime response when an exact injected path can no longer be followed unchanged. */
UENUM(BlueprintType)
enum class EGridInjectedPathInvalidationPolicy : uint8
{
	FailOnInvalidation UMETA(DisplayName = "Fail on Invalidation"),
	RecalculateToOriginalGoal UMETA(DisplayName = "Recalculate to Original Goal")
};

/** Generic origin metadata retained by native GridWorld navigation paths. */
UENUM(BlueprintType)
enum class EGridNavigationPathOrigin : uint8
{
	Computed,
	Preview,
	Injected,
	Recalculated
};

/** Structured reason an exact path could not be accepted by the current navigation authority. */
UENUM(BlueprintType)
enum class EGridInjectedPathFailureReason : uint8
{
	None,
	InvalidPath,
	InvalidNavigationData,
	NavigationDataMismatch,
	AgentMismatch,
	FilterMismatch,
	InvalidStart,
	MissingCell,
	BlockedCell,
	DisconnectedCells,
	ForbiddenLink,
	StaleTopology,
	StaleTraversalState,
	StaleOccupancyState,
	InvalidGoal,
	InvalidPartialPath,
	InvalidConstraint,
	InternalError
};

/**
 * Blueprint-safe exact logical path produced and stamped by GridWorld navigation authority.
 * Ordered cells are authoritative input. World points and following metadata are derived on commit.
 */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridInjectedPath
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	TArray<FGridCellId> Cells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	FGridCellId OriginalGoalCell;

	/**
	 * Destination originally requested by gameplay. It differs from OriginalGoalCell only
	 * when goal contention deliberately shortened the exact path.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	FGridCellId RequestedGoalCell;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	FGuid NavigationDataId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	FGuid PathInstanceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	FGuid SourcePreviewId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	FNavAgentProperties AgentProperties;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	TSubclassOf<UNavigationQueryFilter> FilterClass;

	/** Runtime-only semantic hash of the fully initialized filter and querier context. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	int64 FilterSignature = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	FGridRevisionSet Revisions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	int64 TrafficReservationRevision = 0;

	/** Whether later recalculation may return a partial path. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	bool bAllowPartialPath = true;

	/** Whether Cells currently end at a reachable prefix rather than OriginalGoalCell. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	bool bIsPartial = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Injection")
	EGridInjectedPathInvalidationPolicy InvalidationPolicy =
		EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal;

	bool IsSet() const
	{
		return NavigationDataId.IsValid()
			&& PathInstanceId.IsValid()
			&& !Cells.IsEmpty()
			&& OriginalGoalCell.IsValid();
	}
};

/** Read-only result of authoritative exact-path validation. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridInjectedPathValidationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	EGridInjectedPathFailureReason FailureReason = EGridInjectedPathFailureReason::InvalidPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	FGridCellId InvalidCell;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	int32 InvalidSegmentIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	FGridRevisionSet CurrentRevisions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Injection")
	FString DiagnosticMessage;
};
