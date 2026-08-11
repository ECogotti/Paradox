#include "Perception/ParadoxTemporalVisionComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Paradox.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "TimeLoop/ParadoxTemporalEntityComponent.h"

UParadoxTemporalVisionComponent::UParadoxTemporalVisionComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	OnlyOneArc = true;
	Only_Z_Rotation = true;
	IgnoreOwnerActorInTraceLine = true;
	BeginAndEndOverlapEvent = false;
	Angle1 = 35.0f;
	Angle2 = 35.0f;
	Radius1 = 0.0f;
	Radius2 = 900.0f;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
}

void UParadoxTemporalVisionComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxTemporalVision_VisualMeshTick);
		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	}
	SynchronizeCandidateSphere();
	if (bDetectionAuthoritative)
	{
		QuerySphereCandidates();
		EvaluateSphereCandidates();
	}

	if (bEnableDebug && IsParadoxTimeLoopDebugEnabled())
	{
		DrawTemporalDebug();
	}
}

void UParadoxTemporalVisionComponent::SetCandidateSphereComponent(
	USphereComponent* InCandidateSphere)
{
	if (CandidateSphere == InCandidateSphere)
	{
		SynchronizeCandidateSphere();
		return;
	}

	if (CandidateSphere)
	{
		CandidateSphere->SetGenerateOverlapEvents(false);
		CandidateSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	CandidateSphere = InCandidateSphere;
	if (!CandidateSphere)
	{
		return;
	}

	CandidateSphere->SetCanEverAffectNavigation(false);
	CandidateSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CandidateSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CandidateSphere->SetCollisionResponseToChannel(
		TemporalTargetObjectChannel,
		ECR_Overlap);
	CandidateSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CandidateSphere->SetGenerateOverlapEvents(false);
	SynchronizeCandidateSphere();
}

void UParadoxTemporalVisionComponent::SynchronizeCandidateSphere()
{
	if (!CandidateSphere)
	{
		return;
	}

	const float RequiredRadius = GetConfiguredOuterRadius();
	if (!FMath::IsNearlyEqual(
		CandidateSphere->GetUnscaledSphereRadius(),
		RequiredRadius))
	{
		CandidateSphere->SetSphereRadius(
			RequiredRadius,
			CandidateSphere->IsQueryCollisionEnabled());
	}
}

void UParadoxTemporalVisionComponent::RefreshTemporalCandidateFilter()
{
	SynchronizeCandidateSphere();
	if (!CandidateSphere)
	{
		return;
	}

	QuerySphereCandidates();
	if (bDetectionAuthoritative)
	{
		EvaluateSphereCandidates();
	}
}

bool UParadoxTemporalVisionComponent::IsWorldLocationWithinConfiguredCone(
	const FVector& WorldLocation) const
{
	const FVector ToTarget = WorldLocation - GetComponentLocation();
	const float Distance = ToTarget.Size();
	if (Distance > GetConfiguredOuterRadius()
		|| Distance < GetConfiguredInnerRadius())
	{
		return false;
	}

	FVector DirectionToTarget = ToTarget;
	FVector Forward = GetForwardVector();
	if (Only_Z_Rotation)
	{
		DirectionToTarget.Z = 0.0f;
		Forward.Z = 0.0f;
	}
	if (!DirectionToTarget.Normalize() || !Forward.Normalize())
	{
		return true;
	}

	const float MinimumDot = FMath::Cos(
		FMath::DegreesToRadians(GetConfiguredHalfAngle()));
	return FVector::DotProduct(Forward, DirectionToTarget) >= MinimumDot;
}

bool UParadoxTemporalVisionComponent::PrepareTemporalVision(FString& OutFailure)
{
	OutFailure.Reset();
	DisableTemporalDetection(true);
	bTemporalVisionPrepared = false;

	if (!GetOwner() || !GetWorld())
	{
		OutFailure = TEXT("Temporal Vision has no valid owner or World.");
		return false;
	}
	if (TraceResolution < 1)
	{
		OutFailure = TEXT("Temporal Vision trace resolution must be at least one.");
		return false;
	}
	if (!CandidateSphere)
	{
		OutFailure = TEXT("Temporal Vision has no configured Pawn candidate sphere.");
		return false;
	}
	TemporalTargetObjectChannel = ECC_Pawn;

	SetBeginAndEndOverlapEvent(false);
	SetDynamicMeshCollisionEnabled(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	ConfigureCandidateSphereShape();
	SynchronizeCandidateSphere();

	if (LineOfSightIsActive())
	{
		StopLineTrace();
	}
	StartLineTrace(
		UEngineTypes::ConvertToTraceType(MeshOcclusionTraceChannel),
		TraceResolution);
	StartBuildMesh();
	if (!MeshIsBuilt() || !RefreshLineTraceAndMesh())
	{
		OutFailure = TEXT("LineOfSight failed to build and refresh its procedural visual mesh.");
		StopLineTrace();
		CandidateSphere->SetGenerateOverlapEvents(false);
		CandidateSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return false;
	}

	bTemporalVisionPrepared = true;
	QuerySphereCandidates();
	return true;
}

void UParadoxTemporalVisionComponent::EnableTemporalDetection(
	const int32 InDetectionSessionId)
{
	if (!bTemporalVisionPrepared || !CandidateSphere)
	{
		PARADOX_LOG_ERROR(
			TEXT("Temporal Vision '%s' cannot enable detection before successful preparation."),
			*GetNameSafe(this));
		return;
	}

	DisableTemporalDetection(true);
	DetectionSessionId = InDetectionSessionId;
	bDetectionAuthoritative = true;
	RefreshTemporalCandidateFilter();
}

void UParadoxTemporalVisionComponent::DisableTemporalDetection(
	const bool bClearPhysicalOverlapState)
{
	bDetectionAuthoritative = false;
	DetectionSessionId = INDEX_NONE;
	if (bClearPhysicalOverlapState)
	{
		ActorOverlapStates.Reset();
	}
	else
	{
		for (TPair<TWeakObjectPtr<AActor>, FParadoxTemporalActorOverlapState>& Pair :
			ActorOverlapStates)
		{
			Pair.Value.bPassesConeFilter = false;
			Pair.Value.bBroadcastForCurrentAuthority = false;
		}
	}
}

int32 UParadoxTemporalVisionComponent::GetDeduplicatedOverlapActorCount() const
{
	int32 Count = 0;
	for (const TPair<TWeakObjectPtr<AActor>, FParadoxTemporalActorOverlapState>& Pair :
		ActorOverlapStates)
	{
		Count += Pair.Key.IsValid() && Pair.Value.bPassesConeFilter ? 1 : 0;
	}
	return Count;
}

FParadoxTemporalVisionDebugSnapshot
UParadoxTemporalVisionComponent::GetDebugSnapshot() const
{
	FParadoxTemporalVisionDebugSnapshot Snapshot;
	Snapshot.Observer = GetOwner();
	Snapshot.DetectionSessionId = DetectionSessionId;
	Snapshot.DeduplicatedActorPairCount = GetDeduplicatedOverlapActorCount();
	Snapshot.bDetectionAuthoritative = bDetectionAuthoritative;
	Snapshot.bLocalDebugEnabled = bEnableDebug;
	Snapshot.bGlobalDebugEnabled = IsParadoxTimeLoopDebugEnabled();

	if (const UParadoxTemporalEntityComponent* TemporalEntity =
		GetOwner()
			? GetOwner()->FindComponentByClass<UParadoxTemporalEntityComponent>()
			: nullptr)
	{
		Snapshot.ObserverTemporalIndex = TemporalEntity->GetTemporalIndex();
	}
	for (const TPair<TWeakObjectPtr<AActor>, FParadoxTemporalActorOverlapState>& Pair :
		ActorOverlapStates)
	{
		if (Pair.Value.bPassesConeFilter)
		{
			Snapshot.OverlappingPrimitiveCount += Pair.Value.Components.Num();
		}
	}
	return Snapshot;
}

void UParadoxTemporalVisionComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	DisableTemporalDetection(true);
	bTemporalVisionPrepared = false;
	if (CandidateSphere)
	{
		CandidateSphere->SetGenerateOverlapEvents(false);
		CandidateSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	StopLineTrace();
	SetGenerateOverlapEvents(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Super::EndPlay(EndPlayReason);
}

void UParadoxTemporalVisionComponent::ConfigureCandidateSphereShape()
{
	if (!CandidateSphere)
	{
		return;
	}

	CandidateSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CandidateSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CandidateSphere->SetCollisionResponseToChannel(
		TemporalTargetObjectChannel,
		ECR_Overlap);
	CandidateSphere->SetGenerateOverlapEvents(false);
	CandidateSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UParadoxTemporalVisionComponent::QuerySphereCandidates()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxTemporalVision_PawnSphereQuery);

	UWorld* World = GetWorld();
	if (!CandidateSphere || !World)
	{
		ActorOverlapStates.Reset();
		return;
	}

	for (TPair<TWeakObjectPtr<AActor>, FParadoxTemporalActorOverlapState>& Pair :
		ActorOverlapStates)
	{
		Pair.Value.Components.Reset();
	}

	CandidateOverlapBuffer.Reset();
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(TemporalTargetObjectChannel);
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(ParadoxTemporalVisionPawnSphere),
		false,
		GetOwner());
	World->OverlapMultiByObjectType(
		CandidateOverlapBuffer,
		CandidateSphere->GetComponentLocation(),
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(
			CandidateSphere->GetScaledSphereRadius()),
		QueryParams);

	for (const FOverlapResult& Result : CandidateOverlapBuffer)
	{
		UPrimitiveComponent* Component = Result.Component.Get();
		TrackSphereOverlap(
			Component ? Component->GetOwner() : Result.GetActor(),
			Component);
	}

	for (auto It = ActorOverlapStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value().Components.IsEmpty())
		{
			It.RemoveCurrent();
		}
	}
}

void UParadoxTemporalVisionComponent::TrackSphereOverlap(
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent)
{
	if (!IsValid(OtherActor)
		|| OtherActor == GetOwner()
		|| !IsValid(OtherComponent)
		|| OtherComponent->GetCollisionObjectType() != TemporalTargetObjectChannel)
	{
		return;
	}

	FParadoxTemporalActorOverlapState& State =
		ActorOverlapStates.FindOrAdd(OtherActor);
	State.Components.Add(OtherComponent);
}

void UParadoxTemporalVisionComponent::EvaluateSphereCandidates()
{
	for (auto It = ActorOverlapStates.CreateIterator(); It; ++It)
	{
		AActor* Actor = It.Key().Get();
		FParadoxTemporalActorOverlapState& State = It.Value();
		UPrimitiveComponent* Representative = nullptr;
		for (auto ComponentIt = State.Components.CreateIterator(); ComponentIt; ++ComponentIt)
		{
			if (!ComponentIt->IsValid())
			{
				ComponentIt.RemoveCurrent();
				continue;
			}
			if (!Representative)
			{
				Representative = ComponentIt->Get();
			}
		}

		if (!IsValid(Actor) || State.Components.IsEmpty())
		{
			It.RemoveCurrent();
			continue;
		}

		const bool bPassesFilter =
			PassesDistanceAndAngleFilter(*Actor, Representative);
		if (!bPassesFilter)
		{
			State.bPassesConeFilter = false;
			State.bBroadcastForCurrentAuthority = false;
			continue;
		}

		if (!State.bPassesConeFilter)
		{
			State.bPassesConeFilter = true;
			State.bBroadcastForCurrentAuthority = false;
		}
		BroadcastActorCandidate(*Actor, Representative, State);
	}
}

bool UParadoxTemporalVisionComponent::PassesDistanceAndAngleFilter(
	const AActor& OtherActor,
	const UPrimitiveComponent* RepresentativeComponent) const
{
	const FVector TargetLocation = RepresentativeComponent
		? RepresentativeComponent->Bounds.Origin
		: OtherActor.GetActorLocation();
	if (!IsWorldLocationWithinConfiguredCone(TargetLocation))
	{
		return false;
	}

	return HasClearTemporalLine(OtherActor, TargetLocation);
}

bool UParadoxTemporalVisionComponent::HasClearTemporalLine(
	const AActor& OtherActor,
	const FVector& TargetLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(ParadoxTemporalVisionOcclusion),
		TraceComplex,
		GetOwner());
	QueryParams.AddIgnoredActor(&OtherActor);
	return !World->LineTraceTestByChannel(
		GetComponentLocation(),
		TargetLocation,
		MeshOcclusionTraceChannel,
		QueryParams);
}

float UParadoxTemporalVisionComponent::GetConfiguredOuterRadius() const
{
	return FMath::Max(FMath::Abs(GetRadius1()), FMath::Abs(GetRadius2()));
}

float UParadoxTemporalVisionComponent::GetConfiguredInnerRadius() const
{
	return OnlyOneArc
		? 0.0f
		: FMath::Min(FMath::Abs(GetRadius1()), FMath::Abs(GetRadius2()));
}

float UParadoxTemporalVisionComponent::GetConfiguredHalfAngle() const
{
	const float HalfAngle = OnlyOneArc || FMath::Abs(GetRadius2()) >= FMath::Abs(GetRadius1())
		? FMath::Abs(GetAngle2())
		: FMath::Abs(GetAngle1());
	return FMath::Clamp(HalfAngle, 0.0f, 180.0f);
}

void UParadoxTemporalVisionComponent::BroadcastActorCandidate(
	AActor& OtherActor,
	UPrimitiveComponent* RepresentativeComponent,
	FParadoxTemporalActorOverlapState& State)
{
	if (!bDetectionAuthoritative || State.bBroadcastForCurrentAuthority)
	{
		return;
	}

	State.bBroadcastForCurrentAuthority = true;
	LastPhysicalOverlap = MakeSnapshot(
		OtherActor,
		RepresentativeComponent,
		State.Components.Num());
	OnTemporalOverlapDetected.Broadcast(LastPhysicalOverlap);
}

FParadoxTemporalOverlapSnapshot UParadoxTemporalVisionComponent::MakeSnapshot(
	AActor& OtherActor,
	UPrimitiveComponent* RepresentativeComponent,
	const int32 ComponentCount) const
{
	FParadoxTemporalOverlapSnapshot Snapshot;
	Snapshot.Observer = GetOwner();
	Snapshot.ObserverComponent =
		const_cast<UParadoxTemporalVisionComponent*>(this);
	Snapshot.Target = &OtherActor;
	Snapshot.TargetComponent = RepresentativeComponent;
	Snapshot.OverlappingComponentCount = ComponentCount;
	Snapshot.DetectionSessionId = DetectionSessionId;
	Snapshot.bDetectionAuthoritative = bDetectionAuthoritative;
	Snapshot.ObserverLocation = GetOwner()
		? GetOwner()->GetActorLocation()
		: FVector::ZeroVector;
	Snapshot.TargetLocation = OtherActor.GetActorLocation();

	if (const UParadoxTemporalEntityComponent* ObserverTemporal =
		GetOwner()
			? GetOwner()->FindComponentByClass<UParadoxTemporalEntityComponent>()
			: nullptr)
	{
		Snapshot.ObserverTemporalIndex = ObserverTemporal->GetTemporalIndex();
	}
	if (const UParadoxTemporalEntityComponent* TargetTemporal =
		OtherActor.FindComponentByClass<UParadoxTemporalEntityComponent>())
	{
		Snapshot.TargetTemporalIndex = TargetTemporal->GetTemporalIndex();
	}
	return Snapshot;
}

void UParadoxTemporalVisionComponent::DrawTemporalDebug() const
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const UParadoxTemporalEntityComponent* TemporalEntity =
		Owner->FindComponentByClass<UParadoxTemporalEntityComponent>();
	const int32 TemporalIndex = TemporalEntity
		? TemporalEntity->GetTemporalIndex()
		: INDEX_NONE;
	const FString AuthorityLabel = bDetectionAuthoritative
		? TEXT("AUTH")
		: TEXT("PASSIVE");
	const FString DebugLabel = FString::Printf(
		TEXT("%s T%d %s Session=%d Pairs=%d"),
		*Owner->GetName(),
		TemporalIndex,
		*AuthorityLabel,
		DetectionSessionId,
		GetDeduplicatedOverlapActorCount());

	DrawDebugString(
		World,
		OwnerLocation + FVector(0.0, 0.0, 120.0),
		DebugLabel,
		nullptr,
		bDetectionAuthoritative ? FColor::Red : FColor::Silver,
		0.0f,
		true);

	for (const TPair<TWeakObjectPtr<AActor>, FParadoxTemporalActorOverlapState>& Pair :
		ActorOverlapStates)
	{
		if (!Pair.Value.bPassesConeFilter)
		{
			continue;
		}
		const AActor* Target = Pair.Key.Get();
		if (!Target)
		{
			continue;
		}

		const FVector TargetLocation = Target->GetActorLocation();
		DrawDebugLine(
			World,
			OwnerLocation,
			TargetLocation,
			Pair.Value.bBroadcastForCurrentAuthority ? FColor::Red : FColor::Yellow,
			false,
			0.0f,
			0,
			2.0f);
		DrawDebugString(
			World,
			TargetLocation + FVector(0.0, 0.0, 90.0),
			FString::Printf(TEXT("overlap components=%d"), Pair.Value.Components.Num()),
			nullptr,
			FColor::Yellow,
			0.0f,
			true);
	}
}
