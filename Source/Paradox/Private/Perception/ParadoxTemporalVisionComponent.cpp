#include "Perception/ParadoxTemporalVisionComponent.h"

#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Paradox.h"
#include "TimeLoop/ParadoxTemporalEntityComponent.h"

UParadoxTemporalVisionComponent::UParadoxTemporalVisionComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseAsyncCooking = false;
	OnlyOneArc = true;
	Only_Z_Rotation = true;
	IgnoreOwnerActorInTraceLine = true;
	BeginAndEndOverlapEvent = false;
	Angle1 = 35.0f;
	Angle2 = 35.0f;
	Radius1 = 0.0f;
	Radius2 = 900.0f;
	SetCanEverAffectNavigation(false);
}

void UParadoxTemporalVisionComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bEnableDebug && IsParadoxTimeLoopDebugEnabled())
	{
		DrawTemporalDebug();
	}
}

bool UParadoxTemporalVisionComponent::PrepareTemporalVision(FString& OutFailure)
{
	OutFailure.Reset();
	DisableTemporalDetection(true);

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

	if (!bPhysicalDelegatesBound)
	{
		OnComponentBeginOverlap.AddDynamic(
			this,
			&UParadoxTemporalVisionComponent::HandlePhysicalBeginOverlap);
		OnComponentEndOverlap.AddDynamic(
			this,
			&UParadoxTemporalVisionComponent::HandlePhysicalEndOverlap);
		bPhysicalDelegatesBound = true;
	}

	SetBeginAndEndOverlapEvent(false);
	SetDynamicMeshCollisionEnabled(true);
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionObjectType(ECC_WorldDynamic);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(TemporalTargetObjectChannel, ECR_Overlap);
	SetGenerateOverlapEvents(true);

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
		OutFailure = TEXT("LineOfSight failed to build and refresh its procedural collision mesh.");
		StopLineTrace();
		return false;
	}

	UpdateOverlaps();
	return true;
}

void UParadoxTemporalVisionComponent::EnableTemporalDetection(
	const int32 InDetectionSessionId)
{
	DisableTemporalDetection(true);
	DetectionSessionId = InDetectionSessionId;
	UpdateOverlaps();
	ReconcileExistingOverlaps();
	bDetectionAuthoritative = true;

	for (TPair<TWeakObjectPtr<AActor>, FParadoxTemporalActorOverlapState>& Pair :
		ActorOverlapStates)
	{
		if (AActor* Actor = Pair.Key.Get())
		{
			UPrimitiveComponent* Representative = nullptr;
			for (const TWeakObjectPtr<UPrimitiveComponent>& Component : Pair.Value.Components)
			{
				if (Component.IsValid())
				{
					Representative = Component.Get();
					break;
				}
			}
			BroadcastActorCandidate(*Actor, Representative, Pair.Value);
		}
	}
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
			Pair.Value.bBroadcastForCurrentAuthority = false;
		}
	}
}

FParadoxTemporalVisionDebugSnapshot
UParadoxTemporalVisionComponent::GetDebugSnapshot() const
{
	FParadoxTemporalVisionDebugSnapshot Snapshot;
	Snapshot.Observer = GetOwner();
	Snapshot.DetectionSessionId = DetectionSessionId;
	Snapshot.DeduplicatedActorPairCount = ActorOverlapStates.Num();
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
		Snapshot.OverlappingPrimitiveCount += Pair.Value.Components.Num();
	}
	return Snapshot;
}

void UParadoxTemporalVisionComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	DisableTemporalDetection(true);
	if (bPhysicalDelegatesBound)
	{
		OnComponentBeginOverlap.RemoveDynamic(
			this,
			&UParadoxTemporalVisionComponent::HandlePhysicalBeginOverlap);
		OnComponentEndOverlap.RemoveDynamic(
			this,
			&UParadoxTemporalVisionComponent::HandlePhysicalEndOverlap);
		bPhysicalDelegatesBound = false;
	}
	StopLineTrace();
	SetGenerateOverlapEvents(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Super::EndPlay(EndPlayReason);
}

void UParadoxTemporalVisionComponent::HandlePhysicalBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	TrackPhysicalOverlap(OtherActor, OtherComponent);
}

void UParadoxTemporalVisionComponent::HandlePhysicalEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex)
{
	if (!IsValid(OtherActor))
	{
		return;
	}

	FParadoxTemporalActorOverlapState* State = ActorOverlapStates.Find(OtherActor);
	if (!State)
	{
		return;
	}
	State->Components.Remove(OtherComponent);
	for (auto It = State->Components.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
	if (State->Components.IsEmpty())
	{
		ActorOverlapStates.Remove(OtherActor);
	}
}

void UParadoxTemporalVisionComponent::ReconcileExistingOverlaps()
{
	TArray<UPrimitiveComponent*> Components;
	GetOverlappingComponents(Components);
	for (UPrimitiveComponent* Component : Components)
	{
		TrackPhysicalOverlap(Component ? Component->GetOwner() : nullptr, Component);
	}
}

void UParadoxTemporalVisionComponent::TrackPhysicalOverlap(
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent)
{
	if (!IsValid(OtherActor) || !IsValid(OtherComponent))
	{
		return;
	}

	FParadoxTemporalActorOverlapState& State =
		ActorOverlapStates.FindOrAdd(OtherActor);
	const int32 PreviousCount = State.Components.Num();
	State.Components.Add(OtherComponent);
	if (bDetectionAuthoritative
		&& PreviousCount == 0
		&& !State.bBroadcastForCurrentAuthority)
	{
		BroadcastActorCandidate(*OtherActor, OtherComponent, State);
	}
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
		ActorOverlapStates.Num());

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
