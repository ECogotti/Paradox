// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavQueryFilter.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "GridWorldTypes.h"
#include "GridNavigationQueryFilter.generated.h"

/** Thread-safe native filter data copied into each Unreal navigation query. */
class GRIDWORLD_API FGridNavigationQueryFilterImpl final : public INavigationQueryFilterInterface
{
public:
	static constexpr int32 MaxAreaCount = 64;

	FGridNavigationQueryFilterImpl();

	virtual void Reset() override;
	virtual void SetAreaCost(uint8 AreaType, float Cost) override;
	virtual void SetFixedAreaEnteringCost(uint8 AreaType, float Cost) override;
	virtual void SetExcludedArea(uint8 AreaType) override;
	virtual void SetAllAreaCosts(const float* CostArray, int32 Count) override;
	virtual void GetAllAreaCosts(float* CostArray, float* FixedCostArray, int32 Count) const override;
	virtual void SetBacktrackingEnabled(bool bInBacktracking) override;
	virtual bool IsBacktrackingEnabled() const override { return bBacktracking; }
	virtual float GetHeuristicScale() const override { return 1.0f; }
	virtual bool IsEqual(const INavigationQueryFilterInterface* Other) const override;
	virtual void SetIncludeFlags(uint16 Flags) override { IncludeFlags = Flags; }
	virtual uint16 GetIncludeFlags() const override { return IncludeFlags; }
	virtual void SetExcludeFlags(uint16 Flags) override { ExcludeFlags = Flags; }
	virtual uint16 GetExcludeFlags() const override { return ExcludeFlags; }
	virtual INavigationQueryFilterInterface* CreateCopy() const override { return new FGridNavigationQueryFilterImpl(*this); }

	/** Returns the multiplicative Unreal area cost or infinity for an excluded area. */
	float GetAreaCost(uint8 AreaId) const;
	/** Returns the fixed cost paid when entering AreaId. */
	float GetEnteringCost(uint8 AreaId) const;
	/** Sets the query direction limit. */
	void SetMovementMode(EGridMovementMode InMovementMode) { MovementMode = InMovementMode; }
	/** @return Query direction limit. */
	EGridMovementMode GetMovementMode() const { return MovementMode; }
	/** Selects the path optimization objective. */
	void SetPathOptimizationMode(EGridPathOptimizationMode InMode) { PathOptimizationMode = InMode; }
	/** @return Selected path optimization objective. */
	EGridPathOptimizationMode GetPathOptimizationMode() const { return PathOptimizationMode; }
	/** Sets a finite nonnegative turn penalty in equivalent orthogonal cells. */
	void SetBalancedTurnPenalty(float InPenalty) { BalancedTurnPenalty = FMath::IsFinite(InPenalty) ? FMath::Max(0.0f, InPenalty) : 2.0f; }
	/** @return Balanced turn penalty in equivalent orthogonal cells. */
	float GetBalancedTurnPenalty() const { return BalancedTurnPenalty; }
	/** Bounds directional-state expansion to at least one state. */
	void SetMaxSearchStates(uint32 InMaxSearchStates) { MaxSearchStates = FMath::Max(1u, InMaxSearchStates); }
	/** @return Maximum directional states expanded. */
	uint32 GetMaxSearchStates() const { return MaxSearchStates; }
	/** Sets query-side diagonal corner permission. */
	void SetAllowCornerCutting(bool bInAllowCornerCutting) { bAllowCornerCutting = bInAllowCornerCutting; }
	/** @return Query-side diagonal corner permission. */
	bool AllowsCornerCutting() const { return bAllowCornerCutting; }
	/** Enables/disables authored special links. */
	void SetAllowLinks(bool bInAllowLinks) { bAllowLinks = bInAllowLinks; }
	/** @return Whether authored links may be traversed. */
	bool AllowsLinks() const { return bAllowLinks; }
	/** Sets ordinary occupancy treatment. */
	void SetOccupancyPolicy(EGridOccupancyPolicy InPolicy) { OccupancyPolicy = InPolicy; }
	/** @return Ordinary occupancy treatment. */
	EGridOccupancyPolicy GetOccupancyPolicy() const { return OccupancyPolicy; }
	/** Selects traversal channel 0-15. */
	void SetTraversalChannel(uint8 InChannel) { TraversalChannel = FMath::Min<uint8>(InChannel, 15); }
	/** @return Selected traversal channel. */
	uint8 GetTraversalChannel() const { return TraversalChannel; }
	/** Sets an authored reservation identity allowed to overlap itself. */
	void SetReservationId(const FGuid& InReservationId) { ReservationId = InReservationId; }
	/** @return Authored reservation identity. */
	const FGuid& GetReservationId() const { return ReservationId; }
	/** Selects live-agent following policy. */
	void SetDynamicAgentPolicy(EGridDynamicAgentPolicy InPolicy) { DynamicAgentPolicy = InPolicy; }
	/** @return Live-agent following policy. */
	EGridDynamicAgentPolicy GetDynamicAgentPolicy() const { return DynamicAgentPolicy; }
	/** Sets reactive occupancy look-ahead count. */
	void SetMinimumAgentLookAheadCells(int32 InCells) { MinimumAgentLookAheadCells = FMath::Max(1, InCells); }
	/** @return Reactive occupancy look-ahead count. */
	int32 GetMinimumAgentLookAheadCells() const { return MinimumAgentLookAheadCells; }
	/** Sets designer minimum for Reserved Corridor; runtime may extend it. */
	void SetReservedLookAheadCells(int32 InCells) { ReservedLookAheadCells = FMath::Max(1, InCells); }
	/** @return Designer minimum Reserved Corridor cell count. */
	int32 GetReservedLookAheadCells() const { return ReservedLookAheadCells; }
	/** Sets extra finite nonnegative capsule clearance. */
	void SetAdditionalAgentSeparation(float InSeparation) { AdditionalAgentSeparation = FMath::IsFinite(InSeparation) ? FMath::Max(0.0f, InSeparation) : 5.0f; }
	/** @return Extra capsule clearance in centimetres. */
	float GetAdditionalAgentSeparation() const { return AdditionalAgentSeparation; }
	/** Sets stationary-agent speed threshold in centimetres per second. */
	void SetStationaryAgentSpeedThreshold(float InThreshold) { StationaryAgentSpeedThreshold = FMath::IsFinite(InThreshold) ? FMath::Max(0.0f, InThreshold) : 5.0f; }
	/** @return Stationary-agent speed threshold. */
	float GetStationaryAgentSpeedThreshold() const { return StationaryAgentSpeedThreshold; }
	/** Sets continuous blocker delay before localized repath. */
	void SetDynamicAgentRepathDelay(float InDelay) { DynamicAgentRepathDelay = FMath::IsFinite(InDelay) ? FMath::Max(0.0f, InDelay) : 0.1f; }
	/** @return Dynamic-agent repath delay in seconds. */
	float GetDynamicAgentRepathDelay() const { return DynamicAgentRepathDelay; }
	/** Sets the querying Pawn occupancy identity ignored by pathfinding. */
	void SetIgnoredOccupancyOwnerId(const FGuid& InOwnerId) { IgnoredOccupancyOwnerId = InOwnerId; }
	/** @return Querying Pawn occupancy identity. */
	const FGuid& GetIgnoredOccupancyOwnerId() const { return IgnoredOccupancyOwnerId; }

private:
	/** Per-area multiplicative costs indexed by Unreal area ID. */
	TStaticArray<float, MaxAreaCount> AreaCosts;
	/** Per-area fixed entering costs indexed by Unreal area ID. */
	TStaticArray<float, MaxAreaCount> EnteringCosts;
	/** Traversal flags that must be present. */
	uint16 IncludeFlags = MAX_uint16;
	/** Traversal flags that reject a cell. */
	uint16 ExcludeFlags = 0;
	/** Query direction maximum. */
	EGridMovementMode MovementMode = EGridMovementMode::FourDirections;
	/** Query path-selection objective. */
	EGridPathOptimizationMode PathOptimizationMode = EGridPathOptimizationMode::Balanced;
	/** Turn penalty in equivalent orthogonal cells. */
	float BalancedTurnPenalty = 2.0f;
	/** Directional-state expansion budget. */
	uint32 MaxSearchStates = FNavigationQueryFilter::DefaultMaxSearchNodes;
	/** Ordinary occupancy treatment. */
	EGridOccupancyPolicy OccupancyPolicy = EGridOccupancyPolicy::Ignore;
	/** Live-agent following policy. */
	EGridDynamicAgentPolicy DynamicAgentPolicy = EGridDynamicAgentPolicy::Ignore;
	/** Reactive Yield look-ahead count. */
	int32 MinimumAgentLookAheadCells = 3;
	/** Designer minimum Reserved Corridor prefix length. */
	int32 ReservedLookAheadCells = 1;
	/** Extra capsule separation in centimetres. */
	float AdditionalAgentSeparation = 5.0f;
	/** Horizontal speed considered stationary. */
	float StationaryAgentSpeedThreshold = 5.0f;
	/** Continuous stationary delay before repath. */
	float DynamicAgentRepathDelay = 0.1f;
	/** Active traversal channel. */
	uint8 TraversalChannel = 0;
	/** Authored reservation identity allowed to overlap itself. */
	FGuid ReservationId;
	/** Occupancy identity of the querying Pawn. */
	FGuid IgnoredOccupancyOwnerId;
	/** Native filter backtracking flag. */
	bool bBacktracking = false;
	/** Query-side diagonal corner permission. */
	bool bAllowCornerCutting = false;
	/** Whether authored links may be traversed. */
	bool bAllowLinks = true;
};

/** Designer-facing filter for grid path queries. Unreal area overrides remain supported. */
UCLASS(Blueprintable, EditInlineNew)
class GRIDWORLD_API UGridNavigationQueryFilter : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UGridNavigationQueryFilter(const FObjectInitializer& ObjectInitializer);

	/** Query direction limit; a region may impose a stricter Four Directions maximum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World")
	EGridMovementMode MovementMode = EGridMovementMode::FourDirections;

	/** Chooses whether pathfinding prioritizes traversal cost, direction changes, or a weighted combination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Selection")
	EGridPathOptimizationMode PathOptimizationMode = EGridPathOptimizationMode::Balanced;

	/** Cost of one turn in equivalent orthogonal cells. Used only by Balanced mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Path Selection", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0", EditCondition = "PathOptimizationMode == EGridPathOptimizationMode::Balanced", EditConditionHides))
	float BalancedTurnPenalty = 2.0f;

	/** Maximum directional search states visited before an allowed partial path is returned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Grid World|Path Selection", meta = (ClampMin = "1", UIMin = "2048", UIMax = "262144", EditCondition = "PathOptimizationMode != EGridPathOptimizationMode::ShortestPath", EditConditionHides))
	int32 MaxSearchStates = 65536;

	/** Requests diagonal corner traversal; both the region and query must permit it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World")
	bool bAllowCornerCutting = false;

	/** Allows authored special links that match the selected traversal channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World")
	bool bAllowLinks = true;

	/** Determines whether ordinary occupancy is ignored, adds cost, or blocks cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World")
	EGridOccupancyPolicy OccupancyPolicy = EGridOccupancyPolicy::Ignore;

	/** Optional collision-free following of other GridWorld-controlled Pawns. RVO is not enabled or modified. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Dynamic Agents")
	EGridDynamicAgentPolicy DynamicAgentPolicy = EGridDynamicAgentPolicy::Ignore;

	/** Minimum number of upcoming cells inspected by the reactive Yield policies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Dynamic Agents", meta = (ClampMin = "1", UIMin = "1", UIMax = "16", EditCondition = "DynamicAgentPolicy == EGridDynamicAgentPolicy::Yield || DynamicAgentPolicy == EGridDynamicAgentPolicy::YieldThenRepath", EditConditionHides))
	int32 MinimumAgentLookAheadCells = 3;

	/**
	 * Minimum number of future path cells reserved by Reserved Corridor.
	 * Runtime safety may extend the prefix to cover capsule clearance and braking distance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Dynamic Agents", meta = (ClampMin = "1", UIMin = "1", UIMax = "16", EditCondition = "DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor", EditConditionHides))
	int32 ReservedLookAheadCells = 1;

	/** Extra horizontal clearance added to the sum of both agent radii. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Dynamic Agents", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm", EditCondition = "DynamicAgentPolicy != EGridDynamicAgentPolicy::Ignore", EditConditionHides))
	float AdditionalAgentSeparation = 5.0f;

	/** An occupant at or below this horizontal speed is considered stationary for repathing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Dynamic Agents", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm/s", EditCondition = "DynamicAgentPolicy == EGridDynamicAgentPolicy::YieldThenRepath || DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor", EditConditionHides))
	float StationaryAgentSpeedThreshold = 5.0f;

	/** Time a blocker must remain stationary before the current path is recalculated around it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Dynamic Agents", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s", EditCondition = "DynamicAgentPolicy == EGridDynamicAgentPolicy::YieldThenRepath || DynamicAgentPolicy == EGridDynamicAgentPolicy::ReservedCorridor", EditConditionHides))
	float DynamicAgentRepathDelay = 0.1f;

	/** Traversal channel index used to filter cell flags and explicit links. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World", meta = (ClampMin = "0", ClampMax = "15"))
	uint8 TraversalChannel = 0;

protected:
	/** Copies designer settings into Filter. Querier occupancy is ignored automatically when available. */
	virtual void InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const override;
};
