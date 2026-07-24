// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GridWorldTypes.h"
#include "GridCellPointerComponent.generated.h"

class APlayerController;

/** Controls whether pointer projection accepts only navigable cells or every published cell. */
UENUM(BlueprintType)
enum class EGridCellPointerPolicy : uint8
{
	NavigableOnly UMETA(DisplayName = "Navigable Only"),
	ExistingCells UMETA(DisplayName = "Existing Cells")
};

/** Explicit outcome of one pointer update. */
UENUM(BlueprintType)
enum class EGridCellPointerStatus : uint8
{
	Success,
	InvalidInput,
	NoWorldHit,
	NoCell,
	InvalidGrid
};

/** Blueprint-safe result returned by every pointer update entry point. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridCellPointerResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Pointer")
	EGridCellPointerStatus Status = EGridCellPointerStatus::InvalidInput;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Pointer")
	FGridCellId CellId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Pointer")
	FVector WorldCenter = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Pointer")
	FVector FloorNormal = FVector::UpVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Pointer")
	bool bWalkable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Pointer")
	FHitResult HitResult;
};

/** Emitted only when the stable target cell changes; an invalid CurrentCell means the target was cleared. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnGridCellPointerTargetChanged,
	const FGridCellId&,
	PreviousCell,
	const FGridCellId&,
	CurrentCell);

/**
 * On-demand world-pointer adapter for GridWorld cells.
 * It does not Tick, does not depend on Enhanced Input, and never traces against presentation instances.
 */
UCLASS(ClassGroup = (GridWorld), meta = (BlueprintSpawnableComponent))
class GRIDWORLD_API UGridCellPointerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGridCellPointerComponent();

	/** Trace channel used by screen and world-ray updates. Runtime visualization components have no collision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Pointer|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Pointer|Trace")
	bool bTraceComplex = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Pointer|Trace", meta = (ClampMin = "1.0", Units = "cm"))
	double TraceDistance = 100000.0;

	/** Projection volume around the world hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Pointer|Projection", meta = (ClampMin = "0.0"))
	FVector ProjectionExtent = FVector(50.0, 50.0, 200.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Pointer|Projection")
	EGridCellPointerPolicy ProjectionPolicy = EGridCellPointerPolicy::NavigableOnly;

	/** Clears the current target and automatic hover when an update misses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Pointer|Interaction")
	bool bClearHoverOnMiss = true;

	/** Mirrors target changes into the runtime visualization hover layer. Selection is never modified. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Pointer|Interaction")
	bool bApplyHoverToVisualization = true;

	UPROPERTY(BlueprintAssignable, Category = "Grid World|Pointer")
	FOnGridCellPointerTargetChanged OnTargetCellChanged;

	/** Performs a Visibility-style trace through PlayerController and resolves its hit to a GridWorld cell. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Pointer")
	FGridCellPointerResult UpdateFromScreenPosition(APlayerController* PlayerController, const FVector2D& ScreenPosition);

	/** Traces from WorldOrigin along WorldDirection. Non-positive Distance uses TraceDistance. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Pointer")
	FGridCellPointerResult UpdateFromWorldRay(const FVector& WorldOrigin, const FVector& WorldDirection, double Distance = -1.0);

	/** Resolves a previously obtained blocking hit without performing another trace. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Pointer")
	FGridCellPointerResult UpdateFromHitResult(const FHitResult& HitResult);

	/** Explicitly clears the current target and this component's automatic hover contribution. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Pointer")
	void ClearHoveredCell();

	UFUNCTION(BlueprintPure, Category = "Grid World|Pointer")
	bool HasHoveredCell() const { return HoveredCell.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Grid World|Pointer")
	FGridCellId GetHoveredCell() const { return HoveredCell; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FGridCellId HoveredCell;
	bool bHasAppliedVisualizationHover = false;

	FGridCellPointerResult ResolveHit(const FHitResult& HitResult) const;
	FGridCellPointerResult ResolveExistingCell(const FHitResult& HitResult) const;
	FGridCellPointerResult FinishMiss(EGridCellPointerStatus Status, const FHitResult* HitResult = nullptr);
	void ApplyTarget(const FGridCellPointerResult& Result);
};
