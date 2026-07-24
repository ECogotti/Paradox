// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridNavigationQueryFilter.h"

#include "Components/GridNavigationOccupancyComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/GridNavigationQueryContext.h"

FGridNavigationQueryFilterImpl::FGridNavigationQueryFilterImpl()
{
	Reset();
}

void FGridNavigationQueryFilterImpl::Reset()
{
	for (int32 Index = 0; Index < MaxAreaCount; ++Index)
	{
		AreaCosts[Index] = 1.0f;
		EnteringCosts[Index] = 0.0f;
	}
	IncludeFlags = MAX_uint16;
	ExcludeFlags = 0;
	MovementMode = EGridMovementMode::FourDirections;
	PathOptimizationMode = EGridPathOptimizationMode::Balanced;
	BalancedTurnPenalty = 2.0f;
	MaxSearchStates = FNavigationQueryFilter::DefaultMaxSearchNodes;
	OccupancyPolicy = EGridOccupancyPolicy::Ignore;
	DynamicAgentPolicy = EGridDynamicAgentPolicy::Ignore;
	MinimumAgentLookAheadCells = 3;
	ReservedLookAheadCells = 1;
	AdditionalAgentSeparation = 5.0f;
	StationaryAgentSpeedThreshold = 5.0f;
	DynamicAgentRepathDelay = 0.1f;
	TraversalChannel = 0;
	ReservationId.Invalidate();
	IgnoredOccupancyOwnerId.Invalidate();
	bBacktracking = false;
	bAllowCornerCutting = false;
	bAllowLinks = true;
}

void FGridNavigationQueryFilterImpl::SetAreaCost(uint8 AreaType, float Cost)
{
	if (AreaType < MaxAreaCount)
	{
		AreaCosts[AreaType] = FMath::Max(0.001f, Cost);
	}
}

void FGridNavigationQueryFilterImpl::SetFixedAreaEnteringCost(uint8 AreaType, float Cost)
{
	if (AreaType < MaxAreaCount)
	{
		EnteringCosts[AreaType] = FMath::Max(0.0f, Cost);
	}
}

void FGridNavigationQueryFilterImpl::SetExcludedArea(uint8 AreaType)
{
	if (AreaType < MaxAreaCount)
	{
		AreaCosts[AreaType] = BIG_NUMBER;
	}
}

void FGridNavigationQueryFilterImpl::SetAllAreaCosts(const float* CostArray, int32 Count)
{
	if (CostArray != nullptr)
	{
		FMemory::Memcpy(AreaCosts.GetData(), CostArray, FMath::Min(Count, MaxAreaCount) * sizeof(float));
	}
}

void FGridNavigationQueryFilterImpl::GetAllAreaCosts(float* CostArray, float* FixedCostArray, int32 Count) const
{
	const int32 CopyCount = FMath::Min(Count, MaxAreaCount);
	if (CostArray != nullptr)
	{
		FMemory::Memcpy(CostArray, AreaCosts.GetData(), CopyCount * sizeof(float));
	}
	if (FixedCostArray != nullptr)
	{
		FMemory::Memcpy(FixedCostArray, EnteringCosts.GetData(), CopyCount * sizeof(float));
	}
}

void FGridNavigationQueryFilterImpl::SetBacktrackingEnabled(bool bInBacktracking)
{
	bBacktracking = bInBacktracking;
}

bool FGridNavigationQueryFilterImpl::IsEqual(const INavigationQueryFilterInterface* Other) const
{
	const FGridNavigationQueryFilterImpl* OtherFilter = static_cast<const FGridNavigationQueryFilterImpl*>(Other);
	return OtherFilter != nullptr
		&& AreaCosts == OtherFilter->AreaCosts
		&& EnteringCosts == OtherFilter->EnteringCosts
		&& IncludeFlags == OtherFilter->IncludeFlags
		&& ExcludeFlags == OtherFilter->ExcludeFlags
		&& MovementMode == OtherFilter->MovementMode
		&& PathOptimizationMode == OtherFilter->PathOptimizationMode
		&& BalancedTurnPenalty == OtherFilter->BalancedTurnPenalty
		&& MaxSearchStates == OtherFilter->MaxSearchStates
		&& OccupancyPolicy == OtherFilter->OccupancyPolicy
		&& DynamicAgentPolicy == OtherFilter->DynamicAgentPolicy
		&& MinimumAgentLookAheadCells == OtherFilter->MinimumAgentLookAheadCells
		&& ReservedLookAheadCells == OtherFilter->ReservedLookAheadCells
		&& AdditionalAgentSeparation == OtherFilter->AdditionalAgentSeparation
		&& StationaryAgentSpeedThreshold == OtherFilter->StationaryAgentSpeedThreshold
		&& DynamicAgentRepathDelay == OtherFilter->DynamicAgentRepathDelay
		&& TraversalChannel == OtherFilter->TraversalChannel
		&& ReservationId == OtherFilter->ReservationId
		&& IgnoredOccupancyOwnerId == OtherFilter->IgnoredOccupancyOwnerId
		&& bBacktracking == OtherFilter->bBacktracking
		&& bAllowCornerCutting == OtherFilter->bAllowCornerCutting
		&& bAllowLinks == OtherFilter->bAllowLinks;
}

UGridNavigationQueryFilter::UGridNavigationQueryFilter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bInstantiateForQuerier = true;
}

float FGridNavigationQueryFilterImpl::GetAreaCost(uint8 AreaId) const
{
	return AreaId < MaxAreaCount ? AreaCosts[AreaId] : BIG_NUMBER;
}

float FGridNavigationQueryFilterImpl::GetEnteringCost(uint8 AreaId) const
{
	return AreaId < MaxAreaCount ? EnteringCosts[AreaId] : BIG_NUMBER;
}

void UGridNavigationQueryFilter::InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const
{
	Super::InitializeFilter(NavData, Querier, Filter);
	const uint32 EffectiveMaxSearchStates = PathOptimizationMode == EGridPathOptimizationMode::ShortestPath
		? Filter.GetMaxSearchNodes()
		: static_cast<uint32>(FMath::Max(1, MaxSearchStates));
	Filter.SetMaxSearchNodes(EffectiveMaxSearchStates);
	FGridNavigationQueryFilterImpl* GridFilter = static_cast<FGridNavigationQueryFilterImpl*>(Filter.GetImplementation());
	if (GridFilter != nullptr)
	{
		GridFilter->SetMovementMode(MovementMode);
		GridFilter->SetPathOptimizationMode(PathOptimizationMode);
		GridFilter->SetBalancedTurnPenalty(BalancedTurnPenalty);
		GridFilter->SetMaxSearchStates(EffectiveMaxSearchStates);
		GridFilter->SetAllowCornerCutting(bAllowCornerCutting);
		GridFilter->SetAllowLinks(bAllowLinks);
		GridFilter->SetOccupancyPolicy(OccupancyPolicy);
		GridFilter->SetDynamicAgentPolicy(DynamicAgentPolicy);
		GridFilter->SetMinimumAgentLookAheadCells(MinimumAgentLookAheadCells);
		GridFilter->SetReservedLookAheadCells(ReservedLookAheadCells);
		GridFilter->SetAdditionalAgentSeparation(AdditionalAgentSeparation);
		GridFilter->SetStationaryAgentSpeedThreshold(StationaryAgentSpeedThreshold);
		GridFilter->SetDynamicAgentRepathDelay(DynamicAgentRepathDelay);
		GridFilter->SetTraversalChannel(TraversalChannel);
		if (IsInGameThread() && Querier != nullptr && Querier->GetClass()->ImplementsInterface(UGridNavigationQueryContext::StaticClass()))
		{
			GridFilter->SetReservationId(IGridNavigationQueryContext::Execute_GetGridReservationId(Querier));
			GridFilter->SetTraversalChannel(IGridNavigationQueryContext::Execute_GetGridTraversalChannel(Querier));
		}
		if (IsInGameThread() && Querier != nullptr)
		{
			const APawn* QueryPawn = Cast<APawn>(Querier);
			if (const AController* Controller = Cast<AController>(Querier))
			{
				QueryPawn = Controller->GetPawn();
			}
			if (const UGridNavigationOccupancyComponent* Occupancy = QueryPawn != nullptr
				? UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(*QueryPawn)
				: nullptr)
			{
				GridFilter->SetIgnoredOccupancyOwnerId(Occupancy->OccupantId);
			}
		}
	}
}
