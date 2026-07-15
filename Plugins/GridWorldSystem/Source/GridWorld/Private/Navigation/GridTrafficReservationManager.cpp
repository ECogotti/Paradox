// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridTrafficReservationManager.h"

#include "GameFramework/Pawn.h"

namespace UE::GridWorld::Traffic
{
	/** Deterministic ordering used before publishing reservation arrays to query workers. */
	bool IsCellIdLess(const FGridCellId& Left, const FGridCellId& Right)
	{
		return Left.GridId != Right.GridId ? Left.GridId < Right.GridId : Left.Coord < Right.Coord;
	}

	/** Adds one record's current and future shapes to an immutable traffic snapshot. */
	void AppendAgentShapes(
		const FGridTrafficCellLocation& CurrentCell,
		TConstArrayView<FGridTrafficCellLocation> FutureCells,
		const FGuid& OwnerId,
		float AgentRadius,
		float AgentHeight,
		float AdditionalSeparation,
		bool bParking,
		FGridTrafficReservationSnapshot& OutSnapshot)
	{
		if (CurrentCell.CellId.IsValid())
		{
			OutSnapshot.Cells.Add({
				OwnerId,
				CurrentCell.CellId,
				CurrentCell.WorldCenter,
				AgentRadius,
				AgentHeight,
				AdditionalSeparation,
				false,
				bParking});
		}

		FGridTrafficCellLocation Previous = CurrentCell;
		for (const FGridTrafficCellLocation& FutureCell : FutureCells)
		{
			OutSnapshot.Cells.Add({
				OwnerId,
				FutureCell.CellId,
				FutureCell.WorldCenter,
				AgentRadius,
				AgentHeight,
				AdditionalSeparation,
				true,
				false});
			if (Previous.CellId.IsValid())
			{
				OutSnapshot.Segments.Add({
					OwnerId,
					Previous.CellId,
					FutureCell.CellId,
					Previous.WorldCenter,
					FutureCell.WorldCenter,
					AgentRadius,
					AgentHeight,
					AdditionalSeparation});
			}
			Previous = FutureCell;
		}
	}

	/** Tests a complete desired prefix against an immutable reservation view. */
	bool FindRequestConflict(
		const FGridTrafficCorridorRequest& Request,
		const FGridTrafficReservationSnapshot& Snapshot,
		FGuid& OutBlockingOwnerId,
		FGridCellId& OutBlockingCellId,
		FVector& OutBlockingCellCenter)
	{
		OutBlockingOwnerId.Invalidate();
		OutBlockingCellId = FGridCellId();
		OutBlockingCellCenter = FVector::ZeroVector;

		FGridTrafficCellLocation Previous = Request.CurrentCell;
		for (const FGridTrafficCellLocation& DesiredCell : Request.DesiredFutureCells)
		{
			FGridCellId BlockingCellId;
			if (Snapshot.ConflictsWithCell(
				DesiredCell.WorldCenter,
				Request.AgentRadius,
				Request.AgentHeight,
				Request.AdditionalSeparation,
				Request.OwnerId,
				&OutBlockingOwnerId,
				&BlockingCellId))
			{
				OutBlockingCellId = DesiredCell.CellId;
				OutBlockingCellCenter = DesiredCell.WorldCenter;
				return true;
			}
			if (Previous.CellId.IsValid()
				&& Snapshot.ConflictsWithSegment(
					Previous.WorldCenter,
					DesiredCell.WorldCenter,
					Request.AgentRadius,
					Request.AgentHeight,
					Request.AdditionalSeparation,
					Request.OwnerId,
					&OutBlockingOwnerId))
			{
				OutBlockingCellId = DesiredCell.CellId;
				OutBlockingCellCenter = DesiredCell.WorldCenter;
				return true;
			}
			Previous = DesiredCell;
		}
		return false;
	}
}

FGridTrafficReservationManager::FGridTrafficReservationManager()
{
	PublishSnapshot();
}

bool FGridTrafficReservationManager::UpdateCorridor(
	const FGridTrafficCorridorRequest& Request,
	FGridTrafficCorridorResult& OutResult)
{
	check(IsInGameThread());
	OutResult = FGridTrafficCorridorResult();
	if (!Request.OwnerId.IsValid()
		|| !Request.CurrentCell.CellId.IsValid()
		|| !Request.Source.IsValid()
		|| !Request.Pawn.IsValid()
		|| Request.DesiredFutureCells.ContainsByPredicate([](const FGridTrafficCellLocation& Cell)
		{
			return !Cell.CellId.IsValid() || Cell.WorldCenter.ContainsNaN();
		}))
	{
		return false;
	}

	bool bChanged = PruneInvalidState();
	if (bChanged)
	{
		// Conflict checks retain the published immutable view, so discard dead owners before using it.
		PublishSnapshot();
	}
	FAgentRecord& Record = AgentRecords.FindOrAdd(Request.OwnerId);
	const FAgentRecord PreviousRecord = Record;
	Record.OwnerId = Request.OwnerId;
	Record.Source = Request.Source;
	Record.Pawn = Request.Pawn;
	Record.CurrentCell = Request.CurrentCell;
	Record.AgentRadius = FMath::Max(0.0f, Request.AgentRadius);
	Record.AgentHeight = FMath::Max(0.0f, Request.AgentHeight);
	Record.AdditionalSeparation = FMath::Max(0.0f, Request.AdditionalSeparation);
	Record.bParking = false;
	Record.bRepathing = Request.bRepathing;
	if (const APawn* Pawn = Request.Pawn.Get())
	{
		const FVector CurrentOwnerLocation = Pawn->GetActorLocation();
		if (FVector::DistSquared(CurrentOwnerLocation, Record.DebugOwnerLocation) >= FMath::Square(5.0))
		{
			Record.DebugOwnerLocation = CurrentOwnerLocation;
		}
	}

	const FGridTrafficReservationSnapshotPtr Snapshot = GetSnapshot();
	FGuid BlockingOwnerId;
	FGridCellId BlockingCellId;
	FVector BlockingCellCenter = FVector::ZeroVector;
	bool bBlocked = Snapshot.IsValid()
		&& UE::GridWorld::Traffic::FindRequestConflict(
			Request,
			*Snapshot,
			BlockingOwnerId,
			BlockingCellId,
			BlockingCellCenter);
	if (!bBlocked)
	{
		bBlocked = IsBlockedByOlderWaiter(Request, Record, BlockingOwnerId);
		if (bBlocked && !Request.DesiredFutureCells.IsEmpty())
		{
			BlockingCellId = Request.DesiredFutureCells[0].CellId;
			BlockingCellCenter = Request.DesiredFutureCells[0].WorldCenter;
		}
	}

	if (bBlocked)
	{
		if (!Record.bWaiting)
		{
			Record.WaitOrder = NextWaitOrder++;
		}
		Record.bWaiting = true;
		Record.BlockingOwnerId = BlockingOwnerId;
		Record.BlockingCellId = BlockingCellId;
		Record.BlockingCellCenter = BlockingCellCenter;
		Record.RequestedFutureCells = Request.DesiredFutureCells;

		// A shifted request releases every old cell behind the newly crossed gate immediately.
		Record.GrantedFutureCells.RemoveAll([&Request](const FGridTrafficCellLocation& GrantedCell)
		{
			return !Request.DesiredFutureCells.ContainsByPredicate([&GrantedCell](const FGridTrafficCellLocation& DesiredCell)
			{
				return DesiredCell.CellId == GrantedCell.CellId;
			});
		});
		OutResult.Status = EGridTrafficReservationStatus::Waiting;
	}
	else
	{
		Record.GrantedFutureCells = Request.DesiredFutureCells;
		Record.RequestedFutureCells.Reset();
		Record.BlockingOwnerId.Invalidate();
		Record.BlockingCellId = FGridCellId();
		Record.BlockingCellCenter = FVector::ZeroVector;
		Record.WaitOrder = 0;
		Record.bWaiting = false;
		OutResult.Status = EGridTrafficReservationStatus::Granted;
	}

	bChanged |= !RecordsEqual(PreviousRecord, Record);
	if (bChanged)
	{
		PublishSnapshot();
	}
	OutResult.BlockingOwnerId = Record.BlockingOwnerId;
	OutResult.BlockingCellId = Record.BlockingCellId;
	OutResult.BlockingCellCenter = Record.BlockingCellCenter;
	OutResult.GrantedFutureCells = Record.GrantedFutureCells;
	OutResult.bStateChanged = bChanged;
	return true;
}

bool FGridTrafficReservationManager::ReleaseCorridor(
	const FGuid& OwnerId,
	const UObject* Source,
	bool bKeepCurrentCell)
{
	check(IsInGameThread());
	bool bChanged = PruneInvalidState();
	FAgentRecord* Record = AgentRecords.Find(OwnerId);
	if (Record == nullptr || (Source != nullptr && Record->Source.IsValid() && Record->Source.Get() != Source))
	{
		if (bChanged)
		{
			PublishSnapshot();
		}
		return bChanged;
	}

	if (!bKeepCurrentCell)
	{
		AgentRecords.Remove(OwnerId);
	}
	else
	{
		Record->GrantedFutureCells.Reset();
		Record->RequestedFutureCells.Reset();
		Record->BlockingOwnerId.Invalidate();
		Record->BlockingCellId = FGridCellId();
		Record->BlockingCellCenter = FVector::ZeroVector;
		Record->WaitOrder = 0;
		Record->bWaiting = false;
		Record->bRepathing = false;
		Record->bParking = true;
		Record->Source = Record->Pawn.Get();
	}
	PublishSnapshot();
	return true;
}

bool FGridTrafficReservationManager::CanClaimGoal(
	const FGridTrafficGoalClaimRequest& Request,
	FGuid* OutBlockingOwnerId) const
{
	check(IsInGameThread());
	if (OutBlockingOwnerId != nullptr)
	{
		OutBlockingOwnerId->Invalidate();
	}
	if (!Request.OwnerId.IsValid()
		|| !Request.Claimant.IsValid()
		|| !Request.Pawn.IsValid()
		|| !Request.GoalCell.CellId.IsValid())
	{
		return false;
	}

	if (const FGoalClaim* ExistingClaim = GoalClaims.Find(Request.GoalCell.CellId);
		ExistingClaim != nullptr && ExistingClaim->Claimant.Get() != Request.Claimant.Get())
	{
		if (OutBlockingOwnerId != nullptr)
		{
			*OutBlockingOwnerId = ExistingClaim->OwnerId;
		}
		return false;
	}

	const FGridTrafficReservationSnapshotPtr Snapshot = GetSnapshot();
	return !Snapshot.IsValid()
		|| !Snapshot->ConflictsWithCell(
			Request.GoalCell.WorldCenter,
			Request.AgentRadius,
			Request.AgentHeight,
			Request.AdditionalSeparation,
			Request.OwnerId,
			OutBlockingOwnerId);
}

bool FGridTrafficReservationManager::TryClaimGoal(
	const FGridTrafficGoalClaimRequest& Request,
	bool& OutStateChanged)
{
	check(IsInGameThread());
	OutStateChanged = PruneInvalidState();
	if (OutStateChanged)
	{
		// CanClaimGoal reads the immutable view shared with query workers.
		PublishSnapshot();
	}
	if (!CanClaimGoal(Request))
	{
		return false;
	}

	if (const FGoalClaim* Existing = GoalClaims.Find(Request.GoalCell.CellId);
		Existing != nullptr && Existing->Claimant.Get() == Request.Claimant.Get())
	{
		return true;
	}

	FGoalClaim& Claim = GoalClaims.Add(Request.GoalCell.CellId);
	Claim.OwnerId = Request.OwnerId;
	Claim.Claimant = Request.Claimant;
	Claim.Pawn = Request.Pawn;
	Claim.GoalCell = Request.GoalCell;
	Claim.AgentRadius = FMath::Max(0.0f, Request.AgentRadius);
	Claim.AgentHeight = FMath::Max(0.0f, Request.AgentHeight);
	Claim.AdditionalSeparation = FMath::Max(0.0f, Request.AdditionalSeparation);
	OutStateChanged = true;
	PublishSnapshot();
	return true;
}

bool FGridTrafficReservationManager::IsGoalClaimedByOther(
	const FGridCellId& CellId,
	const UObject* Claimant) const
{
	check(IsInGameThread());
	const FGoalClaim* Claim = GoalClaims.Find(CellId);
	return Claim != nullptr && Claim->Claimant.Get() != Claimant;
}

bool FGridTrafficReservationManager::ReleaseGoalClaims(const UObject* Claimant)
{
	check(IsInGameThread());
	bool bChanged = PruneInvalidState();
	for (auto It = GoalClaims.CreateIterator(); It; ++It)
	{
		if (It.Value().Claimant.Get() == Claimant)
		{
			It.RemoveCurrent();
			bChanged = true;
		}
	}
	if (bChanged)
	{
		PublishSnapshot();
	}
	return bChanged;
}

bool FGridTrafficReservationManager::CommitParking(const FGridTrafficGoalClaimRequest& Request)
{
	check(IsInGameThread());
	if (!Request.OwnerId.IsValid() || !Request.Pawn.IsValid() || !Request.GoalCell.CellId.IsValid())
	{
		return false;
	}

	PruneInvalidState();
	FAgentRecord& Record = AgentRecords.FindOrAdd(Request.OwnerId);
	Record.OwnerId = Request.OwnerId;
	Record.Source = Request.Pawn.Get();
	Record.Pawn = Request.Pawn;
	Record.CurrentCell = Request.GoalCell;
	Record.GrantedFutureCells.Reset();
	Record.RequestedFutureCells.Reset();
	Record.DebugOwnerLocation = Request.Pawn->GetActorLocation();
	Record.BlockingOwnerId.Invalidate();
	Record.BlockingCellId = FGridCellId();
	Record.BlockingCellCenter = FVector::ZeroVector;
	Record.AgentRadius = FMath::Max(0.0f, Request.AgentRadius);
	Record.AgentHeight = FMath::Max(0.0f, Request.AgentHeight);
	Record.AdditionalSeparation = FMath::Max(0.0f, Request.AdditionalSeparation);
	Record.WaitOrder = 0;
	Record.bWaiting = false;
	Record.bRepathing = false;
	Record.bParking = true;

	for (auto It = GoalClaims.CreateIterator(); It; ++It)
	{
		if (It.Value().OwnerId == Request.OwnerId)
		{
			It.RemoveCurrent();
		}
	}
	PublishSnapshot();
	return true;
}

bool FGridTrafficReservationManager::RemoveOwner(const FGuid& OwnerId)
{
	check(IsInGameThread());
	bool bChanged = AgentRecords.Remove(OwnerId) > 0;
	for (auto It = GoalClaims.CreateIterator(); It; ++It)
	{
		if (It.Value().OwnerId == OwnerId)
		{
			It.RemoveCurrent();
			bChanged = true;
		}
	}
	if (bChanged)
	{
		PublishSnapshot();
	}
	return bChanged;
}

FGridTrafficReservationSnapshotPtr FGridTrafficReservationManager::GetSnapshot() const
{
	FReadScopeLock Lock(SnapshotLock);
	return PublishedSnapshot;
}

bool FGridTrafficReservationManager::Reset()
{
	check(IsInGameThread());
	const bool bChanged = !AgentRecords.IsEmpty() || !GoalClaims.IsEmpty();
	AgentRecords.Reset();
	GoalClaims.Reset();
	NextWaitOrder = 1;
	if (bChanged)
	{
		PublishSnapshot();
	}
	return bChanged;
}

bool FGridTrafficReservationManager::PruneInvalidState()
{
	check(IsInGameThread());
	bool bChanged = false;
	for (auto It = AgentRecords.CreateIterator(); It; ++It)
	{
		if (!It.Value().Pawn.IsValid())
		{
			It.RemoveCurrent();
			bChanged = true;
		}
	}
	for (auto It = GoalClaims.CreateIterator(); It; ++It)
	{
		if (!It.Value().Claimant.IsValid() || !It.Value().Pawn.IsValid())
		{
			It.RemoveCurrent();
			bChanged = true;
		}
	}
	return bChanged;
}

void FGridTrafficReservationManager::PublishSnapshot()
{
	check(IsInGameThread());
	TSharedRef<FGridTrafficReservationSnapshot, ESPMode::ThreadSafe> NewSnapshot = MakeShared<FGridTrafficReservationSnapshot, ESPMode::ThreadSafe>();
	NewSnapshot->Revision = NextRevision++;

	TArray<FGuid> OwnerIds;
	AgentRecords.GenerateKeyArray(OwnerIds);
	OwnerIds.Sort();
	for (const FGuid& OwnerId : OwnerIds)
	{
		const FAgentRecord& Record = AgentRecords.FindChecked(OwnerId);
		UE::GridWorld::Traffic::AppendAgentShapes(
			Record.CurrentCell,
			Record.GrantedFutureCells,
			Record.OwnerId,
			Record.AgentRadius,
			Record.AgentHeight,
			Record.AdditionalSeparation,
			Record.bParking,
			*NewSnapshot);

		if (!Record.GrantedFutureCells.IsEmpty() || Record.bWaiting)
		{
			FGridTrafficReservationDebugData& Debug = NewSnapshot->DebugEntries.AddDefaulted_GetRef();
			Debug.OwnerId = Record.OwnerId;
			Debug.OwnerLocation = Record.DebugOwnerLocation;
			Debug.ReservedFutureCells = Record.GrantedFutureCells;
			Debug.WaitingFutureCells = Record.RequestedFutureCells;
			Debug.BlockingCellCenter = Record.BlockingCellCenter;
			Debug.bWaiting = Record.bWaiting;
			Debug.bRepathing = Record.bRepathing;
		}
	}

	TArray<FGridCellId> GoalIds;
	GoalClaims.GenerateKeyArray(GoalIds);
	GoalIds.Sort([](const FGridCellId& Left, const FGridCellId& Right)
	{
		return UE::GridWorld::Traffic::IsCellIdLess(Left, Right);
	});
	for (const FGridCellId& GoalId : GoalIds)
	{
		const FGoalClaim& Claim = GoalClaims.FindChecked(GoalId);
		NewSnapshot->Cells.Add({
			Claim.OwnerId,
			Claim.GoalCell.CellId,
			Claim.GoalCell.WorldCenter,
			Claim.AgentRadius,
			Claim.AgentHeight,
			Claim.AdditionalSeparation,
			false,
			true});
	}

	{
		FWriteScopeLock Lock(SnapshotLock);
		PublishedSnapshot = NewSnapshot;
	}
}

bool FGridTrafficReservationManager::IsBlockedByOlderWaiter(
	const FGridTrafficCorridorRequest& Request,
	const FAgentRecord& RequestRecord,
	FGuid& OutBlockingOwnerId) const
{
	for (const TPair<FGuid, FAgentRecord>& Pair : AgentRecords)
	{
		const FAgentRecord& Other = Pair.Value;
		if (Other.OwnerId == Request.OwnerId
			|| !Other.bWaiting
			|| Other.RequestedFutureCells.IsEmpty()
			|| (RequestRecord.WaitOrder > 0 && Other.WaitOrder >= RequestRecord.WaitOrder))
		{
			continue;
		}

		FGridTrafficReservationSnapshot OlderRequestSnapshot;
		UE::GridWorld::Traffic::AppendAgentShapes(
			Other.CurrentCell,
			Other.RequestedFutureCells,
			Other.OwnerId,
			Other.AgentRadius,
			Other.AgentHeight,
			Other.AdditionalSeparation,
			false,
			OlderRequestSnapshot);
		FGuid IgnoredConflictOwner;
		FGridCellId IgnoredConflictCell;
		FVector IgnoredConflictCenter;
		if (UE::GridWorld::Traffic::FindRequestConflict(
			Request,
			OlderRequestSnapshot,
			IgnoredConflictOwner,
			IgnoredConflictCell,
			IgnoredConflictCenter))
		{
			OutBlockingOwnerId = Other.OwnerId;
			return true;
		}
	}
	return false;
}

bool FGridTrafficReservationManager::RecordsEqual(const FAgentRecord& Left, const FAgentRecord& Right)
{
	return Left.OwnerId == Right.OwnerId
		&& Left.Source == Right.Source
		&& Left.Pawn == Right.Pawn
		&& Left.CurrentCell == Right.CurrentCell
		&& Left.GrantedFutureCells == Right.GrantedFutureCells
		&& Left.RequestedFutureCells == Right.RequestedFutureCells
		&& Left.DebugOwnerLocation.Equals(Right.DebugOwnerLocation)
		&& Left.BlockingOwnerId == Right.BlockingOwnerId
		&& Left.BlockingCellId == Right.BlockingCellId
		&& Left.BlockingCellCenter.Equals(Right.BlockingCellCenter)
		&& FMath::IsNearlyEqual(Left.AgentRadius, Right.AgentRadius)
		&& FMath::IsNearlyEqual(Left.AgentHeight, Right.AgentHeight)
		&& FMath::IsNearlyEqual(Left.AdditionalSeparation, Right.AdditionalSeparation)
		&& Left.WaitOrder == Right.WaitOrder
		&& Left.bWaiting == Right.bWaiting
		&& Left.bRepathing == Right.bRepathing
		&& Left.bParking == Right.bParking;
}
