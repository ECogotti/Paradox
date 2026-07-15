// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GridWorldTypes.h"

class APawn;

/** Outcome of an attempt to reserve the short GridWorld corridor in front of one agent. */
enum class EGridTrafficReservationStatus : uint8
{
	/** Request failed validation and did not mutate the registry. */
	Invalid,
	/** Complete requested future prefix belongs to the owner. */
	Granted,
	/** An existing owner or older waiter prevents safe movement. */
	Waiting
};

/** Immutable world-space description of one cell protected by a traffic owner. */
struct GRIDWORLD_API FGridTrafficReservedCell
{
	/** Stable identity of the agent that owns this protection. */
	FGuid OwnerId;

	/** Persistent identity of the protected GridWorld cell. */
	FGridCellId CellId;

	/** Generated floor position used for capsule-clearance comparisons. */
	FVector WorldCenter = FVector::ZeroVector;

	/** Horizontal collision radius of the owner, in centimetres. */
	float AgentRadius = 42.0f;

	/** Full collision height of the owner, in centimetres. */
	float AgentHeight = 192.0f;

	/** Additional clearance requested by the owner, in centimetres. */
	float AdditionalSeparation = 5.0f;

	/** True only for cells ahead of the owner on its active path. */
	bool bFutureCorridor = false;

	/** True when this shape protects a destination claim or a parked Pawn. */
	bool bGoalOrParking = false;
};

/** Immutable swept-capsule approximation of one protected path transition. */
struct GRIDWORLD_API FGridTrafficReservedSegment
{
	/** Stable identity of the agent that owns this transition. */
	FGuid OwnerId;

	/** Source cell of the protected transition. */
	FGridCellId FromCell;

	/** Destination cell of the protected transition. */
	FGridCellId ToCell;

	/** World-space floor position at the start of the transition. */
	FVector Start = FVector::ZeroVector;

	/** World-space floor position at the end of the transition. */
	FVector End = FVector::ZeroVector;

	/** Horizontal collision radius of the owner, in centimetres. */
	float AgentRadius = 42.0f;

	/** Full collision height of the owner, in centimetres. */
	float AgentHeight = 192.0f;

	/** Additional clearance requested by the owner, in centimetres. */
	float AdditionalSeparation = 5.0f;
};

/** One cell and its generated floor position inside a reservation request. */
struct GRIDWORLD_API FGridTrafficCellLocation
{
	/** Persistent identity of the requested cell. */
	FGridCellId CellId;

	/** World-space center of the requested cell. */
	FVector WorldCenter = FVector::ZeroVector;

	/** @return True when identity and captured world center match. */
	bool operator==(const FGridTrafficCellLocation& Other) const
	{
		return CellId == Other.CellId && WorldCenter.Equals(Other.WorldCenter);
	}
};

/** Game-Thread request used by a GridWorld path follower to roll its protected corridor. */
struct GRIDWORLD_API FGridTrafficCorridorRequest
{
	/** Occupancy identity of the moving Pawn. */
	FGuid OwnerId;

	/** Object responsible for releasing this active corridor. */
	TWeakObjectPtr<UObject> Source;

	/** Pawn represented by OwnerId. Used only on the Game Thread for lifecycle and debug. */
	TWeakObjectPtr<APawn> Pawn;

	/** Cell whose center gate was most recently crossed. */
	FGridTrafficCellLocation CurrentCell;

	/** Ordered short path prefix that must be granted before movement continues. */
	TArray<FGridTrafficCellLocation> DesiredFutureCells;

	/** Horizontal collision radius of the requesting Pawn, in centimetres. */
	float AgentRadius = 42.0f;

	/** Full collision height of the requesting Pawn, in centimetres. */
	float AgentHeight = 192.0f;

	/** Extra clearance added to the sum of both agent radii. */
	float AdditionalSeparation = 5.0f;

	/** True while the path follower is replacing the blocked path. */
	bool bRepathing = false;
};

/** Observable result returned after one corridor request. */
struct GRIDWORLD_API FGridTrafficCorridorResult
{
	/** Whether the complete requested safety horizon was granted. */
	EGridTrafficReservationStatus Status = EGridTrafficReservationStatus::Invalid;

	/** Owner preventing the request, or an invalid GUID when no owner blocks it. */
	FGuid BlockingOwnerId;

	/** First requested cell involved in the conflict, when available. */
	FGridCellId BlockingCellId;

	/** World-space center of BlockingCellId. */
	FVector BlockingCellCenter = FVector::ZeroVector;

	/** Cells that remain owned after the update, excluding the current occupied cell. */
	TArray<FGridTrafficCellLocation> GrantedFutureCells;

	/** True when the authoritative registry changed and observers must be notified. */
	bool bStateChanged = false;
};

/** Request used by Move To Grid Cell to atomically claim a separated destination. */
struct GRIDWORLD_API FGridTrafficGoalClaimRequest
{
	/** Occupancy identity of the Pawn that will use the destination. */
	FGuid OwnerId;

	/** Task that owns the temporary claim. */
	TWeakObjectPtr<UObject> Claimant;

	/** Pawn represented by OwnerId. */
	TWeakObjectPtr<APawn> Pawn;

	/** Destination cell to claim. */
	FGridTrafficCellLocation GoalCell;

	/** Horizontal collision radius of the Pawn, in centimetres. */
	float AgentRadius = 42.0f;

	/** Full collision height of the Pawn, in centimetres. */
	float AgentHeight = 192.0f;

	/** Extra clearance required around the destination. */
	float AdditionalSeparation = 5.0f;
};

/** Render-thread-safe debug record for one active traffic owner. */
struct GRIDWORLD_API FGridTrafficReservationDebugData
{
	/** Occupancy identity displayed next to the owner connection line. */
	FGuid OwnerId;

	/** Last Game-Thread location captured for the owning Pawn. */
	FVector OwnerLocation = FVector::ZeroVector;

	/** Granted future cells rendered in turquoise. */
	TArray<FGridTrafficCellLocation> ReservedFutureCells;

	/** Requested but not granted cells rendered in orange. */
	TArray<FGridTrafficCellLocation> WaitingFutureCells;

	/** First active conflict rendered in red. */
	FVector BlockingCellCenter = FVector::ZeroVector;

	/** True while the owner is waiting for a reservation. */
	bool bWaiting = false;

	/** True while the owner is requesting a replacement path. */
	bool bRepathing = false;
};

/**
 * Immutable traffic view retained by asynchronous GridWorld queries.
 * It contains no UObject references and is never serialized with generated topology.
 */
struct GRIDWORLD_API FGridTrafficReservationSnapshot
{
	/** Monotonic runtime-only revision. */
	int64 Revision = 0;

	/** Current, future, goal and parking protection owned by all registered agents. */
	TArray<FGridTrafficReservedCell> Cells;

	/** Protected transitions used to reject edge swaps and crossing segments. */
	TArray<FGridTrafficReservedSegment> Segments;

	/** Debug-only copies of active short-corridor state. */
	TArray<FGridTrafficReservationDebugData> DebugEntries;

	/**
	 * Tests whether a candidate cell violates another owner's capsule clearance.
	 * @param CandidateCenter World-space floor position of the candidate cell.
	 * @param CandidateRadius Horizontal radius of the candidate agent.
	 * @param CandidateHeight Full height of the candidate agent.
	 * @param CandidateSeparation Additional clearance requested by the candidate.
	 * @param IgnoredOwnerId Owner allowed to overlap its own existing reservations.
	 * @param OutBlockingOwnerId Optional owner responsible for the first deterministic conflict.
	 * @param OutBlockingCellId Optional protected cell responsible for the conflict.
	 * @return True when the candidate cell must not be entered.
	 */
	bool ConflictsWithCell(
		const FVector& CandidateCenter,
		float CandidateRadius,
		float CandidateHeight,
		float CandidateSeparation,
		const FGuid& IgnoredOwnerId,
		FGuid* OutBlockingOwnerId = nullptr,
		FGridCellId* OutBlockingCellId = nullptr) const;

	/**
	 * Tests a candidate transition against cells and swept segments owned by other agents.
	 * @param Start World-space floor position at the transition start.
	 * @param End World-space floor position at the transition end.
	 * @param CandidateRadius Horizontal radius of the candidate agent.
	 * @param CandidateHeight Full height of the candidate agent.
	 * @param CandidateSeparation Additional clearance requested by the candidate.
	 * @param IgnoredOwnerId Owner allowed to overlap its own existing reservations.
	 * @param OutBlockingOwnerId Optional owner responsible for the first deterministic conflict.
	 * @return True when the transition is not currently safe.
	 */
	bool ConflictsWithSegment(
		const FVector& Start,
		const FVector& End,
		float CandidateRadius,
		float CandidateHeight,
		float CandidateSeparation,
		const FGuid& IgnoredOwnerId,
		FGuid* OutBlockingOwnerId = nullptr) const;
};

/** Thread-safe immutable traffic view shared with query workers and the debug renderer. */
using FGridTrafficReservationSnapshotPtr = TSharedPtr<const FGridTrafficReservationSnapshot, ESPMode::ThreadSafe>;
