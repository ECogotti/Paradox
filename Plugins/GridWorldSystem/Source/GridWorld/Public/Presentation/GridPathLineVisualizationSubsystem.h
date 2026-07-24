// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Presentation/GridPathPresentationTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridPathLineVisualizationSubsystem.generated.h"

class AActor;
class UGridPathLineVisualStyle;
class UGridPathPresentationSubsystem;
class UGridWorldSubsystem;
class UHierarchicalInstancedStaticMeshComponent;

/** Internal immutable snapshot of one line-enabled semantic path session. */
struct FGridPathLineRenderRecord
{
	TArray<FGridCellId> Cells;
	TMap<FGridCellId, float> PreservedTraversedCells;
	EGridPathPresentationPurpose Purpose = EGridPathPresentationPurpose::Preview;
	EGridPathProgressPresentationMode ProgressMode = EGridPathProgressPresentationMode::AllCells;
	uint64 CreationSequence = 0;
	int32 Priority = 0;
	int32 CurrentCellIndex = 0;
	bool bInvalid = false;
};

/** Optional local strict-polyline backend for GridWorld path presentation sessions. */
UCLASS()
class GRIDWORLD_API UGridPathLineVisualizationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Lazily creates the line renderer. Null selects the plugin-provided default line style. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path Line")
	bool EnableLineVisualization(UGridPathLineVisualStyle* Style = nullptr);

	/** Releases all line Actor/HISM resources while preserving presentation sessions. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path Line")
	void DisableLineVisualization();

	/** Hides or shows allocated line resources without rebuilding them. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path Line")
	void SetLineVisualizationVisible(bool bVisible);

	/** Rebuilds strict-polyline segment and marker instances from current session snapshots. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path Line")
	bool RebuildLineVisualization();

	UFUNCTION(BlueprintPure, Category = "Grid World|Presentation|Path Line")
	bool IsLineVisualizationEnabled() const { return bVisualizationEnabled; }

	UFUNCTION(BlueprintPure, Category = "Grid World|Presentation|Path Line")
	bool IsLineVisualizationVisible() const { return bVisualizationEnabled && bVisualizationVisible; }

	/** C++ diagnostics; renderer components and instance indices remain private. */
	int32 GetSegmentInstanceCount() const { return SegmentInstanceCount; }
	int32 GetMarkerInstanceCount() const { return MarkerInstanceCount; }
	int64 GetRendererGeneration() const { return RendererGeneration; }
	const UGridPathLineVisualStyle* GetActiveStyle() const { return ActiveStyle; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	friend class UGridPathPresentationSubsystem;
	friend class FGridPathLineRendererTest;

	UPROPERTY(Transient)
	TObjectPtr<AActor> VisualizationActor;

	UPROPERTY(Transient)
	TObjectPtr<UGridPathLineVisualStyle> ActiveStyle;

	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SegmentComponent;

	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> MarkerComponent;

	TArray<FGridPathLineRenderRecord> RenderRecords;
	int64 RendererGeneration = 0;
	int32 SegmentInstanceCount = 0;
	int32 MarkerInstanceCount = 0;
	bool bVisualizationEnabled = false;
	bool bVisualizationVisible = true;

	UGridWorldSubsystem* GetGridWorldSubsystem() const;
	UGridPathLineVisualStyle* ResolveStyle(UGridPathLineVisualStyle* RequestedStyle) const;
	bool EnsureVisualizationActor();
	bool CreateRenderComponents();
	void DestroyVisualizationActor();
	void ApplyVisibility();
	void ReplaceRenderRecordsInternal(TArray<FGridPathLineRenderRecord>&& NewRecords);
};
