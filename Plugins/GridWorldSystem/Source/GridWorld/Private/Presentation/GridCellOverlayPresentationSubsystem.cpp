// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/GridCellOverlayPresentationSubsystem.h"

#include "Engine/World.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Presentation/GridRuntimeVisualizationSubsystem.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "UObject/UObjectGlobals.h"

namespace UE::GridWorld::CellOverlayPresentation::Private
{
	struct FResolvedCandidate
	{
		EGridCellOverlayVisualState State = EGridCellOverlayVisualState::None;
		int32 Priority = 0;
		int32 StateRank = 0;
		uint64 CreationSequence = 0;
		int32 EntryIndex = INDEX_NONE;
	};

	int32 GetStateRank(const EGridCellOverlayVisualState State)
	{
		return State == EGridCellOverlayVisualState::Secondary
			? 2
			: (State == EGridCellOverlayVisualState::Primary ? 1 : 0);
	}

	bool ShouldReplaceCandidate(const FResolvedCandidate& Candidate, const FResolvedCandidate& Current)
	{
		if (Candidate.Priority != Current.Priority)
		{
			return Candidate.Priority > Current.Priority;
		}
		if (Candidate.StateRank != Current.StateRank)
		{
			return Candidate.StateRank > Current.StateRank;
		}
		if (Candidate.CreationSequence != Current.CreationSequence)
		{
			return Candidate.CreationSequence < Current.CreationSequence;
		}
		return Candidate.EntryIndex < Current.EntryIndex;
	}
}

bool UGridCellOverlayPresentationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer) || IsRunningDedicatedServer())
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->GetNetMode() != NM_DedicatedServer;
}

bool UGridCellOverlayPresentationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGridCellOverlayPresentationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGridWorldSubsystem>();
	Collection.InitializeDependency<UGridRuntimeVisualizationSubsystem>();
	if (UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem())
	{
		GridSubsystem->OnGridWorldChanged.AddDynamic(this, &ThisClass::HandleGridWorldChanged);
	}
	PostGarbageCollectHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(
		this,
		&ThisClass::HandlePostGarbageCollect);
}

void UGridCellOverlayPresentationSubsystem::Deinitialize()
{
	if (UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem())
	{
		GridSubsystem->OnGridWorldChanged.RemoveDynamic(this, &ThisClass::HandleGridWorldChanged);
	}
	if (PostGarbageCollectHandle.IsValid())
	{
		FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGarbageCollectHandle);
		PostGarbageCollectHandle.Reset();
	}
	Sessions.Reset();
	RebuildPresentationOutput();
	Super::Deinitialize();
}

bool UGridCellOverlayPresentationSubsystem::CreateCellOverlayPresentation(
	const FGridCellOverlayPresentationRequest& Request,
	FGridCellOverlayPresentationHandle& OutHandle)
{
	OutHandle = FGridCellOverlayPresentationHandle();
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutput();
	}
	if (!IsValid(Request.Owner) || !ValidateEntries(Request.Entries, false))
	{
		return false;
	}

	FGridCellOverlayPresentationSession Session;
	do
	{
		Session.Handle.SessionId = FGuid::NewGuid();
	}
	while (Sessions.Contains(Session.Handle.SessionId));
	Session.Entries = Request.Entries;
	Session.Owner = Request.Owner;
	Session.CreationSequence = NextCreationSequence++;
	Session.Priority = Request.Priority;
	Session.bVisible = Request.bVisible;
	OutHandle = Session.Handle;
	Sessions.Add(Session.Handle.SessionId, MoveTemp(Session));
	RebuildPresentationOutput();
	return true;
}

bool UGridCellOverlayPresentationSubsystem::UpdateCellOverlayPresentation(
	const FGridCellOverlayPresentationHandle& Handle,
	const TArray<FGridCellOverlayEntry>& Entries)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutput();
	}
	FGridCellOverlayPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr || !ValidateEntries(Entries, true))
	{
		return false;
	}
	if (Session->Entries != Entries)
	{
		Session->Entries = Entries;
		RebuildPresentationOutput();
	}
	return true;
}

bool UGridCellOverlayPresentationSubsystem::SetCellOverlayPresentationVisible(
	const FGridCellOverlayPresentationHandle& Handle,
	const bool bVisible)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutput();
	}
	FGridCellOverlayPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr)
	{
		return false;
	}
	if (Session->bVisible != bVisible)
	{
		Session->bVisible = bVisible;
		RebuildPresentationOutput();
	}
	return true;
}

bool UGridCellOverlayPresentationSubsystem::SetCellOverlayPresentationPriority(
	const FGridCellOverlayPresentationHandle& Handle,
	const int32 Priority)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutput();
	}
	FGridCellOverlayPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr)
	{
		return false;
	}
	if (Session->Priority != Priority)
	{
		Session->Priority = Priority;
		RebuildPresentationOutput();
	}
	return true;
}

bool UGridCellOverlayPresentationSubsystem::ClearCellOverlayPresentation(
	const FGridCellOverlayPresentationHandle& Handle)
{
	return UpdateCellOverlayPresentation(Handle, {});
}

bool UGridCellOverlayPresentationSubsystem::ReleaseCellOverlayPresentation(
	const FGridCellOverlayPresentationHandle& Handle)
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutput();
	}
	if (Sessions.Remove(Handle.SessionId) == 0)
	{
		return false;
	}
	RebuildPresentationOutput();
	return true;
}

bool UGridCellOverlayPresentationSubsystem::IsCellOverlayPresentationValid(
	const FGridCellOverlayPresentationHandle& Handle) const
{
	return Handle.IsSet() && Sessions.Contains(Handle.SessionId);
}

bool UGridCellOverlayPresentationSubsystem::GetCellOverlayPresentation(
	const FGridCellOverlayPresentationHandle& Handle,
	FGridCellOverlayPresentationSnapshot& OutPresentation) const
{
	OutPresentation = FGridCellOverlayPresentationSnapshot();
	const FGridCellOverlayPresentationSession* Session = Sessions.Find(Handle.SessionId);
	if (Session == nullptr)
	{
		return false;
	}
	OutPresentation.Handle = Session->Handle;
	OutPresentation.Entries = Session->Entries;
	OutPresentation.Priority = Session->Priority;
	OutPresentation.bVisible = Session->bVisible;
	return true;
}

void UGridCellOverlayPresentationSubsystem::HandleGridWorldChanged(const FGridChangeSet& ChangeSet)
{
	if (ChangeSet.PreviousRevisions.Topology != ChangeSet.NewRevisions.Topology)
	{
		PruneExpiredOwnerSessions();
		RebuildPresentationOutput();
	}
}

void UGridCellOverlayPresentationSubsystem::HandlePostGarbageCollect()
{
	if (PruneExpiredOwnerSessions())
	{
		RebuildPresentationOutput();
	}
}

UGridWorldSubsystem* UGridCellOverlayPresentationSubsystem::GetGridWorldSubsystem() const
{
	UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UGridWorldSubsystem>() : nullptr;
}

UGridRuntimeVisualizationSubsystem* UGridCellOverlayPresentationSubsystem::GetVisualizationSubsystem() const
{
	UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UGridRuntimeVisualizationSubsystem>() : nullptr;
}

bool UGridCellOverlayPresentationSubsystem::ValidateEntries(
	const TConstArrayView<FGridCellOverlayEntry> Entries,
	const bool bAllowEmpty) const
{
	if (Entries.IsEmpty())
	{
		return bAllowEmpty;
	}
	const UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem();
	const AGridNavigationData* NavData = GridSubsystem != nullptr ? GridSubsystem->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	if (!Snapshot.IsValid())
	{
		return false;
	}
	for (const FGridCellOverlayEntry& Entry : Entries)
	{
		if (Entry.State == EGridCellOverlayVisualState::None || Snapshot->FindCell(Entry.CellId) == nullptr)
		{
			return false;
		}
	}
	return true;
}

bool UGridCellOverlayPresentationSubsystem::PruneExpiredOwnerSessions()
{
	bool bRemovedAny = false;
	for (auto It = Sessions.CreateIterator(); It; ++It)
	{
		if (!It.Value().Owner.IsValid())
		{
			It.RemoveCurrent();
			bRemovedAny = true;
		}
	}
	return bRemovedAny;
}

void UGridCellOverlayPresentationSubsystem::RebuildPresentationOutput()
{
	using namespace UE::GridWorld::CellOverlayPresentation::Private;

	const UGridWorldSubsystem* GridSubsystem = GetGridWorldSubsystem();
	const AGridNavigationData* NavData = GridSubsystem != nullptr ? GridSubsystem->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
	TMap<FGridCellId, FResolvedCandidate> Candidates;
	for (const TPair<FGuid, FGridCellOverlayPresentationSession>& Pair : Sessions)
	{
		const FGridCellOverlayPresentationSession& Session = Pair.Value;
		if (!Session.bVisible)
		{
			continue;
		}
		for (int32 EntryIndex = 0; EntryIndex < Session.Entries.Num(); ++EntryIndex)
		{
			const FGridCellOverlayEntry& Entry = Session.Entries[EntryIndex];
			if (Entry.State == EGridCellOverlayVisualState::None
				|| !Snapshot.IsValid()
				|| Snapshot->FindCell(Entry.CellId) == nullptr)
			{
				continue;
			}
			FResolvedCandidate Candidate;
			Candidate.State = Entry.State;
			Candidate.Priority = Session.Priority;
			Candidate.StateRank = GetStateRank(Entry.State);
			Candidate.CreationSequence = Session.CreationSequence;
			Candidate.EntryIndex = EntryIndex;
			if (FResolvedCandidate* Current = Candidates.Find(Entry.CellId))
			{
				if (ShouldReplaceCandidate(Candidate, *Current))
				{
					*Current = Candidate;
				}
			}
			else
			{
				Candidates.Add(Entry.CellId, Candidate);
			}
		}
	}

	TMap<FGridCellId, EGridCellOverlayVisualState> ResolvedStates;
	ResolvedStates.Reserve(Candidates.Num());
	for (const TPair<FGridCellId, FResolvedCandidate>& Pair : Candidates)
	{
		ResolvedStates.Add(Pair.Key, Pair.Value.State);
	}
	if (UGridRuntimeVisualizationSubsystem* Visualization = GetVisualizationSubsystem())
	{
		Visualization->ReplaceResolvedOverlayStatesInternal(MoveTemp(ResolvedStates));
	}
}
