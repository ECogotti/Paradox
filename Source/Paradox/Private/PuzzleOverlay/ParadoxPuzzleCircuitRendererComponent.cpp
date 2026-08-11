#include "PuzzleOverlay/ParadoxPuzzleCircuitRendererComponent.h"

#include "Async/Async.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Controllers/ParadoxPlayerController.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Graph/PuzzleGraphSubsystem.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Interaction/ParadoxSelectionComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DataValidation.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Paradox.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "PuzzleOverlay/ParadoxPuzzleWireRouter.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "Tasks/Task.h"

#define LOCTEXT_NAMESPACE "ParadoxPuzzleCircuitRenderer"

namespace ParadoxPuzzleCircuitRenderer
{
	constexpr int32 MaxSurfaceSamplesPerGeneration = 8192;
	constexpr double SurfaceHeightBucketSize = 10.0;
	const FName WireTargetComponentTag(TEXT("WireTarget"));

	int32 ToHeightBucket(const double Height)
	{
		return FMath::RoundToInt(Height / SurfaceHeightBucketSize);
	}

	FVector GetActorAnchor(const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return FVector::ZeroVector;
		}
		// Actor bounds can be empty for logical endpoints and can include a transient
		// world-space selection widget. The Actor transform is stable in both cases.
		return Actor->GetActorLocation();
	}

	void AddProjectedBox(
		const FBox& LocalBox,
		const FTransform& LocalToWorld,
		const FTransform& RoutingFrame,
		FBox& InOutRoutingBox)
	{
		if (!LocalBox.IsValid)
		{
			return;
		}
		for (int32 XIndex = 0; XIndex < 2; ++XIndex)
		{
			for (int32 YIndex = 0; YIndex < 2; ++YIndex)
			{
				for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
				{
					const FVector LocalCorner(
						XIndex == 0 ? LocalBox.Min.X : LocalBox.Max.X,
						YIndex == 0 ? LocalBox.Min.Y : LocalBox.Max.Y,
						ZIndex == 0 ? LocalBox.Min.Z : LocalBox.Max.Z);
					InOutRoutingBox += RoutingFrame.InverseTransformPosition(
						LocalToWorld.TransformPosition(LocalCorner));
				}
			}
		}
	}

	bool IsUsableEndpointBox(const FBox& Box)
	{
		return Box.IsValid
			&& !Box.Min.ContainsNaN()
			&& !Box.Max.ContainsNaN()
			&& Box.GetExtent().X > KINDA_SMALL_NUMBER
			&& Box.GetExtent().Y > KINDA_SMALL_NUMBER
			&& Box.Max.Z >= Box.Min.Z;
	}
}

UParadoxPuzzleCircuitRendererComponent::UParadoxPuzzleCircuitRendererComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	WireMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
}

void UParadoxPuzzleCircuitRendererComponent::BeginPlay()
{
	Super::BeginPlay();

	AParadoxPlayerController* Controller = Cast<AParadoxPlayerController>(GetOwner());
	UParadoxSelectionComponent* Selection = Controller ? Controller->GetSelectionComponent() : nullptr;
	if (!Selection)
	{
		PARADOX_LOG_WARNING(
			TEXT("Puzzle overlay renderer '%s' has no Paradox Selection Component on owner '%s'."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
		return;
	}

	BoundSelectionComponent = Selection;
	Selection->OnSelectedActorChanged.AddDynamic(
		this,
		&ThisClass::HandleSelectedActorChanged);
	if (UPuzzleGraphSubsystem* Graph = GetWorld()->GetSubsystem<UPuzzleGraphSubsystem>())
	{
		Graph->OnPuzzleGraphTopologyChangedNative.AddUObject(
			this,
			&ThisClass::HandleGraphTopologyChanged);
		Graph->OnPuzzleGraphLinkStateChangedNative.AddUObject(
			this,
			&ThisClass::HandleGraphLinkStateChanged);
	}

	HandleSelectedActorChanged(nullptr, Selection->GetSelectedActor());
}

void UParadoxPuzzleCircuitRendererComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	CancelActiveRoutingRequest(EParadoxPuzzleWireCalculationCompletionStatus::Cancelled);
	bPendingRebuild = false;
	PendingLocalizedEndpoint.Reset();
	if (UParadoxSelectionComponent* Selection = BoundSelectionComponent.Get())
	{
		Selection->OnSelectedActorChanged.RemoveDynamic(
			this,
			&ThisClass::HandleSelectedActorChanged);
	}
	if (UPuzzleGraphSubsystem* Graph = GetWorld()
		? GetWorld()->GetSubsystem<UPuzzleGraphSubsystem>()
		: nullptr)
	{
		Graph->OnPuzzleGraphTopologyChangedNative.RemoveAll(this);
		Graph->OnPuzzleGraphLinkStateChangedNative.RemoveAll(this);
	}

	ClearOverlay();
	BoundSelectionComponent.Reset();
	if (IsValid(PresentationActor))
	{
		PresentationActor->Destroy();
	}
	PresentationActor = nullptr;
	InputWireInstances = nullptr;
	OutputWireInstances = nullptr;

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
EDataValidationResult UParadoxPuzzleCircuitRendererComponent::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	const auto AddError = [&Context, &Result](const FText& Error)
	{
		Context.AddError(Error);
		Result = EDataValidationResult::Invalid;
	};
	if (InputStencilValue < 210 || InputStencilValue > 219)
	{
		AddError(LOCTEXT(
			"InputStencilOutsideRange",
			"Puzzle overlay Input Stencil must be in the reserved range 210-219."));
	}
	if (OutputStencilValue < 220 || OutputStencilValue > 229)
	{
		AddError(LOCTEXT(
			"OutputStencilOutsideRange",
			"Puzzle overlay Output Stencil must be in the reserved range 220-229."));
	}
	if (InputStencilValue == OutputStencilValue
		|| (InputStencilValue >= 230 && InputStencilValue <= 249)
		|| (OutputStencilValue >= 230 && OutputStencilValue <= 249))
	{
		AddError(LOCTEXT(
			"StencilRangesOverlap",
			"Puzzle overlay stencil values must be distinct and must not overlap Hover/Selection 230-249."));
	}
	if (RoutingSettings.Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive)
	{
		if (RoutingSettings.MaxDistributedCandidatesPerLink < 36)
		{
			AddError(LOCTEXT(
				"DistributedCandidateBudgetTooSmall",
				"Distributed Repulsive requires at least 36 candidates per link so every source/target face pair can remain represented."));
		}
		if (RoutingSettings.MaxNegotiationPasses < 1)
		{
			AddError(LOCTEXT(
				"DistributedNegotiationPassesInvalid",
				"Distributed Repulsive requires at least one bounded negotiation pass."));
		}
		if (RoutingSettings.ProximityRadius < 0
			|| RoutingSettings.ProximityPenalty < 0.0
			|| RoutingSettings.SharedEdgePenalty < 0.0
			|| RoutingSettings.HistoricalCongestionWeight < 0.0)
		{
			AddError(LOCTEXT(
				"DistributedCostsInvalid",
				"Distributed Repulsive radius and congestion/proximity costs cannot be negative."));
		}
		if (RoutingSettings.SingleLinkFineFacePairLimit < 1
			|| RoutingSettings.SingleLinkFineFacePairLimit > 36
			|| RoutingSettings.SubdividedFineFacePairLimit < 1
			|| RoutingSettings.SubdividedFineFacePairLimit > 36
			|| RoutingSettings.BaseResolutionFineFacePairLimit < 1
			|| RoutingSettings.BaseResolutionFineFacePairLimit > 36)
		{
			AddError(LOCTEXT(
				"DistributedFineFacePairLimitsInvalid",
				"Distributed Repulsive fine face-pair limits must remain between 1 and 36."));
		}
		if (RoutingSettings.SpatialIndexLinkThreshold < 0
			|| RoutingSettings.SpatialIndexEdgeThreshold < 0)
		{
			AddError(LOCTEXT(
				"DistributedSpatialThresholdsInvalid",
				"Distributed Repulsive spatial-index thresholds cannot be negative."));
		}
	}
	return Result;
}
#endif

void UParadoxPuzzleCircuitRendererComponent::HandleSelectedActorChanged(
	AActor* PreviousActor,
	AActor* NewActor)
{
	CancelActiveRoutingRequest(
		IsValid(NewActor)
			? EParadoxPuzzleWireCalculationCompletionStatus::Superseded
			: EParadoxPuzzleWireCalculationCompletionStatus::Cancelled);
	ClearOverlay();
	if (!IsValid(NewActor))
	{
		return;
	}

	const UParadoxSelectableComponent* Selectable =
		NewActor->FindComponentByClass<UParadoxSelectableComponent>();
	if (!Selectable || !Selectable->bShowPuzzleConnectionsWhenSelected)
	{
		return;
	}

	DisplayedActor = NewActor;
	RebuildSelectedGraph(true);
}

void UParadoxPuzzleCircuitRendererComponent::HandleEndpointDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == DisplayedActor.Get())
	{
		ClearOverlay();
		return;
	}
	// PuzzleGraph deliberately retains invalid endpoint relationships until Controller refresh.
	// Preserve the last meaningful route instead of rerouting the weak endpoint to world origin;
	// the graph state callback independently lowers SignalStrength to its inactive value.
	BoundEndpointActors.Remove(DestroyedActor);
}

void UParadoxPuzzleCircuitRendererComponent::HandleEndpointTransformUpdated(
	USceneComponent* UpdatedComponent,
	EUpdateTransformFlags UpdateTransformFlags,
	ETeleportType TeleportType)
{
	AActor* EndpointActor = UpdatedComponent ? UpdatedComponent->GetOwner() : nullptr;
	if (!IsValid(EndpointActor) || !DisplayedActor.IsValid())
	{
		return;
	}

	const FParadoxPuzzleRoutingCoord NewCoord = QuantizeWorldLocation(
		ParadoxPuzzleCircuitRenderer::GetActorAnchor(EndpointActor));
	const FParadoxPuzzleWireEndpointBounds NewBounds = ResolveEndpointBounds(EndpointActor);
	const uint32 NewGeometrySignature = CalculateEndpointGeometrySignature(NewBounds);
	const FParadoxPuzzleRoutingCoord* PreviousCoord = EndpointCoordinates.Find(EndpointActor);
	const uint32* PreviousGeometrySignature = EndpointGeometrySignatures.Find(EndpointActor);
	if (PreviousCoord && *PreviousCoord == NewCoord
		&& PreviousGeometrySignature && *PreviousGeometrySignature == NewGeometrySignature)
	{
		return;
	}
	EndpointCoordinates.FindOrAdd(EndpointActor) = NewCoord;
	EndpointGeometrySignatures.FindOrAdd(EndpointActor) = NewGeometrySignature;
	QueueEndpointReroute(EndpointActor);
}

void UParadoxPuzzleCircuitRendererComponent::QueueEndpointReroute(AActor* EndpointActor)
{
	if (!IsValid(EndpointActor) || !GetWorld())
	{
		return;
	}
	PendingRerouteActors.Add(EndpointActor);
	if (!PendingRerouteTimerHandle.IsValid())
	{
		PendingRerouteTimerHandle = GetWorld()->GetTimerManager().SetTimerForNextTick(
			this,
			&ThisClass::ProcessPendingEndpointReroutes);
	}
}

void UParadoxPuzzleCircuitRendererComponent::ProcessPendingEndpointReroutes()
{
	PendingRerouteTimerHandle.Invalidate();
	if (PendingRerouteActors.IsEmpty() || !DisplayedActor.IsValid())
	{
		PendingRerouteActors.Reset();
		return;
	}

	TArray<TWeakObjectPtr<AActor>> Pending = PendingRerouteActors.Array();
	PendingRerouteActors.Reset();
	Pending.Sort([](const TWeakObjectPtr<AActor>& A, const TWeakObjectPtr<AActor>& B)
	{
		return GetPathNameSafe(A.Get()) < GetPathNameSafe(B.Get());
	});
	if (RoutingSettings.Algorithm == EParadoxPuzzleRoutingAlgorithm::OrderedBundles
		|| RoutingSettings.Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive)
	{
		RebuildSelectedGraph(true);
		return;
	}
	for (const TWeakObjectPtr<AActor>& Endpoint : Pending)
	{
		if (Endpoint.IsValid())
		{
			RebuildRoutesForEndpointActor(Endpoint.Get());
		}
	}
}

void UParadoxPuzzleCircuitRendererComponent::HandleGraphTopologyChanged(
	int64 GraphTopologyRevision,
	APuzzleController* AffectedController,
	EPuzzleGraphTopologyChangeKind ChangeKind)
{
	if (DisplayedActor.IsValid())
	{
		RebuildSelectedGraph(false);
	}
}

void UParadoxPuzzleCircuitRendererComponent::HandleGraphLinkStateChanged(
	const FPuzzleGraphLinkHandle& LinkHandle,
	const FPuzzleGraphLinkState& PreviousState,
	const FPuzzleGraphLinkState& NewState)
{
	const TArray<FRenderedInstanceRef>* InstanceRefs = InstancesByLink.Find(LinkHandle);
	if (!InstanceRefs)
	{
		return;
	}

	bool bActive = false;
	for (FParadoxPuzzleWireRoute& Route : RenderedRoutes)
	{
		if (Route.LinkHandle == LinkHandle)
		{
			bActive = Route.LinkKind == EPuzzleGraphLinkKind::GateInfluence
				? NewState.bGateInputValid && NewState.bGateInputActive
				: NewState.bEffectivePrimaryValid && NewState.bEffectivePrimaryActive;
			Route.bSignalValid = Route.LinkKind == EPuzzleGraphLinkKind::GateInfluence
				? NewState.bGateInputValid
				: NewState.bEffectivePrimaryValid;
			Route.GateMode = NewState.GateMode;
			Route.bGateValid = NewState.bGateValid;
			Route.bGateAllowsSignal = NewState.bGateAllowsSignal;
			Route.bControllerResultValid = NewState.bControllerResultValid;
			Route.bControllerResultActive = NewState.bControllerResultActive;
			break;
		}
	}
	UpdateLinkSignalStrength(LinkHandle, bActive);
}

void UParadoxPuzzleCircuitRendererComponent::RefreshOverlay()
{
	InvalidateRoutingCache();
	RebuildSelectedGraph(true);
}

bool UParadoxPuzzleCircuitRendererComponent::IsWireCalculationInProgress() const
{
	return ActiveCalculationContext.RequestId.IsValid()
		|| bRoutingTaskActive
		|| bPendingRebuild;
}

void UParadoxPuzzleCircuitRendererComponent::ClearOverlay()
{
	CancelActiveRoutingRequest(EParadoxPuzzleWireCalculationCompletionStatus::Cancelled);
	bPendingRebuild = false;
	bPendingRebuildForce = false;
	bPendingFullRebuild = false;
	PendingLocalizedEndpoint.Reset();
	++RoutingGeneration;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(PendingRerouteTimerHandle);
	}
	PendingRerouteTimerHandle.Invalidate();
	PendingRerouteActors.Reset();
	InvalidateRoutingCache();
	UnbindEndpointActors();
	if (InputWireInstances)
	{
		InputWireInstances->ClearInstances();
	}
	if (OutputWireInstances)
	{
		OutputWireInstances->ClearInstances();
	}
	RenderedRoutes.Reset();
	InstancesByLink.Reset();
	DisplayedActor.Reset();
	TopologySignature = 0;
	RoutingGridId.Invalidate();
	WarnedInvalidBoundsActors.Reset();
}

void UParadoxPuzzleCircuitRendererComponent::EnsureRenderComponents()
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(GetOwner()))
	{
		return;
	}

	if (!IsValid(PresentationActor))
	{
		InputWireInstances = nullptr;
		OutputWireInstances = nullptr;
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = GetOwner();
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PresentationActor = World->SpawnActor<AActor>(
			AActor::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
		if (!PresentationActor)
		{
			PARADOX_LOG_ERROR(
				TEXT("Puzzle overlay renderer '%s' could not create its transient presentation Actor in world '%s'."),
				*GetNameSafe(this),
				*GetNameSafe(World));
			return;
		}
		PresentationActor->SetActorHiddenInGame(false);
		PresentationActor->SetActorEnableCollision(false);
		PresentationActor->SetActorTickEnabled(false);
	}

	if (!IsValid(InputWireInstances))
	{
		InputWireInstances = NewObject<UInstancedStaticMeshComponent>(
			PresentationActor,
			TEXT("Puzzle Input Wire Instances"));
		PresentationActor->AddInstanceComponent(InputWireInstances);
		InputWireInstances->SetAbsolute(true, true, true);
		InputWireInstances->RegisterComponent();
	}
	if (!IsValid(OutputWireInstances))
	{
		OutputWireInstances = NewObject<UInstancedStaticMeshComponent>(
			PresentationActor,
			TEXT("Puzzle Output Wire Instances"));
		PresentationActor->AddInstanceComponent(OutputWireInstances);
		OutputWireInstances->SetAbsolute(true, true, true);
		OutputWireInstances->RegisterComponent();
	}

	ConfigureRenderComponent(InputWireInstances, EParadoxPuzzleWireDirection::Input);
	ConfigureRenderComponent(OutputWireInstances, EParadoxPuzzleWireDirection::Output);
}

void UParadoxPuzzleCircuitRendererComponent::ConfigureRenderComponent(
	UInstancedStaticMeshComponent* Component,
	EParadoxPuzzleWireDirection Direction)
{
	if (!Component)
	{
		return;
	}

	UStaticMesh* ResolvedMesh = WireMesh.LoadSynchronous();
	if (!ResolvedMesh)
	{
		ResolvedMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}
	Component->SetStaticMesh(ResolvedMesh);
	UMaterialInterface* ResolvedMaterial = Direction == EParadoxPuzzleWireDirection::Input
		? InputMaterial.LoadSynchronous()
		: OutputMaterial.LoadSynchronous();
	if (!ResolvedMaterial)
	{
		ResolvedMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	Component->SetMaterial(0, ResolvedMaterial);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(false);
	Component->bReceivesDecals = false;
	Component->NumCustomDataFloats = 1;
	Component->SetRenderCustomDepth(bRenderWiresInCustomDepth);
	Component->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
	Component->SetCustomDepthStencilValue(
		Direction == EParadoxPuzzleWireDirection::Input
			? InputStencilValue
			: OutputStencilValue);
}

void UParadoxPuzzleCircuitRendererComponent::RebuildSelectedGraph(const bool bForce)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_QueryAndRoute);
	if (!DisplayedActor.IsValid() || !GetWorld())
	{
		return;
	}
	if (bRebuilding || bRoutingTaskActive)
	{
		RequestPendingRebuild(bForce);
		return;
	}
	const double RequestStartSeconds = FPlatformTime::Seconds();
	TGuardValue<bool> RebuildGuard(bRebuilding, true);

	UPuzzleGraphSubsystem* Graph = GetWorld()->GetSubsystem<UPuzzleGraphSubsystem>();
	if (!Graph)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_QueryGraph);
	const FPuzzleActorGraphView View = Graph->QueryActorGraph(DisplayedActor.Get());
	TArray<FPuzzleGraphLink> InputLinks = View.IncomingPrimaryLinks;
	InputLinks.Append(View.IncomingGateLinks);
	TArray<FPuzzleGraphLink> OutputLinks = View.OutgoingPrimaryLinks;
	OutputLinks.Append(View.OutgoingGateLinks);
	const uint32 NewSignature = CalculateTopologySignature(InputLinks, OutputLinks);
	if (!bForce && NewSignature == TopologySignature)
	{
		return;
	}

	TopologySignature = NewSignature;
	EnsureRenderComponents();
	ResolveRoutingFrame(
		ParadoxPuzzleCircuitRenderer::GetActorAnchor(DisplayedActor.Get()));
	TArray<FPuzzleGraphLink> AllLinks = InputLinks;
	AllLinks.Append(OutputLinks);
	BindEndpointActors(AllLinks);
	FParadoxPuzzleRoutingSnapshot Snapshot;
	BuildRoutingSnapshot(InputLinks, OutputLinks, Snapshot);
	FPreparedRoutingRequest Request;
	Request.Context.RequestId = FGuid::NewGuid();
	Request.Context.RoutingGeneration = Snapshot.RoutingGeneration;
	Request.Context.TargetActor = DisplayedActor;
	Request.Context.Algorithm = Snapshot.Settings.Algorithm;
	Request.Context.ExecutionMode = ExecutionMode;
	Request.Snapshot = MoveTemp(Snapshot);
	Request.RoutingFrame = RoutingFrame;
	Request.RoutingGridId = RoutingGridId;
	Request.RequestStartSeconds = RequestStartSeconds;
	ActiveCalculationContext = Request.Context;
	BroadcastCalculationStarted(Request.Context);
	const uint32 RoutingSignature = CalculateRoutingSnapshotSignature(Request.Snapshot);
	Request.RoutingSignature = RoutingSignature;
	if (bHasCachedRoutingResult && RoutingSignature == CachedRoutingSignature)
	{
		FParadoxPuzzleRoutingResult CachedResult = CachedRoutingResult;
		CachedResult.RoutingGeneration = Request.Snapshot.RoutingGeneration;
		ApplySnapshotStateToResult(Request.Snapshot, CachedResult);
		Request.PreparationMilliseconds =
			(FPlatformTime::Seconds() - RequestStartSeconds) * 1000.0;
		bRebuilding = false;
		CompleteRoutingRequest(
			MoveTemp(Request),
			MoveTemp(CachedResult),
			EParadoxPuzzleWireCalculationCompletionStatus::AppliedFromCache,
			0.0,
			0.0);
		ActiveCalculationContext = FParadoxPuzzleWireCalculationContext();
		ProcessPendingRebuild();
		return;
	}
	ResolveSurfaceSamples(Request.Snapshot);
	Request.PreparationMilliseconds =
		(FPlatformTime::Seconds() - RequestStartSeconds) * 1000.0;
	bRebuilding = false;
	SubmitRoutingRequest(MoveTemp(Request));
}

void UParadoxPuzzleCircuitRendererComponent::RequestPendingRebuild(
	const bool bForce,
	AActor* EndpointActor)
{
	if (bEndingPlay || !DisplayedActor.IsValid())
	{
		return;
	}
	bPendingRebuild = true;
	bPendingRebuildForce |= bForce;
	if (!IsValid(EndpointActor))
	{
		bPendingFullRebuild = true;
		PendingLocalizedEndpoint.Reset();
	}
	else if (!bPendingFullRebuild)
	{
		PendingLocalizedEndpoint = EndpointActor;
	}
	CancelActiveRoutingRequest(EParadoxPuzzleWireCalculationCompletionStatus::Superseded);
}

void UParadoxPuzzleCircuitRendererComponent::ProcessPendingRebuild()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_ProcessPendingRequest);
	if (bEndingPlay || bRoutingTaskActive || bRebuilding || !bPendingRebuild)
	{
		return;
	}

	const bool bForce = bPendingRebuildForce;
	const bool bFullRebuild = bPendingFullRebuild;
	TWeakObjectPtr<AActor> LocalizedEndpoint = PendingLocalizedEndpoint;
	bPendingRebuild = false;
	bPendingRebuildForce = false;
	bPendingFullRebuild = false;
	PendingLocalizedEndpoint.Reset();

	if (!DisplayedActor.IsValid())
	{
		return;
	}
	if (!bFullRebuild
		&& LocalizedEndpoint.IsValid()
		&& RoutingSettings.Algorithm == EParadoxPuzzleRoutingAlgorithm::LegacyIndependent)
	{
		RebuildRoutesForEndpointActor(LocalizedEndpoint.Get());
		return;
	}
	RebuildSelectedGraph(bForce || bFullRebuild);
}

void UParadoxPuzzleCircuitRendererComponent::SubmitRoutingRequest(
	FPreparedRoutingRequest&& Request)
{
	if (bEndingPlay
		|| Request.Context.RoutingGeneration != RoutingGeneration
		|| Request.Context.TargetActor.Get() != DisplayedActor.Get())
	{
		FParadoxPuzzleRoutingResult EmptyResult;
		EmptyResult.RoutingGeneration = Request.Context.RoutingGeneration;
		CompleteRoutingRequest(
			MoveTemp(Request),
			MoveTemp(EmptyResult),
			ActiveCancellationStatus,
			0.0,
			0.0);
		ActiveCalculationContext = FParadoxPuzzleWireCalculationContext();
		ProcessPendingRebuild();
		return;
	}

	if (ExecutionMode == EParadoxPuzzleRoutingExecutionMode::Standard)
	{
		ExecuteRoutingRequestStandard(MoveTemp(Request));
	}
	else
	{
		ExecuteRoutingRequestAsync(MoveTemp(Request));
	}
}

void UParadoxPuzzleCircuitRendererComponent::ExecuteRoutingRequestStandard(
	FPreparedRoutingRequest&& Request)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_StandardSolve);
	const double SolveStartSeconds = FPlatformTime::Seconds();
	FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Request.Snapshot);
	const double SolveMilliseconds =
		(FPlatformTime::Seconds() - SolveStartSeconds) * 1000.0;
	CompleteRoutingRequest(
		MoveTemp(Request),
		MoveTemp(Result),
		EParadoxPuzzleWireCalculationCompletionStatus::Applied,
		0.0,
		SolveMilliseconds);
	ActiveCalculationContext = FParadoxPuzzleWireCalculationContext();
	ProcessPendingRebuild();
}

void UParadoxPuzzleCircuitRendererComponent::ExecuteRoutingRequestAsync(
	FPreparedRoutingRequest&& Request)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_LaunchWorkerSolve);
	bRoutingTaskActive = true;
	ActiveCancellationStatus = EParadoxPuzzleWireCalculationCompletionStatus::Cancelled;
	ActiveCancellationToken = MakeShared<UE::Tasks::FCancellationToken, ESPMode::ThreadSafe>();
	ActivePreparedRequest = MakeUnique<FPreparedRoutingRequest>(MoveTemp(Request));

	FParadoxPuzzleRoutingSnapshot WorkerSnapshot = MoveTemp(ActivePreparedRequest->Snapshot);
	const FGuid RequestId = ActivePreparedRequest->Context.RequestId;
	const double EnqueuedSeconds = FPlatformTime::Seconds();
	const TSharedPtr<UE::Tasks::FCancellationToken, ESPMode::ThreadSafe> CancellationToken =
		ActiveCancellationToken;
	const TWeakObjectPtr<UParadoxPuzzleCircuitRendererComponent> WeakThis(this);
	TFunction<void()> BeforeWorkerSolve;
#if WITH_DEV_AUTOMATION_TESTS
	BeforeWorkerSolve = BeforeWorkerSolveTestHook;
#endif

	UE::Tasks::Launch(
		UE_SOURCE_LOCATION,
		[WeakThis,
			RequestId,
			EnqueuedSeconds,
			CancellationToken,
			BeforeWorkerSolve = MoveTemp(BeforeWorkerSolve),
			Snapshot = MoveTemp(WorkerSnapshot)]() mutable
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_WorkerSolve);
			if (BeforeWorkerSolve)
			{
				BeforeWorkerSolve();
			}
			const double SolveStartSeconds = FPlatformTime::Seconds();
			const double QueueMilliseconds =
				(SolveStartSeconds - EnqueuedSeconds) * 1000.0;
			UE::Tasks::FCancellationTokenScope CancellationScope(*CancellationToken);
			FParadoxPuzzleRoutingResult Result =
				FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
			Result.bCancelled |= CancellationToken->IsCanceled();
			const double SolveMilliseconds =
				(FPlatformTime::Seconds() - SolveStartSeconds) * 1000.0;

			AsyncTask(
				ENamedThreads::GameThread,
				[WeakThis,
					RequestId,
					QueueMilliseconds,
					SolveMilliseconds,
					Snapshot = MoveTemp(Snapshot),
					Result = MoveTemp(Result)]() mutable
				{
					UParadoxPuzzleCircuitRendererComponent* Component = WeakThis.Get();
					if (!Component
						|| !Component->ActivePreparedRequest
						|| Component->ActivePreparedRequest->Context.RequestId != RequestId)
					{
						return;
					}
					Component->ActivePreparedRequest->Snapshot = MoveTemp(Snapshot);
					FPreparedRoutingRequest CompletedRequest =
						MoveTemp(*Component->ActivePreparedRequest);
					Component->ActivePreparedRequest.Reset();
					Component->CompleteAsyncRoutingRequest(
						MoveTemp(CompletedRequest),
						MoveTemp(Result),
						QueueMilliseconds,
						SolveMilliseconds);
				});
		},
		UE::Tasks::ETaskPriority::Normal);
}

void UParadoxPuzzleCircuitRendererComponent::CompleteAsyncRoutingRequest(
	FPreparedRoutingRequest&& Request,
	FParadoxPuzzleRoutingResult&& Result,
	const double QueueMilliseconds,
	const double SolveMilliseconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_CompleteWorkerSolve);
	check(IsInGameThread());
	const bool bCancelled = Result.bCancelled
		|| (ActiveCancellationToken && ActiveCancellationToken->IsCanceled());
	const EParadoxPuzzleWireCalculationCompletionStatus Status = bCancelled
		? ActiveCancellationStatus
		: EParadoxPuzzleWireCalculationCompletionStatus::Applied;
	bRoutingTaskActive = false;
	ActiveCancellationToken.Reset();

	CompleteRoutingRequest(
		MoveTemp(Request),
		MoveTemp(Result),
		Status,
		QueueMilliseconds,
		SolveMilliseconds);
	ActiveCalculationContext = FParadoxPuzzleWireCalculationContext();
	if (!bEndingPlay)
	{
		ProcessPendingRebuild();
	}
}

void UParadoxPuzzleCircuitRendererComponent::CompleteRoutingRequest(
	FPreparedRoutingRequest&& Request,
	FParadoxPuzzleRoutingResult&& Result,
	EParadoxPuzzleWireCalculationCompletionStatus Status,
	const double QueueMilliseconds,
	const double SolveMilliseconds)
{
	check(IsInGameThread());
	FParadoxPuzzleWireCalculationResult CalculationResult;
	CalculationResult.Context = Request.Context;
	CalculationResult.Status = Status;
	CalculationResult.PreparationMilliseconds = Request.PreparationMilliseconds;
	CalculationResult.QueueMilliseconds = QueueMilliseconds;
	CalculationResult.SolveMilliseconds = SolveMilliseconds;

	const bool bRequestStillCurrent = !bEndingPlay
		&& Request.Context.RoutingGeneration == RoutingGeneration
		&& Request.Context.TargetActor.Get() == DisplayedActor.Get();
	if ((Status == EParadoxPuzzleWireCalculationCompletionStatus::Applied
			|| Status == EParadoxPuzzleWireCalculationCompletionStatus::AppliedFromCache)
		&& !bRequestStillCurrent)
	{
		Status = DisplayedActor.IsValid()
			? EParadoxPuzzleWireCalculationCompletionStatus::Superseded
			: EParadoxPuzzleWireCalculationCompletionStatus::Cancelled;
		CalculationResult.Status = Status;
	}

	if (Status == EParadoxPuzzleWireCalculationCompletionStatus::Applied
		|| Status == EParadoxPuzzleWireCalculationCompletionStatus::AppliedFromCache)
	{
		RoutingFrame = Request.RoutingFrame;
		RoutingGridId = Request.RoutingGridId;
		ReconcileCurrentLinkState(Result);
		if (Status == EParadoxPuzzleWireCalculationCompletionStatus::Applied
			&& Request.RoutingSignature != 0)
		{
			CachedRoutingSignature = Request.RoutingSignature;
			CachedRoutingResult = Result;
			bHasCachedRoutingResult = true;
		}
		const double ApplyStartSeconds = FPlatformTime::Seconds();
		ApplyRoutingResult(MoveTemp(Result));
		CalculationResult.ApplyMilliseconds =
			(FPlatformTime::Seconds() - ApplyStartSeconds) * 1000.0;
		CalculationResult.RouteCount = RenderedRoutes.Num();
		CalculationResult.bResultApplied = true;
	}
	else
	{
		CalculationResult.RouteCount = Result.Routes.Num();
	}
	CalculationResult.TotalMilliseconds =
		(FPlatformTime::Seconds() - Request.RequestStartSeconds) * 1000.0;
	BroadcastCalculationFinished(CalculationResult);
}

void UParadoxPuzzleCircuitRendererComponent::CancelActiveRoutingRequest(
	const EParadoxPuzzleWireCalculationCompletionStatus Status)
{
	if (!ActiveCalculationContext.RequestId.IsValid() && !bRoutingTaskActive)
	{
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_CancelRouting);
	if (ActiveCancellationStatus != EParadoxPuzzleWireCalculationCompletionStatus::Superseded)
	{
		ActiveCancellationStatus = Status;
	}
	if (ActiveCancellationToken)
	{
		ActiveCancellationToken->Cancel();
	}
}

void UParadoxPuzzleCircuitRendererComponent::BroadcastCalculationStarted(
	const FParadoxPuzzleWireCalculationContext& Context)
{
	check(IsInGameThread());
	ActiveCancellationStatus = EParadoxPuzzleWireCalculationCompletionStatus::Cancelled;
	OnWireCalculationStartedNative.Broadcast(Context);
	OnWireCalculationStarted.Broadcast(Context);
}

void UParadoxPuzzleCircuitRendererComponent::BroadcastCalculationFinished(
	const FParadoxPuzzleWireCalculationResult& Result)
{
	check(IsInGameThread());
	OnWireCalculationFinishedNative.Broadcast(Result);
	OnWireCalculationFinished.Broadcast(Result);
}

void UParadoxPuzzleCircuitRendererComponent::ReconcileCurrentLinkState(
	FParadoxPuzzleRoutingResult& InOutResult) const
{
	UPuzzleGraphSubsystem* Graph = GetWorld()
		? GetWorld()->GetSubsystem<UPuzzleGraphSubsystem>()
		: nullptr;
	if (!Graph)
	{
		return;
	}
	for (FParadoxPuzzleWireRoute& Route : InOutResult.Routes)
	{
		FPuzzleGraphLinkState State;
		if (!Graph->TryGetLinkState(Route.LinkHandle, State))
		{
			Route.bSignalValid = false;
			Route.bActive = false;
			continue;
		}
		Route.bSignalValid = Route.LinkKind == EPuzzleGraphLinkKind::GateInfluence
			? State.bGateInputValid
			: State.bEffectivePrimaryValid;
		Route.bActive = Route.LinkKind == EPuzzleGraphLinkKind::GateInfluence
			? State.bGateInputValid && State.bGateInputActive
			: State.bEffectivePrimaryValid && State.bEffectivePrimaryActive;
		Route.GateMode = State.GateMode;
		Route.bGateValid = State.bGateValid;
		Route.bGateAllowsSignal = State.bGateAllowsSignal;
		Route.bControllerResultValid = State.bControllerResultValid;
		Route.bControllerResultActive = State.bControllerResultActive;
	}
}

void UParadoxPuzzleCircuitRendererComponent::BindEndpointActors(
	const TArray<FPuzzleGraphLink>& Links)
{
	UnbindEndpointActors();
	for (const FPuzzleGraphLink& Link : Links)
	{
		AActor* Source = ResolveEndpointActor(Link, true);
		AActor* Target = ResolveEndpointActor(Link, false);
		for (AActor* Endpoint : {Source, Target})
		{
			if (!IsValid(Endpoint) || BoundEndpointActors.Contains(Endpoint))
			{
				continue;
			}
			BoundEndpointActors.Add(Endpoint);
			EndpointCoordinates.Add(
				Endpoint,
				QuantizeWorldLocation(ParadoxPuzzleCircuitRenderer::GetActorAnchor(Endpoint)));
			EndpointGeometrySignatures.Add(
				Endpoint,
				CalculateEndpointGeometrySignature(ResolveEndpointBounds(Endpoint)));
			Endpoint->OnDestroyed.AddDynamic(this, &ThisClass::HandleEndpointDestroyed);
			const auto BindTransform = [this](USceneComponent* Component)
			{
				if (!Component || BoundEndpointComponents.Contains(Component))
				{
					return;
				}
				BoundEndpointComponents.Add(Component);
				Component->TransformUpdated.AddUObject(
					this,
					&ThisClass::HandleEndpointTransformUpdated);
			};
			BindTransform(Endpoint->GetRootComponent());
			bool bMultipleWireTargets = false;
			BindTransform(FindUniqueWireTargetComponent(Endpoint, bMultipleWireTargets));
		}
	}
}

void UParadoxPuzzleCircuitRendererComponent::RebuildRoutesForEndpointActor(AActor* EndpointActor)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_LocalizedReroute);
	if (!IsValid(EndpointActor) || !DisplayedActor.IsValid() || !GetWorld())
	{
		return;
	}
	if (bRebuilding || bRoutingTaskActive)
	{
		RequestPendingRebuild(true, EndpointActor);
		return;
	}
	const double RequestStartSeconds = FPlatformTime::Seconds();
	TGuardValue<bool> RebuildGuard(bRebuilding, true);

	UPuzzleGraphSubsystem* Graph = GetWorld()->GetSubsystem<UPuzzleGraphSubsystem>();
	if (!Graph)
	{
		return;
	}
	if (RoutingSettings.Algorithm == EParadoxPuzzleRoutingAlgorithm::OrderedBundles
		|| RoutingSettings.Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive)
	{
		// Both global strategies choose faces and corridors using the complete selected subgraph.
		// A localized replacement would leave bundle or negotiated-congestion state inconsistent.
		bRebuilding = false;
		RebuildSelectedGraph(true);
		return;
	}
	const FPuzzleActorGraphView View = Graph->QueryActorGraph(DisplayedActor.Get());
	TArray<FPuzzleGraphLink> InputLinks = View.IncomingPrimaryLinks;
	InputLinks.Append(View.IncomingGateLinks);
	TArray<FPuzzleGraphLink> OutputLinks = View.OutgoingPrimaryLinks;
	OutputLinks.Append(View.OutgoingGateLinks);
	if (CalculateTopologySignature(InputLinks, OutputLinks) != TopologySignature)
	{
		bRebuilding = false;
		RebuildSelectedGraph(true);
		return;
	}

	TSet<FPuzzleGraphLinkHandle> AffectedHandles;
	const auto CollectAffected = [EndpointActor, &AffectedHandles](const TArray<FPuzzleGraphLink>& Links)
	{
		for (const FPuzzleGraphLink& Link : Links)
		{
			AActor* Source = Link.LinkKind == EPuzzleGraphLinkKind::GateInfluence
				? Link.GateEmitterActor.Get()
				: Link.PrimaryEmitterActor.Get();
			AActor* Target = Link.LinkKind == EPuzzleGraphLinkKind::GateInfluence
				? Link.PrimaryEmitterActor.Get()
				: Link.TargetReceiverActor.Get();
			if (Source == EndpointActor || Target == EndpointActor)
			{
				AffectedHandles.Add(Link.LinkHandle);
			}
		}
	};
	CollectAffected(InputLinks);
	CollectAffected(OutputLinks);
	TSet<FString> AffectedPortGroups;
	for (const FParadoxPuzzleWireRoute& Route : RenderedRoutes)
	{
		if (!AffectedHandles.Contains(Route.LinkHandle))
		{
			continue;
		}
		AffectedPortGroups.Add(FString::Printf(
			TEXT("%s|%d"),
			*Route.SourcePort.Bounds.EndpointKey,
			static_cast<int32>(Route.SourcePort.Side)));
		AffectedPortGroups.Add(FString::Printf(
			TEXT("%s|%d"),
			*Route.TargetPort.Bounds.EndpointKey,
			static_cast<int32>(Route.TargetPort.Side)));
	}
	for (const FParadoxPuzzleWireRoute& Route : RenderedRoutes)
	{
		const FString SourceGroup = FString::Printf(
			TEXT("%s|%d"),
			*Route.SourcePort.Bounds.EndpointKey,
			static_cast<int32>(Route.SourcePort.Side));
		const FString TargetGroup = FString::Printf(
			TEXT("%s|%d"),
			*Route.TargetPort.Bounds.EndpointKey,
			static_cast<int32>(Route.TargetPort.Side));
		if (AffectedPortGroups.Contains(SourceGroup) || AffectedPortGroups.Contains(TargetGroup))
		{
			AffectedHandles.Add(Route.LinkHandle);
		}
	}
	if (AffectedHandles.IsEmpty())
	{
		return;
	}

	const FTransform PreviousRoutingFrame = RoutingFrame;
	const FGuid PreviousGridId = RoutingGridId;
	const FVector2D PreviousPitch(RoutingSettings.PitchX, RoutingSettings.PitchY);
	FParadoxPuzzleRoutingSnapshot Snapshot;
	BuildRoutingSnapshot(InputLinks, OutputLinks, Snapshot);
	const auto SubmitPreparedSnapshot = [this, RequestStartSeconds](
		FParadoxPuzzleRoutingSnapshot&& PreparedSnapshot)
	{
		FPreparedRoutingRequest Request;
		Request.Context.RequestId = FGuid::NewGuid();
		Request.Context.RoutingGeneration = PreparedSnapshot.RoutingGeneration;
		Request.Context.TargetActor = DisplayedActor;
		Request.Context.Algorithm = PreparedSnapshot.Settings.Algorithm;
		Request.Context.ExecutionMode = ExecutionMode;
		Request.Snapshot = MoveTemp(PreparedSnapshot);
		Request.RoutingFrame = RoutingFrame;
		Request.RoutingGridId = RoutingGridId;
		Request.RequestStartSeconds = RequestStartSeconds;
		ActiveCalculationContext = Request.Context;
		BroadcastCalculationStarted(Request.Context);
		ResolveSurfaceSamples(Request.Snapshot);
		Request.PreparationMilliseconds =
			(FPlatformTime::Seconds() - RequestStartSeconds) * 1000.0;
		bRebuilding = false;
		SubmitRoutingRequest(MoveTemp(Request));
	};
	if (!RoutingFrame.Equals(PreviousRoutingFrame)
		|| RoutingGridId != PreviousGridId
		|| !PreviousPitch.Equals(FVector2D(RoutingSettings.PitchX, RoutingSettings.PitchY)))
	{
		TArray<FPuzzleGraphLink> AllLinks = InputLinks;
		AllLinks.Append(OutputLinks);
		BindEndpointActors(AllLinks);
		SubmitPreparedSnapshot(MoveTemp(Snapshot));
		return;
	}

	for (const FParadoxPuzzleWireRoute& Route : RenderedRoutes)
	{
		if (!AffectedHandles.Contains(Route.LinkHandle))
		{
			Snapshot.PreservedRoutes.Add(Route);
		}
	}

	Snapshot.Links.RemoveAll([&AffectedHandles](const FParadoxPuzzleRoutingLink& Link)
	{
		return !AffectedHandles.Contains(Link.LinkHandle);
	});

	TArray<FPuzzleGraphLink> AllLinks = InputLinks;
	AllLinks.Append(OutputLinks);
	BindEndpointActors(AllLinks);
	SubmitPreparedSnapshot(MoveTemp(Snapshot));
}

void UParadoxPuzzleCircuitRendererComponent::UnbindEndpointActors()
{
	for (const TWeakObjectPtr<AActor>& EndpointPtr : BoundEndpointActors)
	{
		if (AActor* Endpoint = EndpointPtr.Get())
		{
			Endpoint->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleEndpointDestroyed);
		}
	}
	for (const TWeakObjectPtr<USceneComponent>& ComponentPtr : BoundEndpointComponents)
	{
		if (USceneComponent* Component = ComponentPtr.Get())
		{
			Component->TransformUpdated.RemoveAll(this);
		}
	}
	BoundEndpointActors.Reset();
	BoundEndpointComponents.Reset();
	EndpointCoordinates.Reset();
	EndpointGeometrySignatures.Reset();
}

void UParadoxPuzzleCircuitRendererComponent::BuildRoutingSnapshot(
	const TArray<FPuzzleGraphLink>& InputLinks,
	const TArray<FPuzzleGraphLink>& OutputLinks,
	FParadoxPuzzleRoutingSnapshot& OutSnapshot)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_BuildSnapshot);
	const FVector SelectedLocation = ParadoxPuzzleCircuitRenderer::GetActorAnchor(DisplayedActor.Get());
	ResolveRoutingFrame(SelectedLocation);
	OutSnapshot.RoutingGeneration = ++RoutingGeneration;
	OutSnapshot.Settings = RoutingSettings;
	OutSnapshot.Settings.PitchX = RoutingSettings.PitchX;
	OutSnapshot.Settings.PitchY = RoutingSettings.PitchY;
	OutSnapshot.SelectedAnchor = QuantizeWorldLocation(SelectedLocation);
	OutSnapshot.bCollectDebugData = bEnableDebug && IsParadoxPuzzleOverlayDebugEnabled();

	int32 StableOrder = 0;
	const auto AppendLink = [this, &OutSnapshot, &StableOrder](
		const FPuzzleGraphLink& GraphLink,
		const EParadoxPuzzleWireDirection Direction)
	{
		FParadoxPuzzleRoutingLink& Link = OutSnapshot.Links.AddDefaulted_GetRef();
		Link.LinkHandle = GraphLink.LinkHandle;
		Link.Direction = Direction;
		Link.LinkKind = GraphLink.LinkKind;
		AActor* SourceActor = ResolveEndpointActor(GraphLink, true);
		AActor* TargetActor = ResolveEndpointActor(GraphLink, false);
		Link.Source = QuantizeWorldLocation(ParadoxPuzzleCircuitRenderer::GetActorAnchor(SourceActor));
		Link.Target = QuantizeWorldLocation(ParadoxPuzzleCircuitRenderer::GetActorAnchor(TargetActor));
		Link.SourceBounds = ResolveEndpointBounds(SourceActor);
		Link.TargetBounds = ResolveEndpointBounds(TargetActor);
		AActor* RemoteActor = Direction == EParadoxPuzzleWireDirection::Input
			? (GraphLink.LinkKind == EPuzzleGraphLinkKind::GateInfluence
				? GraphLink.GateEmitterActor.Get()
				: GraphLink.PrimaryEmitterActor.Get())
			: (GraphLink.LinkKind == EPuzzleGraphLinkKind::GateInfluence
				? GraphLink.PrimaryEmitterActor.Get()
				: GraphLink.TargetReceiverActor.Get());
		Link.RemoteEndpointKey = GetPathNameSafe(RemoteActor);
		Link.StableOrder = StableOrder++;
		FPuzzleGraphLinkState State;
		const UPuzzleGraphSubsystem* Graph = GetWorld()->GetSubsystem<UPuzzleGraphSubsystem>();
		if (Graph && Graph->TryGetLinkState(GraphLink.LinkHandle, State))
		{
			Link.bSignalValid = GraphLink.LinkKind == EPuzzleGraphLinkKind::GateInfluence
				? State.bGateInputValid
				: State.bEffectivePrimaryValid;
			Link.bActive = GraphLink.LinkKind == EPuzzleGraphLinkKind::GateInfluence
				? State.bGateInputValid && State.bGateInputActive
				: State.bEffectivePrimaryValid && State.bEffectivePrimaryActive;
			Link.GateMode = State.GateMode;
			Link.bGateValid = State.bGateValid;
			Link.bGateAllowsSignal = State.bGateAllowsSignal;
			Link.bControllerResultValid = State.bControllerResultValid;
			Link.bControllerResultActive = State.bControllerResultActive;
		}
	};

	for (const FPuzzleGraphLink& Link : InputLinks)
	{
		AppendLink(Link, EParadoxPuzzleWireDirection::Input);
	}
	for (const FPuzzleGraphLink& Link : OutputLinks)
	{
		AppendLink(Link, EParadoxPuzzleWireDirection::Output);
	}

}

void UParadoxPuzzleCircuitRendererComponent::ResolveRoutingFrame(const FVector& SelectedLocation)
{
	RoutingFrame = FTransform::Identity;
	RoutingGridId.Invalidate();
	const int32 SubdivisionFactor = GetParadoxPuzzleRoutingSubdivisionFactor(
		RoutingSettings.GridCellSubdivision);
	RoutingSettings.PitchX = 100.0 / SubdivisionFactor;
	RoutingSettings.PitchY = 100.0 / SubdivisionFactor;

	UGridWorldSubsystem* GridWorld = GetWorld()->GetSubsystem<UGridWorldSubsystem>();
	AGridNavigationData* NavData = GridWorld ? GridWorld->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr Snapshot = NavData ? NavData->GetSnapshot() : nullptr;
	if (!Snapshot.IsValid())
	{
		return;
	}

	const FGridRegionData* SelectedRegion = nullptr;
	const FGridCellQueryResult Projected = GridWorld->ProjectPoint(SelectedLocation);
	if (Projected.Status == EGridQueryStatus::Success)
	{
		SelectedRegion = Snapshot->FindRegion(Projected.CellId.GridId);
	}

	if (!SelectedRegion)
	{
		TArray<const FGridRegionData*> Regions;
		for (const TPair<FGuid, FGridRegionData>& Pair : Snapshot->Regions)
		{
			Regions.Add(&Pair.Value);
		}
		Regions.Sort([](const FGridRegionData& A, const FGridRegionData& B)
		{
			return A.GridId < B.GridId;
		});
		double BestDistance = TNumericLimits<double>::Max();
		for (const FGridRegionData* Region : Regions)
		{
			const double Distance = Region->WorldBounds.ComputeSquaredDistanceToPoint(SelectedLocation);
			if (!SelectedRegion || Distance < BestDistance)
			{
				SelectedRegion = Region;
				BestDistance = Distance;
			}
		}
	}

	if (SelectedRegion && SelectedRegion->GridTransform.IsValid())
	{
		RoutingGridId = SelectedRegion->GridId;
		RoutingFrame = FTransform(
			SelectedRegion->GridTransform.Rotation,
			SelectedRegion->GridTransform.Origin);
		RoutingSettings.PitchX = FMath::Max(
			1.0,
			SelectedRegion->GridTransform.CellSize.X / SubdivisionFactor);
		RoutingSettings.PitchY = FMath::Max(
			1.0,
			SelectedRegion->GridTransform.CellSize.Y / SubdivisionFactor);
	}
}

FParadoxPuzzleRoutingCoord UParadoxPuzzleCircuitRendererComponent::QuantizeWorldLocation(
	const FVector& WorldLocation) const
{
	const FVector Local = RoutingFrame.InverseTransformPosition(WorldLocation);
	return {
		FMath::RoundToInt32(Local.X / FMath::Max(1.0, RoutingSettings.PitchX)),
		FMath::RoundToInt32(Local.Y / FMath::Max(1.0, RoutingSettings.PitchY)),
		Local.Z};
}

void UParadoxPuzzleCircuitRendererComponent::ResolveSurfaceSamples(
	FParadoxPuzzleRoutingSnapshot& InOutSnapshot) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_ResolveSurfaces);
	if (InOutSnapshot.Links.IsEmpty())
	{
		return;
	}

	int32 MinX = InOutSnapshot.SelectedAnchor.X;
	int32 MaxX = MinX;
	int32 MinY = InOutSnapshot.SelectedAnchor.Y;
	int32 MaxY = MinY;
	TArray<double> Levels{InOutSnapshot.SelectedAnchor.Z};
	for (const FParadoxPuzzleRoutingLink& Link : InOutSnapshot.Links)
	{
		MinX = FMath::Min3(MinX, Link.Source.X, Link.Target.X);
		MaxX = FMath::Max3(MaxX, Link.Source.X, Link.Target.X);
		MinY = FMath::Min3(MinY, Link.Source.Y, Link.Target.Y);
		MaxY = FMath::Max3(MaxY, Link.Source.Y, Link.Target.Y);
		Levels.AddUnique(Link.Source.Z);
		Levels.AddUnique(Link.Target.Z);
		const auto IncludeBounds = [this, &MinX, &MaxX, &MinY, &MaxY, &Levels](
			const FParadoxPuzzleWireEndpointBounds& Bounds,
			const double EndpointZ)
		{
			if (!Bounds.bValid)
			{
				return;
			}
			MinX = FMath::Min(MinX, FMath::FloorToInt(Bounds.Min.X / FMath::Max(1.0, RoutingSettings.PitchX)));
			MaxX = FMath::Max(MaxX, FMath::CeilToInt(Bounds.Max.X / FMath::Max(1.0, RoutingSettings.PitchX)));
			MinY = FMath::Min(MinY, FMath::FloorToInt(Bounds.Min.Y / FMath::Max(1.0, RoutingSettings.PitchY)));
			MaxY = FMath::Max(MaxY, FMath::CeilToInt(Bounds.Max.Y / FMath::Max(1.0, RoutingSettings.PitchY)));
			Levels.AddUnique(FMath::Clamp(EndpointZ, Bounds.Min.Z, Bounds.Max.Z));
		};
		IncludeBounds(Link.SourceBounds, Link.Source.Z);
		IncludeBounds(Link.TargetBounds, Link.Target.Z);
	}
	const int32 RepulsiveRadius = RoutingSettings.Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive
		? RoutingSettings.ProximityRadius
		: 0;
	const int32 Margin = FMath::Clamp(
		FMath::Max(RoutingSettings.MaxRerouteAttempts, RepulsiveRadius) + 2,
		2,
		18);
	MinX -= Margin;
	MaxX += Margin;
	MinY -= Margin;
	MaxY += Margin;

	TSet<FParadoxPuzzleSurfaceKey> Keys;
	for (const double Level : Levels)
	{
		for (int32 Y = MinY; Y <= MaxY && Keys.Num() < ParadoxPuzzleCircuitRenderer::MaxSurfaceSamplesPerGeneration; ++Y)
		{
			for (int32 X = MinX; X <= MaxX && Keys.Num() < ParadoxPuzzleCircuitRenderer::MaxSurfaceSamplesPerGeneration; ++X)
			{
				Keys.Add({X, Y, ParadoxPuzzleCircuitRenderer::ToHeightBucket(Level)});
			}
		}
	}

	UGridWorldSubsystem* GridWorld = GetWorld()->GetSubsystem<UGridWorldSubsystem>();
	AGridNavigationData* NavData = GridWorld ? GridWorld->GetNavigationData() : nullptr;
	const FGridWorldSnapshotPtr GridSnapshot = NavData ? NavData->GetSnapshot() : nullptr;
	const FGridRegionData* RoutingRegion = GridSnapshot.IsValid() && RoutingGridId.IsValid()
		? GridSnapshot->FindRegion(RoutingGridId)
		: nullptr;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ParadoxPuzzleOverlaySurface), false);
	QueryParams.AddIgnoredActor(GetOwner());
	if (IsValid(PresentationActor))
	{
		QueryParams.AddIgnoredActor(PresentationActor);
	}
	for (const TWeakObjectPtr<AActor>& Endpoint : BoundEndpointActors)
	{
		if (Endpoint.IsValid())
		{
			QueryParams.AddIgnoredActor(Endpoint.Get());
		}
	}

	for (const FParadoxPuzzleSurfaceKey& Key : Keys)
	{
		const double RequestedLocalZ = Key.HeightBucket * ParadoxPuzzleCircuitRenderer::SurfaceHeightBucketSize;
		FParadoxPuzzleSurfaceSample Sample;
		const FVector LocalPoint(
			Key.X * RoutingSettings.PitchX,
			Key.Y * RoutingSettings.PitchY,
			RequestedLocalZ);
		const FVector WorldPoint = RoutingFrame.TransformPosition(LocalPoint);
		if (RoutingRegion && RoutingRegion->GridTransform.IsValid())
		{
			const FGridCellCoord CellCoord = RoutingRegion->GridTransform.WorldToCell(WorldPoint);
			FGridCellId CellId;
			CellId.GridId = RoutingGridId;
			CellId.Coord = CellCoord;
			if (const FGridCellData* Cell = GridSnapshot->FindCell(CellId))
			{
				const FVector LocalCenter = RoutingFrame.InverseTransformPosition(Cell->WorldCenter);
				const double Distance = FMath::Abs(LocalCenter.Z - RequestedLocalZ);
				Sample.bHasSurface = Distance <= MaxSurfaceSnapDistance;
				Sample.SurfaceZ = LocalCenter.Z;
				Sample.Normal = FVector(Cell->FloorNormal);
				Sample.bFromGridWorld = true;
			}
		}

		if (!Sample.bHasSurface)
		{
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByChannel(
				Hit,
				WorldPoint + FVector::UpVector * SurfaceTraceHeight,
				WorldPoint - FVector::UpVector * SurfaceTraceDepth,
				SurfaceTraceChannel,
				QueryParams)
				&& FMath::Abs(Hit.ImpactPoint.Z - WorldPoint.Z) <= MaxSurfaceSnapDistance)
			{
				Sample.bHasSurface = true;
				Sample.SurfaceZ = RoutingFrame.InverseTransformPosition(Hit.ImpactPoint).Z;
				Sample.Normal = Hit.ImpactNormal;
				Sample.bFromGridWorld = false;
			}
		}
		InOutSnapshot.SurfaceSamples.Add(Key, Sample);
	}
}

void UParadoxPuzzleCircuitRendererComponent::ApplyRoutingResult(
	FParadoxPuzzleRoutingResult&& Result)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleOverlay_ApplyInstances);
	if (Result.RoutingGeneration != RoutingGeneration)
	{
		return;
	}
	if (InputWireInstances)
	{
		InputWireInstances->ClearInstances();
	}
	if (OutputWireInstances)
	{
		OutputWireInstances->ClearInstances();
	}
	InstancesByLink.Reset();
	RenderedRoutes = MoveTemp(Result.Routes);
	struct FPendingWireInstance
	{
		FTransform Transform;
		FPuzzleGraphLinkHandle LinkHandle;
		float SignalStrength = 0.0f;
	};
	TArray<FPendingWireInstance> InputInstances;
	TArray<FPendingWireInstance> OutputInstances;
	for (const FParadoxPuzzleWireRoute& Route : RenderedRoutes)
	{
		UInstancedStaticMeshComponent* Component = Route.Direction == EParadoxPuzzleWireDirection::Input
			? InputWireInstances.Get()
			: OutputWireInstances.Get();
		if (!Component || !Component->GetStaticMesh())
		{
			continue;
		}
		TArray<FPendingWireInstance>& Pending = Route.Direction == EParadoxPuzzleWireDirection::Input
			? InputInstances
			: OutputInstances;
		const FVector MeshSize = Component->GetStaticMesh()->GetBounds().BoxExtent * 2.0;
		for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
		{
			FVector LocalStart = Segment.Start;
			FVector LocalEnd = Segment.End;
			LocalStart.Z += GroundWireHeightOffset;
			LocalEnd.Z += GroundWireHeightOffset;
			const FVector WorldStart = RoutingFrame.TransformPosition(LocalStart);
			const FVector WorldEnd = RoutingFrame.TransformPosition(LocalEnd);
			const FVector InstanceDirection = WorldEnd - WorldStart;
			const double Length = InstanceDirection.Size();
			if (Length <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const FVector Scale(
				Length / FMath::Max(1.0, MeshSize.X),
				WireThickness / FMath::Max(1.0, MeshSize.Y),
				WireThickness / FMath::Max(1.0, MeshSize.Z));
			FPendingWireInstance& Instance = Pending.AddDefaulted_GetRef();
			Instance.Transform = FTransform(
				InstanceDirection.Rotation(),
				(WorldStart + WorldEnd) * 0.5,
				Scale);
			Instance.LinkHandle = Route.LinkHandle;
			Instance.SignalStrength = Route.bActive ? ActiveSignalStrength : InactiveSignalStrength;
		}
	}
	const auto SubmitBatch = [this](
		UInstancedStaticMeshComponent* Component,
		const EParadoxPuzzleWireDirection Direction,
		const TArray<FPendingWireInstance>& Pending)
	{
		if (!Component || Pending.IsEmpty())
		{
			return;
		}
		TArray<FTransform> Transforms;
		Transforms.Reserve(Pending.Num());
		for (const FPendingWireInstance& Instance : Pending)
		{
			Transforms.Add(Instance.Transform);
		}
		const TArray<int32> InstanceIndices = Component->AddInstances(
			Transforms,
			true,
			true,
			false);
		const int32 Count = FMath::Min(Pending.Num(), InstanceIndices.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 InstanceIndex = InstanceIndices[Index];
			if (InstanceIndex == INDEX_NONE)
			{
				continue;
			}
			Component->SetCustomDataValue(
				InstanceIndex,
				0,
				Pending[Index].SignalStrength,
				false);
			InstancesByLink.FindOrAdd(Pending[Index].LinkHandle).Add({Direction, InstanceIndex});
		}
		Component->MarkRenderStateDirty();
	};
	SubmitBatch(InputWireInstances.Get(), EParadoxPuzzleWireDirection::Input, InputInstances);
	SubmitBatch(OutputWireInstances.Get(), EParadoxPuzzleWireDirection::Output, OutputInstances);

	if (bEnableDebug && IsParadoxPuzzleOverlayDebugEnabled())
	{
#if ENABLE_DRAW_DEBUG
		for (const FParadoxPuzzleWireRoute& Route : RenderedRoutes)
		{
			const FColor Color = Route.Direction == EParadoxPuzzleWireDirection::Input
				? FColor::Cyan
				: FColor(255, 96, 0);
			for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
			{
				DrawDebugLine(
					GetWorld(),
					RoutingFrame.TransformPosition(Segment.Start),
					RoutingFrame.TransformPosition(Segment.End),
					Color,
					false,
					1.0f,
					0,
					2.0f);
			}
			for (const FParadoxPuzzleWirePort* Port : {&Route.SourcePort, &Route.TargetPort})
			{
				if (Port->Bounds.bValid)
				{
					DrawDebugBox(
						GetWorld(),
						RoutingFrame.TransformPosition(Port->Bounds.GetCenter()),
						Port->Bounds.GetExtent(),
						RoutingFrame.GetRotation(),
						FColor::Yellow,
						false,
						1.0f,
						0,
						1.0f);
				}
				DrawDebugDirectionalArrow(
					GetWorld(),
					RoutingFrame.TransformPosition(Port->Position),
					RoutingFrame.TransformPosition(Port->ClearancePoint),
					8.0f,
					Color,
					false,
					1.0f,
					0,
					2.0f);
			}
		}
		for (const FParadoxPuzzleWireBundle& Bundle : Result.Bundles)
		{
			for (const FParadoxPuzzleWireSegment& Segment : Bundle.CenterlineSegments)
			{
				DrawDebugLine(
					GetWorld(),
					RoutingFrame.TransformPosition(Segment.Start + FVector(0.0, 0.0, 3.0)),
					RoutingFrame.TransformPosition(Segment.End + FVector(0.0, 0.0, 3.0)),
					FColor::Magenta,
					false,
					1.0f,
					0,
					1.0f);
			}
		}
		for (const FParadoxPuzzleFaceCandidateDebug& Candidate : Result.FaceCandidates)
		{
			const FColor CandidateColor = Candidate.bChosen ? FColor::Green : FColor(96, 96, 96);
			DrawDebugDirectionalArrow(
				GetWorld(),
				RoutingFrame.TransformPosition(Candidate.SourcePort.Position),
				RoutingFrame.TransformPosition(Candidate.SourcePort.ClearancePoint),
				6.0f,
				CandidateColor,
				false,
				1.0f,
				0,
				Candidate.bChosen ? 2.0f : 0.5f);
		}
		for (const FParadoxPuzzleCongestionEdgeDebug& Edge : Result.CongestionEdges)
		{
			const FColor CongestionColor = Edge.UsageCount > 1
				? FColor::Red
				: (Edge.HistoricalCongestion > 0.0 ? FColor::Orange : FColor::Green);
			DrawDebugLine(
				GetWorld(),
				RoutingFrame.TransformPosition(FVector(
					Edge.Start.X * RoutingSettings.PitchX,
					Edge.Start.Y * RoutingSettings.PitchY,
					Edge.Start.Z + 6.0)),
				RoutingFrame.TransformPosition(FVector(
					Edge.End.X * RoutingSettings.PitchX,
					Edge.End.Y * RoutingSettings.PitchY,
					Edge.End.Z + 6.0)),
				CongestionColor,
				false,
				1.0f,
				0,
				Edge.UsageCount > 1 ? 5.0f : 1.0f);
		}
#endif
		if (Result.Diagnostics.Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive)
		{
			PARADOX_LOG_INFO(
				TEXT("Puzzle overlay '%s' DistributedRepulsive generation %lld rendered %d route(s), %d candidate(s), coarse/pruned face pairs=%d/%d, fast-path wires=%d, context builds=%d, spatial queries/visits=%d/%d, negotiation %d pass(es) best=%d, rerouted=%d, shared=%d, parallel-near=%d, proximity=%.1f, history=%.1f, max usage=%d, nudged=%d, crossing(s)=%d, bridge(s)=%d in %.3f ms."),
				*GetNameSafe(this),
				RoutingGeneration,
				RenderedRoutes.Num(),
				Result.Diagnostics.CandidateCount,
				Result.Diagnostics.HierarchicalCoarseFacePairCount,
				Result.Diagnostics.PrunedFineFacePairCount,
				Result.Diagnostics.FastPathWireCount,
				Result.Diagnostics.RepulsiveContextBuildCount,
				Result.Diagnostics.SpatialQueryCount,
				Result.Diagnostics.SpatialEdgeVisitCount,
				Result.Diagnostics.NegotiationPassCount,
				Result.Diagnostics.BestNegotiationPass,
				Result.Diagnostics.ReroutedWireCount,
				Result.Diagnostics.SharedUnitEdgeLength,
				Result.Diagnostics.ParallelNearUnitEdgeLength,
				Result.Diagnostics.TotalProximityCost,
				Result.Diagnostics.TotalHistoricalCongestionCost,
				Result.Diagnostics.MaxEdgeUsageCount,
				Result.Diagnostics.NudgedSegmentCount,
				Result.Diagnostics.CrossingCount,
				Result.Diagnostics.BridgeCount,
				Result.Diagnostics.RoutingMilliseconds);
		}
		else
		{
			PARADOX_LOG_INFO(
				TEXT("Puzzle overlay '%s' generation %lld rendered %d route(s), %d candidate(s), %d bundle(s), %d bundled edge(s), reuse bonus %.1f, congestion %d, inversions %d->%d, nudged %d, corners topology/terminal/bridge/rendered=%d/%d/%d/%d, %d crossing(s), %d bridge(s) in %.3f ms."),
				*GetNameSafe(this),
				RoutingGeneration,
				RenderedRoutes.Num(),
				Result.Diagnostics.CandidateCount,
				Result.Diagnostics.BundleCount,
				Result.Diagnostics.BundledUnitEdgeCount,
				Result.Diagnostics.AppliedBundleReuseBonus,
				Result.Diagnostics.CongestedUnitEdgeCount,
				Result.Diagnostics.InversionsBeforeOrdering,
				Result.Diagnostics.InversionsAfterOrdering,
				Result.Diagnostics.NudgedSegmentCount,
				Result.Diagnostics.TotalTopologyCornerCount,
				Result.Diagnostics.TotalTerminalCornerCount,
				Result.Diagnostics.TotalBridgeCornerCount,
				Result.Diagnostics.TotalRenderedCornerCount,
				Result.Diagnostics.CrossingCount,
				Result.Diagnostics.BridgeCount,
				Result.Diagnostics.RoutingMilliseconds);
		}
	}
}

void UParadoxPuzzleCircuitRendererComponent::UpdateLinkSignalStrength(
	const FPuzzleGraphLinkHandle& LinkHandle,
	const bool bActive)
{
	const TArray<FRenderedInstanceRef>* InstanceRefs = InstancesByLink.Find(LinkHandle);
	if (!InstanceRefs)
	{
		return;
	}
	for (const FRenderedInstanceRef& Ref : *InstanceRefs)
	{
		UInstancedStaticMeshComponent* Component = Ref.Direction == EParadoxPuzzleWireDirection::Input
			? InputWireInstances.Get()
			: OutputWireInstances.Get();
		if (Component && Ref.InstanceIndex != INDEX_NONE)
		{
			Component->SetCustomDataValue(
				Ref.InstanceIndex,
				0,
				bActive ? ActiveSignalStrength : InactiveSignalStrength,
				true);
		}
	}
	for (FParadoxPuzzleWireRoute& Route : RenderedRoutes)
	{
		if (Route.LinkHandle == LinkHandle)
		{
			Route.bActive = bActive;
		}
	}
}

uint32 UParadoxPuzzleCircuitRendererComponent::CalculateTopologySignature(
	const TArray<FPuzzleGraphLink>& InputLinks,
	const TArray<FPuzzleGraphLink>& OutputLinks) const
{
	uint32 Signature = 0;
	for (const FPuzzleGraphLink& Link : InputLinks)
	{
		Signature = HashCombineFast(Signature, GetTypeHash(Link.LinkHandle));
		Signature = HashCombineFast(Signature, static_cast<uint32>(Link.LinkKind));
		Signature = HashCombineFast(Signature, static_cast<uint32>(EParadoxPuzzleWireDirection::Input));
	}
	for (const FPuzzleGraphLink& Link : OutputLinks)
	{
		Signature = HashCombineFast(Signature, GetTypeHash(Link.LinkHandle));
		Signature = HashCombineFast(Signature, static_cast<uint32>(Link.LinkKind));
		Signature = HashCombineFast(Signature, static_cast<uint32>(EParadoxPuzzleWireDirection::Output));
	}
	return Signature;
}

uint32 UParadoxPuzzleCircuitRendererComponent::CalculateRoutingSnapshotSignature(
	const FParadoxPuzzleRoutingSnapshot& Snapshot) const
{
	uint32 Signature = HashCombineFast(TopologySignature, GetTypeHash(RoutingGridId));
	const auto AddHash = [&Signature](const auto& Value)
	{
		Signature = HashCombineFast(Signature, GetTypeHash(Value));
	};
	const FParadoxPuzzleRoutingSettings& Settings = Snapshot.Settings;
	AddHash(static_cast<uint8>(Settings.Algorithm));
	AddHash(static_cast<uint8>(Settings.GridCellSubdivision));
	AddHash(Settings.PitchX);
	AddHash(Settings.PitchY);
	AddHash(Settings.LaneSpacing);
	AddHash(Settings.MaxLanesPerEdge);
	AddHash(Settings.BridgeHeightOffset);
	AddHash(Settings.MaxCandidatesPerLink);
	AddHash(Settings.MaxRerouteAttempts);
	AddHash(Settings.MaxOrderedBundleCandidatesPerLink);
	AddHash(Settings.MaxBundleOptimizationPasses);
	AddHash(Settings.MaxMetroOrderingPasses);
	AddHash(Settings.BendPenalty);
	AddHash(Settings.BundleReuseBonus);
	AddHash(Settings.MaxDistributedCandidatesPerLink);
	AddHash(Settings.MaxNegotiationPasses);
	AddHash(Settings.LengthWeight);
	AddHash(Settings.SharedEdgePenalty);
	AddHash(Settings.HistoricalCongestionWeight);
	AddHash(Settings.ProximityRadius);
	AddHash(Settings.ProximityPenalty);
	AddHash(Settings.ProximityFalloffExponent);
	AddHash(Settings.ParallelRunPenalty);
	AddHash(Settings.PerpendicularProximityScale);
	AddHash(Settings.EndpointEscapeDistance);
	AddHash(Settings.VerticalProximityThreshold);
	AddHash(Settings.bEnableHierarchicalFacePairPruning);
	AddHash(Settings.SingleLinkFineFacePairLimit);
	AddHash(Settings.SubdividedFineFacePairLimit);
	AddHash(Settings.BaseResolutionFineFacePairLimit);
	AddHash(Settings.bEnableSingleLinkFastPath);
	AddHash(Settings.bEnableConflictFreeNegotiationSkip);
	AddHash(Settings.SpatialIndexLinkThreshold);
	AddHash(Settings.SpatialIndexEdgeThreshold);
	AddHash(Settings.CornerPenalty);
	AddHash(Settings.UnsupportedPenalty);
	AddHash(Settings.LanePenalty);
	AddHash(Settings.CrossingPenalty);
	AddHash(Settings.VerticalPenalty);
	AddHash(Settings.BridgePenalty);
	AddHash(Settings.EndpointClearance);
	AddHash(Settings.MultiPortFanoutLength);
	AddHash(Settings.PortEdgeInset);
	AddHash(SurfaceTraceHeight);
	AddHash(SurfaceTraceDepth);
	AddHash(MaxSurfaceSnapDistance);
	AddHash(static_cast<uint8>(SurfaceTraceChannel.GetValue()));
	AddHash(RoutingFrame.GetLocation());
	AddHash(RoutingFrame.GetRotation());

	for (const FParadoxPuzzleRoutingLink& Link : Snapshot.Links)
	{
		AddHash(Link.LinkHandle);
		AddHash(static_cast<uint8>(Link.Direction));
		AddHash(static_cast<uint8>(Link.LinkKind));
		AddHash(Link.Source);
		AddHash(Link.Target);
		AddHash(Link.StableOrder);
		AddHash(CalculateEndpointGeometrySignature(Link.SourceBounds));
		AddHash(CalculateEndpointGeometrySignature(Link.TargetBounds));
	}
	for (const FParadoxPuzzleWireRoute& Route : Snapshot.PreservedRoutes)
	{
		AddHash(Route.LinkHandle);
		for (const FVector& Point : Route.RoutePoints)
		{
			AddHash(Point);
		}
	}
	return Signature;
}

void UParadoxPuzzleCircuitRendererComponent::ApplySnapshotStateToResult(
	const FParadoxPuzzleRoutingSnapshot& Snapshot,
	FParadoxPuzzleRoutingResult& InOutResult) const
{
	TMap<FPuzzleGraphLinkHandle, const FParadoxPuzzleRoutingLink*> LinksByHandle;
	for (const FParadoxPuzzleRoutingLink& Link : Snapshot.Links)
	{
		LinksByHandle.Add(Link.LinkHandle, &Link);
	}
	for (FParadoxPuzzleWireRoute& Route : InOutResult.Routes)
	{
		const FParadoxPuzzleRoutingLink* const* Link = LinksByHandle.Find(Route.LinkHandle);
		if (!Link || !*Link)
		{
			continue;
		}
		Route.bActive = (*Link)->bActive;
		Route.bSignalValid = (*Link)->bSignalValid;
		Route.GateMode = (*Link)->GateMode;
		Route.bGateValid = (*Link)->bGateValid;
		Route.bGateAllowsSignal = (*Link)->bGateAllowsSignal;
		Route.bControllerResultValid = (*Link)->bControllerResultValid;
		Route.bControllerResultActive = (*Link)->bControllerResultActive;
		Route.RoutingGeneration = Snapshot.RoutingGeneration;
	}
}

void UParadoxPuzzleCircuitRendererComponent::InvalidateRoutingCache()
{
	bHasCachedRoutingResult = false;
	CachedRoutingSignature = 0;
	CachedRoutingResult = FParadoxPuzzleRoutingResult();
}

AActor* UParadoxPuzzleCircuitRendererComponent::ResolveEndpointActor(
	const FPuzzleGraphLink& Link,
	const bool bSource) const
{
	if (Link.LinkKind == EPuzzleGraphLinkKind::GateInfluence)
	{
		return bSource ? Link.GateEmitterActor.Get() : Link.PrimaryEmitterActor.Get();
	}
	return bSource ? Link.PrimaryEmitterActor.Get() : Link.TargetReceiverActor.Get();
}

UBoxComponent* UParadoxPuzzleCircuitRendererComponent::FindUniqueWireTargetComponent(
	AActor* EndpointActor,
	bool& bOutMultiple) const
{
	bOutMultiple = false;
	if (!IsValid(EndpointActor))
	{
		return nullptr;
	}

	TArray<UBoxComponent*> TaggedBoxes;
	EndpointActor->GetComponents<UBoxComponent>(TaggedBoxes);
	TaggedBoxes.RemoveAll([EndpointActor](const UBoxComponent* Box)
	{
		return !IsValid(Box)
			|| Box->GetOwner() != EndpointActor
			|| !Box->ComponentHasTag(ParadoxPuzzleCircuitRenderer::WireTargetComponentTag);
	});
	TaggedBoxes.Sort([](const UBoxComponent& A, const UBoxComponent& B)
	{
		return A.GetPathName() < B.GetPathName();
	});
	bOutMultiple = TaggedBoxes.Num() > 1;
	return TaggedBoxes.IsEmpty() ? nullptr : TaggedBoxes[0];
}

FParadoxPuzzleWireEndpointBounds UParadoxPuzzleCircuitRendererComponent::ResolveEndpointBounds(
	AActor* EndpointActor)
{
	FParadoxPuzzleWireEndpointBounds Result;
	Result.EndpointKey = GetPathNameSafe(EndpointActor);
	const FVector ActorLocal = RoutingFrame.InverseTransformPosition(
		ParadoxPuzzleCircuitRenderer::GetActorAnchor(EndpointActor));
	Result.Min = ActorLocal;
	Result.Max = ActorLocal;
	Result.bPointFallback = true;
	if (!IsValid(EndpointActor))
	{
		return Result;
	}

	bool bMultipleWireTargets = false;
	UBoxComponent* WireTarget = FindUniqueWireTargetComponent(EndpointActor, bMultipleWireTargets);
	if (bMultipleWireTargets && !WarnedInvalidBoundsActors.Contains(EndpointActor))
	{
		WarnedInvalidBoundsActors.Add(EndpointActor);
		PARADOX_LOG_WARNING(
			TEXT("Puzzle overlay endpoint '%s' owns multiple UBoxComponent instances tagged WireTarget. '%s' was selected deterministically by component path."),
			*GetNameSafe(EndpointActor),
			*GetPathNameSafe(WireTarget));
	}

	if (WireTarget)
	{
		const FVector Extent = WireTarget->GetUnscaledBoxExtent();
		FBox RoutingBox(ForceInit);
		ParadoxPuzzleCircuitRenderer::AddProjectedBox(
			FBox(-Extent, Extent),
			WireTarget->GetComponentTransform(),
			RoutingFrame,
			RoutingBox);
		if (ParadoxPuzzleCircuitRenderer::IsUsableEndpointBox(RoutingBox))
		{
			Result.Min = RoutingBox.Min;
			Result.Max = RoutingBox.Max;
			Result.bValid = true;
			Result.bFromWireTarget = true;
			Result.bPointFallback = false;
			Result.Source = EParadoxPuzzleWireBoxSource::CustomWireTarget;
			return Result;
		}
		if (!WarnedInvalidBoundsActors.Contains(EndpointActor))
		{
			WarnedInvalidBoundsActors.Add(EndpointActor);
			PARADOX_LOG_WARNING(
				TEXT("Puzzle overlay endpoint '%s' has an unusable WireTarget box. Direct visual-mesh bounds are used."),
				*GetNameSafe(EndpointActor));
		}
	}

	FBox MeshRoutingBox(ForceInit);
	TInlineComponentArray<UStaticMeshComponent*> StaticMeshes(EndpointActor);
	for (UStaticMeshComponent* Mesh : StaticMeshes)
	{
		if (!IsValid(Mesh) || Mesh->GetOwner() != EndpointActor || !Mesh->GetStaticMesh()
			|| !Mesh->IsVisible() || Mesh->bHiddenInGame)
		{
			continue;
		}
		FVector LocalMin;
		FVector LocalMax;
		Mesh->GetLocalBounds(LocalMin, LocalMax);
		ParadoxPuzzleCircuitRenderer::AddProjectedBox(
			FBox(LocalMin, LocalMax),
			Mesh->GetComponentTransform(),
			RoutingFrame,
			MeshRoutingBox);
	}
	TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes(EndpointActor);
	for (USkeletalMeshComponent* Mesh : SkeletalMeshes)
	{
		if (!IsValid(Mesh) || Mesh->GetOwner() != EndpointActor || !Mesh->GetSkeletalMeshAsset()
			|| !Mesh->IsVisible() || Mesh->bHiddenInGame)
		{
			continue;
		}
		ParadoxPuzzleCircuitRenderer::AddProjectedBox(
			Mesh->GetLocalBounds().GetBox(),
			Mesh->GetComponentTransform(),
			RoutingFrame,
			MeshRoutingBox);
	}
	if (ParadoxPuzzleCircuitRenderer::IsUsableEndpointBox(MeshRoutingBox))
	{
		Result.Min = MeshRoutingBox.Min;
		Result.Max = MeshRoutingBox.Max;
		Result.bValid = true;
		Result.bPointFallback = false;
		Result.Source = EParadoxPuzzleWireBoxSource::VisibleMeshes;
		return Result;
	}

	if (!WarnedInvalidBoundsActors.Contains(EndpointActor))
	{
		WarnedInvalidBoundsActors.Add(EndpointActor);
		PARADOX_LOG_WARNING(
			TEXT("Puzzle overlay endpoint '%s' has no usable WireTarget or direct visual-mesh bounds; Actor Location point fallback is used."),
			*GetNameSafe(EndpointActor));
	}
	return Result;
}

uint32 UParadoxPuzzleCircuitRendererComponent::CalculateEndpointGeometrySignature(
	const FParadoxPuzzleWireEndpointBounds& Bounds) const
{
	uint32 Signature = GetTypeHash(Bounds.EndpointKey);
	Signature = HashCombineFast(Signature, GetTypeHash(Bounds.Min));
	Signature = HashCombineFast(Signature, GetTypeHash(Bounds.Max));
	Signature = HashCombineFast(Signature, GetTypeHash(Bounds.bValid));
	Signature = HashCombineFast(Signature, GetTypeHash(Bounds.bFromWireTarget));
	Signature = HashCombineFast(Signature, static_cast<uint32>(Bounds.Source));
	return Signature;
}

#undef LOCTEXT_NAMESPACE
