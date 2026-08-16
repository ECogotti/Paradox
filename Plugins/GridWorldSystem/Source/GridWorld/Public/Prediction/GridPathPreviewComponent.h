// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Navigation/GridPathInjectionTypes.h"
#include "NavigationPath.h"
#include "Presentation/GridPathPresentationTypes.h"
#include "GridPathPreviewComponent.generated.h"

class AController;
class AGridNavigationData;
class UGridCellVisualStyle;
class UGridPathLineVisualStyle;
class UNavigationQueryFilter;
struct FGridCellData;
struct FGridTrafficGoalClaimRequest;

/** Response to a navigation revision that invalidates the current preview signature. */
UENUM(BlueprintType)
enum class EGridPathPreviewStalePolicy : uint8
{
	KeepButMarkStale UMETA(DisplayName = "Keep but Mark Stale"),
	ClearImmediately UMETA(DisplayName = "Clear Immediately"),
	RecalculateAutomatically UMETA(DisplayName = "Recalculate Automatically")
};

/** Designer-selected presentation and commit policy for an allowed partial result. */
UENUM(BlueprintType)
enum class EGridPartialPathPreviewPolicy : uint8
{
	ShowAndAllowCommit UMETA(DisplayName = "Show and Allow Commit"),
	ShowButBlockCommit UMETA(DisplayName = "Show but Block Commit"),
	HideAndBlockCommit UMETA(DisplayName = "Hide and Block Commit")
};

/** Selects whether the requested terminal cell belongs to the movement path. */
UENUM(BlueprintType)
enum class EGridPathPreviewTerminalPolicy : uint8
{
	IncludeRequestedGoal UMETA(DisplayName = "Include Requested Goal"),
	StopBeforeRequestedGoal UMETA(DisplayName = "Stop Before Requested Goal")
};

/** Failure specific to preview request construction and lifetime. */
UENUM(BlueprintType)
enum class EGridPathPreviewFailureReason : uint8
{
	None,
	InvalidController,
	InvalidGoal,
	InvalidNavigationData,
	StartNotNavigable,
	NoPath,
	PartialPathBlocked,
	GoalOccupied,
	TerminalGoalUnavailable,
	Stale,
	PresentationUnavailable,
	InternalError
};

/** Input-independent semantic request; the controller supplies start, agent and query context. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridPathPreviewRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview")
	TObjectPtr<AController> Controller;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview")
	FGridCellId GoalCell;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview")
	TSubclassOf<UNavigationQueryFilter> FilterClass;
};

/** Latest immutable Blueprint-safe preview snapshot. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridPathPreviewResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	EGridQueryStatus Status = EGridQueryStatus::InvalidInput;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	EGridPathPreviewFailureReason FailureReason = EGridPathPreviewFailureReason::InvalidController;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	FGridCellId StartCell;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	FGridCellId GoalCell;

	/** Destination originally requested before optional goal-contention adjustment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	FGridCellId RequestedGoalCell;

	/** True when GoalCell is the preceding cell selected by Stop Before Occupied. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	bool bGoalAdjustedForContention = false;

	/** True when GoalCell excludes the requested terminal cell by explicit preview policy. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	bool bGoalAdjustedForTerminalPolicy = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	FGridPathQueryResult Path;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	FGuid QuerySignature;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	int32 RequestGeneration = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	bool bIsStale = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Path Preview")
	bool bIsCommittable = false;

	bool IsSuccessful() const
	{
		return Status == EGridQueryStatus::Success || Status == EGridQueryStatus::Partial;
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnGridPathPreviewChanged,
	const FGridPathPreviewResult&,
	Preview);

/**
 * On-demand prediction owner. It performs normal GridWorld queries only when semantic inputs change
 * and contributes one optional Preview session to the existing path presentation subsystem.
 */
UCLASS(ClassGroup = (GridWorld), meta = (BlueprintSpawnableComponent))
class GRIDWORLD_API UGridPathPreviewComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGridPathPreviewComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview")
	EGridPathPreviewStalePolicy StalePolicy = EGridPathPreviewStalePolicy::RecalculateAutomatically;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview")
	EGridPartialPathPreviewPolicy PartialPathPolicy = EGridPartialPathPreviewPolicy::ShowAndAllowCommit;

	/** Policy retained by the exact path exported at click/commit time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview")
	EGridInjectedPathInvalidationPolicy InjectedPathInvalidationPolicy = EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal;

	/** Query filter used by prediction and retained by the exported exact path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview")
	TSubclassOf<UNavigationQueryFilter> NavigationFilter;

	/** Optional exact-goal arbitration shared with Move To Grid Cell. Preview never acquires the claim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview")
	EGridGoalContentionPolicy GoalContentionPolicy = EGridGoalContentionPolicy::StopBeforeOccupied;

	/** Clearance used when testing traffic claims for exact occupied-goal policies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "GoalContentionPolicy == EGridGoalContentionPolicy::RejectOccupied || GoalContentionPolicy == EGridGoalContentionPolicy::StopBeforeOccupied", EditConditionHides))
	float AdditionalGoalSeparation = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview|Presentation")
	bool bAutoPresentPreview = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview|Presentation", meta = (EditCondition = "bAutoPresentPreview"))
	bool bRenderCellOverlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview|Presentation", meta = (EditCondition = "bAutoPresentPreview"))
	bool bRenderLine = true;

	/** Lazily enables only the selected renderer backends on the first presentable result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview|Presentation", meta = (EditCondition = "bAutoPresentPreview"))
	bool bAutoEnableRenderers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview|Presentation", meta = (EditCondition = "bAutoPresentPreview && bRenderCellOverlay"))
	TObjectPtr<UGridCellVisualStyle> CellVisualStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview|Presentation", meta = (EditCondition = "bAutoPresentPreview && bRenderLine"))
	TObjectPtr<UGridPathLineVisualStyle> LineVisualStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Preview|Presentation", meta = (EditCondition = "bAutoPresentPreview"))
	int32 PresentationPriority = 0;

	UPROPERTY(BlueprintAssignable, Category = "Grid World|Path Preview")
	FOnGridPathPreviewChanged OnPreviewChanged;

	/** Updates or reuses a preview. No query runs when every semantic input is unchanged. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Path Preview")
	FGridPathPreviewResult UpdatePreviewForController(AController* Controller, const FGridCellId& GoalCell);

	/**
	 * Updates or reuses a preview with an explicit terminal-cell policy. Stop Before Requested Goal
	 * queries the requested cell normally, then exports and presents the complete prefix ending at
	 * its ordinary predecessor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Path Preview")
	FGridPathPreviewResult UpdatePreviewForControllerWithTerminalPolicy(
		AController* Controller,
		const FGridCellId& GoalCell,
		EGridPathPreviewTerminalPolicy TerminalPolicy);

	/** Re-evaluates the retained request even when its controller and goal are unchanged. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Path Preview")
	FGridPathPreviewResult RefreshPreview();

	/** Clears result and presentation while retaining component configuration. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Path Preview")
	void ClearPreview();

	UFUNCTION(BlueprintPure, Category = "Grid World|Path Preview")
	FGridPathPreviewResult GetLatestPreview() const { return LatestResult; }

	/** Refreshes stale/start-changed data once and exports an authoritative exact path when committable. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Path Preview")
	bool PreparePreviewForCommit(FGridInjectedPath& OutInjectedPath, FGridPathPreviewResult& OutPreview);

	UFUNCTION(BlueprintPure, Category = "Grid World|Path Preview")
	bool HasCommittablePreview() const { return LatestResult.bIsCommittable && LatestInjectedPath.IsSet(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TWeakObjectPtr<AController> PreviewController;
	TWeakObjectPtr<AGridNavigationData> PreviewNavigationData;
	FGridCellId PreviewGoalCell;
	TSubclassOf<UNavigationQueryFilter> PreviewFilterClass;
	EGridPathPreviewTerminalPolicy PreviewTerminalPolicy =
		EGridPathPreviewTerminalPolicy::IncludeRequestedGoal;
	FGridPathPreviewResult LatestResult;
	FGridInjectedPath LatestInjectedPath;
	FGridPathPresentationHandle PresentationHandle;
	FNavPathSharedPtr NativePreviewPath;
	FDelegateHandle TrafficReservationsChangedHandle;
	int64 LastFilterSignature = 0;
	FNavAgentProperties LastAgentProperties;
	FGridRevisionSet LastRelevantRevisions;
	int64 LastTrafficRevision = 0;
	EGridPartialPathPreviewPolicy LastPartialPathPolicy = EGridPartialPathPreviewPolicy::ShowAndAllowCommit;
	EGridInjectedPathInvalidationPolicy LastInjectedPathInvalidationPolicy = EGridInjectedPathInvalidationPolicy::RecalculateToOriginalGoal;
	EGridGoalContentionPolicy LastGoalContentionPolicy = EGridGoalContentionPolicy::StopBeforeOccupied;
	EGridPathPreviewTerminalPolicy LastTerminalPolicy =
		EGridPathPreviewTerminalPolicy::IncludeRequestedGoal;
	float LastAdditionalGoalSeparation = 5.0f;
	int32 RequestGeneration = 0;
	bool bRefreshInProgress = false;

	UFUNCTION()
	void HandleGridWorldChanged(const FGridChangeSet& ChangeSet);

	void HandleTrafficReservationsChanged();
	FGridPathPreviewResult ExecutePreview(bool bForce);
	bool BuildGoalClaimRequest(
		AController& Controller,
		const FGridCellData& GoalCell,
		FGridTrafficGoalClaimRequest& OutRequest) const;
	void PublishFailure(EGridQueryStatus Status, EGridPathPreviewFailureReason Reason);
	void UpdatePresentation();
	void ClearPresentation();
	void BindNavigationData(AGridNavigationData* NavigationData);
	void UnbindNavigationData();
};
