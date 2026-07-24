#include "Subsystems/WorldStateSubsystem.h"

#include "Components/SceneComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/WorldStatePropertySerializer.h"
#include "Snapshots/WorldStateSnapshotTypes.h"
#include "Spawning/WorldStateDefaultSpawnStrategy.h"
#include "UObject/UnrealType.h"
#include "WorldStateModule.h"

namespace UE::WorldState::Private
{
	/** Current private snapshot schema and built-in strategy identity. */
	constexpr int32 SnapshotFormatVersion = 1;
	const FName DefaultSpawnStrategyId(TEXT("WorldState.DefaultActor"));

	/** Builds a structured diagnostic with optional participant/source/property context. */
	FWorldStateIssue MakeIssue(
		EWorldStateIssueSeverity Severity,
		FName Code,
		FString Message,
		FWorldStateParticipantId ParticipantId = FWorldStateParticipantId(),
		FWorldStateCaptureSourceId SourceId = FWorldStateCaptureSourceId(),
		FName PropertyName = NAME_None)
	{
		FWorldStateIssue Issue;
		Issue.Severity = Severity;
		Issue.Code = Code;
		Issue.Message = MoveTemp(Message);
		Issue.ParticipantId = ParticipantId;
		Issue.CaptureSourceId = SourceId;
		Issue.PropertyName = PropertyName;
		return Issue;
	}

	/** Tests the two policies that authorize native recreation. */
	bool IsRespawnPolicy(EWorldStateExistencePolicy Policy)
	{
		return Policy == EWorldStateExistencePolicy::RespawnIfMissing || Policy == EWorldStateExistencePolicy::RespawnAndDestroy;
	}

	/** Tests the two policies that authorize removal from complete-snapshot restores. */
	bool IsDestroyPolicy(EWorldStateExistencePolicy Policy)
	{
		return Policy == EWorldStateExistencePolicy::DestroyIfAbsent || Policy == EWorldStateExistencePolicy::RespawnAndDestroy;
	}

	/** Compares exact already-loaded class identity without resolving the captured soft path. */
	bool MatchesCapturedSourceClass(const UObject* Source, const FSoftClassPath& CapturedClass)
	{
		return Source && !CapturedClass.IsNull() && Source->GetClass()->GetPathName() == CapturedClass.ToString();
	}

	/** Converts authored phases into strong graph-order ranks. */
	int32 PhaseOrder(EWorldStateRestorePhase Phase)
	{
		switch (Phase)
		{
		case EWorldStateRestorePhase::Early: return 0;
		case EWorldStateRestorePhase::Late: return 2;
		default: return 1;
		}
	}

	/** Applies GUID-string ordering wherever otherwise-unordered containers could affect behavior. */
	void SortIds(TArray<FWorldStateParticipantId>& Ids)
	{
		Ids.Sort([](const FWorldStateParticipantId& Left, const FWorldStateParticipantId& Right)
		{
			return Left.ToString() < Right.ToString();
		});
	}

	/** Resolves an attachment Component by stable UObject name; None denotes the Actor root. */
	USceneComponent* FindSceneComponent(AActor* Actor, FName ComponentName)
	{
		if (!Actor || ComponentName.IsNone())
		{
			return Actor ? Actor->GetRootComponent() : nullptr;
		}
		TArray<USceneComponent*> Components;
		Actor->GetComponents(Components);
		for (USceneComponent* Component : Components)
		{
			if (Component && Component->GetFName() == ComponentName)
			{
				return Component;
			}
		}
		return nullptr;
	}

	bool ContainsAnyGroup(const TArray<FName>& ParticipantGroups, const TArray<FName>& RequestedGroups)
	{
		return RequestedGroups.ContainsByPredicate([&ParticipantGroups](FName Group) { return ParticipantGroups.Contains(Group); });
	}

	/** Collects all mandatory predecessors, including explicit edges and captured attachment parents. */
	void GetPredecessors(
		const FWorldStateSnapshot& Snapshot,
		const FWorldStateParticipantId& Id,
		TArray<FWorldStateParticipantId>& OutPredecessors)
	{
		OutPredecessors.Reset();
		if (const FWorldStateParticipantSnapshot* Participant = Snapshot.Participants.Find(Id))
		{
			OutPredecessors.Append(Participant->RestoreAfter);
			if (Participant->bCaptureAttachment && Participant->AttachmentParentParticipantId.IsValid())
			{
				OutPredecessors.AddUnique(Participant->AttachmentParentParticipantId);
			}
		}
		for (const TPair<FWorldStateParticipantId, FWorldStateParticipantSnapshot>& Pair : Snapshot.Participants)
		{
			if (Pair.Value.RestoreBefore.Contains(Id))
			{
				OutPredecessors.AddUnique(Pair.Key);
			}
		}
	}

	/** Collects dependents for the opt-in bidirectional partial-scope expansion policy. */
	void GetSuccessors(
		const FWorldStateSnapshot& Snapshot,
		const FWorldStateParticipantId& Id,
		TArray<FWorldStateParticipantId>& OutSuccessors)
	{
		OutSuccessors.Reset();
		if (const FWorldStateParticipantSnapshot* Participant = Snapshot.Participants.Find(Id))
		{
			OutSuccessors.Append(Participant->RestoreBefore);
		}
		for (const TPair<FWorldStateParticipantId, FWorldStateParticipantSnapshot>& Pair : Snapshot.Participants)
		{
			if (Pair.Value.RestoreAfter.Contains(Id) ||
				(Pair.Value.bCaptureAttachment && Pair.Value.AttachmentParentParticipantId == Id))
			{
				OutSuccessors.AddUnique(Pair.Key);
			}
		}
	}

	/** Resolves scope, expands dependencies and performs deterministic Kahn topological sorting. */
	bool BuildRestoreOrder(
		const FWorldStateSnapshot& Snapshot,
		const FWorldStateRestoreRequest& Request,
		const TSet<FWorldStateParticipantId>& DirtyParticipants,
		TArray<FWorldStateParticipantId>& OutOrder,
		int32& OutRequestedCount,
		FString& OutError)
	{
		TSet<FWorldStateParticipantId> Scope;
		switch (Request.Scope.Kind)
		{
		case EWorldStateRestoreScopeKind::CompleteSnapshot:
			for (const TPair<FWorldStateParticipantId, FWorldStateParticipantSnapshot>& Pair : Snapshot.Participants)
			{
				Scope.Add(Pair.Key);
			}
			break;
		case EWorldStateRestoreScopeKind::ParticipantIds:
			for (const FWorldStateParticipantId& Id : Request.Scope.ParticipantIds)
			{
				if (!Snapshot.Participants.Contains(Id))
				{
					OutError = FString::Printf(TEXT("Participant %s is not present in the source snapshot."), *Id.ToString());
					return false;
				}
				Scope.Add(Id);
			}
			break;
		case EWorldStateRestoreScopeKind::Groups:
			for (const TPair<FWorldStateParticipantId, FWorldStateParticipantSnapshot>& Pair : Snapshot.Participants)
			{
				if (ContainsAnyGroup(Pair.Value.Groups, Request.Scope.Groups))
				{
					Scope.Add(Pair.Key);
				}
			}
			break;
		case EWorldStateRestoreScopeKind::DirtyParticipants:
			for (const FWorldStateParticipantId& Id : DirtyParticipants)
			{
				if (Snapshot.Participants.Contains(Id))
				{
					Scope.Add(Id);
				}
			}
			break;
		default:
			break;
		}

		OutRequestedCount = Scope.Num();
		if (Scope.IsEmpty())
		{
			OutError = TEXT("The restore scope contains no participants.");
			return false;
		}

		// Breadth-first expansion reaches every transitive dependency before graph construction.
		TArray<FWorldStateParticipantId> Frontier = Scope.Array();
		for (int32 FrontierIndex = 0; FrontierIndex < Frontier.Num(); ++FrontierIndex)
		{
			const FWorldStateParticipantId Current = Frontier[FrontierIndex];
			TArray<FWorldStateParticipantId> Related;
			GetPredecessors(Snapshot, Current, Related);
			for (const FWorldStateParticipantId& Required : Related)
			{
				if (!Snapshot.Participants.Contains(Required))
				{
					OutError = FString::Printf(TEXT("Participant %s depends on missing participant %s."), *Current.ToString(), *Required.ToString());
					return false;
				}
				if (!Scope.Contains(Required))
				{
					if (Request.DependencyExpansion == EWorldStateDependencyExpansionPolicy::ExactSelection ||
						Request.DependencyExpansion == EWorldStateDependencyExpansionPolicy::RejectIncompleteScope)
					{
						OutError = FString::Printf(TEXT("Restore scope omits required dependency %s."), *Required.ToString());
						return false;
					}
					Scope.Add(Required);
					Frontier.Add(Required);
				}
			}

			if (Request.DependencyExpansion == EWorldStateDependencyExpansionPolicy::IncludeDependenciesAndDependents)
			{
				GetSuccessors(Snapshot, Current, Related);
				for (const FWorldStateParticipantId& Dependent : Related)
				{
					if (!Snapshot.Participants.Contains(Dependent))
					{
						OutError = FString::Printf(TEXT("Participant %s references missing dependent %s."), *Current.ToString(), *Dependent.ToString());
						return false;
					}
					if (!Scope.Contains(Dependent))
					{
						Scope.Add(Dependent);
						Frontier.Add(Dependent);
					}
				}
			}
		}

		// Phase boundaries are represented as real edges, so explicit edges that contradict them form a cycle.
		TMap<FWorldStateParticipantId, TSet<FWorldStateParticipantId>> Edges;
		TMap<FWorldStateParticipantId, int32> InDegree;
		for (const FWorldStateParticipantId& Id : Scope)
		{
			Edges.Add(Id);
			InDegree.Add(Id, 0);
		}
		auto AddEdge = [&Edges, &InDegree](const FWorldStateParticipantId& Before, const FWorldStateParticipantId& After)
		{
			if (Before == After || !Edges.Contains(Before) || !Edges.Contains(After))
			{
				return;
			}
			TSet<FWorldStateParticipantId>& Successors = Edges.FindChecked(Before);
			if (!Successors.Contains(After))
			{
				Successors.Add(After);
				++InDegree.FindChecked(After);
			}
		};

		const TArray<FWorldStateParticipantId> ScopeIds = Scope.Array();
		for (const FWorldStateParticipantId& Left : ScopeIds)
		{
			for (const FWorldStateParticipantId& Right : ScopeIds)
			{
				if (PhaseOrder(Snapshot.Participants.FindChecked(Left).RestorePhase) < PhaseOrder(Snapshot.Participants.FindChecked(Right).RestorePhase))
				{
					AddEdge(Left, Right);
				}
			}
			const FWorldStateParticipantSnapshot& Participant = Snapshot.Participants.FindChecked(Left);
			for (const FWorldStateParticipantId& AfterId : Participant.RestoreAfter)
			{
				AddEdge(AfterId, Left);
			}
			for (const FWorldStateParticipantId& BeforeId : Participant.RestoreBefore)
			{
				AddEdge(Left, BeforeId);
			}
			if (Participant.bCaptureAttachment && Participant.AttachmentParentParticipantId.IsValid())
			{
				AddEdge(Participant.AttachmentParentParticipantId, Left);
			}
		}

		// Kahn's ready set is GUID-sorted at every iteration to make restore order reproducible.
		OutOrder.Reset();
		while (OutOrder.Num() < Scope.Num())
		{
			TArray<FWorldStateParticipantId> Ready;
			for (const TPair<FWorldStateParticipantId, int32>& Pair : InDegree)
			{
				if (Pair.Value == 0 && !OutOrder.Contains(Pair.Key))
				{
					Ready.Add(Pair.Key);
				}
			}
			SortIds(Ready);
			if (Ready.IsEmpty())
			{
				OutError = TEXT("Restore dependency graph contains a cycle or a phase/dependency conflict.");
				return false;
			}
			const FWorldStateParticipantId Next = Ready[0];
			OutOrder.Add(Next);
			InDegree.FindChecked(Next) = -1;
			for (const FWorldStateParticipantId& Successor : Edges.FindChecked(Next))
			{
				--InDegree.FindChecked(Successor);
			}
		}
		return true;
	}
}

/** Non-UObject pimpl that keeps mutable registry and private snapshot ownership out of the public header. */
struct FWorldStateSubsystemRuntime
{
	/** Registration change queued while callbacks, spawn or destruction can mutate the registry. */
	struct FPendingRegistryMutation
	{
		FWorldStateParticipantId ParticipantId;
		TWeakObjectPtr<UWorldStateParticipantComponent> Participant;
		bool bRegister = false;
	};

	/** Live UObjects are weak; snapshots below are immutable value-owned shared allocations. */
	TMap<FWorldStateParticipantId, TWeakObjectPtr<UWorldStateParticipantComponent>> Registry;
	TArray<FPendingRegistryMutation> PendingRegistryMutations;
	int32 RegistryMutationDeferralDepth = 0;
	TSet<FWorldStateParticipantId> DirtyParticipants;
	TSharedPtr<const FWorldStateSnapshot> Baseline;
	TMap<FWorldStateSnapshotId, TSharedPtr<const FWorldStateSnapshot>> RuntimeSnapshots;
	TMap<FName, TSharedPtr<IWorldStateSpawnStrategy>> SpawnStrategies;
	/** Exact Actor path to identity handoff, populated only around deferred spawn. */
	TMap<FSoftObjectPath, FWorldStateParticipantId> PendingRespawnIdentities;
	uint64 CaptureSequence = 0;
	bool bRestoreActive = false;
	FWorldStateRestoreLifecycleNativeEvent RestoreStartedNative;
	FWorldStateRestoreLifecycleNativeEvent RestoreScopeResolvedNative;
	FWorldStateRestoreTerminalNativeEvent RestoreCompletedNative;
	FWorldStateRestoreTerminalNativeEvent RestoreFailedNative;
};

UWorldStateSubsystem::UWorldStateSubsystem()
	: Runtime(new FWorldStateSubsystemRuntime())
{
}

UWorldStateSubsystem::~UWorldStateSubsystem()
{
	delete Runtime;
	Runtime = nullptr;
}

void UWorldStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// UWorldSubsystem instances may be reconstructed; always replace constructor-time storage with a clean state.
	delete Runtime;
	Runtime = new FWorldStateSubsystemRuntime();
	Runtime->SpawnStrategies.Add(UE::WorldState::Private::DefaultSpawnStrategyId, MakeShared<FWorldStateDefaultSpawnStrategy>());
	State = EWorldStateSubsystemState::Registering;
}

void UWorldStateSubsystem::Deinitialize()
{
	// Teardown first blocks new work, then releases every session/cache/reference symmetrically.
	State = EWorldStateSubsystemState::ShuttingDown;
	Runtime->bRestoreActive = false;
	Runtime->PendingRespawnIdentities.Reset();
	Runtime->PendingRegistryMutations.Reset();
	Runtime->RegistryMutationDeferralDepth = 0;
	Runtime->Registry.Reset();
	Runtime->DirtyParticipants.Reset();
	Runtime->Baseline.Reset();
	Runtime->RuntimeSnapshots.Reset();
	Runtime->SpawnStrategies.Reset();
	Runtime->RestoreStartedNative.Clear();
	Runtime->RestoreScopeResolvedNative.Clear();
	Runtime->RestoreCompletedNative.Clear();
	Runtime->RestoreFailedNative.Clear();
	OnWorldStateRestoreStarted.Clear();
	OnWorldStateRestoreScopeResolved.Clear();
	OnWorldStateRestoreCompleted.Clear();
	OnWorldStateRestoreFailed.Clear();
	Super::Deinitialize();
}

bool UWorldStateSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

bool UWorldStateSubsystem::RegisterParticipant(UWorldStateParticipantComponent* Participant)
{
	if (!Participant || State == EWorldStateSubsystemState::ShuttingDown || !Participant->ParticipantId.IsValid())
	{
		return false;
	}
	if (Runtime->RegistryMutationDeferralDepth > 0)
	{
		// Callbacks and Actor lifetime operations can re-enter BeginPlay/EndPlay during protected iteration.
		FWorldStateSubsystemRuntime::FPendingRegistryMutation& Mutation = Runtime->PendingRegistryMutations.AddDefaulted_GetRef();
		Mutation.ParticipantId = Participant->ParticipantId;
		Mutation.Participant = Participant;
		Mutation.bRegister = true;
		return true;
	}
	if (const TWeakObjectPtr<UWorldStateParticipantComponent>* Existing = Runtime->Registry.Find(Participant->ParticipantId))
	{
		if (Existing->Get() == Participant)
		{
			return true;
		}
		if (Existing->IsValid())
		{
			WORLDSTATE_LOG_ERROR("Duplicate ParticipantId %s on %s; already owned by %s.", *Participant->ParticipantId.ToString(), *GetNameSafe(Participant->GetOwner()), *GetNameSafe(Existing->Get()->GetOwner()));
			return false;
		}
	}
	Runtime->Registry.Add(Participant->ParticipantId, Participant);
	if (State == EWorldStateSubsystemState::ReadyWithoutBaseline)
	{
		State = EWorldStateSubsystemState::Registering;
	}
	return true;
}

void UWorldStateSubsystem::UnregisterParticipant(UWorldStateParticipantComponent* Participant)
{
	if (!Participant || !Runtime)
	{
		return;
	}
	if (Runtime->RegistryMutationDeferralDepth > 0)
	{
		FWorldStateSubsystemRuntime::FPendingRegistryMutation& Mutation = Runtime->PendingRegistryMutations.AddDefaulted_GetRef();
		Mutation.ParticipantId = Participant->ParticipantId;
		Mutation.Participant = Participant;
		Mutation.bRegister = false;
		return;
	}
	if (const TWeakObjectPtr<UWorldStateParticipantComponent>* Existing = Runtime->Registry.Find(Participant->ParticipantId); Existing && Existing->Get() == Participant)
	{
		Runtime->Registry.Remove(Participant->ParticipantId);
	}
	if (State == EWorldStateSubsystemState::ReadyWithoutBaseline)
	{
		State = EWorldStateSubsystemState::Registering;
	}
}

void UWorldStateSubsystem::BeginRegistryMutationDeferral()
{
	++Runtime->RegistryMutationDeferralDepth;
}

void UWorldStateSubsystem::EndRegistryMutationDeferral()
{
	if (Runtime->RegistryMutationDeferralDepth <= 0)
	{
		return;
	}
	--Runtime->RegistryMutationDeferralDepth;
	if (Runtime->RegistryMutationDeferralDepth == 0)
	{
		FlushPendingRegistryMutations();
	}
}

void UWorldStateSubsystem::FlushPendingRegistryMutations()
{
	if (Runtime->RegistryMutationDeferralDepth > 0 || Runtime->PendingRegistryMutations.IsEmpty())
	{
		return;
	}
	// Move the queue so applying one mutation may safely enqueue another without invalidating this iteration.
	TArray<FWorldStateSubsystemRuntime::FPendingRegistryMutation> Pending = MoveTemp(Runtime->PendingRegistryMutations);
	Runtime->PendingRegistryMutations.Reset();
	for (const FWorldStateSubsystemRuntime::FPendingRegistryMutation& Mutation : Pending)
	{
		if (Mutation.bRegister)
		{
			if (UWorldStateParticipantComponent* Participant = Mutation.Participant.Get())
			{
				RegisterParticipant(Participant);
			}
		}
		else if (const TWeakObjectPtr<UWorldStateParticipantComponent>* Existing = Runtime->Registry.Find(Mutation.ParticipantId))
		{
			if (!Existing->IsValid() || Existing->Get() == Mutation.Participant.Get())
			{
				Runtime->Registry.Remove(Mutation.ParticipantId);
			}
		}
	}
}

bool UWorldStateSubsystem::IsParticipantRegistered(const UWorldStateParticipantComponent* Participant) const
{
	if (!Participant)
	{
		return false;
	}
	const TWeakObjectPtr<UWorldStateParticipantComponent>* Existing = Runtime->Registry.Find(Participant->ParticipantId);
	return Existing && Existing->Get() == Participant;
}

void UWorldStateSubsystem::MarkParticipantDirty(const FWorldStateParticipantId& ParticipantId)
{
	if (Runtime->Registry.Contains(ParticipantId))
	{
		Runtime->DirtyParticipants.Add(ParticipantId);
	}
}

bool UWorldStateSubsystem::ClaimPendingRespawnIdentity(UWorldStateParticipantComponent* Participant)
{
	if (!Participant || !Participant->GetOwner())
	{
		return false;
	}
	const FSoftObjectPath ActorPath(Participant->GetOwner());
	// Actor path is known before the Participant Component registers and exactly matches the spawn descriptor key.
	if (const FWorldStateParticipantId* PendingId = Runtime->PendingRespawnIdentities.Find(ActorPath))
	{
		Participant->ParticipantId = *PendingId;
		return true;
	}
	return false;
}

FWorldStateOperationResult UWorldStateSubsystem::FinalizeWorldStateRegistration()
{
	FWorldStateOperationResult Result;
	if (!IsInGameThread() || State != EWorldStateSubsystemState::Registering)
	{
		Result.Status = State == EWorldStateSubsystemState::Capturing || State == EWorldStateSubsystemState::Restoring
			? EWorldStateOperationStatus::RejectedBusy
			: EWorldStateOperationStatus::RejectedInvalidRequest;
		Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("InvalidSubsystemState"), TEXT("Registration can only be finalized while the subsystem is Registering.")));
		return Result;
	}

	// Finalization is a validation barrier: baseline capture cannot observe stale weak entries or invalid authoring.
	for (auto It = Runtime->Registry.CreateIterator(); It; ++It)
	{
		UWorldStateParticipantComponent* Participant = It.Value().Get();
		if (!Participant)
		{
			It.RemoveCurrent();
			continue;
		}
		FWorldStateOperationResult Validation = Participant->ValidateCapturedProperties();
		Result.Issues.Append(Validation.Issues);
	}
	if (Result.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue) { return Issue.Severity == EWorldStateIssueSeverity::Error; }))
	{
		Result.Status = EWorldStateOperationStatus::RejectedInvalidRequest;
		return Result;
	}
	State = EWorldStateSubsystemState::ReadyWithoutBaseline;
	Result.Status = Result.Issues.IsEmpty() ? EWorldStateOperationStatus::Success : EWorldStateOperationStatus::SuccessWithWarnings;
	return Result;
}

namespace UE::WorldState::Private
{
	/** Freezes a deterministic list of live participants for the capture transaction. */
	bool ResolveCaptureScope(
		const FWorldStateSubsystemRuntime& Runtime,
		const FWorldStateParticipantScope& Scope,
		TArray<UWorldStateParticipantComponent*>& OutParticipants,
		FString& OutError)
	{
		OutParticipants.Reset();
		for (const TPair<FWorldStateParticipantId, TWeakObjectPtr<UWorldStateParticipantComponent>>& Pair : Runtime.Registry)
		{
			UWorldStateParticipantComponent* Participant = Pair.Value.Get();
			if (!Participant)
			{
				continue;
			}
			bool bInclude = false;
			switch (Scope.Kind)
			{
			case EWorldStateRestoreScopeKind::CompleteSnapshot: bInclude = true; break;
			case EWorldStateRestoreScopeKind::ParticipantIds: bInclude = Scope.ParticipantIds.Contains(Pair.Key); break;
			case EWorldStateRestoreScopeKind::Groups: bInclude = ContainsAnyGroup(Participant->Groups, Scope.Groups); break;
			case EWorldStateRestoreScopeKind::DirtyParticipants: bInclude = Runtime.DirtyParticipants.Contains(Pair.Key); break;
			default: break;
			}
			if (bInclude)
			{
				OutParticipants.Add(Participant);
			}
		}
		OutParticipants.Sort([](const UWorldStateParticipantComponent& Left, const UWorldStateParticipantComponent& Right)
		{
			return Left.ParticipantId.ToString() < Right.ParticipantId.ToString();
		});
		if (OutParticipants.IsEmpty())
		{
			OutError = TEXT("The capture scope contains no registered participants.");
			return false;
		}
		if (Scope.Kind == EWorldStateRestoreScopeKind::ParticipantIds)
		{
			for (const FWorldStateParticipantId& Id : Scope.ParticipantIds)
			{
				if (!Runtime.Registry.Contains(Id))
				{
					OutError = FString::Printf(TEXT("Requested participant %s is not registered."), *Id.ToString());
					return false;
				}
			}
		}
		return true;
	}
}

FWorldStateCaptureResult UWorldStateSubsystem::CaptureBaseline(const FWorldStateCaptureRequest& Request)
{
	return CaptureSnapshotInternal(Request, true);
}

FWorldStateCaptureResult UWorldStateSubsystem::CaptureRuntimeSnapshot(const FWorldStateCaptureRequest& Request)
{
	return CaptureSnapshotInternal(Request, false);
}

FWorldStateCaptureResult UWorldStateSubsystem::CaptureSnapshotInternal(const FWorldStateCaptureRequest& Request, bool bBaseline)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WorldState_CaptureSnapshot);
	FWorldStateCaptureResult Result;
	if (!IsInGameThread())
	{
		Result.Status = EWorldStateOperationStatus::RejectedInvalidRequest;
		Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("GameThreadRequired"), TEXT("World State capture must run on the Game Thread.")));
		return Result;
	}
	if (Runtime->bRestoreActive || State == EWorldStateSubsystemState::Capturing || State == EWorldStateSubsystemState::Restoring)
	{
		Result.Status = EWorldStateOperationStatus::RejectedBusy;
		return Result;
	}
	if ((bBaseline && State != EWorldStateSubsystemState::ReadyWithoutBaseline) || (!bBaseline && State != EWorldStateSubsystemState::Ready))
	{
		Result.Status = EWorldStateOperationStatus::RejectedInvalidRequest;
		Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("InvalidSubsystemState"), bBaseline ? TEXT("Finalize registration before capturing the baseline, and capture it only once.") : TEXT("Runtime snapshots require a valid baseline and Ready state.")));
		return Result;
	}
	if (bBaseline && Runtime->Baseline.IsValid())
	{
		Result.Status = EWorldStateOperationStatus::RejectedInvalidRequest;
		Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("ImmutableBaseline"), TEXT("The baseline is immutable and cannot be overwritten.")));
		return Result;
	}

	// Validate request and freeze scope before entering Capturing or invoking user callbacks.
	TArray<UWorldStateParticipantComponent*> Participants;
	FString ScopeError;
	if (!UE::WorldState::Private::ResolveCaptureScope(*Runtime, Request.Scope, Participants, ScopeError))
	{
		Result.Status = EWorldStateOperationStatus::RejectedInvalidRequest;
		Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("InvalidCaptureScope"), ScopeError));
		return Result;
	}

	const EWorldStateSubsystemState PreviousState = State;
	State = EWorldStateSubsystemState::Capturing;
	// Build a private draft by value; no valid snapshot collection is touched until all required work succeeds.
	FWorldStateSnapshot Snapshot;
	Snapshot.SnapshotId = FWorldStateSnapshotId::NewId();
	Snapshot.FormatVersion = UE::WorldState::Private::SnapshotFormatVersion;
	Snapshot.CaptureSequence = ++Runtime->CaptureSequence;
	Snapshot.WorldPackageName = GetWorld()->GetPackage()->GetFName();
	Snapshot.Label = Request.Label;
	Result.SnapshotId = Snapshot.SnapshotId;
	TArray<TWeakObjectPtr<UWorldStateParticipantComponent>> CapturedParticipants;
	bool bFailed = false;

	for (UWorldStateParticipantComponent* Participant : Participants)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(WorldState_CaptureParticipant);
		if (!Participant || !Participant->GetOwner())
		{
			continue;
		}
		FWorldStateParticipantResult ParticipantResult;
		ParticipantResult.ParticipantId = Participant->ParticipantId;
		// Authored identity/type validation precedes callbacks so avoidable failures cannot mutate participant state.
		FWorldStateOperationResult Validation = Participant->ValidateCapturedProperties();
		ParticipantResult.Issues.Append(Validation.Issues);
		if (!Validation.IsSuccess())
		{
			ParticipantResult.bSucceeded = false;
			Result.ParticipantResults.Add(ParticipantResult);
			if (bBaseline || Request.FailurePolicy == EWorldStateCaptureFailurePolicy::FailEntireSnapshot)
			{
				bFailed = true;
				break;
			}
			if (Request.FailurePolicy == EWorldStateCaptureFailurePolicy::SkipInvalidParticipant)
			{
				continue;
			}
		}
		BeginRegistryMutationDeferral();
		Participant->OnWorldStatePreCapture.Broadcast(Participant->ParticipantId);
		EndRegistryMutationDeferral();
		if (!IsParticipantRegistered(Participant))
		{
			ParticipantResult.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("ParticipantUnregisteredDuringCapture"), TEXT("Participant unregistered during its pre-capture callback."), Participant->ParticipantId));
			Result.ParticipantResults.Add(ParticipantResult);
			bFailed = true;
			break;
		}

		// Structural data is captured before property payloads and contains no live object pointers.
		AActor* Actor = Participant->GetOwner();
		FWorldStateParticipantSnapshot ParticipantSnapshot;
		ParticipantSnapshot.ParticipantId = Participant->ParticipantId;
		ParticipantSnapshot.bCaptureExistence = Participant->bCaptureExistence;
		ParticipantSnapshot.bCaptureActorTransform = Participant->bCaptureActorTransform;
		ParticipantSnapshot.bCaptureAttachment = Participant->bCaptureAttachment;
		ParticipantSnapshot.ActorTransform = Actor->GetActorTransform();
		ParticipantSnapshot.ExistencePolicy = Participant->ExistencePolicy;
		ParticipantSnapshot.RestorePhase = Participant->RestorePhase;
		ParticipantSnapshot.RestoreAfter = Participant->RestoreAfter;
		ParticipantSnapshot.RestoreBefore = Participant->RestoreBefore;
		ParticipantSnapshot.Groups = Participant->Groups;
		ParticipantSnapshot.SpawnDescriptor.ParticipantId = Participant->ParticipantId;
		ParticipantSnapshot.SpawnDescriptor.ActorClass = Actor->GetClass();
		ParticipantSnapshot.SpawnDescriptor.CapturedObjectPath = FSoftObjectPath(Actor);
		ParticipantSnapshot.SpawnDescriptor.ActorName = Actor->GetFName();
		ParticipantSnapshot.SpawnDescriptor.LevelPackageName = Actor->GetLevel()->GetPackage()->GetFName();
		ParticipantSnapshot.SpawnDescriptor.Transform = Actor->GetActorTransform();
		ParticipantSnapshot.SpawnDescriptor.StrategyId = Participant->SpawnStrategyId.IsNone() ? UE::WorldState::Private::DefaultSpawnStrategyId : Participant->SpawnStrategyId;
		ParticipantSnapshot.SpawnDescriptor.bWasRuntimeSpawned = !Actor->HasAnyFlags(RF_WasLoaded);

		if (Participant->bCaptureAttachment)
		{
			if (AActor* ParentActor = Actor->GetAttachParentActor())
			{
				ParticipantSnapshot.bHadActorAttachment = true;
				if (UWorldStateParticipantComponent* ParentParticipant = ParentActor->FindComponentByClass<UWorldStateParticipantComponent>())
				{
					ParticipantSnapshot.AttachmentParentParticipantId = ParentParticipant->ParticipantId;
				}
				else
				{
					ParticipantSnapshot.AttachmentParentActorPath = FSoftObjectPath(ParentActor);
				}
				if (USceneComponent* AttachParent = Actor->GetRootComponent() ? Actor->GetRootComponent()->GetAttachParent() : nullptr)
				{
					ParticipantSnapshot.AttachmentParentComponentName = AttachParent->GetFName();
					ParticipantSnapshot.AttachmentSocketName = Actor->GetRootComponent()->GetAttachSocketName();
				}
			}
		}

		for (const FWorldStateSceneComponentCaptureSelection& Selection : Participant->SceneComponentCaptureSelections)
		{
			if (!Selection.bEnabled || !Selection.bCaptureRelativeTransform)
			{
				continue;
			}
			USceneComponent* SceneComponent = Cast<USceneComponent>(Participant->ResolveCaptureSource(Selection.CaptureSourceId));
			if (!SceneComponent)
			{
				ParticipantResult.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("MissingSceneComponent"), TEXT("Selected Scene Component disappeared during capture."), Participant->ParticipantId, Selection.CaptureSourceId));
				bFailed = bBaseline || Request.FailurePolicy != EWorldStateCaptureFailurePolicy::SkipInvalidProperty;
				if (bFailed) break;
				continue;
			}
			FWorldStateSceneComponentSnapshot& Structural = ParticipantSnapshot.SceneComponents.AddDefaulted_GetRef();
			Structural.CaptureSourceId = Selection.CaptureSourceId;
			Structural.RelativeTransform = SceneComponent->GetRelativeTransform();
			Structural.bStrictParentValidation = Selection.bStrictParentValidation;
			if (USceneComponent* Parent = SceneComponent->GetAttachParent())
			{
				Structural.bHadParent = true;
				Structural.SocketName = SceneComponent->GetAttachSocketName();
				if (Parent->GetOwner() == Actor)
				{
					Structural.bParentOwnedByActor = true;
					Structural.ParentSourceId = FWorldStateCaptureSourceId::Component(Parent->GetFName());
				}
				else
				{
					Structural.ExternalParentPath = FSoftObjectPath(Parent);
				}
			}
		}
		if (bFailed)
		{
			Result.ParticipantResults.Add(ParticipantResult);
			break;
		}

		// Each root property owns an independent archive payload and compatibility signature.
		for (const FWorldStatePropertySelection& Selection : Participant->CapturedProperties)
		{
			if (!Selection.bEnabled)
			{
				continue;
			}
			FWorldStatePropertyResult PropertyResult;
			PropertyResult.ParticipantId = Participant->ParticipantId;
			PropertyResult.CaptureSourceId = Selection.CaptureSourceId;
			PropertyResult.PropertyName = Selection.PropertyName;
			UObject* Source = Participant->ResolveCaptureSource(Selection.CaptureSourceId);
			FProperty* Property = Source ? FindFProperty<FProperty>(Source->GetClass(), Selection.PropertyName) : nullptr;
			FWorldStatePropertyValidationResult PropertyValidation = FWorldStatePropertySerializer::Validate(Property);
			FString SerializeError;
			FWorldStateCapturedProperty CapturedProperty;
			if (!Source || !PropertyValidation.IsValid() || !FWorldStatePropertySerializer::Serialize(Property, Source, CapturedProperty.Payload, SerializeError))
			{
				PropertyResult.bSucceeded = false;
				PropertyResult.Message = !SerializeError.IsEmpty() ? SerializeError : PropertyValidation.Message;
				ParticipantResult.PropertyResults.Add(PropertyResult);
				ParticipantResult.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("PropertyCaptureFailed"), PropertyResult.Message, Participant->ParticipantId, Selection.CaptureSourceId, Selection.PropertyName));
				if (bBaseline || Request.FailurePolicy != EWorldStateCaptureFailurePolicy::SkipInvalidProperty)
				{
					bFailed = true;
					break;
				}
				continue;
			}
			CapturedProperty.CaptureSourceId = Selection.CaptureSourceId;
			CapturedProperty.PropertyName = Selection.PropertyName;
			CapturedProperty.SourceClass = Source->GetClass();
			CapturedProperty.TypeSignature = PropertyValidation.TypeSignature;
			CapturedProperty.ReferenceRequirement = Selection.ReferenceRequirement;
			CapturedProperty.RestorePhase = Selection.bOverrideRestorePhase ? Selection.RestorePhase : Participant->RestorePhase;
			ParticipantSnapshot.Properties.Add(MoveTemp(CapturedProperty));
			PropertyResult.bSucceeded = true;
			ParticipantResult.PropertyResults.Add(PropertyResult);
		}

		ParticipantResult.bSucceeded = !bFailed;
		Result.ParticipantResults.Add(ParticipantResult);
		if (bFailed)
		{
			break;
		}
		Snapshot.Participants.Add(ParticipantSnapshot.ParticipantId, MoveTemp(ParticipantSnapshot));
		CapturedParticipants.Add(Participant);
	}

	if (bFailed || Snapshot.Participants.IsEmpty())
	{
		State = PreviousState;
		Result.Status = EWorldStateOperationStatus::CaptureFailed;
		Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("SnapshotNotPublished"), TEXT("Capture failed; no partial snapshot was published.")));
		return Result;
	}

	// A const shared allocation is the atomic publication boundary for the completed transaction.
	TSharedPtr<const FWorldStateSnapshot> Published = MakeShared<FWorldStateSnapshot>(MoveTemp(Snapshot));
	if (bBaseline)
	{
		Runtime->Baseline = Published;
	}
	else
	{
		Runtime->RuntimeSnapshots.Add(Published->SnapshotId, Published);
	}
	State = EWorldStateSubsystemState::Ready;
	// Post-capture callbacks observe only already-published immutable data.
	for (const TWeakObjectPtr<UWorldStateParticipantComponent>& Participant : CapturedParticipants)
	{
		if (Participant.IsValid())
		{
			BeginRegistryMutationDeferral();
			Participant->OnWorldStateCaptured.Broadcast(Participant->ParticipantId);
			EndRegistryMutationDeferral();
		}
	}
	Result.Status = Result.Issues.IsEmpty() ? EWorldStateOperationStatus::Success : EWorldStateOperationStatus::SuccessWithWarnings;
	return Result;
}

bool UWorldStateSubsystem::HasBaseline() const
{
	return Runtime && Runtime->Baseline.IsValid();
}

bool UWorldStateSubsystem::GetSnapshotSummary(FWorldStateSnapshotId SnapshotId, FWorldStateSnapshotSummary& OutSummary) const
{
	const FWorldStateSnapshot* Snapshot = nullptr;
	bool bBaseline = false;
	if (Runtime->Baseline.IsValid() && Runtime->Baseline->SnapshotId == SnapshotId)
	{
		Snapshot = Runtime->Baseline.Get();
		bBaseline = true;
	}
	else if (const TSharedPtr<const FWorldStateSnapshot>* Found = Runtime->RuntimeSnapshots.Find(SnapshotId))
	{
		Snapshot = Found->Get();
	}
	if (!Snapshot)
	{
		return false;
	}
	OutSummary.SnapshotId = Snapshot->SnapshotId;
	OutSummary.Label = Snapshot->Label;
	OutSummary.ParticipantCount = Snapshot->Participants.Num();
	OutSummary.PayloadBytes = Snapshot->GetPayloadBytes();
	OutSummary.bBaseline = bBaseline;
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool UWorldStateSubsystem::CorruptSnapshotPropertyPayloadForTests(
	FWorldStateSnapshotId SnapshotId,
	FWorldStateParticipantId ParticipantId,
	FName PropertyName)
{
	TSharedPtr<const FWorldStateSnapshot>* FoundSnapshot = Runtime ? Runtime->RuntimeSnapshots.Find(SnapshotId) : nullptr;
	FWorldStateSnapshot* Snapshot = FoundSnapshot ? const_cast<FWorldStateSnapshot*>(FoundSnapshot->Get()) : nullptr;
	FWorldStateParticipantSnapshot* Participant = Snapshot ? Snapshot->Participants.Find(ParticipantId) : nullptr;
	FWorldStateCapturedProperty* Property = Participant ? Participant->Properties.FindByPredicate([PropertyName](const FWorldStateCapturedProperty& Candidate)
	{
		return Candidate.PropertyName == PropertyName;
	}) : nullptr;
	if (!Property)
	{
		return false;
	}
	Property->Payload.Reset();
	return true;
}
#endif

TArray<FWorldStateParticipantSummary> UWorldStateSubsystem::GetParticipantStateSummaries() const
{
	TArray<FWorldStateParticipantSummary> Summaries;
	for (const TPair<FWorldStateParticipantId, TWeakObjectPtr<UWorldStateParticipantComponent>>& Pair : Runtime->Registry)
	{
		if (const UWorldStateParticipantComponent* Participant = Pair.Value.Get())
		{
			FWorldStateParticipantSummary& Summary = Summaries.AddDefaulted_GetRef();
			Summary.ParticipantId = Pair.Key;
			Summary.ActorPath = GetPathNameSafe(Participant->GetOwner());
			Summary.bRegistered = true;
			Summary.bDirty = Runtime->DirtyParticipants.Contains(Pair.Key);
		}
	}
	Summaries.Sort([](const FWorldStateParticipantSummary& Left, const FWorldStateParticipantSummary& Right)
	{
		return Left.ParticipantId.ToString() < Right.ParticipantId.ToString();
	});
	return Summaries;
}

void UWorldStateSubsystem::DumpWorldStateToLog() const
{
	WORLDSTATE_LOG_INFO("World=%s State=%d Participants=%d Baseline=%s RuntimeSnapshots=%d RestoreActive=%s",
		*GetPathNameSafe(GetWorld()),
		static_cast<int32>(State),
		Runtime->Registry.Num(),
		Runtime->Baseline.IsValid() ? TEXT("yes") : TEXT("no"),
		Runtime->RuntimeSnapshots.Num(),
		Runtime->bRestoreActive ? TEXT("yes") : TEXT("no"));
	for (const FWorldStateParticipantSummary& Summary : GetParticipantStateSummaries())
	{
		WORLDSTATE_LOG_INFO("Participant=%s Actor=%s Dirty=%s", *Summary.ParticipantId.ToString(), *Summary.ActorPath, Summary.bDirty ? TEXT("yes") : TEXT("no"));
	}
}

bool UWorldStateSubsystem::RegisterSpawnStrategy(FName StrategyId, TSharedRef<IWorldStateSpawnStrategy> Strategy)
{
	if (!IsInGameThread() || StrategyId.IsNone() || Runtime->SpawnStrategies.Contains(StrategyId) || Runtime->bRestoreActive)
	{
		return false;
	}
	Runtime->SpawnStrategies.Add(StrategyId, Strategy);
	return true;
}

bool UWorldStateSubsystem::UnregisterSpawnStrategy(FName StrategyId)
{
	if (!IsInGameThread() || StrategyId == UE::WorldState::Private::DefaultSpawnStrategyId || Runtime->bRestoreActive)
	{
		return false;
	}
	return Runtime->SpawnStrategies.Remove(StrategyId) > 0;
}

FWorldStateRestoreLifecycleNativeEvent& UWorldStateSubsystem::OnRestoreStartedNative() { return Runtime->RestoreStartedNative; }
FWorldStateRestoreLifecycleNativeEvent& UWorldStateSubsystem::OnRestoreScopeResolvedNative() { return Runtime->RestoreScopeResolvedNative; }
FWorldStateRestoreTerminalNativeEvent& UWorldStateSubsystem::OnRestoreCompletedNative() { return Runtime->RestoreCompletedNative; }
FWorldStateRestoreTerminalNativeEvent& UWorldStateSubsystem::OnRestoreFailedNative() { return Runtime->RestoreFailedNative; }

FWorldStateRestoreResult UWorldStateSubsystem::RestoreBaseline(const FWorldStateRestoreRequest& Request)
{
	FWorldStateRestoreRequest BaselineRequest = Request;
	if (Runtime->Baseline.IsValid())
	{
		BaselineRequest.SnapshotId = Runtime->Baseline->SnapshotId;
	}
	return RestoreSnapshotInternal(BaselineRequest, true);
}

FWorldStateRestoreResult UWorldStateSubsystem::RestoreSnapshot(const FWorldStateRestoreRequest& Request)
{
	return RestoreSnapshotInternal(Request, false);
}

FWorldStateRestoreResult UWorldStateSubsystem::RestoreParticipants(
	FWorldStateSnapshotId SnapshotId,
	const TArray<FWorldStateParticipantId>& ParticipantIds,
	FWorldStateRestoreRequest Request)
{
	Request.SnapshotId = SnapshotId;
	Request.Scope.Kind = EWorldStateRestoreScopeKind::ParticipantIds;
	Request.Scope.ParticipantIds = ParticipantIds;
	return RestoreSnapshotInternal(Request, false);
}

FWorldStateRestoreResult UWorldStateSubsystem::RestoreSnapshotInternal(const FWorldStateRestoreRequest& Request, bool bBaselineRequest)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WorldState_PreflightRestore);
	FWorldStateRestoreResult Result;
	const double StartSeconds = FPlatformTime::Seconds();
	if (!IsInGameThread() || State == EWorldStateSubsystemState::ShuttingDown)
	{
		Result.Status = State == EWorldStateSubsystemState::ShuttingDown ? EWorldStateOperationStatus::WorldTeardown : EWorldStateOperationStatus::RejectedInvalidRequest;
		return Result;
	}
	if (Runtime->bRestoreActive || State == EWorldStateSubsystemState::Capturing || State == EWorldStateSubsystemState::Restoring)
	{
		Result.Status = EWorldStateOperationStatus::RejectedBusy;
		return Result;
	}
	if (State != EWorldStateSubsystemState::Ready && State != EWorldStateSubsystemState::Failed)
	{
		Result.Status = EWorldStateOperationStatus::RejectedInvalidRequest;
		return Result;
	}

	const FWorldStateSnapshot* Snapshot = nullptr;
	bool bSourceIsBaseline = false;
	if (Runtime->Baseline.IsValid() && Runtime->Baseline->SnapshotId == Request.SnapshotId)
	{
		Snapshot = Runtime->Baseline.Get();
		bSourceIsBaseline = true;
	}
	else if (!bBaselineRequest)
	{
		if (const TSharedPtr<const FWorldStateSnapshot>* Found = Runtime->RuntimeSnapshots.Find(Request.SnapshotId))
		{
			Snapshot = Found->Get();
		}
	}
	if (!Snapshot || (bBaselineRequest && !bSourceIsBaseline))
	{
		Result.Status = EWorldStateOperationStatus::RejectedInvalidRequest;
		Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("MissingSnapshot"), TEXT("The requested snapshot does not exist in this world.")));
		return Result;
	}
	if (Snapshot->FormatVersion != UE::WorldState::Private::SnapshotFormatVersion || Snapshot->WorldPackageName != GetWorld()->GetPackage()->GetFName())
	{
		Result.Status = EWorldStateOperationStatus::RejectedInvalidRequest;
		Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("IncompatibleSnapshot"), TEXT("Snapshot format or world identity is incompatible.")));
		return Result;
	}

	// Acceptance begins only after world, state and snapshot identity validation; rejected requests emit no events.
	const EWorldStateSubsystemState PreviousState = State;
	State = EWorldStateSubsystemState::Restoring;
	Runtime->bRestoreActive = true;
	Result.RestoreSessionId = FWorldStateRestoreSessionId::NewId();
	FWorldStateRestoreLifecycleContext Context;
	Context.RestoreSessionId = Result.RestoreSessionId;
	Context.SnapshotId = Snapshot->SnapshotId;
	Context.RequestedScope = Request.Scope.Kind;
	Context.bBaseline = bSourceIsBaseline;
	Context.Stage = EWorldStateRestoreStage::Preflight;
	BeginRegistryMutationDeferral();
	OnWorldStateRestoreStarted.Broadcast(Context);
	Runtime->RestoreStartedNative.Broadcast(Context);
	EndRegistryMutationDeferral();

	// Every accepted exit funnels through one terminal path, preserving session ID and exactly-once notification.
	auto Finalize = [&](bool bSuccess, EWorldStateRestoreStage FailureStage)
	{
		Result.DurationSeconds = FPlatformTime::Seconds() - StartSeconds;
		if (bSuccess)
		{
			const bool bHasWarnings = Result.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue) { return Issue.Severity == EWorldStateIssueSeverity::Warning; });
			Result.Status = bHasWarnings ? EWorldStateOperationStatus::SuccessWithWarnings : EWorldStateOperationStatus::Success;
			Result.FailureStage = EWorldStateRestoreStage::None;
			Context.Stage = EWorldStateRestoreStage::Completed;
			Context.bMutationBegan = Result.bMutationBegan;
			BeginRegistryMutationDeferral();
			OnWorldStateRestoreCompleted.Broadcast(Result);
			Runtime->RestoreCompletedNative.Broadcast(Result);
			EndRegistryMutationDeferral();
			State = EWorldStateSubsystemState::Ready;
		}
		else
		{
			TSet<FWorldStateParticipantId> FailedParticipants;
			for (const FWorldStateParticipantResult& ParticipantResult : Result.ParticipantResults)
			{
				if (!ParticipantResult.bSucceeded && ParticipantResult.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue)
				{
					return Issue.Severity == EWorldStateIssueSeverity::Error;
				}))
				{
					FailedParticipants.Add(ParticipantResult.ParticipantId);
				}
			}
			for (const FWorldStateIssue& Issue : Result.Issues)
			{
				if (Issue.Severity == EWorldStateIssueSeverity::Error && Issue.ParticipantId.IsValid())
				{
					FailedParticipants.Add(Issue.ParticipantId);
					if (!Result.ParticipantResults.ContainsByPredicate([&Issue](const FWorldStateParticipantResult& Item)
					{
						return Item.ParticipantId == Issue.ParticipantId;
					}))
					{
						FWorldStateParticipantResult& ParticipantResult = Result.ParticipantResults.AddDefaulted_GetRef();
						ParticipantResult.ParticipantId = Issue.ParticipantId;
						ParticipantResult.Issues.Add(Issue);
					}
				}
			}
			for (const FWorldStateParticipantId& ParticipantId : FailedParticipants)
			{
				if (UWorldStateParticipantComponent* Participant = Runtime->Registry.FindRef(ParticipantId).Get())
				{
					if (const FWorldStateParticipantResult* ParticipantResult = Result.ParticipantResults.FindByPredicate([&ParticipantId](const FWorldStateParticipantResult& Item)
					{
						return Item.ParticipantId == ParticipantId;
					}))
					{
						BeginRegistryMutationDeferral();
						Participant->OnWorldStateRestoreFailed.Broadcast(*ParticipantResult);
						EndRegistryMutationDeferral();
					}
				}
			}
			Result.Status = FailureStage == EWorldStateRestoreStage::Preflight || FailureStage == EWorldStateRestoreStage::ScopeConstruction
				? EWorldStateOperationStatus::PreflightFailed
				: EWorldStateOperationStatus::RestoreFailed;
			Result.FailureStage = FailureStage;
			Result.bPartiallyRestored = Result.bMutationBegan;
			Context.Stage = EWorldStateRestoreStage::Failed;
			Context.bMutationBegan = Result.bMutationBegan;
			BeginRegistryMutationDeferral();
			OnWorldStateRestoreFailed.Broadcast(Result);
			Runtime->RestoreFailedNative.Broadcast(Result);
			EndRegistryMutationDeferral();
			State = Result.bMutationBegan ? EWorldStateSubsystemState::Failed : PreviousState;
		}
		Runtime->bRestoreActive = false;
	};

	// Scope expansion and graph ordering are pure preflight and happen before any world mutation.
	TArray<FWorldStateParticipantId> RestoreOrder;
	FString OrderError;
	if (!UE::WorldState::Private::BuildRestoreOrder(*Snapshot, Request, Runtime->DirtyParticipants, RestoreOrder, Result.RequestedParticipantCount, OrderError))
	{
		Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("InvalidRestoreScope"), OrderError));
		Finalize(false, EWorldStateRestoreStage::ScopeConstruction);
		return Result;
	}

	// Validate every live property signature and every missing Actor strategy before announcing resolved scope.
	bool bDeferredPropertyFailure = false;
	for (const FWorldStateParticipantId& Id : RestoreOrder)
	{
		const FWorldStateParticipantSnapshot& ParticipantSnapshot = Snapshot->Participants.FindChecked(Id);
		UWorldStateParticipantComponent* Participant = Runtime->Registry.FindRef(Id).Get();
		if (Participant)
		{
			for (const FWorldStateCapturedProperty& CapturedProperty : ParticipantSnapshot.Properties)
			{
				UObject* Source = Participant->ResolveCaptureSource(CapturedProperty.CaptureSourceId);
				FProperty* Property = Source ? FindFProperty<FProperty>(Source->GetClass(), CapturedProperty.PropertyName) : nullptr;
				if (!UE::WorldState::Private::MatchesCapturedSourceClass(Source, CapturedProperty.SourceClass) ||
					!Property ||
					FWorldStatePropertySerializer::BuildTypeSignature(Property) != CapturedProperty.TypeSignature)
				{
					if (Request.MissingPropertyPolicy == EWorldStateMissingPropertyPolicy::FailRestore)
					{
						Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("PreflightPropertyMismatch"), TEXT("A required source/property is missing or type-incompatible."), Id, CapturedProperty.CaptureSourceId, CapturedProperty.PropertyName));
						Finalize(false, EWorldStateRestoreStage::Preflight);
						return Result;
					}
					Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Warning, TEXT("SkippedProperty"), TEXT("A missing or incompatible property will be skipped."), Id, CapturedProperty.CaptureSourceId, CapturedProperty.PropertyName));
				}
			}
		}
		else if (ParticipantSnapshot.bCaptureExistence && UE::WorldState::Private::IsRespawnPolicy(ParticipantSnapshot.ExistencePolicy))
		{
			const TSharedPtr<IWorldStateSpawnStrategy>* Strategy = Runtime->SpawnStrategies.Find(ParticipantSnapshot.SpawnDescriptor.StrategyId);
			FString SpawnError;
			if (!Strategy || !(*Strategy)->CanSpawn(*GetWorld(), ParticipantSnapshot.SpawnDescriptor, SpawnError))
			{
				Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("MissingSpawnStrategy"), Strategy ? SpawnError : TEXT("No spawn strategy is registered for the captured participant."), Id));
				Finalize(false, EWorldStateRestoreStage::Preflight);
				return Result;
			}
		}
		else if (ParticipantSnapshot.ExistencePolicy == EWorldStateExistencePolicy::ExistingOnly)
		{
			Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("MissingParticipant"), TEXT("A required ExistingOnly participant is missing."), Id));
			Finalize(false, EWorldStateRestoreStage::Preflight);
			return Result;
		}
	}

	Context.ResolvedParticipantCount = RestoreOrder.Num();
	Context.Stage = EWorldStateRestoreStage::ScopeConstruction;
	BeginRegistryMutationDeferral();
	OnWorldStateRestoreScopeResolved.Broadcast(Context);
	Runtime->RestoreScopeResolvedNative.Broadcast(Context);
	EndRegistryMutationDeferral();

	// Existence runs first because later structure, property and soft-reference phases require final Actor instances.
	TRACE_CPUPROFILER_EVENT_SCOPE(WorldState_RestoreExistence);
	for (const FWorldStateParticipantId& Id : RestoreOrder)
	{
		const FWorldStateParticipantSnapshot& ParticipantSnapshot = Snapshot->Participants.FindChecked(Id);
		if (Runtime->Registry.FindRef(Id).IsValid())
		{
			continue;
		}
		if (!ParticipantSnapshot.bCaptureExistence || !UE::WorldState::Private::IsRespawnPolicy(ParticipantSnapshot.ExistencePolicy))
		{
			Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Warning, TEXT("ParticipantNotRestored"), TEXT("Missing Persistent or ExternallyManaged participant was left to its owner."), Id));
			continue;
		}

		const TSharedPtr<IWorldStateSpawnStrategy> Strategy = Runtime->SpawnStrategies.FindChecked(ParticipantSnapshot.SpawnDescriptor.StrategyId);
		// Stage identity before deferred spawn; BeginPlay consumes it when the new Component registers.
		Runtime->PendingRespawnIdentities.Add(ParticipantSnapshot.SpawnDescriptor.CapturedObjectPath, Id);
		FString SpawnError;
		BeginRegistryMutationDeferral();
		AActor* SpawnedActor = Strategy->Spawn(*GetWorld(), ParticipantSnapshot.SpawnDescriptor, SpawnError);
		EndRegistryMutationDeferral();
		Runtime->PendingRespawnIdentities.Remove(ParticipantSnapshot.SpawnDescriptor.CapturedObjectPath);
		Result.bMutationBegan = true;
		if (!SpawnedActor)
		{
			Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("RespawnFailed"), SpawnError.IsEmpty() ? TEXT("The spawn strategy did not recreate the Actor.") : SpawnError, Id));
			Finalize(false, EWorldStateRestoreStage::Existence);
			return Result;
		}
		const bool bPreservedCapturedPath = FSoftObjectPath(SpawnedActor) == ParticipantSnapshot.SpawnDescriptor.CapturedObjectPath;
		if (!bPreservedCapturedPath && ParticipantSnapshot.SpawnDescriptor.StrategyId == UE::WorldState::Private::DefaultSpawnStrategyId)
		{
			Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("RespawnFailed"), TEXT("The default spawn strategy did not preserve the captured Actor path."), Id));
			Finalize(false, EWorldStateRestoreStage::Existence);
			return Result;
		}
		if (!bPreservedCapturedPath)
		{
			Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Warning, TEXT("RespawnPathChanged"), TEXT("The external spawn strategy recreated the participant at a different object path; captured soft references may remain unresolved."), Id));
		}
		UWorldStateParticipantComponent* SpawnedParticipant = SpawnedActor->FindComponentByClass<UWorldStateParticipantComponent>();
		if (!SpawnedParticipant)
		{
			Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("MissingParticipantComponent"), TEXT("Respawned Actor has no World State Participant Component."), Id));
			Finalize(false, EWorldStateRestoreStage::Existence);
			return Result;
		}
		if (SpawnedParticipant->ParticipantId != Id)
		{
			UnregisterParticipant(SpawnedParticipant);
			SpawnedParticipant->ParticipantId = Id;
			RegisterParticipant(SpawnedParticipant);
		}
	}

	// Destruction is meaningful only for a complete snapshot; partial scopes never imply global absence.
	if (Request.Scope.Kind == EWorldStateRestoreScopeKind::CompleteSnapshot)
	{
		TArray<TWeakObjectPtr<UWorldStateParticipantComponent>> CurrentParticipants;
		Runtime->Registry.GenerateValueArray(CurrentParticipants);
		BeginRegistryMutationDeferral();
		for (const TWeakObjectPtr<UWorldStateParticipantComponent>& WeakParticipant : CurrentParticipants)
		{
			UWorldStateParticipantComponent* Participant = WeakParticipant.Get();
			if (Participant && !Snapshot->Participants.Contains(Participant->ParticipantId) && Participant->bCaptureExistence && UE::WorldState::Private::IsDestroyPolicy(Participant->ExistencePolicy))
			{
				Result.bMutationBegan = true;
				if (AActor* Actor = Participant->GetOwner())
				{
					Actor->Destroy();
				}
			}
		}
		EndRegistryMutationDeferral();
	}

	// Pre-restore callbacks receive the complete accepted scope before participant-specific mutation begins.
	TMap<FWorldStateParticipantId, int32> ResultIndices;
	// Actor transform/attachment precedes Component-relative structure for each topologically ordered participant.
	for (const FWorldStateParticipantId& Id : RestoreOrder)
	{
		FWorldStateParticipantResult& ParticipantResult = Result.ParticipantResults.AddDefaulted_GetRef();
		ParticipantResult.ParticipantId = Id;
		ResultIndices.Add(Id, Result.ParticipantResults.Num() - 1);
		if (UWorldStateParticipantComponent* Participant = Runtime->Registry.FindRef(Id).Get())
		{
			BeginRegistryMutationDeferral();
			Participant->OnWorldStatePreRestore.Broadcast(Id);
			EndRegistryMutationDeferral();
		}
	}

	for (const FWorldStateParticipantId& Id : RestoreOrder)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(WorldState_RestoreParticipant);
		const FWorldStateParticipantSnapshot& ParticipantSnapshot = Snapshot->Participants.FindChecked(Id);
		UWorldStateParticipantComponent* Participant = Runtime->Registry.FindRef(Id).Get();
		if (!Participant || !Participant->GetOwner())
		{
			continue;
		}
		AActor* Actor = Participant->GetOwner();
		if (ParticipantSnapshot.bCaptureActorTransform)
		{
			Result.bMutationBegan = true;
			Actor->SetActorTransform(ParticipantSnapshot.ActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (ParticipantSnapshot.bCaptureAttachment && Actor->GetRootComponent())
		{
			if (!ParticipantSnapshot.bHadActorAttachment)
			{
				Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			}
			else
			{
				AActor* ParentActor = nullptr;
				if (ParticipantSnapshot.AttachmentParentParticipantId.IsValid())
				{
					if (UWorldStateParticipantComponent* ParentParticipant = Runtime->Registry.FindRef(ParticipantSnapshot.AttachmentParentParticipantId).Get())
					{
						ParentActor = ParentParticipant->GetOwner();
					}
				}
				else
				{
					ParentActor = Cast<AActor>(ParticipantSnapshot.AttachmentParentActorPath.ResolveObject());
				}
				USceneComponent* ParentComponent = UE::WorldState::Private::FindSceneComponent(ParentActor, ParticipantSnapshot.AttachmentParentComponentName);
				if (!ParentComponent)
				{
					Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("AttachmentParentMissing"), TEXT("Captured Actor attachment parent could not be resolved."), Id));
					Finalize(false, EWorldStateRestoreStage::Structure);
					return Result;
				}
				Actor->AttachToComponent(ParentComponent, FAttachmentTransformRules::KeepWorldTransform, ParticipantSnapshot.AttachmentSocketName);
			}
		}

		TArray<const FWorldStateSceneComponentSnapshot*> SceneSnapshots;
		for (const FWorldStateSceneComponentSnapshot& SceneSnapshot : ParticipantSnapshot.SceneComponents)
		{
			SceneSnapshots.Add(&SceneSnapshot);
		}
		// Captured hierarchy depth enforces parent-first relative-transform application; identity breaks ties.
		SceneSnapshots.Sort([&ParticipantSnapshot](const FWorldStateSceneComponentSnapshot& Left, const FWorldStateSceneComponentSnapshot& Right)
		{
			auto CapturedDepth = [&ParticipantSnapshot](const FWorldStateSceneComponentSnapshot& SnapshotRecord)
			{
				int32 Depth = 0;
				FWorldStateCaptureSourceId ParentSourceId = SnapshotRecord.ParentSourceId;
				TSet<FWorldStateCaptureSourceId> Visited;
				while (SnapshotRecord.bHadParent && SnapshotRecord.bParentOwnedByActor && !Visited.Contains(ParentSourceId))
				{
					Visited.Add(ParentSourceId);
					const FWorldStateSceneComponentSnapshot* ParentSnapshot = ParticipantSnapshot.SceneComponents.FindByPredicate([&ParentSourceId](const FWorldStateSceneComponentSnapshot& Candidate)
					{
						return Candidate.CaptureSourceId == ParentSourceId;
					});
					if (!ParentSnapshot)
					{
						break;
					}
					++Depth;
					if (!ParentSnapshot->bHadParent || !ParentSnapshot->bParentOwnedByActor)
					{
						break;
					}
					ParentSourceId = ParentSnapshot->ParentSourceId;
				}
				return Depth;
			};
			const int32 LeftDepth = CapturedDepth(Left);
			const int32 RightDepth = CapturedDepth(Right);
			return LeftDepth == RightDepth ? Left.CaptureSourceId.ToString() < Right.CaptureSourceId.ToString() : LeftDepth < RightDepth;
		});

		for (const FWorldStateSceneComponentSnapshot* SceneSnapshot : SceneSnapshots)
		{
			USceneComponent* SceneComponent = Cast<USceneComponent>(Participant->ResolveCaptureSource(SceneSnapshot->CaptureSourceId));
			if (!SceneComponent)
			{
				Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("SceneComponentMissing"), TEXT("Selected Scene Component is missing during structural restore."), Id, SceneSnapshot->CaptureSourceId));
				Finalize(false, EWorldStateRestoreStage::SceneComponents);
				return Result;
			}
			USceneComponent* ExpectedParent = nullptr;
			if (SceneSnapshot->bHadParent)
			{
				ExpectedParent = SceneSnapshot->bParentOwnedByActor
					? Cast<USceneComponent>(Participant->ResolveCaptureSource(SceneSnapshot->ParentSourceId))
					: Cast<USceneComponent>(SceneSnapshot->ExternalParentPath.ResolveObject());
			}
			if (ParticipantSnapshot.bCaptureAttachment)
			{
				if (SceneSnapshot->bHadParent && !ExpectedParent)
				{
					Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("SceneParentMissing"), TEXT("Captured Scene Component parent could not be restored."), Id, SceneSnapshot->CaptureSourceId));
					Finalize(false, EWorldStateRestoreStage::SceneComponents);
					return Result;
				}
				if (ExpectedParent)
				{
					SceneComponent->AttachToComponent(ExpectedParent, FAttachmentTransformRules::KeepWorldTransform, SceneSnapshot->SocketName);
				}
			}
			else if (ExpectedParent && SceneComponent->GetAttachParent() != ExpectedParent)
			{
				const EWorldStateIssueSeverity Severity = SceneSnapshot->bStrictParentValidation ? EWorldStateIssueSeverity::Error : EWorldStateIssueSeverity::Warning;
				Result.Issues.Add(UE::WorldState::Private::MakeIssue(Severity, TEXT("SceneParentChanged"), TEXT("Relative transform is being applied against a different current parent."), Id, SceneSnapshot->CaptureSourceId));
				if (SceneSnapshot->bStrictParentValidation)
				{
					Finalize(false, EWorldStateRestoreStage::SceneComponents);
					return Result;
				}
			}
			SceneComponent->SetRelativeTransform(SceneSnapshot->RelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// Property phase overrides are honored within each already ordered participant.
	for (const FWorldStateParticipantId& Id : RestoreOrder)
	{
		const FWorldStateParticipantSnapshot& ParticipantSnapshot = Snapshot->Participants.FindChecked(Id);
		UWorldStateParticipantComponent* Participant = Runtime->Registry.FindRef(Id).Get();
		if (!Participant)
		{
			continue;
		}
		FWorldStateParticipantResult& ParticipantResult = Result.ParticipantResults[ResultIndices.FindChecked(Id)];
		TArray<const FWorldStateCapturedProperty*> Properties;
		for (const FWorldStateCapturedProperty& CapturedProperty : ParticipantSnapshot.Properties)
		{
			Properties.Add(&CapturedProperty);
		}
		Properties.Sort([](const FWorldStateCapturedProperty& Left, const FWorldStateCapturedProperty& Right)
		{
			const int32 LeftPhase = UE::WorldState::Private::PhaseOrder(Left.RestorePhase);
			const int32 RightPhase = UE::WorldState::Private::PhaseOrder(Right.RestorePhase);
			if (LeftPhase != RightPhase) return LeftPhase < RightPhase;
			const FString LeftKey = Left.CaptureSourceId.ToString() + Left.PropertyName.ToString();
			const FString RightKey = Right.CaptureSourceId.ToString() + Right.PropertyName.ToString();
			return LeftKey < RightKey;
		});

		for (const FWorldStateCapturedProperty* CapturedProperty : Properties)
		{
			FWorldStatePropertyResult& PropertyResult = ParticipantResult.PropertyResults.AddDefaulted_GetRef();
			PropertyResult.ParticipantId = Id;
			PropertyResult.CaptureSourceId = CapturedProperty->CaptureSourceId;
			PropertyResult.PropertyName = CapturedProperty->PropertyName;
			UObject* Source = Participant->ResolveCaptureSource(CapturedProperty->CaptureSourceId);
			FProperty* Property = Source ? FindFProperty<FProperty>(Source->GetClass(), CapturedProperty->PropertyName) : nullptr;
			if (!UE::WorldState::Private::MatchesCapturedSourceClass(Source, CapturedProperty->SourceClass) ||
				!Property ||
				FWorldStatePropertySerializer::BuildTypeSignature(Property) != CapturedProperty->TypeSignature)
			{
				PropertyResult.Message = TEXT("Source/property missing or type-incompatible during value restore.");
				if (Request.MissingPropertyPolicy == EWorldStateMissingPropertyPolicy::SkipWithWarning)
				{
					ParticipantResult.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Warning, TEXT("PropertySkipped"), PropertyResult.Message, Id, CapturedProperty->CaptureSourceId, CapturedProperty->PropertyName));
					continue;
				}
				ParticipantResult.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("PropertyRestoreFailed"), PropertyResult.Message, Id, CapturedProperty->CaptureSourceId, CapturedProperty->PropertyName));
				Finalize(false, EWorldStateRestoreStage::Properties);
				return Result;
			}
			FString DeserializeError;
			PropertyResult.bSucceeded = FWorldStatePropertySerializer::Deserialize(Property, Source, CapturedProperty->Payload, DeserializeError);
			PropertyResult.Message = DeserializeError;
			Result.bMutationBegan = true;
			if (!PropertyResult.bSucceeded)
			{
				ParticipantResult.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("PropertyRestoreFailed"), DeserializeError, Id, CapturedProperty->CaptureSourceId, CapturedProperty->PropertyName));
				if (Request.FailurePolicy == EWorldStateRestoreFailurePolicy::FailFast)
				{
					Finalize(false, EWorldStateRestoreStage::Properties);
					return Result;
				}
				bDeferredPropertyFailure = true;
			}
		}
		BeginRegistryMutationDeferral();
		Participant->OnWorldStatePropertiesRestored.Broadcast(Id);
		EndRegistryMutationDeferral();
	}

	// ResolveObject is deliberately the only resolution operation here; World State never loads soft targets.
	TRACE_CPUPROFILER_EVENT_SCOPE(WorldState_ResolveReferences);
	bool bRequiredReferenceFailed = false;
	for (const FWorldStateParticipantId& Id : RestoreOrder)
	{
		const FWorldStateParticipantSnapshot& ParticipantSnapshot = Snapshot->Participants.FindChecked(Id);
		UWorldStateParticipantComponent* Participant = Runtime->Registry.FindRef(Id).Get();
		if (!Participant)
		{
			continue;
		}
		for (const FWorldStateCapturedProperty& CapturedProperty : ParticipantSnapshot.Properties)
		{
			UObject* Source = Participant->ResolveCaptureSource(CapturedProperty.CaptureSourceId);
			FProperty* Property = Source ? FindFProperty<FProperty>(Source->GetClass(), CapturedProperty.PropertyName) : nullptr;
			if (!Property)
			{
				continue;
			}
			TArray<FWorldStateDiscoveredSoftReference> References;
			FWorldStatePropertySerializer::CollectSoftReferences(Property, Source, References);
			for (const FWorldStateDiscoveredSoftReference& Reference : References)
			{
				FWorldStateReferenceResolutionResult& ReferenceResult = Result.ReferenceResults.AddDefaulted_GetRef();
				ReferenceResult.ParticipantId = Id;
				ReferenceResult.CaptureSourceId = CapturedProperty.CaptureSourceId;
				ReferenceResult.PropertyName = CapturedProperty.PropertyName;
				ReferenceResult.NestedValuePath = Reference.NestedValuePath;
				ReferenceResult.SoftObjectPath = Reference.Path;
				if (Reference.Path.IsNull())
				{
					if (CapturedProperty.ReferenceRequirement == EWorldStateReferenceRequirement::Required)
					{
						ReferenceResult.Status = EWorldStateReferenceResolutionStatus::InvalidPath;
						ReferenceResult.Message = TEXT("Required soft reference restored a null path.");
						if (const int32* ParticipantResultIndex = ResultIndices.Find(Id))
						{
							Result.ParticipantResults[*ParticipantResultIndex].Issues.Add(UE::WorldState::Private::MakeIssue(
								EWorldStateIssueSeverity::Error,
								TEXT("RequiredReferenceInvalidPath"),
								ReferenceResult.Message,
								Id,
								CapturedProperty.CaptureSourceId,
								CapturedProperty.PropertyName));
						}
						bRequiredReferenceFailed = true;
					}
					else
					{
						ReferenceResult.Status = EWorldStateReferenceResolutionStatus::PathRestored;
					}
				}
				else if (Reference.Path.ResolveObject())
				{
					ReferenceResult.Status = EWorldStateReferenceResolutionStatus::Resolved;
				}
				else if (CapturedProperty.ReferenceRequirement == EWorldStateReferenceRequirement::Required)
				{
					ReferenceResult.Status = EWorldStateReferenceResolutionStatus::UnresolvedRequired;
					ReferenceResult.Message = TEXT("Required soft path did not resolve after existence restoration.");
					if (const int32* ParticipantResultIndex = ResultIndices.Find(Id))
					{
						Result.ParticipantResults[*ParticipantResultIndex].Issues.Add(UE::WorldState::Private::MakeIssue(
							EWorldStateIssueSeverity::Error,
							TEXT("RequiredReferenceUnresolved"),
							ReferenceResult.Message,
							Id,
							CapturedProperty.CaptureSourceId,
							CapturedProperty.PropertyName));
					}
					bRequiredReferenceFailed = true;
				}
				else
				{
					ReferenceResult.Status = EWorldStateReferenceResolutionStatus::UnresolvedAllowed;
					ReferenceResult.Message = TEXT("Optional soft path was restored without synchronously loading its target.");
					Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Warning, TEXT("OptionalReferenceUnresolved"), ReferenceResult.Message, Id, CapturedProperty.CaptureSourceId, CapturedProperty.PropertyName));
				}
			}
		}
	}
	if (bRequiredReferenceFailed)
	{
		Result.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("RequiredReferenceUnresolved"), TEXT("At least one required soft reference did not resolve.")));
		Finalize(false, EWorldStateRestoreStage::References);
		return Result;
	}

	// Final validation and derived-state callbacks decide the frozen per-participant terminal outcomes.
	TRACE_CPUPROFILER_EVENT_SCOPE(WorldState_ValidateRestore);
	bool bValidationFailed = false;
	for (const FWorldStateParticipantId& Id : RestoreOrder)
	{
		const FWorldStateParticipantSnapshot& ParticipantSnapshot = Snapshot->Participants.FindChecked(Id);
		UWorldStateParticipantComponent* Participant = Runtime->Registry.FindRef(Id).Get();
		FWorldStateParticipantResult& ParticipantResult = Result.ParticipantResults[ResultIndices.FindChecked(Id)];
		if (!Participant || !Participant->GetOwner())
		{
			ParticipantResult.bSucceeded = ParticipantSnapshot.ExistencePolicy == EWorldStateExistencePolicy::Persistent || ParticipantSnapshot.ExistencePolicy == EWorldStateExistencePolicy::ExternallyManaged;
			if (!ParticipantResult.bSucceeded)
			{
				ParticipantResult.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("ParticipantMissingAfterCallbacks"), TEXT("Participant became unavailable during the accepted restore session."), Id));
				bValidationFailed = true;
			}
			continue;
		}
		if (ParticipantSnapshot.bCaptureActorTransform && !Participant->GetOwner()->GetActorTransform().Equals(ParticipantSnapshot.ActorTransform, 0.01f))
		{
			ParticipantResult.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("ActorTransformMismatch"), TEXT("Actor transform does not match the snapshot after restore."), Id));
			bValidationFailed = true;
		}
		for (const FWorldStateSceneComponentSnapshot& SceneSnapshot : ParticipantSnapshot.SceneComponents)
		{
			USceneComponent* SceneComponent = Cast<USceneComponent>(Participant->ResolveCaptureSource(SceneSnapshot.CaptureSourceId));
			if (!SceneComponent || !SceneComponent->GetRelativeTransform().Equals(SceneSnapshot.RelativeTransform, 0.01f))
			{
				ParticipantResult.Issues.Add(UE::WorldState::Private::MakeIssue(EWorldStateIssueSeverity::Error, TEXT("SceneTransformMismatch"), TEXT("Scene Component relative transform does not match the snapshot."), Id, SceneSnapshot.CaptureSourceId));
				bValidationFailed = true;
			}
		}
		ParticipantResult.bSucceeded = !ParticipantResult.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue) { return Issue.Severity == EWorldStateIssueSeverity::Error; });
		bValidationFailed |= !ParticipantResult.bSucceeded;
		if (ParticipantResult.bSucceeded)
		{
			BeginRegistryMutationDeferral();
			Participant->OnWorldStateRestored.Broadcast(Id);
			EndRegistryMutationDeferral();
			++Result.RestoredParticipantCount;
			Runtime->DirtyParticipants.Remove(Id);
#if ENABLE_DRAW_DEBUG
			if (IsWorldStateVisualDebugEnabled() && Participant->bEnableDebug)
			{
				DrawDebugString(GetWorld(), Participant->GetOwner()->GetActorLocation() + FVector(0.0, 0.0, 80.0), FString::Printf(TEXT("WorldState %s restored"), *Id.ToString()), nullptr, FColor::Green, 2.0f, true);
			}
#endif
		}
	}
	if (bDeferredPropertyFailure || (bValidationFailed && Request.bStrictValidation))
	{
		Finalize(false, bDeferredPropertyFailure ? EWorldStateRestoreStage::Properties : EWorldStateRestoreStage::Validation);
		return Result;
	}

	Finalize(true, EWorldStateRestoreStage::None);
	return Result;
}
