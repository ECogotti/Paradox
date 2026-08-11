#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Graph/PuzzleGraphTypes.h"
#include "PuzzleOverlay/ParadoxPuzzleCircuitTypes.h"
#include "TimerManager.h"
#include "ParadoxPuzzleCircuitRendererComponent.generated.h"

class AParadoxPlayerController;
class APuzzleController;
class UBoxComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UParadoxSelectionComponent;
class UStaticMesh;
class USceneComponent;

namespace UE::Tasks
{
	class FCancellationToken;
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxPuzzleWireCalculationStartedSignature,
	const FParadoxPuzzleWireCalculationContext&,
	Context);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxPuzzleWireCalculationFinishedSignature,
	const FParadoxPuzzleWireCalculationResult&,
	Result);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FParadoxPuzzleWireCalculationStartedNative,
	const FParadoxPuzzleWireCalculationContext&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FParadoxPuzzleWireCalculationFinishedNative,
	const FParadoxPuzzleWireCalculationResult&);

/** Renders the selected Actor's read-only PuzzleSystem subgraph as orthogonal world-space wires. */
UCLASS(ClassGroup = (Paradox), BlueprintType, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxPuzzleCircuitRendererComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxPuzzleCircuitRendererComponent();

	/** Geometry, candidate-search, bundle, lane, port, and scoring controls used whenever the selected puzzle subgraph is rebuilt. Expand the nested categories for detailed tooltips. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Routing")
	FParadoxPuzzleRoutingSettings RoutingSettings;

	/** Selects where the pure wire solve runs. Multi-Thread is the production default; graph queries, traces and ISM updates always remain on the Game Thread. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	EParadoxPuzzleRoutingExecutionMode ExecutionMode = EParadoxPuzzleRoutingExecutionMode::MultiThreaded;

	/** Broadcast on the Game Thread when a real routing request begins preparation. Cache hits also produce a balanced start/finish pair. */
	UPROPERTY(BlueprintAssignable, Category = "Paradox|Puzzle Overlay|Calculation")
	FParadoxPuzzleWireCalculationStartedSignature OnWireCalculationStarted;

	/** Broadcast on the Game Thread after apply, cache apply, cancellation, supersession or failure. */
	UPROPERTY(BlueprintAssignable, Category = "Paradox|Puzzle Overlay|Calculation")
	FParadoxPuzzleWireCalculationFinishedSignature OnWireCalculationFinished;

	/** Native counterpart of OnWireCalculationStarted. Always emitted on the Game Thread. */
	FParadoxPuzzleWireCalculationStartedNative OnWireCalculationStartedNative;

	/** Native counterpart of OnWireCalculationFinished. Always emitted on the Game Thread. */
	FParadoxPuzzleWireCalculationFinishedNative OnWireCalculationFinishedNative;

	/** Static Mesh instanced once per final wire segment. The Engine cube is the safe default; its local X axis is stretched along each segment. Loaded only while a graph is presented. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Rendering")
	TSoftObjectPtr<UStaticMesh> WireMesh;

	/** Optional material used by Input-wire instances. It should support Instanced Static Meshes and may read PerInstanceCustomData[0] as SignalStrength. Null uses the mesh material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Rendering")
	TSoftObjectPtr<UMaterialInterface> InputMaterial;

	/** Optional material used by Output-wire instances. It should support Instanced Static Meshes and may read PerInstanceCustomData[0] as SignalStrength. Null uses the mesh material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Rendering")
	TSoftObjectPtr<UMaterialInterface> OutputMaterial;

	/** Rendered cross-section thickness, in centimetres, applied on the wire mesh local Y and Z axes. It does not affect routing, lane spacing, collisions, or bounds tests. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Rendering", meta = (ClampMin = "0.1"))
	float WireThickness = 4.0f;

	/** Visual Z offset, in centimetres, applied after routing so ground-supported wires sit above the floor. It does not affect surfaces, crossings, bridges, or endpoint ports. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Rendering")
	float GroundWireHeightOffset = 3.0f;

	/** Value written to PerInstanceCustomData[0] when the graph link is valid but inactive. Materials normally multiply emissive intensity by this value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Signal", meta = (ClampMin = "0.0"))
	float InactiveSignalStrength = 0.15f;

	/** Value written to PerInstanceCustomData[0] when the graph link's effective signal is active. This changes material presentation only and never reroutes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Signal", meta = (ClampMin = "0.0"))
	float ActiveSignalStrength = 1.0f;

	/** Distance, in centimetres, above a lattice sample at which the downward Visibility fallback trace starts when GridWorld has no usable surface sample. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Surface", meta = (ClampMin = "0.0"))
	float SurfaceTraceHeight = 300.0f;

	/** Distance, in centimetres, below a lattice sample reached by the downward fallback trace. A deep hit is still rejected when it exceeds MaxSurfaceSnapDistance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Surface", meta = (ClampMin = "0.0"))
	float SurfaceTraceDepth = 500.0f;

	/** Maximum vertical distance, in centimetres, between the requested wire elevation and a GridWorld/trace surface. More distant floors are rejected so wires remain unsupported over voids instead of dropping downward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Surface", meta = (ClampMin = "0.0"))
	float MaxSurfaceSnapDistance = 200.0f;

	/** Collision channel used only by the downward surface fallback trace. Endpoint Actors and renderer geometry are ignored. It does not control wire collision, which is always disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Surface")
	TEnumAsByte<ECollisionChannel> SurfaceTraceChannel = ECC_Visibility;

	/** Enables Custom Depth/Stencil writes on both Input and Output wire ISMs. Disable only when the project does not use the Paradox outline post process for circuit wires. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Custom Depth")
	bool bRenderWiresInCustomDepth = true;

	/** Custom Stencil value for Input wires. Must remain in the reserved 210-219 range and differ from OutputStencilValue. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Custom Depth", meta = (ClampMin = "210", ClampMax = "219"))
	int32 InputStencilValue = 210;

	/** Custom Stencil value for Output wires. Must remain in the reserved 220-229 range and differ from InputStencilValue. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Custom Depth", meta = (ClampMin = "220", ClampMax = "229"))
	int32 OutputStencilValue = 220;

	/** Local debug gate for this renderer instance. Debug lines, Wire Boxes, candidate faces, bundle centerlines, lane data, and logs appear only when this is true and Paradox.PuzzleOverlay.Debug is globally enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Debug")
	bool bEnableDebug = false;

	UFUNCTION(BlueprintPure, Category = "Paradox|Puzzle Overlay")
	AActor* GetDisplayedActor() const { return DisplayedActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Puzzle Overlay")
	int64 GetRoutingGeneration() const { return RoutingGeneration; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Puzzle Overlay|Calculation")
	bool IsWireCalculationInProgress() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Puzzle Overlay|Calculation")
	FGuid GetActiveWireCalculationRequestId() const { return ActiveCalculationContext.RequestId; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Puzzle Overlay|Calculation")
	int64 GetActiveWireCalculationGeneration() const { return ActiveCalculationContext.RoutingGeneration; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Puzzle Overlay")
	TArray<FParadoxPuzzleWireRoute> GetRenderedRoutes() const { return RenderedRoutes; }

	/** Explicit event-driven refresh; does not alter selection or PuzzleSystem state. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Puzzle Overlay")
	void RefreshOverlay();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Puzzle Overlay")
	void ClearOverlay();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FParadoxPuzzleCircuitRendererLifecycleTest;
	friend class FParadoxPuzzleCircuitRendererAsyncLifecycleTest;
#endif

	struct FRenderedInstanceRef
	{
		EParadoxPuzzleWireDirection Direction = EParadoxPuzzleWireDirection::Input;
		int32 InstanceIndex = INDEX_NONE;
	};

	struct FPreparedRoutingRequest
	{
		FParadoxPuzzleWireCalculationContext Context;
		FParadoxPuzzleRoutingSnapshot Snapshot;
		FTransform RoutingFrame = FTransform::Identity;
		FGuid RoutingGridId;
		uint32 RoutingSignature = 0;
		double RequestStartSeconds = 0.0;
		double PreparationMilliseconds = 0.0;
	};

	UFUNCTION()
	void HandleSelectedActorChanged(AActor* PreviousActor, AActor* NewActor);

	UFUNCTION()
	void HandleEndpointDestroyed(AActor* DestroyedActor);

	void HandleEndpointTransformUpdated(
		USceneComponent* UpdatedComponent,
		EUpdateTransformFlags UpdateTransformFlags,
		ETeleportType TeleportType);
	void HandleGraphTopologyChanged(
		int64 GraphTopologyRevision,
		APuzzleController* AffectedController,
		EPuzzleGraphTopologyChangeKind ChangeKind);
	void HandleGraphLinkStateChanged(
		const FPuzzleGraphLinkHandle& LinkHandle,
		const FPuzzleGraphLinkState& PreviousState,
		const FPuzzleGraphLinkState& NewState);

	void EnsureRenderComponents();
	void ConfigureRenderComponent(
		UInstancedStaticMeshComponent* Component,
		EParadoxPuzzleWireDirection Direction);
	void RebuildSelectedGraph(bool bForce);
	void RebuildRoutesForEndpointActor(AActor* EndpointActor);
	void RequestPendingRebuild(bool bForce, AActor* EndpointActor = nullptr);
	void ProcessPendingRebuild();
	void SubmitRoutingRequest(FPreparedRoutingRequest&& Request);
	void ExecuteRoutingRequestStandard(FPreparedRoutingRequest&& Request);
	void ExecuteRoutingRequestAsync(FPreparedRoutingRequest&& Request);
	void CompleteAsyncRoutingRequest(
		FPreparedRoutingRequest&& Request,
		FParadoxPuzzleRoutingResult&& Result,
		double QueueMilliseconds,
		double SolveMilliseconds);
	void CompleteRoutingRequest(
		FPreparedRoutingRequest&& Request,
		FParadoxPuzzleRoutingResult&& Result,
		EParadoxPuzzleWireCalculationCompletionStatus Status,
		double QueueMilliseconds,
		double SolveMilliseconds);
	void CancelActiveRoutingRequest(EParadoxPuzzleWireCalculationCompletionStatus Status);
	void BroadcastCalculationStarted(const FParadoxPuzzleWireCalculationContext& Context);
	void BroadcastCalculationFinished(const FParadoxPuzzleWireCalculationResult& Result);
	void ReconcileCurrentLinkState(FParadoxPuzzleRoutingResult& InOutResult) const;
	void QueueEndpointReroute(AActor* EndpointActor);
	void ProcessPendingEndpointReroutes();
	void BindEndpointActors(const TArray<FPuzzleGraphLink>& Links);
	void UnbindEndpointActors();
	void BuildRoutingSnapshot(
		const TArray<FPuzzleGraphLink>& InputLinks,
		const TArray<FPuzzleGraphLink>& OutputLinks,
		FParadoxPuzzleRoutingSnapshot& OutSnapshot);
	void ResolveRoutingFrame(const FVector& SelectedLocation);
	FParadoxPuzzleRoutingCoord QuantizeWorldLocation(const FVector& WorldLocation) const;
	void ResolveSurfaceSamples(FParadoxPuzzleRoutingSnapshot& InOutSnapshot) const;
	void ApplyRoutingResult(FParadoxPuzzleRoutingResult&& Result);
	void UpdateLinkSignalStrength(const FPuzzleGraphLinkHandle& LinkHandle, bool bActive);
	uint32 CalculateTopologySignature(
		const TArray<FPuzzleGraphLink>& InputLinks,
		const TArray<FPuzzleGraphLink>& OutputLinks) const;
	uint32 CalculateRoutingSnapshotSignature(const FParadoxPuzzleRoutingSnapshot& Snapshot) const;
	void ApplySnapshotStateToResult(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		FParadoxPuzzleRoutingResult& InOutResult) const;
	void InvalidateRoutingCache();
	AActor* ResolveEndpointActor(const FPuzzleGraphLink& Link, bool bSource) const;
	FParadoxPuzzleWireEndpointBounds ResolveEndpointBounds(AActor* EndpointActor);
	UBoxComponent* FindUniqueWireTargetComponent(AActor* EndpointActor, bool& bOutMultiple) const;
	uint32 CalculateEndpointGeometrySignature(
		const FParadoxPuzzleWireEndpointBounds& Bounds) const;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> InputWireInstances = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> OutputWireInstances = nullptr;

	/** Visible transient owner for the render primitives; Controllers are hidden Actors in game. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> PresentationActor = nullptr;

	UPROPERTY(Transient)
	TArray<FParadoxPuzzleWireRoute> RenderedRoutes;

	TWeakObjectPtr<AActor> DisplayedActor;
	TWeakObjectPtr<UParadoxSelectionComponent> BoundSelectionComponent;
	TSet<TWeakObjectPtr<AActor>> BoundEndpointActors;
	TSet<TWeakObjectPtr<USceneComponent>> BoundEndpointComponents;
	TMap<FPuzzleGraphLinkHandle, TArray<FRenderedInstanceRef>> InstancesByLink;
	TMap<TWeakObjectPtr<AActor>, FParadoxPuzzleRoutingCoord> EndpointCoordinates;
	TMap<TWeakObjectPtr<AActor>, uint32> EndpointGeometrySignatures;
	TSet<TWeakObjectPtr<AActor>> WarnedInvalidBoundsActors;
	TSet<TWeakObjectPtr<AActor>> PendingRerouteActors;
	FTimerHandle PendingRerouteTimerHandle;
	FTransform RoutingFrame = FTransform::Identity;
	FGuid RoutingGridId;
	FParadoxPuzzleRoutingResult CachedRoutingResult;
	uint32 CachedRoutingSignature = 0;
	int64 RoutingGeneration = 0;
	uint32 TopologySignature = 0;
	bool bHasCachedRoutingResult = false;
	bool bRebuilding = false;
	bool bRoutingTaskActive = false;
	bool bPendingRebuild = false;
	bool bPendingRebuildForce = false;
	bool bPendingFullRebuild = false;
	bool bEndingPlay = false;
	EParadoxPuzzleWireCalculationCompletionStatus ActiveCancellationStatus =
		EParadoxPuzzleWireCalculationCompletionStatus::Cancelled;
	TWeakObjectPtr<AActor> PendingLocalizedEndpoint;
	FParadoxPuzzleWireCalculationContext ActiveCalculationContext;
	TUniquePtr<FPreparedRoutingRequest> ActivePreparedRequest;
	TSharedPtr<UE::Tasks::FCancellationToken, ESPMode::ThreadSafe> ActiveCancellationToken;
#if WITH_DEV_AUTOMATION_TESTS
	/** Deterministic worker barrier used only by automation lifecycle tests. */
	TFunction<void()> BeforeWorkerSolveTestHook;
#endif
};
