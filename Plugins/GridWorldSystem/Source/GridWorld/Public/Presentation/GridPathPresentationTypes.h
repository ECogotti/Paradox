// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GridWorldTypes.h"
#include "GridPathPresentationTypes.generated.h"

/** High-level intent used as the second deterministic overlap key after explicit priority. */
UENUM(BlueprintType)
enum class EGridPathPresentationPurpose : uint8
{
	Preview,
	Active
};

/** Selects which logical portions of an ordered path contribute to the cell overlay. */
UENUM(BlueprintType)
enum class EGridPathProgressPresentationMode : uint8
{
	AllCells UMETA(DisplayName = "All Cells"),
	RemainingOnly UMETA(DisplayName = "Remaining Only"),
	TraversedAndRemaining UMETA(DisplayName = "Traversed and Remaining"),
	CurrentAndRemaining UMETA(DisplayName = "Current and Remaining"),
	DestinationOnly UMETA(DisplayName = "Destination Only"),
	EndpointsAndTurns UMETA(DisplayName = "Endpoints and Turns")
};

/** Controls which contribution from the previous path survives a replacement. */
UENUM(BlueprintType)
enum class EGridPathReplacementPolicy : uint8
{
	ReplaceImmediately UMETA(DisplayName = "Replace Immediately"),
	PreserveTraversed UMETA(DisplayName = "Preserve Traversed")
};

/** Defines who is responsible for releasing a presentation session. */
UENUM(BlueprintType)
enum class EGridPathPresentationLifetime : uint8
{
	Manual,
	OwnerLifetime UMETA(DisplayName = "Owner Lifetime")
};

/** Describes why an observed GridWorld path changed while it remained assigned to a follower. */
UENUM(BlueprintType)
enum class EGridPathFollowingPresentationChange : uint8
{
	Accepted,
	Replaced,
	Recalculated
};

/** Blueprint-safe projection of the native navigation path invalidation events used by presentation. */
UENUM(BlueprintType)
enum class EGridPathPresentationInvalidationReason : uint8
{
	Invalidated,
	RepathFailed UMETA(DisplayName = "Repath Failed")
};

/** Opaque world-local identity. Renderer objects and instance indices never cross this boundary. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridPathPresentationHandle
{
	GENERATED_BODY()

	/** @return True when this value contains an identifier; the subsystem must still validate it. */
	bool IsSet() const { return SessionId.IsValid(); }

	bool operator==(const FGridPathPresentationHandle& Other) const { return SessionId == Other.SessionId; }
	bool operator!=(const FGridPathPresentationHandle& Other) const { return !(*this == Other); }

private:
	friend class UGridPathPresentationSubsystem;

	UPROPERTY(VisibleAnywhere, Category = "Grid World|Presentation")
	FGuid SessionId;
};

/** Blueprint-safe settings and immutable path snapshot used to create one presentation session. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridPathPresentationRequest
{
	GENERATED_BODY()

	/** Ordered persistent cells. Every cell must exist in the current topology. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation")
	TArray<FGridCellId> Cells;

	/** Revisions retained by the source query/path for diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation")
	FGridRevisionSet SourceRevisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation")
	EGridPathPresentationPurpose Purpose = EGridPathPresentationPurpose::Preview;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation")
	EGridPathProgressPresentationMode ProgressMode = EGridPathProgressPresentationMode::AllCells;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation")
	EGridPathReplacementPolicy ReplacementPolicy = EGridPathReplacementPolicy::ReplaceImmediately;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation")
	EGridPathPresentationLifetime Lifetime = EGridPathPresentationLifetime::Manual;

	/** Required when Lifetime is Owner Lifetime and otherwise retained only as correlation metadata. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation")
	TObjectPtr<UObject> Owner;

	/** Larger values win deterministic overlap resolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation")
	int32 Priority = 0;

	/** Logical current index. Preview sessions ignore it unless their selected mode filters by progress. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation", meta = (ClampMin = "0"))
	int32 CurrentCellIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation")
	bool bVisible = true;

	/** Contributes semantic path state to the runtime cell-overlay renderer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation|Renderers")
	bool bRenderCellOverlay = true;

	/** Contributes strict-polyline geometry to the independent path-line renderer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation|Renderers")
	bool bRenderLine = false;
};

/** Read-only snapshot of one live session. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridPathPresentationSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	FGridPathPresentationHandle Handle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	TArray<FGridCellId> Cells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	FGridRevisionSet SourceRevisions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	EGridPathPresentationPurpose Purpose = EGridPathPresentationPurpose::Preview;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	EGridPathProgressPresentationMode ProgressMode = EGridPathProgressPresentationMode::AllCells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	EGridPathReplacementPolicy ReplacementPolicy = EGridPathReplacementPolicy::ReplaceImmediately;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	int32 Priority = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	int32 CurrentCellIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	bool bVisible = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation")
	bool bInvalid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Renderers")
	bool bRenderCellOverlay = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Renderers")
	bool bRenderLine = false;
};
