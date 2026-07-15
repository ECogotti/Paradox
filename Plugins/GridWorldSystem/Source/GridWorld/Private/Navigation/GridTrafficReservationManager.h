// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Navigation/GridTrafficReservation.h"

/** Game-Thread authority that publishes immutable GridWorld traffic reservation views. */
class FGridTrafficReservationManager
{
public:
	FGridTrafficReservationManager();

	/**
	 * Attempts to replace one owner's rolling future corridor.
	 * @param Request Complete desired safety prefix and owner geometry.
	 * @param OutResult Receives the grant or deterministic blocking owner.
	 * @return True when the request was valid, including a valid Waiting result.
	 */
	bool UpdateCorridor(const FGridTrafficCorridorRequest& Request, FGridTrafficCorridorResult& OutResult);

	/**
	 * Releases future cells owned by Source.
	 * @param OwnerId Stable owner whose corridor should be changed.
	 * @param Source Expected source; a mismatched stale caller cannot release a replacement request.
	 * @param bKeepCurrentCell True to retain the current cell as parking protection.
	 * @return True when authoritative state changed.
	 */
	bool ReleaseCorridor(const FGuid& OwnerId, const UObject* Source, bool bKeepCurrentCell);

	/**
	 * Tests whether a destination can be claimed without mutating the registry.
	 * @param Request Candidate goal and agent geometry.
	 * @param OutBlockingOwnerId Optional owner preventing the claim.
	 * @return True when no other current, future, goal or parking shape conflicts.
	 */
	bool CanClaimGoal(const FGridTrafficGoalClaimRequest& Request, FGuid* OutBlockingOwnerId = nullptr) const;

	/**
	 * Atomically claims a separated destination for Move To Grid Cell.
	 * @param Request Candidate goal and task ownership.
	 * @param OutStateChanged Receives whether a new claim was published.
	 * @return True when the claim belongs to Request.Claimant after the call.
	 */
	bool TryClaimGoal(const FGridTrafficGoalClaimRequest& Request, bool& OutStateChanged);

	/** Returns whether CellId is claimed by a task other than Claimant. */
	bool IsGoalClaimedByOther(const FGridCellId& CellId, const UObject* Claimant) const;

	/** Releases all goal claims owned by Claimant and returns whether state changed. */
	bool ReleaseGoalClaims(const UObject* Claimant);

	/**
	 * Converts a reached destination into parking protection tied to the Pawn lifetime.
	 * @param Request Reached cell and owner geometry.
	 * @return True when the published parking state changed.
	 */
	bool CommitParking(const FGridTrafficGoalClaimRequest& Request);

	/** Removes every corridor, claim and parking shape owned by OwnerId. */
	bool RemoveOwner(const FGuid& OwnerId);

	/** Returns the current immutable runtime-only view. */
	FGridTrafficReservationSnapshotPtr GetSnapshot() const;

	/** Clears all runtime reservations during world teardown. */
	bool Reset();

private:
	struct FAgentRecord
	{
		/** Stable occupancy identity and deterministic arbitration key. */
		FGuid OwnerId;
		/** Object allowed to replace/release the active corridor. */
		TWeakObjectPtr<UObject> Source;
		/** Pawn lifetime and debug-location source. */
		TWeakObjectPtr<APawn> Pawn;
		/** Most recently crossed logical cell. */
		FGridTrafficCellLocation CurrentCell;
		/** Future cells currently protected from subtraction. */
		TArray<FGridTrafficCellLocation> GrantedFutureCells;
		/** Complete desired prefix retained while waiting. */
		TArray<FGridTrafficCellLocation> RequestedFutureCells;
		/** Last Pawn position copied into render-thread-safe debug data. */
		FVector DebugOwnerLocation = FVector::ZeroVector;
		/** Owner responsible for the current wait. */
		FGuid BlockingOwnerId;
		/** Requested cell where the first conflict was found. */
		FGridCellId BlockingCellId;
		/** World center used by conflict debug rendering. */
		FVector BlockingCellCenter = FVector::ZeroVector;
		/** Owner horizontal capsule radius. */
		float AgentRadius = 42.0f;
		/** Owner full capsule height. */
		float AgentHeight = 192.0f;
		/** Extra horizontal clearance required by this owner. */
		float AdditionalSeparation = 5.0f;
		/** Monotonic first-wait ordering used before OwnerId tie breaks. */
		uint64 WaitOrder = 0;
		/** True while RequestedFutureCells cannot be granted. */
		bool bWaiting = false;
		/** True while the follower is obtaining a replacement path. */
		bool bRepathing = false;
		/** True when only CurrentCell protects a reached destination. */
		bool bParking = false;
	};

	struct FGoalClaim
	{
		/** Occupancy identity of the future occupant. */
		FGuid OwnerId;
		/** Task lifetime that owns the temporary claim. */
		TWeakObjectPtr<UObject> Claimant;
		/** Pawn expected to occupy GoalCell. */
		TWeakObjectPtr<APawn> Pawn;
		/** Atomically selected destination. */
		FGridTrafficCellLocation GoalCell;
		/** Claiming Pawn horizontal capsule radius. */
		float AgentRadius = 42.0f;
		/** Claiming Pawn full capsule height. */
		float AgentHeight = 192.0f;
		/** Extra horizontal goal clearance. */
		float AdditionalSeparation = 5.0f;
	};

	/** Mutable Game-Thread owner records keyed by occupancy identity. */
	TMap<FGuid, FAgentRecord> AgentRecords;
	/** Temporary destination claims keyed by persistent cell identity. */
	TMap<FGridCellId, FGoalClaim> GoalClaims;
	/** Next monotonic fairness order for a new continuous wait episode. */
	uint64 NextWaitOrder = 1;
	/** Next immutable runtime snapshot revision. */
	int64 NextRevision = 1;
	/** Protects only immutable shared-pointer exchange. */
	mutable FRWLock SnapshotLock;
	/** Snapshot retained by A*, waiting tasks, and the scene proxy. */
	FGridTrafficReservationSnapshotPtr PublishedSnapshot;

	/** Removes dead Pawn/task records. @return True when mutable state changed. */
	bool PruneInvalidState();
	/** Deterministically rebuilds and atomically publishes the immutable view. */
	void PublishSnapshot();
	/** Applies wait-age priority without revoking already granted reservations. */
	bool IsBlockedByOlderWaiter(const FGridTrafficCorridorRequest& Request, const FAgentRecord& RequestRecord, FGuid& OutBlockingOwnerId) const;
	/** @return True when publishing Left or Right would produce equivalent observable state. */
	static bool RecordsEqual(const FAgentRecord& Left, const FAgentRecord& Right);
};
