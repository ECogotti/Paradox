// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Presentation/GridPresentationTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridRuntimeVisualizationSubsystem.generated.h"

class AActor;
class UGridCellVisualStyle;
class UGridPathPresentationSubsystem;
class UGridWorldSubsystem;
class UHierarchicalInstancedStaticMeshComponent;

/** Local, non-authoritative runtime presentation of published GridWorld cells. */
UCLASS()
class GRIDWORLD_API UGridRuntimeVisualizationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Lazily creates runtime visualization. Null selects the plugin-provided default style. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	bool EnableVisualization(UGridCellVisualStyle* Style = nullptr);

	/** Releases every rendering resource while preserving semantic hover/selection state. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	void DisableVisualization();

	/** Hides or shows allocated resources without rebuilding them. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	void SetVisualizationVisible(bool bVisible);

	/** Rebuilds every chunk from the current authoritative snapshot. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	bool RebuildVisualization();

	/** Refreshes semantic/custom data for existing cells without changing instance structure. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	void RefreshCells(const TArray<FGridCellId>& CellIds);

	/** Adds or removes one hover contribution. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	bool SetCellHovered(const FGridCellId& CellId, bool bHovered);

	/** Adds or removes one selection contribution. Selection has visual priority over hover. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	bool SetCellSelected(const FGridCellId& CellId, bool bSelected);

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	void ClearHoveredCells();

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	void ClearSelectedCells();

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation")
	void ClearInteractionStates();

	/** Resolves all presentation layers for a currently published cell. */
	UFUNCTION(BlueprintPure, Category = "Grid World|Presentation")
	bool GetCellVisualState(const FGridCellId& CellId, FGridCellVisualState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Grid World|Presentation")
	bool IsVisualizationEnabled() const { return bVisualizationEnabled; }

	UFUNCTION(BlueprintPure, Category = "Grid World|Presentation")
	bool IsVisualizationVisible() const { return bVisualizationEnabled && bVisualizationVisible; }

	/** C++ diagnostics that do not expose renderer pointers or instance indices. */
	int32 GetVisualizedCellCount() const { return CellHandles.Num(); }
	int64 GetRendererGeneration() const { return RendererGeneration; }
	bool IsCellVisualized(const FGridCellId& CellId) const { return CellHandles.Contains(CellId); }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	friend class FGridRuntimeVisualizationLifecycleTest;
	friend class FGridRuntimeVisualizationStateTest;
	friend class UGridPathPresentationSubsystem;

	struct FGridCellVisualHandle
	{
		int32 RendererId = INDEX_NONE;
		int32 InstanceIndex = INDEX_NONE;
		int64 RendererGeneration = 0;
	};

	struct FGridChunkRenderer
	{
		TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Component;
		int32 RendererId = INDEX_NONE;
	};

	UPROPERTY(Transient)
	TObjectPtr<AActor> VisualizationActor;

	UPROPERTY(Transient)
	TObjectPtr<UGridCellVisualStyle> ActiveStyle;

	TMap<FGridChunkCoord, FGridChunkRenderer> ChunkRenderers;
	TMap<FGridCellId, FGridCellVisualHandle> CellHandles;
	TSet<FGridCellId> HoveredCells;
	TSet<FGridCellId> SelectedCells;
	TMap<FGridCellId, FGridResolvedPathVisualState> PathStates;
	FGridRevisionSet CachedRevisions;
	int64 RendererGeneration = 0;
	bool bVisualizationEnabled = false;
	bool bVisualizationVisible = true;

	UFUNCTION()
	void HandleGridWorldChanged(const FGridChangeSet& ChangeSet);

	UGridWorldSubsystem* GetGridWorldSubsystem() const;
	UGridCellVisualStyle* ResolveStyle(UGridCellVisualStyle* RequestedStyle) const;
	bool EnsureVisualizationActor();
	void DestroyVisualizationActor();
	void ApplyVisibility();
	FGridCellVisualState BuildCellVisualState(const FGridCellData& Cell) const;
	FTransform BuildCellTransform(const FGridRegionData& Region, const FGridCellData& Cell) const;
	bool ApplyCellCustomData(const FGridCellId& CellId, bool bMarkRenderStateDirty);
	void ApplyCustomDataBatch(TConstArrayView<FGridCellId> CellIds);
	void PruneStaleInteractionState(const FGridWorldSnapshot& Snapshot);
	bool SetCellPathStateInternal(
		const FGridCellId& CellId,
		EGridCellPathVisualState PathState,
		float PathProgress = 0.0f);
	void ReplaceResolvedPathStatesInternal(TMap<FGridCellId, FGridResolvedPathVisualState>&& NewStates);
	bool GetVisualHandleForTesting(const FGridCellId& CellId, FGridCellVisualHandle& OutHandle) const;
};
