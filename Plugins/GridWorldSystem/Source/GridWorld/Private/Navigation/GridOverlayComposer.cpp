// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridOverlayComposer.h"

#include "Components/GridNavigationLinkComponent.h"
#include "Components/GridNavigationModifierComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

namespace UE::GridWorld::Private
{
	template <typename ComponentType>
	void GatherComponents(UWorld& World, TArray<ComponentType*>& OutComponents)
	{
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			TInlineComponentArray<ComponentType*> ActorComponents(*It);
			for (ComponentType* Component : ActorComponents)
			{
				if (IsValid(Component)
					&& Component->IsRegistered()
					&& IGridNavigationContributor::Execute_IsGridContributionEnabled(Component))
				{
					OutComponents.Add(Component);
				}
			}
		}
	}

	bool IsGuidPreferred(const FGuid& Left, const FGuid& Right)
	{
		return Left < Right;
	}

	int32 FindClosestCell(const FGridWorldSnapshot& Snapshot, const FVector& Location)
	{
		int32 BestIndex = INDEX_NONE;
		double BestDistanceSquared = TNumericLimits<double>::Max();
		for (int32 CellIndex = 0; CellIndex < Snapshot.Cells.Num(); ++CellIndex)
		{
			const FGridCellData& Cell = Snapshot.Cells[CellIndex];
			if (!Cell.bWalkable)
			{
				continue;
			}
			const double DistanceSquared = FVector::DistSquared(Location, Cell.WorldCenter);
			if (DistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestIndex = CellIndex;
			}
		}
		return BestIndex;
	}
}

TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> FGridOverlayComposer::Compose(
	UWorld& World,
	const FGridWorldSnapshot& BaseTopology,
	const FGridWorldSnapshot* PreviousSnapshot,
	bool bOccupancyOnly,
	FGridChangeSet& OutChangeSet)
{
	check(IsInGameThread());
	const FGridWorldSnapshot& CompositionSource = bOccupancyOnly && PreviousSnapshot != nullptr ? *PreviousSnapshot : BaseTopology;
	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Result = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>(CompositionSource);
	OutChangeSet = FGridChangeSet();
	OutChangeSet.PreviousRevisions = PreviousSnapshot != nullptr ? PreviousSnapshot->Revisions : BaseTopology.Revisions;
	Result->Revisions = OutChangeSet.PreviousRevisions;
	Result->Revisions.Topology = BaseTopology.Revisions.Topology;
	if (bOccupancyOnly)
	{
		++Result->Revisions.Occupancy;
	}
	else
	{
		++Result->Revisions.Traversal;
		++Result->Revisions.Occupancy;
	}

	TArray<UGridNavigationModifierComponent*> Modifiers;
	TArray<UGridNavigationOccupancyComponent*> Occupants;
	TArray<UGridNavigationLinkComponent*> Links;
	UE::GridWorld::Private::GatherComponents(World, Modifiers);
	UE::GridWorld::Private::GatherComponents(World, Occupants);
	UE::GridWorld::Private::GatherComponents(World, Links);
	if (!bOccupancyOnly)
	{
		for (UGridNavigationOccupancyComponent* Occupant : Occupants)
		{
			// Topology may have been replaced since the last component transform. Refresh without
			// notifying NavData recursively so Pawn owner cells participate in this publication.
			Occupant->UpdateCachedOccupiedCells();
		}
	}
	Modifiers.Sort([](const UGridNavigationModifierComponent& Left, const UGridNavigationModifierComponent& Right)
	{
		return Left.Priority != Right.Priority ? Left.Priority < Right.Priority : Left.ModifierId < Right.ModifierId;
	});
	Occupants.Sort([](const UGridNavigationOccupancyComponent& Left, const UGridNavigationOccupancyComponent& Right)
	{
		return Left.OccupantId < Right.OccupantId;
	});
	Links.Sort([](const UGridNavigationLinkComponent& Left, const UGridNavigationLinkComponent& Right)
	{
		return Left.LinkId < Right.LinkId;
	});

	for (int32 CellIndex = 0; CellIndex < Result->Cells.Num(); ++CellIndex)
	{
		FGridCellData& Cell = Result->Cells[CellIndex];
		const FGridCellData& BaseCell = BaseTopology.Cells[CellIndex];
		if (!bOccupancyOnly)
		{
			Cell.bWalkable = BaseCell.bWalkable;
			Cell.TraversalCost = BaseCell.TraversalCost;
		}
		Cell.bOccupied = false;
		Cell.bOccupancyBlocks = false;
		Cell.OccupancyCost = 0;
		Cell.OccupancyOwners.Reset();
		Cell.ReservationOwners.Reset();

		if (!bOccupancyOnly)
		{
			bool bHasBlocker = false;
			bool bRequestsUnblock = false;
			int64 AdditiveCost = 0;
			double MultiplicativeCost = 1.0;
			const UGridNavigationModifierComponent* WinningOverride = nullptr;
			for (const UGridNavigationModifierComponent* Modifier : Modifiers)
			{
				if (!Modifier->AffectsPoint(Cell.WorldCenter))
				{
					continue;
				}
				bHasBlocker |= Modifier->bBlockCells;
				bRequestsUnblock |= Modifier->bRequestAuthoredUnblock;
				AdditiveCost += Modifier->AdditiveCost;
				MultiplicativeCost *= FMath::Max(0.001, Modifier->CostMultiplier);
				if (Modifier->bUseCostOverride
					&& (WinningOverride == nullptr
						|| Modifier->Priority > WinningOverride->Priority
						|| (Modifier->Priority == WinningOverride->Priority && Modifier->ModifierId < WinningOverride->ModifierId)))
				{
					WinningOverride = Modifier;
				}
			}

			const bool bAuthoredWalkable = BaseCell.bAuthoredBlocked
				? (BaseCell.bAuthoredBlockCanBeRemoved && bRequestsUnblock)
				: BaseCell.bWalkable;
			Cell.bWalkable = bAuthoredWalkable && !bHasBlocker;
			const int64 AddedThenMultiplied = FMath::RoundToInt64((BaseCell.TraversalCost + AdditiveCost) * MultiplicativeCost);
			Cell.TraversalCost = WinningOverride != nullptr
				? FMath::Max(1, WinningOverride->OverrideCost)
				: static_cast<int32>(FMath::Clamp<int64>(AddedThenMultiplied, 1, MAX_int32));
		}

		for (const UGridNavigationOccupancyComponent* Occupant : Occupants)
		{
			if (Occupant->AffectsCell(Cell.Id, Cell.WorldCenter))
			{
				Cell.bOccupied = true;
				Cell.bOccupancyBlocks |= Occupant->bBlocksWhenConsidered;
				Cell.OccupancyCost = FMath::Clamp<int64>(static_cast<int64>(Cell.OccupancyCost) + Occupant->AdditionalCost, 0, MAX_int32);
				Cell.OccupancyOwners.AddUnique(Occupant->OccupantId);
				if (Occupant->bIsReservation)
				{
					Cell.ReservationOwners.AddUnique(Occupant->OccupantId);
				}
			}
		}

		const FGridCellData* PreviousCell = PreviousSnapshot != nullptr && PreviousSnapshot->Cells.IsValidIndex(CellIndex)
			? &PreviousSnapshot->Cells[CellIndex]
			: nullptr;
		const bool bTraversalChanged = PreviousCell == nullptr
			|| Cell.bWalkable != PreviousCell->bWalkable
			|| Cell.TraversalCost != PreviousCell->TraversalCost
			|| Cell.AreaId != PreviousCell->AreaId
			|| Cell.TraversalFlags != PreviousCell->TraversalFlags
			|| Cell.TraversalChannels != PreviousCell->TraversalChannels;
		const bool bBlockingOccupancyChanged = PreviousCell == nullptr
			|| Cell.bOccupancyBlocks != PreviousCell->bOccupancyBlocks
			|| Cell.ReservationOwners != PreviousCell->ReservationOwners;
		const bool bOccupancyCostChanged = PreviousCell == nullptr
			|| Cell.OccupancyCost != PreviousCell->OccupancyCost;
		const bool bOccupancyOwnersChanged = PreviousCell == nullptr
			|| Cell.bOccupied != PreviousCell->bOccupied
			|| Cell.OccupancyOwners != PreviousCell->OccupancyOwners;
		if (bTraversalChanged)
		{
			OutChangeSet.ChangedTraversalCells.Add(Cell.Id);
		}
		if (bBlockingOccupancyChanged)
		{
			OutChangeSet.ChangedBlockingOccupancyCells.Add(Cell.Id);
		}
		if (bOccupancyCostChanged)
		{
			OutChangeSet.ChangedOccupancyCostCells.Add(Cell.Id);
		}
		if (bOccupancyOwnersChanged)
		{
			OutChangeSet.ChangedOccupancyOwnerCells.Add(Cell.Id);
		}
		if (bTraversalChanged
			|| bBlockingOccupancyChanged
			|| bOccupancyCostChanged
			|| bOccupancyOwnersChanged)
		{
			OutChangeSet.ChangedCells.Add(Cell.Id);
		}
	}

	if (!bOccupancyOnly)
	{
		Result->Links.Reset();
		for (int32 LinkIndex = 0; LinkIndex < Links.Num();)
		{
			const FGuid LinkId = Links[LinkIndex]->LinkId;
			const UGridNavigationLinkComponent* SelectedLink = Links[LinkIndex];
			bool bAnyDisabled = false;
			int32 GroupEnd = LinkIndex;
			while (GroupEnd < Links.Num() && Links[GroupEnd]->LinkId == LinkId)
			{
				bAnyDisabled |= !Links[GroupEnd]->bEnabled;
				++GroupEnd;
			}
			if (!bAnyDisabled)
			{
				const int32 FromCellIndex = UE::GridWorld::Private::FindClosestCell(BaseTopology, SelectedLink->GetStartWorldLocation());
				const int32 ToCellIndex = UE::GridWorld::Private::FindClosestCell(BaseTopology, SelectedLink->GetEndWorldLocation());
				if (FromCellIndex != INDEX_NONE && ToCellIndex != INDEX_NONE && FromCellIndex != ToCellIndex)
				{
					FGridLinkData& Link = Result->Links.AddDefaulted_GetRef();
					Link.LinkId = LinkId;
					Link.FromCellIndex = FromCellIndex;
					Link.ToCellIndex = ToCellIndex;
					Link.TraversalCost = FMath::Max(1, SelectedLink->TraversalCost);
					Link.TraversalChannels = static_cast<uint16>(SelectedLink->TraversalChannels & MAX_uint16);
					Link.bBidirectional = SelectedLink->bBidirectional;
					Link.bEnabled = true;
				}
			}
			LinkIndex = GroupEnd;
		}

		TSet<FGuid> PreviousLinkIds;
		if (PreviousSnapshot != nullptr)
		{
			for (const FGridLinkData& Link : PreviousSnapshot->Links)
			{
				PreviousLinkIds.Add(Link.LinkId);
			}
		}
		TSet<FGuid> NewLinkIds;
		for (const FGridLinkData& Link : Result->Links)
		{
			NewLinkIds.Add(Link.LinkId);
			if (!PreviousLinkIds.Contains(Link.LinkId))
			{
				OutChangeSet.ChangedLinks.Add(Link.LinkId);
			}
		}
		for (const FGuid& PreviousLinkId : PreviousLinkIds)
		{
			if (!NewLinkIds.Contains(PreviousLinkId))
			{
				OutChangeSet.ChangedLinks.Add(PreviousLinkId);
			}
		}
	}
	OutChangeSet.NewRevisions = Result->Revisions;
	return Result;
}
