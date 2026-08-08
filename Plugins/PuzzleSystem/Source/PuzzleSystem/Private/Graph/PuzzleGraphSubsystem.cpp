#include "Graph/PuzzleGraphSubsystem.h"

#include "Controllers/PuzzleController.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "GameFramework/Actor.h"
#include "PuzzleSystem.h"
#include "Receivers/PuzzleReceiverComponent.h"

void UPuzzleGraphSubsystem::Deinitialize()
{
	bIsDeinitializing = true;
	for (const TPair<TWeakObjectPtr<UPuzzleReceiverComponent>, int32>& Pair : ReceiverLinkRefCounts)
	{
		if (UPuzzleReceiverComponent* Receiver = Pair.Key.Get())
		{
			Receiver->OnReceiverInvalidatedNative.RemoveAll(this);
		}
	}

	LinksByHandle.Reset();
	LinksByController.Reset();
	IncomingPrimaryByActor.Reset();
	IncomingGateByActor.Reset();
	OutgoingPrimaryByActor.Reset();
	OutgoingGateByActor.Reset();
	LinksByEmitter.Reset();
	LinksByReceiver.Reset();
	ReceiverLinkRefCounts.Reset();
	InvalidatedReceivers.Reset();
	OnPuzzleGraphTopologyChangedNative.Clear();
	OnPuzzleGraphLinkStateChangedNative.Clear();

	Super::Deinitialize();
}

FPuzzleActorGraphView UPuzzleGraphSubsystem::QueryActorGraph(AActor* Actor) const
{
	FPuzzleActorGraphView View;
	View.GraphTopologyRevision = GraphTopologyRevision;
	if (!IsValid(Actor))
	{
		return View;
	}

	const TWeakObjectPtr<AActor> ActorKey(Actor);
	View.IncomingPrimaryLinks = BuildSortedLinks(IncomingPrimaryByActor.Find(ActorKey));
	View.IncomingGateLinks = BuildSortedLinks(IncomingGateByActor.Find(ActorKey));
	View.OutgoingPrimaryLinks = BuildSortedLinks(OutgoingPrimaryByActor.Find(ActorKey));
	View.OutgoingGateLinks = BuildSortedLinks(OutgoingGateByActor.Find(ActorKey));
	return View;
}

TArray<FPuzzleGraphLink> UPuzzleGraphSubsystem::QueryIncomingLinksForActor(AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return {};
	}

	const TWeakObjectPtr<AActor> ActorKey(Actor);
	return BuildSortedLinks(
		IncomingPrimaryByActor.Find(ActorKey),
		IncomingGateByActor.Find(ActorKey));
}

TArray<FPuzzleGraphLink> UPuzzleGraphSubsystem::QueryOutgoingLinksForActor(AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return {};
	}

	const TWeakObjectPtr<AActor> ActorKey(Actor);
	return BuildSortedLinks(
		OutgoingPrimaryByActor.Find(ActorKey),
		OutgoingGateByActor.Find(ActorKey));
}

TArray<FPuzzleGraphLink> UPuzzleGraphSubsystem::QueryLinksForEmitterComponent(
	UPuzzleEmitterComponent* Component) const
{
	return IsValid(Component)
		? BuildSortedLinks(LinksByEmitter.Find(TWeakObjectPtr<UPuzzleEmitterComponent>(Component)))
		: TArray<FPuzzleGraphLink>();
}

TArray<FPuzzleGraphLink> UPuzzleGraphSubsystem::QueryLinksForReceiverComponent(
	UPuzzleReceiverComponent* Component) const
{
	return IsValid(Component)
		? BuildSortedLinks(LinksByReceiver.Find(TWeakObjectPtr<UPuzzleReceiverComponent>(Component)))
		: TArray<FPuzzleGraphLink>();
}

bool UPuzzleGraphSubsystem::TryGetLink(
	const FPuzzleGraphLinkHandle& Handle,
	FPuzzleGraphLink& OutLink) const
{
	const FIndexedGraphLink* IndexedLink = LinksByHandle.Find(Handle);
	if (!IndexedLink)
	{
		OutLink = FPuzzleGraphLink();
		return false;
	}

	OutLink = IndexedLink->Descriptor;
	return true;
}

bool UPuzzleGraphSubsystem::TryGetLinkState(
	const FPuzzleGraphLinkHandle& Handle,
	FPuzzleGraphLinkState& OutState) const
{
	const FIndexedGraphLink* IndexedLink = LinksByHandle.Find(Handle);
	if (!IndexedLink)
	{
		OutState = FPuzzleGraphLinkState();
		return false;
	}

	OutState = BuildLinkState(*IndexedLink);
	return true;
}

void UPuzzleGraphSubsystem::RegisterOrRefreshController(APuzzleController* Controller)
{
	if (bIsDeinitializing
		|| !IsValid(Controller)
		|| !Controller->bIsInitialized
		|| !Controller->bConfigurationValid)
	{
		return;
	}

	const TWeakObjectPtr<APuzzleController> ControllerKey(Controller);
	const bool bWasRegistered = LinksByController.Contains(ControllerKey);
	if (bWasRegistered)
	{
		RemoveControllerLinks(Controller);
	}

	for (int32 PrimaryIndex = 0; PrimaryIndex < Controller->ResolvedInputBindings.Num(); ++PrimaryIndex)
	{
		const APuzzleController::FResolvedInputBinding& PrimaryBinding =
			Controller->ResolvedInputBindings[PrimaryIndex];
		UPuzzleEmitterComponent* PrimaryEmitter = PrimaryBinding.Emitter.Get();
		if (!IsValid(PrimaryEmitter))
		{
			continue;
		}

		for (int32 ReceiverIndex = 0; ReceiverIndex < Controller->ResolvedReceivers.Num(); ++ReceiverIndex)
		{
			UPuzzleReceiverComponent* Receiver = Controller->ResolvedReceivers[ReceiverIndex].Get();
			if (!IsValid(Receiver))
			{
				continue;
			}

			FIndexedGraphLink IndexedLink;
			IndexedLink.PrimaryBindingIndex = PrimaryIndex;
			IndexedLink.PrimaryConfigurationIndex = PrimaryBinding.ConfigurationIndex;
			IndexedLink.SecondaryBindingIndex = ReceiverIndex;
			IndexedLink.Descriptor.LinkHandle = CreateUniqueHandle();
			IndexedLink.Descriptor.LinkKind = EPuzzleGraphLinkKind::PrimarySignal;
			IndexedLink.Descriptor.Controller = Controller;
			IndexedLink.Descriptor.PrimaryInputId = PrimaryBinding.InputId;
			IndexedLink.Descriptor.PrimarySignalTag = PrimaryBinding.SignalTag;
			IndexedLink.Descriptor.PrimaryEmitterActor = PrimaryEmitter->GetOwner();
			IndexedLink.Descriptor.PrimaryEmitterComponent = PrimaryEmitter;
			IndexedLink.Descriptor.TargetReceiverActor = Receiver->GetOwner();
			IndexedLink.Descriptor.TargetReceiverComponent = Receiver;
			IndexedLink.CachedState = BuildLinkState(IndexedLink);
			AddIndexedLink(MoveTemp(IndexedLink));
		}

		for (int32 GateIndex = 0; GateIndex < PrimaryBinding.GateInputs.Num(); ++GateIndex)
		{
			const APuzzleController::FResolvedGateInputBinding& GateBinding =
				PrimaryBinding.GateInputs[GateIndex];
			UPuzzleEmitterComponent* GateEmitter = GateBinding.Emitter.Get();
			if (!IsValid(GateEmitter))
			{
				continue;
			}

			FIndexedGraphLink IndexedLink;
			IndexedLink.PrimaryBindingIndex = PrimaryIndex;
			IndexedLink.PrimaryConfigurationIndex = PrimaryBinding.ConfigurationIndex;
			IndexedLink.SecondaryBindingIndex = GateIndex;
			IndexedLink.Descriptor.LinkHandle = CreateUniqueHandle();
			IndexedLink.Descriptor.LinkKind = EPuzzleGraphLinkKind::GateInfluence;
			IndexedLink.Descriptor.Controller = Controller;
			IndexedLink.Descriptor.PrimaryInputId = PrimaryBinding.InputId;
			IndexedLink.Descriptor.PrimarySignalTag = PrimaryBinding.SignalTag;
			IndexedLink.Descriptor.PrimaryEmitterActor = PrimaryEmitter->GetOwner();
			IndexedLink.Descriptor.PrimaryEmitterComponent = PrimaryEmitter;
			IndexedLink.Descriptor.GateInputId = GateBinding.InputId;
			IndexedLink.Descriptor.GateSignalTag = GateBinding.SignalTag;
			IndexedLink.Descriptor.GateEmitterActor = GateEmitter->GetOwner();
			IndexedLink.Descriptor.GateEmitterComponent = GateEmitter;
			IndexedLink.CachedState = BuildLinkState(IndexedLink);
			AddIndexedLink(MoveTemp(IndexedLink));
		}
	}

	++GraphTopologyRevision;
	BroadcastTopologyChanged(
		Controller,
		bWasRegistered
			? EPuzzleGraphTopologyChangeKind::Refreshed
			: EPuzzleGraphTopologyChangeKind::Added);

	if (IsPuzzleSystemDebugEnabled())
	{
		const int32 LinkCount = LinksByController.FindRef(ControllerKey).Num();
		PUZZLESYSTEM_LOG_INFO(
			"Puzzle graph %s Controller '%s': Links=%d TopologyRevision=%lld.",
			bWasRegistered ? TEXT("refreshed") : TEXT("registered"),
			*GetNameSafe(Controller),
			LinkCount,
			GraphTopologyRevision);
	}
}

void UPuzzleGraphSubsystem::UnregisterController(APuzzleController* Controller)
{
	if (bIsDeinitializing || !Controller)
	{
		return;
	}

	const TWeakObjectPtr<APuzzleController> ControllerKey(Controller);
	if (!LinksByController.Contains(ControllerKey))
	{
		return;
	}

	RemoveControllerLinks(Controller);
	++GraphTopologyRevision;
	BroadcastTopologyChanged(Controller, EPuzzleGraphTopologyChangeKind::Removed);

	if (IsPuzzleSystemDebugEnabled())
	{
		PUZZLESYSTEM_LOG_INFO(
			"Puzzle graph unregistered Controller '%s': TopologyRevision=%lld.",
			*GetNameSafe(Controller),
			GraphTopologyRevision);
	}
}

void UPuzzleGraphSubsystem::RefreshControllerState(APuzzleController* Controller)
{
	if (bIsDeinitializing || !Controller)
	{
		return;
	}

	const TArray<FPuzzleGraphLinkHandle>* ControllerHandles =
		LinksByController.Find(TWeakObjectPtr<APuzzleController>(Controller));
	if (!ControllerHandles)
	{
		return;
	}

	const TArray<FPuzzleGraphLinkHandle> HandlesSnapshot = *ControllerHandles;
	for (const FPuzzleGraphLinkHandle& Handle : HandlesSnapshot)
	{
		FIndexedGraphLink* IndexedLink = LinksByHandle.Find(Handle);
		if (!IndexedLink)
		{
			continue;
		}

		const FPuzzleGraphLinkState PreviousState = IndexedLink->CachedState;
		const FPuzzleGraphLinkState NewState = BuildLinkState(*IndexedLink);
		IndexedLink->CachedState = NewState;
		if (PreviousState != NewState)
		{
			BroadcastLinkStateChanged(Handle, PreviousState, NewState);
		}
	}
}

void UPuzzleGraphSubsystem::RefreshReceiverState(UPuzzleReceiverComponent* Receiver)
{
	if (bIsDeinitializing || !IsValid(Receiver))
	{
		return;
	}

	const TArray<FPuzzleGraphLinkHandle>* ReceiverHandles =
		LinksByReceiver.Find(TWeakObjectPtr<UPuzzleReceiverComponent>(Receiver));
	if (!ReceiverHandles)
	{
		return;
	}

	const TArray<FPuzzleGraphLinkHandle> HandlesSnapshot = *ReceiverHandles;
	for (const FPuzzleGraphLinkHandle& Handle : HandlesSnapshot)
	{
		FIndexedGraphLink* IndexedLink = LinksByHandle.Find(Handle);
		if (!IndexedLink)
		{
			continue;
		}

		const FPuzzleGraphLinkState PreviousState = IndexedLink->CachedState;
		const FPuzzleGraphLinkState NewState = BuildLinkState(*IndexedLink);
		IndexedLink->CachedState = NewState;
		if (PreviousState != NewState)
		{
			BroadcastLinkStateChanged(Handle, PreviousState, NewState);
		}
	}
}

void UPuzzleGraphSubsystem::HandleReceiverInvalidated(UPuzzleReceiverComponent* Receiver)
{
	if (bIsDeinitializing || !Receiver)
	{
		return;
	}

	const TWeakObjectPtr<UPuzzleReceiverComponent> ReceiverKey(Receiver);
	if (InvalidatedReceivers.Contains(ReceiverKey))
	{
		return;
	}
	InvalidatedReceivers.Add(ReceiverKey);

	const TArray<FPuzzleGraphLinkHandle>* ReceiverHandles = LinksByReceiver.Find(ReceiverKey);
	if (!ReceiverHandles)
	{
		return;
	}

	const TArray<FPuzzleGraphLinkHandle> HandlesSnapshot = *ReceiverHandles;
	for (const FPuzzleGraphLinkHandle& Handle : HandlesSnapshot)
	{
		FIndexedGraphLink* IndexedLink = LinksByHandle.Find(Handle);
		if (!IndexedLink)
		{
			continue;
		}

		const FPuzzleGraphLinkState PreviousState = IndexedLink->CachedState;
		const FPuzzleGraphLinkState NewState = BuildLinkState(*IndexedLink);
		IndexedLink->CachedState = NewState;
		if (PreviousState != NewState)
		{
			BroadcastLinkStateChanged(Handle, PreviousState, NewState);
		}
	}
}

FPuzzleGraphLinkHandle UPuzzleGraphSubsystem::CreateUniqueHandle()
{
	FPuzzleGraphLinkHandle Handle(FGuid::NewGuid());
	while (LinksByHandle.Contains(Handle))
	{
		Handle = FPuzzleGraphLinkHandle(FGuid::NewGuid());
	}
	return Handle;
}

void UPuzzleGraphSubsystem::AddIndexedLink(FIndexedGraphLink&& IndexedLink)
{
	const FPuzzleGraphLinkHandle Handle = IndexedLink.Descriptor.LinkHandle;
	const FPuzzleGraphLink Descriptor = IndexedLink.Descriptor;
	LinksByHandle.Add(Handle, MoveTemp(IndexedLink));
	LinksByController.FindOrAdd(Descriptor.Controller).Add(Handle);

	if (Descriptor.LinkKind == EPuzzleGraphLinkKind::PrimarySignal)
	{
		OutgoingPrimaryByActor.FindOrAdd(Descriptor.PrimaryEmitterActor).Add(Handle);
		IncomingPrimaryByActor.FindOrAdd(Descriptor.TargetReceiverActor).Add(Handle);
		LinksByEmitter.FindOrAdd(Descriptor.PrimaryEmitterComponent).Add(Handle);
		LinksByReceiver.FindOrAdd(Descriptor.TargetReceiverComponent).Add(Handle);

		int32& ReceiverLinkCount = ReceiverLinkRefCounts.FindOrAdd(Descriptor.TargetReceiverComponent);
		if (ReceiverLinkCount == 0)
		{
			InvalidatedReceivers.Remove(Descriptor.TargetReceiverComponent);
			if (UPuzzleReceiverComponent* Receiver = Descriptor.TargetReceiverComponent.Get())
			{
				Receiver->OnReceiverInvalidatedNative.AddUObject(
					this,
					&UPuzzleGraphSubsystem::HandleReceiverInvalidated);
			}
		}
		++ReceiverLinkCount;
	}
	else
	{
		IncomingGateByActor.FindOrAdd(Descriptor.PrimaryEmitterActor).Add(Handle);
		OutgoingGateByActor.FindOrAdd(Descriptor.GateEmitterActor).Add(Handle);
		LinksByEmitter.FindOrAdd(Descriptor.PrimaryEmitterComponent).AddUnique(Handle);
		LinksByEmitter.FindOrAdd(Descriptor.GateEmitterComponent).AddUnique(Handle);
	}
}

void UPuzzleGraphSubsystem::RemoveIndexedLink(const FPuzzleGraphLinkHandle& Handle)
{
	const FIndexedGraphLink* ExistingLink = LinksByHandle.Find(Handle);
	if (!ExistingLink)
	{
		return;
	}
	const FPuzzleGraphLink Descriptor = ExistingLink->Descriptor;

	auto RemoveFromIndex = [&Handle](auto& Index, const auto& Key)
	{
		if (auto* Handles = Index.Find(Key))
		{
			Handles->Remove(Handle);
			if (Handles->IsEmpty())
			{
				Index.Remove(Key);
			}
		}
	};

	RemoveFromIndex(LinksByController, Descriptor.Controller);
	RemoveFromIndex(LinksByEmitter, Descriptor.PrimaryEmitterComponent);
	if (Descriptor.LinkKind == EPuzzleGraphLinkKind::PrimarySignal)
	{
		RemoveFromIndex(OutgoingPrimaryByActor, Descriptor.PrimaryEmitterActor);
		RemoveFromIndex(IncomingPrimaryByActor, Descriptor.TargetReceiverActor);
		RemoveFromIndex(LinksByReceiver, Descriptor.TargetReceiverComponent);

		if (int32* ReceiverLinkCount = ReceiverLinkRefCounts.Find(Descriptor.TargetReceiverComponent))
		{
			--(*ReceiverLinkCount);
			if (*ReceiverLinkCount <= 0)
			{
				if (UPuzzleReceiverComponent* Receiver = Descriptor.TargetReceiverComponent.Get())
				{
					Receiver->OnReceiverInvalidatedNative.RemoveAll(this);
				}
				ReceiverLinkRefCounts.Remove(Descriptor.TargetReceiverComponent);
				InvalidatedReceivers.Remove(Descriptor.TargetReceiverComponent);
			}
		}
	}
	else
	{
		RemoveFromIndex(IncomingGateByActor, Descriptor.PrimaryEmitterActor);
		RemoveFromIndex(OutgoingGateByActor, Descriptor.GateEmitterActor);
		if (Descriptor.GateEmitterComponent != Descriptor.PrimaryEmitterComponent)
		{
			RemoveFromIndex(LinksByEmitter, Descriptor.GateEmitterComponent);
		}
	}

	LinksByHandle.Remove(Handle);
}

void UPuzzleGraphSubsystem::RemoveControllerLinks(APuzzleController* Controller)
{
	const TWeakObjectPtr<APuzzleController> ControllerKey(Controller);
	const TArray<FPuzzleGraphLinkHandle>* ExistingHandles = LinksByController.Find(ControllerKey);
	if (!ExistingHandles)
	{
		return;
	}

	const TArray<FPuzzleGraphLinkHandle> HandlesSnapshot = *ExistingHandles;
	for (const FPuzzleGraphLinkHandle& Handle : HandlesSnapshot)
	{
		RemoveIndexedLink(Handle);
	}
	LinksByController.Remove(ControllerKey);
}

FPuzzleGraphLinkState UPuzzleGraphSubsystem::BuildLinkState(const FIndexedGraphLink& IndexedLink) const
{
	FPuzzleGraphLinkState State;
	const APuzzleController* Controller = IndexedLink.Descriptor.Controller.Get();
	if (!Controller || !Controller->ResolvedInputBindings.IsValidIndex(IndexedLink.PrimaryBindingIndex))
	{
		return State;
	}

	const APuzzleController::FResolvedInputBinding& PrimaryBinding =
		Controller->ResolvedInputBindings[IndexedLink.PrimaryBindingIndex];
	State.bRawPrimaryValid = PrimaryBinding.RawState.bIsValid;
	State.bRawPrimaryActive = PrimaryBinding.RawState.bIsValid && PrimaryBinding.RawState.bIsActive;
	State.RawPrimaryRevision = PrimaryBinding.RawState.bIsValid ? PrimaryBinding.RawState.Revision : 0;
	State.RawPrimaryPayload = PrimaryBinding.RawState.bIsValid
		? PrimaryBinding.RawState.Payload.Get()
		: nullptr;

	if (!PrimaryBinding.bGateEnabled)
	{
		State.GateMode = EPuzzleGraphGateMode::Bypassed;
		State.bGateValid = true;
		State.bGateAllowsSignal = true;
	}
	else if (!PrimaryBinding.bGateValid)
	{
		State.GateMode = EPuzzleGraphGateMode::Invalid;
		State.bGateValid = false;
		State.bGateAllowsSignal = false;
	}
	else
	{
		State.GateMode = PrimaryBinding.bGateAllowsSignal
			? EPuzzleGraphGateMode::Open
			: EPuzzleGraphGateMode::Closed;
		State.bGateValid = true;
		State.bGateAllowsSignal = PrimaryBinding.bGateAllowsSignal;
	}

	if (const FPuzzleSignalState* EffectiveState = Controller->RuntimeInputCache.Find(PrimaryBinding.InputId))
	{
		State.bEffectivePrimaryValid = EffectiveState->bIsValid;
		State.bEffectivePrimaryActive = EffectiveState->bIsValid && EffectiveState->bIsActive;
		State.EffectivePrimaryRevision = EffectiveState->bIsValid ? EffectiveState->Revision : 0;
		State.EffectivePrimaryPayload = EffectiveState->bIsValid
			? EffectiveState->Payload.Get()
			: nullptr;
	}

	if (IndexedLink.Descriptor.LinkKind == EPuzzleGraphLinkKind::GateInfluence
		&& PrimaryBinding.GateInputs.IsValidIndex(IndexedLink.SecondaryBindingIndex))
	{
		const APuzzleController::FResolvedGateInputBinding& GateInput =
			PrimaryBinding.GateInputs[IndexedLink.SecondaryBindingIndex];
		State.bGateInputValid = GateInput.State.bIsValid;
		State.bGateInputActive = GateInput.State.bIsValid && GateInput.State.bIsActive;
		State.GateInputRevision = GateInput.State.bIsValid ? GateInput.State.Revision : 0;
		State.GateInputPayload = GateInput.State.bIsValid ? GateInput.State.Payload.Get() : nullptr;
	}

	State.bControllerResultValid = Controller->bIsInitialized && Controller->bHasEvaluationResult;
	State.bControllerResultActive = State.bControllerResultValid && Controller->bLastEvaluationResult;

	if (IndexedLink.Descriptor.LinkKind == EPuzzleGraphLinkKind::PrimarySignal)
	{
		const TWeakObjectPtr<UPuzzleReceiverComponent> ReceiverKey =
			IndexedLink.Descriptor.TargetReceiverComponent;
		UPuzzleReceiverComponent* Receiver = ReceiverKey.Get();
		State.bTargetReceiverValid = IsValid(Receiver) && !InvalidatedReceivers.Contains(ReceiverKey);
		if (State.bTargetReceiverValid)
		{
			State.TargetReceiverActivationMode = Receiver->GetActivationMode();
			State.bTargetReceiverPrerequisitesSatisfied =
				Receiver->AreActivationPrerequisitesSatisfied();
			State.bTargetReceiverManualActivationRequested =
				Receiver->IsManualActivationRequested();
			State.bTargetReceiverEffectiveActive = Receiver->IsReceiverActive();
		}
	}

	return State;
}

TArray<FPuzzleGraphLink> UPuzzleGraphSubsystem::BuildSortedLinks(
	const TArray<FPuzzleGraphLinkHandle>* Handles) const
{
	TArray<FPuzzleGraphLinkHandle> SortedHandles;
	if (Handles)
	{
		SortedHandles = *Handles;
	}
	SortedHandles.RemoveAll([this](const FPuzzleGraphLinkHandle& Handle)
	{
		return !LinksByHandle.Contains(Handle);
	});

	SortedHandles.Sort([this](const FPuzzleGraphLinkHandle& Left, const FPuzzleGraphLinkHandle& Right)
	{
		const FIndexedGraphLink& LeftLink = LinksByHandle.FindChecked(Left);
		const FIndexedGraphLink& RightLink = LinksByHandle.FindChecked(Right);
		const FString LeftControllerPath = GetPathNameSafe(LeftLink.Descriptor.Controller.Get());
		const FString RightControllerPath = GetPathNameSafe(RightLink.Descriptor.Controller.Get());
		const int32 PathComparison = LeftControllerPath.Compare(RightControllerPath, ESearchCase::CaseSensitive);
		if (PathComparison != 0)
		{
			return PathComparison < 0;
		}
		if (LeftLink.PrimaryConfigurationIndex != RightLink.PrimaryConfigurationIndex)
		{
			return LeftLink.PrimaryConfigurationIndex < RightLink.PrimaryConfigurationIndex;
		}
		if (LeftLink.Descriptor.LinkKind != RightLink.Descriptor.LinkKind)
		{
			return static_cast<uint8>(LeftLink.Descriptor.LinkKind)
				< static_cast<uint8>(RightLink.Descriptor.LinkKind);
		}
		return LeftLink.SecondaryBindingIndex < RightLink.SecondaryBindingIndex;
	});

	TArray<FPuzzleGraphLink> Result;
	Result.Reserve(SortedHandles.Num());
	for (const FPuzzleGraphLinkHandle& Handle : SortedHandles)
	{
		if (const FIndexedGraphLink* IndexedLink = LinksByHandle.Find(Handle))
		{
			Result.Add(IndexedLink->Descriptor);
		}
	}
	return Result;
}

TArray<FPuzzleGraphLink> UPuzzleGraphSubsystem::BuildSortedLinks(
	const TArray<FPuzzleGraphLinkHandle>* First,
	const TArray<FPuzzleGraphLinkHandle>* Second) const
{
	TArray<FPuzzleGraphLinkHandle> Combined;
	if (First)
	{
		Combined.Append(*First);
	}
	if (Second)
	{
		for (const FPuzzleGraphLinkHandle& Handle : *Second)
		{
			Combined.AddUnique(Handle);
		}
	}
	return BuildSortedLinks(&Combined);
}

void UPuzzleGraphSubsystem::BroadcastTopologyChanged(
	APuzzleController* Controller,
	const EPuzzleGraphTopologyChangeKind ChangeKind)
{
	OnPuzzleGraphTopologyChangedNative.Broadcast(GraphTopologyRevision, Controller, ChangeKind);
	OnPuzzleGraphTopologyChanged.Broadcast(GraphTopologyRevision, Controller, ChangeKind);
}

void UPuzzleGraphSubsystem::BroadcastLinkStateChanged(
	const FPuzzleGraphLinkHandle& Handle,
	const FPuzzleGraphLinkState& PreviousState,
	const FPuzzleGraphLinkState& NewState)
{
	OnPuzzleGraphLinkStateChangedNative.Broadcast(Handle, PreviousState, NewState);
	OnPuzzleGraphLinkStateChanged.Broadcast(Handle, PreviousState, NewState);
}
