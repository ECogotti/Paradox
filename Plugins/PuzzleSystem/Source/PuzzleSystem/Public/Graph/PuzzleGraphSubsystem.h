#pragma once

#include "CoreMinimal.h"
#include "Graph/PuzzleGraphTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "PuzzleGraphSubsystem.generated.h"

class APuzzleController;
class UPuzzleEmitterComponent;
class UPuzzleReceiverComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FPuzzleGraphTopologyChangedDelegate,
	int64, GraphTopologyRevision,
	APuzzleController*, AffectedController,
	EPuzzleGraphTopologyChangeKind, ChangeKind);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FPuzzleGraphLinkStateChangedDelegate,
	FPuzzleGraphLinkHandle, LinkHandle,
	FPuzzleGraphLinkState, PreviousState,
	FPuzzleGraphLinkState, NewState);
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FPuzzleGraphTopologyChangedNativeDelegate,
	int64,
	APuzzleController*,
	EPuzzleGraphTopologyChangeKind);
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FPuzzleGraphLinkStateChangedNativeDelegate,
	const FPuzzleGraphLinkHandle&,
	const FPuzzleGraphLinkState&,
	const FPuzzleGraphLinkState&);

/** World-scoped read-only index and observation service for PuzzleSystem topology. */
UCLASS()
class PUZZLESYSTEM_API UPuzzleGraphSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	/** Broadcast after a Controller's complete topology has been added, removed, or refreshed. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Graph")
	FPuzzleGraphTopologyChangedDelegate OnPuzzleGraphTopologyChanged;

	/** Broadcast after one retained link's public state snapshot changes. */
	UPROPERTY(BlueprintAssignable, Category = "Puzzle|Graph")
	FPuzzleGraphLinkStateChangedDelegate OnPuzzleGraphLinkStateChanged;

	/** Native counterpart to OnPuzzleGraphTopologyChanged. */
	FPuzzleGraphTopologyChangedNativeDelegate OnPuzzleGraphTopologyChangedNative;

	/** Native counterpart to OnPuzzleGraphLinkStateChanged. */
	FPuzzleGraphLinkStateChangedNativeDelegate OnPuzzleGraphLinkStateChangedNative;

	/** Returns all relationships in which Actor is an exact resolved endpoint, grouped by semantic role. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Graph")
	FPuzzleActorGraphView QueryActorGraph(AActor* Actor) const;

	/** Returns primary links targeting Actor plus gate links influencing Actor's primary Emitters. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Graph")
	TArray<FPuzzleGraphLink> QueryIncomingLinksForActor(AActor* Actor) const;

	/** Returns primary links sourced by Actor plus gate links sourced by Actor. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Graph")
	TArray<FPuzzleGraphLink> QueryOutgoingLinksForActor(AActor* Actor) const;

	/** Returns links using this exact component as a primary or gate Emitter endpoint. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Graph")
	TArray<FPuzzleGraphLink> QueryLinksForEmitterComponent(UPuzzleEmitterComponent* Component) const;

	/** Returns primary links targeting this exact Receiver component. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Graph")
	TArray<FPuzzleGraphLink> QueryLinksForReceiverComponent(UPuzzleReceiverComponent* Component) const;

	/** Resolves a current runtime handle to its read-only topology descriptor. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Graph")
	bool TryGetLink(const FPuzzleGraphLinkHandle& Handle, FPuzzleGraphLink& OutLink) const;

	/** Resolves current state for a handle; gameplay-invalid retained links still return true. */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Graph")
	bool TryGetLinkState(const FPuzzleGraphLinkHandle& Handle, FPuzzleGraphLinkState& OutState) const;

	/** Monotonic world-local topology revision; state-only changes do not increment it. */
	UFUNCTION(BlueprintPure, Category = "Puzzle|Graph")
	int64 GetGraphTopologyRevision() const { return GraphTopologyRevision; }

private:
	struct FIndexedGraphLink
	{
		FPuzzleGraphLink Descriptor;
		FPuzzleGraphLinkState CachedState;
		int32 PrimaryBindingIndex = INDEX_NONE;
		int32 PrimaryConfigurationIndex = INDEX_NONE;
		int32 SecondaryBindingIndex = INDEX_NONE;
	};

	TMap<FPuzzleGraphLinkHandle, FIndexedGraphLink> LinksByHandle;
	TMap<TWeakObjectPtr<APuzzleController>, TArray<FPuzzleGraphLinkHandle>> LinksByController;
	TMap<TWeakObjectPtr<AActor>, TArray<FPuzzleGraphLinkHandle>> IncomingPrimaryByActor;
	TMap<TWeakObjectPtr<AActor>, TArray<FPuzzleGraphLinkHandle>> IncomingGateByActor;
	TMap<TWeakObjectPtr<AActor>, TArray<FPuzzleGraphLinkHandle>> OutgoingPrimaryByActor;
	TMap<TWeakObjectPtr<AActor>, TArray<FPuzzleGraphLinkHandle>> OutgoingGateByActor;
	TMap<TWeakObjectPtr<UPuzzleEmitterComponent>, TArray<FPuzzleGraphLinkHandle>> LinksByEmitter;
	TMap<TWeakObjectPtr<UPuzzleReceiverComponent>, TArray<FPuzzleGraphLinkHandle>> LinksByReceiver;
	TMap<TWeakObjectPtr<UPuzzleReceiverComponent>, int32> ReceiverLinkRefCounts;
	TSet<TWeakObjectPtr<UPuzzleReceiverComponent>> InvalidatedReceivers;

	int64 GraphTopologyRevision = 0;
	bool bIsDeinitializing = false;

	void RegisterOrRefreshController(APuzzleController* Controller);
	void UnregisterController(APuzzleController* Controller);
	void RefreshControllerState(APuzzleController* Controller);
	void RefreshReceiverState(UPuzzleReceiverComponent* Receiver);
	void HandleReceiverInvalidated(UPuzzleReceiverComponent* Receiver);

	FPuzzleGraphLinkHandle CreateUniqueHandle();
	void AddIndexedLink(FIndexedGraphLink&& IndexedLink);
	void RemoveIndexedLink(const FPuzzleGraphLinkHandle& Handle);
	void RemoveControllerLinks(APuzzleController* Controller);
	FPuzzleGraphLinkState BuildLinkState(const FIndexedGraphLink& IndexedLink) const;
	TArray<FPuzzleGraphLink> BuildSortedLinks(const TArray<FPuzzleGraphLinkHandle>* Handles) const;
	TArray<FPuzzleGraphLink> BuildSortedLinks(
		const TArray<FPuzzleGraphLinkHandle>* First,
		const TArray<FPuzzleGraphLinkHandle>* Second) const;
	void BroadcastTopologyChanged(APuzzleController* Controller, EPuzzleGraphTopologyChangeKind ChangeKind);
	void BroadcastLinkStateChanged(
		const FPuzzleGraphLinkHandle& Handle,
		const FPuzzleGraphLinkState& PreviousState,
		const FPuzzleGraphLinkState& NewState);

	friend class APuzzleController;
	friend class UPuzzleReceiverComponent;
};
