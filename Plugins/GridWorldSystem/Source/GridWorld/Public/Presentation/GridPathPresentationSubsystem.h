// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Presentation/GridPathPresentationTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridPathPresentationSubsystem.generated.h"

struct FGridNavigationPath;
class UGridRuntimeVisualizationSubsystem;
class UGridPathLineVisualizationSubsystem;
class UGridWorldSubsystem;

/** Owns local, non-authoritative path presentation sessions for one render-capable game World. */
UCLASS()
class GRIDWORLD_API UGridPathPresentationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Creates a session after validating every requested cell against the current topology. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool CreatePathPresentation(
		const FGridPathPresentationRequest& Request,
		FGridPathPresentationHandle& OutHandle);

	/** Uses PathResult.Cells/Revisions and every non-path setting from TemplateRequest. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool CreatePathPresentationFromQueryResult(
		const FGridPathQueryResult& PathResult,
		const FGridPathPresentationRequest& TemplateRequest,
		FGridPathPresentationHandle& OutHandle);

	/** C++ path overload. The session snapshots cells/revisions and never retains the native path. */
	bool CreatePathPresentation(
		const FGridNavigationPath& Path,
		const FGridPathPresentationRequest& TemplateRequest,
		FGridPathPresentationHandle& OutHandle);

	/** Atomically replaces the ordered path and its logical current index. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool UpdatePathPresentation(
		const FGridPathPresentationHandle& Handle,
		const TArray<FGridCellId>& Cells,
		int32 CurrentCellIndex = 0);

	/** Query-result overload that also refreshes source revisions. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool UpdatePathPresentationFromQueryResult(
		const FGridPathPresentationHandle& Handle,
		const FGridPathQueryResult& PathResult,
		int32 CurrentCellIndex = 0);

	/** C++ path overload used by the GridWorld path follower during recalculation. */
	bool UpdatePathPresentation(
		const FGridPathPresentationHandle& Handle,
		const FGridNavigationPath& Path,
		int32 CurrentCellIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool UpdatePathPresentationProgress(const FGridPathPresentationHandle& Handle, int32 CurrentCellIndex);

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool SetPathPresentationVisible(const FGridPathPresentationHandle& Handle, bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool SetPathPresentationPriority(const FGridPathPresentationHandle& Handle, int32 Priority);

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool SetPathPresentationMode(
		const FGridPathPresentationHandle& Handle,
		EGridPathProgressPresentationMode ProgressMode);

	/** Selects cell overlay, strict line, both, or neither without replacing the session. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool SetPathPresentationRenderers(
		const FGridPathPresentationHandle& Handle,
		bool bRenderCellOverlay,
		bool bRenderLine);

	/** Preserves the path snapshot but resolves every existing contribution as Invalid. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool MarkPathPresentationInvalid(const FGridPathPresentationHandle& Handle);

	/** Clears path/history while retaining a valid reusable session handle. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool ClearPathPresentation(const FGridPathPresentationHandle& Handle);

	/** Removes the session. Copies of Handle immediately become stale. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Path")
	bool ReleasePathPresentation(const FGridPathPresentationHandle& Handle);

	UFUNCTION(BlueprintPure, Category = "Grid World|Presentation|Path")
	bool IsPathPresentationValid(const FGridPathPresentationHandle& Handle) const;

	UFUNCTION(BlueprintPure, Category = "Grid World|Presentation|Path")
	bool GetPathPresentation(
		const FGridPathPresentationHandle& Handle,
		FGridPathPresentationSnapshot& OutPresentation) const;

	/** C++ diagnostics. */
	int32 GetActiveSessionCount() const { return Sessions.Num(); }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FGridPathPresentationSession
	{
		FGridPathPresentationHandle Handle;
		TArray<FGridCellId> Cells;
		FGridRevisionSet SourceRevisions;
		EGridPathPresentationPurpose Purpose = EGridPathPresentationPurpose::Preview;
		EGridPathProgressPresentationMode ProgressMode = EGridPathProgressPresentationMode::AllCells;
		EGridPathReplacementPolicy ReplacementPolicy = EGridPathReplacementPolicy::ReplaceImmediately;
		EGridPathPresentationLifetime Lifetime = EGridPathPresentationLifetime::Manual;
		TWeakObjectPtr<UObject> Owner;
		TMap<FGridCellId, float> PreservedTraversedCells;
		uint64 CreationSequence = 0;
		int32 Priority = 0;
		int32 CurrentCellIndex = 0;
		bool bVisible = true;
		bool bInvalid = false;
		bool bRenderCellOverlay = true;
		bool bRenderLine = false;
	};

	TMap<FGuid, FGridPathPresentationSession> Sessions;
	uint64 NextCreationSequence = 1;
	FDelegateHandle PostGarbageCollectHandle;

	UFUNCTION()
	void HandleGridWorldChanged(const FGridChangeSet& ChangeSet);

	void HandlePostGarbageCollect();
	UGridWorldSubsystem* GetGridWorldSubsystem() const;
	UGridRuntimeVisualizationSubsystem* GetVisualizationSubsystem() const;
	UGridPathLineVisualizationSubsystem* GetLineVisualizationSubsystem() const;
	bool ValidateCells(TConstArrayView<FGridCellId> Cells) const;
	bool ValidateCurrentIndex(const FGridPathPresentationSession& Session, int32 CurrentCellIndex) const;
	bool PruneExpiredOwnerSessions();
	void RebuildPresentationOutputs();
};
