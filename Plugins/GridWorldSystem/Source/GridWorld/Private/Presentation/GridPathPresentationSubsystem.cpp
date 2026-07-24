// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/GridPathPresentationSubsystem.h"

#include "Engine/World.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridNavigationPath.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Presentation/GridPathLineVisualizationSubsystem.h"
#include "Presentation/GridPresentationTypes.h"
#include "Presentation/GridRuntimeVisualizationSubsystem.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "UObject/UObjectGlobals.h"

namespace UE::GridWorld::PathPresentation::Private
{
	struct FResolvedCandidate
	{
		FGridResolvedPathVisualState VisualState;
		int32 Priority = 0;
		int32 PurposeRank = 0;
		int32 StateRank = 0;
		uint64 CreationSequence = 0;
		int32 OccurrenceIndex = INDEX_NONE;
	};

	int32 GetPurposeRank(EGridPathPresentationPurpose Purpose)
	{
		return Purpose == EGridPathPresentationPurpose::Active ? 1 : 0;
	}

	int32 GetStateRank(EGridCellPathVisualState State)
	{
		switch (State)
		{
		case EGridCellPathVisualState::Invalid:
			return 6;
		case EGridCellPathVisualState::Destination:
			return 5;
		case EGridCellPathVisualState::ActiveCurrent:
			return 4;
		case EGridCellPathVisualState::ActiveRemaining:
			return 3;
		case EGridCellPathVisualState::ActiveTraversed:
			return 2;
		case EGridCellPathVisualState::Preview:
			return 1;
		default:
			return 0;
		}
	}

	bool ShouldReplaceCandidate(const FResolvedCandidate& Candidate, const FResolvedCandidate& Current)
	{
		if (Candidate.Priority != Current.Priority)
		{
			return Candidate.Priority > Current.Priority;
		}
		if (Candidate.PurposeRank != Current.PurposeRank)
		{
			return Candidate.PurposeRank > Current.PurposeRank;
		}
		if (Candidate.StateRank != Current.StateRank)
		{
			return Candidate.StateRank > Current.StateRank;
		}
		if (Candidate.CreationSequence != Current.CreationSequence)
		{
			return Candidate.CreationSequence < Current.CreationSequence;
		}
		return Candidate.OccurrenceIndex > Current.OccurrenceIndex;
	}

	float GetNormalizedProgress(int32 CellIndex, int32 NumCells)
	{
		return NumCells > 1 ? static_cast<float>(CellIndex) / static_cast<float>(NumCells - 1) : 1.0f;
	}

	FIntPoint GetDirection(const FGridCellId& From, const FGridCellId& To)
	{
		return FIntPoint(
			FMath::Sign(To.Coord.X - From.Coord.X),
			FMath::Sign(To.Coord.Y - From.Coord.Y));
	}

	bool IsEndpointOrTurn(TConstArrayView<FGridCellId> Cells, int32 CellIndex)
	{
		if (CellIndex <= 0 || CellIndex >= Cells.Num() - 1)
		{
			return true;
		}
		const FGridCellId& Previous = Cells[CellIndex - 1];
		const FGridCellId& Current = Cells[CellIndex];
		const FGridCellId& Next = Cells[CellIndex + 1];
		if (Previous.GridId != Current.GridId
			|| Current.GridId != Next.GridId
			|| Previous.Coord.Layer != Current.Coord.Layer
			|| Current.Coord.Layer != Next.Coord.Layer)
		{
			return true;
		}
		return GetDirection(Previous, Current) != GetDirection(Current, Next);
	}

	bool ShouldIncludeCell(
		EGridPathProgressPresentationMode Mode,
		TConstArrayView<FGridCellId> Cells,
		int32 CellIndex,
		int32 CurrentCellIndex)
	{
		switch (Mode)
		{
		case EGridPathProgressPresentationMode::AllCells:
		case EGridPathProgressPresentationMode::TraversedAndRemaining:
			return true;
		case EGridPathProgressPresentationMode::RemainingOnly:
			return CellIndex > CurrentCellIndex || CellIndex == Cells.Num() - 1;
		case EGridPathProgressPresentationMode::CurrentAndRemaining:
			return CellIndex >= CurrentCellIndex;
		case EGridPathProgressPresentationMode::DestinationOnly:
			return CellIndex == Cells.Num() - 1;
		case EGridPathProgressPresentationMode::EndpointsAndTurns:
			return IsEndpointOrTurn(Cells, CellIndex);
		default:
			return false;
		}
	}

	EGridCellPathVisualState ResolveState(
		EGridPathPresentationPurpose Purpose,
		EGridPathProgressPresentationMode Mode,
		int32 CellIndex,
		int32 CurrentCellIndex,
		int32 NumCells)
	{
		if (Purpose == EGridPathPresentationPurpose::Preview)
		{
			return EGridCellPathVisualState::Preview;
		}
		if (CellIndex == NumCells - 1)
		{
			return EGridCellPathVisualState::Destination;
		}
		if (Mode == EGridPathProgressPresentationMode::AllCells)
		{
			return EGridCellPathVisualState::ActiveRemaining;
		}
		if (CellIndex < CurrentCellIndex)
		{
			return EGridCellPathVisualState::ActiveTraversed;
		}
		if (CellIndex == CurrentCellIndex)
		{
			return EGridCellPathVisualState::ActiveCurrent;
		}
		return EGridCellPathVisualState::ActiveRemaining;
	}
}

bool UGridPathPresentationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer) || IsRunningDedicatedServer())
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->GetNetMode() != NM_DedicatedServer;
}

bool UGridPathPresentationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGridPathPresentationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGridWorldSubsystem>();
	Collection.InitializeDependency<UGridRuntimeVisualizationSubsystem>();
	Collection.InitializeDependency<UGridPathLineVisualizationSubsystem>();
	if (UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem())
	{
		GridSubsystem->OnGridWorldChanged.AddDynamic(this, &UGridPathPresentationSubsystem::HandleGridWorldChanged);
	}
	PostGarbageCollectHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(
		this,
		&UGridPathPresentationSubsystem::HandlePostGarbageCollect);
}

void UGridPathPresentationSubsystem::Deinitialize()
{
	if (UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem())
	{
		GridSubsystem->OnGridWorldChanged.RemoveDynamic(this, &UGridPathPresentationSubsystem::HandleGridWorldChanged);
	}
	if (PostGarbageCollectHandle.IsValid())
	{
		FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGarbageCollectHandle);
		PostGarbageCollectHandle.Reset();
	}
	Sessions.Reset();
	RebuildPresentationOutputs();
	Super::Deinitialize();
}

bool UGridPathPresentationSubsystem::CreatePathPresentation(
	const FGridPathPresentationRequest& Request,
	FGridPathPresentationHandle& OutHandle)
{
	OutHandle = FGridPathPresentationHandle();
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutputs();
	}
	if (!ValidateCells(Request.Cells)
		|| Request.CurrentCellIndex < 0
		|| Request.CurrentCellIndex >= Request.Cells.Num()
		|| (Request.Lifetime == EGridPathPresentationLifetime::OwnerLifetime && !IsValid(Request.Owner)))
	{
		return false;
	}

	FGridPathPresentationSession Session;
	do
	{
		Session.Handle.SessionId = FGuid::NewGuid();
	}
	while (Sessions.Contains(Session.Handle.SessionId));
	Session.Cells = Request.Cells;
	Session.SourceRevisions = Request.SourceRevisions;
	Session.Purpose = Request.Purpose;
	Session.ProgressMode = Request.ProgressMode;
	Session.ReplacementPolicy = Request.ReplacementPolicy;
	Session.Lifetime = Request.Lifetime;
	Session.Owner = Request.Owner;
	Session.CreationSequence = NextCreationSequence++;
	Session.Priority = Request.Priority;
	Session.CurrentCellIndex = Request.CurrentCellIndex;
	Session.bVisible = Request.bVisible;
	Session.bRenderCellOverlay = Request.bRenderCellOverlay;
	Session.bRenderLine = Request.bRenderLine;
	OutHandle = Session.Handle;
	Sessions.Add(Session.Handle.SessionId, MoveTemp(Session));
	RebuildPresentationOutputs();
	return true;
}

bool UGridPathPresentationSubsystem::CreatePathPresentationFromQueryResult(
	const FGridPathQueryResult& PathResult,
	const FGridPathPresentationRequest& TemplateRequest,
	FGridPathPresentationHandle& OutHandle)
{
	if (PathResult.Status != EGridQueryStatus::Success && PathResult.Status != EGridQueryStatus::Partial)
	{
		OutHandle = FGridPathPresentationHandle();
		return false;
	}
	FGridPathPresentationRequest Request = TemplateRequest;
	Request.Cells = PathResult.Cells;
	Request.SourceRevisions = PathResult.Revisions;
	return CreatePathPresentation(Request, OutHandle);
}

bool UGridPathPresentationSubsystem::CreatePathPresentation(
	const FGridNavigationPath& Path,
	const FGridPathPresentationRequest& TemplateRequest,
	FGridPathPresentationHandle& OutHandle)
{
	FGridPathPresentationRequest Request = TemplateRequest;
	Request.Cells = Path.CellPath;
	Request.SourceRevisions = Path.Revisions;
	return CreatePathPresentation(Request, OutHandle);
}

bool UGridPathPresentationSubsystem::UpdatePathPresentation(
	const FGridPathPresentationHandle& Handle,
	const TArray<FGridCellId>& Cells,
	int32 CurrentCellIndex)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutputs();
	}
	FGridPathPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr || !ValidateCells(Cells) || CurrentCellIndex < 0 || CurrentCellIndex >= Cells.Num())
	{
		return false;
	}

	if (Session->ReplacementPolicy == EGridPathReplacementPolicy::PreserveTraversed
		&& Session->Purpose == EGridPathPresentationPurpose::Active
		&& Session->ProgressMode == EGridPathProgressPresentationMode::TraversedAndRemaining)
	{
		for (int32 CellIndex = 0; CellIndex < Session->CurrentCellIndex && Session->Cells.IsValidIndex(CellIndex); ++CellIndex)
		{
			Session->PreservedTraversedCells.Add(
				Session->Cells[CellIndex],
				UE::GridWorld::PathPresentation::Private::GetNormalizedProgress(CellIndex, Session->Cells.Num()));
		}
	}
	else
	{
		Session->PreservedTraversedCells.Reset();
	}

	Session->Cells = Cells;
	Session->CurrentCellIndex = CurrentCellIndex;
	Session->bInvalid = false;
	RebuildPresentationOutputs();
	return true;
}

bool UGridPathPresentationSubsystem::UpdatePathPresentation(
	const FGridPathPresentationHandle& Handle,
	const FGridNavigationPath& Path,
	int32 CurrentCellIndex)
{
	if (!UpdatePathPresentation(Handle, Path.CellPath, CurrentCellIndex))
	{
		return false;
	}
	if (FGridPathPresentationSession* Session = Sessions.Find(Handle.SessionId))
	{
		Session->SourceRevisions = Path.Revisions;
	}
	return true;
}

bool UGridPathPresentationSubsystem::UpdatePathPresentationFromQueryResult(
	const FGridPathPresentationHandle& Handle,
	const FGridPathQueryResult& PathResult,
	int32 CurrentCellIndex)
{
	if ((PathResult.Status != EGridQueryStatus::Success && PathResult.Status != EGridQueryStatus::Partial)
		|| !UpdatePathPresentation(Handle, PathResult.Cells, CurrentCellIndex))
	{
		return false;
	}
	if (FGridPathPresentationSession* Session = Sessions.Find(Handle.SessionId))
	{
		Session->SourceRevisions = PathResult.Revisions;
	}
	return true;
}

bool UGridPathPresentationSubsystem::UpdatePathPresentationProgress(
	const FGridPathPresentationHandle& Handle,
	int32 CurrentCellIndex)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutputs();
	}
	FGridPathPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr || !ValidateCurrentIndex(*Session, CurrentCellIndex))
	{
		return false;
	}
	if (Session->CurrentCellIndex == CurrentCellIndex)
	{
		return true;
	}
	Session->CurrentCellIndex = CurrentCellIndex;
	RebuildPresentationOutputs();
	return true;
}

bool UGridPathPresentationSubsystem::SetPathPresentationVisible(
	const FGridPathPresentationHandle& Handle,
	bool bVisible)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutputs();
	}
	FGridPathPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr)
	{
		return false;
	}
	if (Session->bVisible != bVisible)
	{
		Session->bVisible = bVisible;
		RebuildPresentationOutputs();
	}
	return true;
}

bool UGridPathPresentationSubsystem::SetPathPresentationPriority(
	const FGridPathPresentationHandle& Handle,
	int32 Priority)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutputs();
	}
	FGridPathPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr)
	{
		return false;
	}
	if (Session->Priority != Priority)
	{
		Session->Priority = Priority;
		RebuildPresentationOutputs();
	}
	return true;
}

bool UGridPathPresentationSubsystem::SetPathPresentationMode(
	const FGridPathPresentationHandle& Handle,
	EGridPathProgressPresentationMode ProgressMode)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutputs();
	}
	FGridPathPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr)
	{
		return false;
	}
	if (Session->ProgressMode != ProgressMode)
	{
		Session->ProgressMode = ProgressMode;
		if (ProgressMode != EGridPathProgressPresentationMode::TraversedAndRemaining)
		{
			Session->PreservedTraversedCells.Reset();
		}
		RebuildPresentationOutputs();
	}
	return true;
}

bool UGridPathPresentationSubsystem::SetPathPresentationRenderers(
	const FGridPathPresentationHandle& Handle,
	bool bRenderCellOverlay,
	bool bRenderLine)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutputs();
	}
	FGridPathPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr)
	{
		return false;
	}
	if (Session->bRenderCellOverlay != bRenderCellOverlay || Session->bRenderLine != bRenderLine)
	{
		Session->bRenderCellOverlay = bRenderCellOverlay;
		Session->bRenderLine = bRenderLine;
		RebuildPresentationOutputs();
	}
	return true;
}

bool UGridPathPresentationSubsystem::MarkPathPresentationInvalid(const FGridPathPresentationHandle& Handle)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutputs();
	}
	FGridPathPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr)
	{
		return false;
	}
	if (!Session->bInvalid)
	{
		Session->bInvalid = true;
		RebuildPresentationOutputs();
	}
	return true;
}

bool UGridPathPresentationSubsystem::ClearPathPresentation(const FGridPathPresentationHandle& Handle)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutputs();
	}
	FGridPathPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr)
	{
		return false;
	}
	Session->Cells.Reset();
	Session->PreservedTraversedCells.Reset();
	Session->CurrentCellIndex = INDEX_NONE;
	Session->bInvalid = false;
	RebuildPresentationOutputs();
	return true;
}

bool UGridPathPresentationSubsystem::ReleasePathPresentation(const FGridPathPresentationHandle& Handle)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutputs();
	}
	if (Sessions.Remove(Handle.SessionId) == 0)
	{
		return false;
	}
	RebuildPresentationOutputs();
	return true;
}

bool UGridPathPresentationSubsystem::IsPathPresentationValid(const FGridPathPresentationHandle& Handle) const
{
	return Handle.IsSet() && Sessions.Contains(Handle.SessionId);
}

bool UGridPathPresentationSubsystem::GetPathPresentation(
	const FGridPathPresentationHandle& Handle,
	FGridPathPresentationSnapshot& OutPresentation) const
{
	OutPresentation = FGridPathPresentationSnapshot();
	const FGridPathPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr)
	{
		return false;
	}
	OutPresentation.Handle = Session->Handle;
	OutPresentation.Cells = Session->Cells;
	OutPresentation.SourceRevisions = Session->SourceRevisions;
	OutPresentation.Purpose = Session->Purpose;
	OutPresentation.ProgressMode = Session->ProgressMode;
	OutPresentation.ReplacementPolicy = Session->ReplacementPolicy;
	OutPresentation.Priority = Session->Priority;
	OutPresentation.CurrentCellIndex = Session->CurrentCellIndex;
	OutPresentation.bVisible = Session->bVisible;
	OutPresentation.bInvalid = Session->bInvalid;
	OutPresentation.bRenderCellOverlay = Session->bRenderCellOverlay;
	OutPresentation.bRenderLine = Session->bRenderLine;
	return true;
}

void UGridPathPresentationSubsystem::HandleGridWorldChanged(const FGridChangeSet& ChangeSet)
{
	if (ChangeSet.PreviousRevisions.Topology == ChangeSet.NewRevisions.Topology)
	{
		return;
	}
	PruneExpiredOwnerSessions();
	const UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem();
	const AGridNavigationData* NavData = GridSubsystem != nullptr ? GridSubsystem->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	for (TPair<FGuid, FGridPathPresentationSession>& Pair : Sessions)
	{
		for (const FGridCellId& CellId : Pair.Value.Cells)
		{
			if (!Snapshot.IsValid() || Snapshot->FindCell(CellId) == nullptr)
			{
				Pair.Value.bInvalid = true;
				break;
			}
		}
	}
	RebuildPresentationOutputs();
}

void UGridPathPresentationSubsystem::HandlePostGarbageCollect()
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutputs();
	}
}

UGridWorldSubsystem* UGridPathPresentationSubsystem::GetGridWorldSubsystem() const
{
	UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UGridWorldSubsystem>() : nullptr;
}

UGridRuntimeVisualizationSubsystem* UGridPathPresentationSubsystem::GetVisualizationSubsystem() const
{
	UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UGridRuntimeVisualizationSubsystem>() : nullptr;
}

UGridPathLineVisualizationSubsystem* UGridPathPresentationSubsystem::GetLineVisualizationSubsystem() const
{
	UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UGridPathLineVisualizationSubsystem>() : nullptr;
}

bool UGridPathPresentationSubsystem::ValidateCells(TConstArrayView<FGridCellId> Cells) const
{
	if (Cells.IsEmpty())
	{
		return false;
	}
	const UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem();
	const AGridNavigationData* NavData = GridSubsystem != nullptr ? GridSubsystem->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	if (!Snapshot.IsValid())
	{
		return false;
	}
	for (const FGridCellId& CellId : Cells)
	{
		if (Snapshot->FindCell(CellId) == nullptr)
		{
			return false;
		}
	}
	return true;
}

bool UGridPathPresentationSubsystem::ValidateCurrentIndex(
	const FGridPathPresentationSession& Session,
	int32 CurrentCellIndex) const
{
	return Session.Cells.IsValidIndex(CurrentCellIndex);
}

bool UGridPathPresentationSubsystem::PruneExpiredOwnerSessions()
{
	bool bRemovedAny = false;
	for (auto It = Sessions.CreateIterator(); It; ++It)
	{
		if (It.Value().Lifetime == EGridPathPresentationLifetime::OwnerLifetime && !It.Value().Owner.IsValid())
		{
			It.RemoveCurrent();
			bRemovedAny = true;
		}
	}
	return bRemovedAny;
}

void UGridPathPresentationSubsystem::RebuildPresentationOutputs()
{
	using namespace UE::GridWorld::PathPresentation::Private;

	const UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem();
	const AGridNavigationData* NavData = GridSubsystem != nullptr ? GridSubsystem->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	TMap<FGridCellId, FResolvedCandidate> Candidates;
	TArray<FGridPathLineRenderRecord> LineRecords;
	for (const TPair<FGuid, FGridPathPresentationSession>& Pair : Sessions)
	{
		const FGridPathPresentationSession& Session = Pair.Value;
		if (!Session.bVisible)
		{
			continue;
		}
		if (Session.bRenderLine)
		{
			FGridPathLineRenderRecord& Record = LineRecords.AddDefaulted_GetRef();
			Record.Cells = Session.Cells;
			Record.PreservedTraversedCells = Session.PreservedTraversedCells;
			Record.Purpose = Session.Purpose;
			Record.ProgressMode = Session.ProgressMode;
			Record.CreationSequence = Session.CreationSequence;
			Record.Priority = Session.Priority;
			Record.CurrentCellIndex = Session.CurrentCellIndex;
			Record.bInvalid = Session.bInvalid;
		}
		if (!Session.bRenderCellOverlay)
		{
			continue;
		}

		auto AddCandidate = [&Candidates, &Session](
			const FGridCellId& CellId,
			EGridCellPathVisualState State,
			float PathProgress,
			int32 OccurrenceIndex)
		{
			FResolvedCandidate Candidate;
			Candidate.VisualState.State = State;
			Candidate.VisualState.PathProgress = FMath::Clamp(PathProgress, 0.0f, 1.0f);
			Candidate.Priority = Session.Priority;
			Candidate.PurposeRank = GetPurposeRank(Session.Purpose);
			Candidate.StateRank = GetStateRank(State);
			Candidate.CreationSequence = Session.CreationSequence;
			Candidate.OccurrenceIndex = OccurrenceIndex;
			if (FResolvedCandidate* Current = Candidates.Find(CellId))
			{
				if (ShouldReplaceCandidate(Candidate, *Current))
				{
					*Current = Candidate;
				}
			}
			else
			{
				Candidates.Add(CellId, Candidate);
			}
		};

		for (const TPair<FGridCellId, float>& Preserved : Session.PreservedTraversedCells)
		{
			if (Snapshot.IsValid() && Snapshot->FindCell(Preserved.Key) != nullptr)
			{
				AddCandidate(Preserved.Key, EGridCellPathVisualState::ActiveTraversed, Preserved.Value, INDEX_NONE);
			}
		}

		for (int32 CellIndex = 0; CellIndex < Session.Cells.Num(); ++CellIndex)
		{
			const FGridCellId& CellId = Session.Cells[CellIndex];
			if (!Snapshot.IsValid() || Snapshot->FindCell(CellId) == nullptr)
			{
				continue;
			}
			if (Session.bInvalid)
			{
				AddCandidate(
					CellId,
					EGridCellPathVisualState::Invalid,
					GetNormalizedProgress(CellIndex, Session.Cells.Num()),
					CellIndex);
				continue;
			}
			if (!ShouldIncludeCell(Session.ProgressMode, Session.Cells, CellIndex, Session.CurrentCellIndex))
			{
				continue;
			}
			AddCandidate(
				CellId,
				ResolveState(
					Session.Purpose,
					Session.ProgressMode,
					CellIndex,
					Session.CurrentCellIndex,
					Session.Cells.Num()),
				GetNormalizedProgress(CellIndex, Session.Cells.Num()),
				CellIndex);
		}
	}

	TMap<FGridCellId, FGridResolvedPathVisualState> ResolvedStates;
	ResolvedStates.Reserve(Candidates.Num());
	for (const TPair<FGridCellId, FResolvedCandidate>& Pair : Candidates)
	{
		ResolvedStates.Add(Pair.Key, Pair.Value.VisualState);
	}
	if (UGridRuntimeVisualizationSubsystem* Visualization = GetVisualizationSubsystem())
	{
		Visualization->ReplaceResolvedPathStatesInternal(MoveTemp(ResolvedStates));
	}
	if (UGridPathLineVisualizationSubsystem* LineVisualization = GetLineVisualizationSubsystem())
	{
		LineVisualization->ReplaceRenderRecordsInternal(MoveTemp(LineRecords));
	}
}
