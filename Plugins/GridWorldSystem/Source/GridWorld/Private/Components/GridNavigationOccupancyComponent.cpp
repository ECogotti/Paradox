// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GridNavigationOccupancyComponent.h"

#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Navigation/GridNavigationData.h"
#include "NavigationSystem.h"

namespace UE::GridWorld::Private
{
	using FOccupantRegistry = TMap<FGuid, TWeakObjectPtr<UGridNavigationOccupancyComponent>>;
	TMap<TWeakObjectPtr<UWorld>, FOccupantRegistry> OccupantsByWorld;

	void PruneOccupantRegistry()
	{
		for (auto WorldIt = OccupantsByWorld.CreateIterator(); WorldIt; ++WorldIt)
		{
			if (!WorldIt.Key().IsValid())
			{
				WorldIt.RemoveCurrent();
				continue;
			}
			for (auto OccupantIt = WorldIt.Value().CreateIterator(); OccupantIt; ++OccupantIt)
			{
				if (!OccupantIt.Value().IsValid())
				{
					OccupantIt.RemoveCurrent();
				}
			}
		}
	}
}

UGridNavigationOccupancyComponent::UGridNavigationOccupancyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

bool UGridNavigationOccupancyComponent::AffectsPoint(const FVector& WorldPoint) const
{
	const FVector LocalPoint = GetComponentTransform().InverseTransformPosition(WorldPoint).GetAbs();
	return LocalPoint.X <= BoxExtent.X && LocalPoint.Y <= BoxExtent.Y && LocalPoint.Z <= BoxExtent.Z;
}

UGridNavigationOccupancyComponent* UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(const AActor& Actor)
{
	TInlineComponentArray<UGridNavigationOccupancyComponent*> Components(&Actor);
	for (UGridNavigationOccupancyComponent* Component : Components)
	{
		if (IsValid(Component) && Component->IsRegistered() && Component->IsActive() && !Component->bIsReservation)
		{
			return Component;
		}
	}
	return nullptr;
}

UGridNavigationOccupancyComponent* UGridNavigationOccupancyComponent::FindOrAddAgentOccupancy(
	APawn& Pawn,
	const float AgentRadius,
	const float AgentHeight,
	const bool bAutoCreate)
{
	if (UGridNavigationOccupancyComponent* Existing = FindActiveAgentOccupancy(Pawn))
	{
		return Existing;
	}
	if (!bAutoCreate)
	{
		return nullptr;
	}

	UGridNavigationOccupancyComponent* Component = NewObject<UGridNavigationOccupancyComponent>(
		&Pawn,
		UGridNavigationOccupancyComponent::StaticClass(),
		NAME_None,
		RF_Transient);
	if (Component == nullptr)
	{
		return nullptr;
	}

	const float SafeRadius = AgentRadius > 0.0f ? AgentRadius : 42.0f;
	const float SafeHeight = AgentHeight > 0.0f ? AgentHeight : 192.0f;
	Component->BoxExtent = FVector(SafeRadius, SafeRadius, SafeHeight * 0.5f);
	Component->bBlocksWhenConsidered = false;
	Component->AdditionalCost = 0;
	if (USceneComponent* RootComponent = Pawn.GetRootComponent())
	{
		Component->SetupAttachment(RootComponent);
	}
	Pawn.AddInstanceComponent(Component);
	Component->OnComponentCreated();
	Component->RegisterComponent();
	if (!Component->IsActive())
	{
		Component->Activate(true);
		Component->RefreshOccupancy();
	}
	return Component;
}

UGridNavigationOccupancyComponent* UGridNavigationOccupancyComponent::FindOccupantById(
	const UWorld& World,
	const FGuid& InOccupantId)
{
	check(IsInGameThread());
	if (!InOccupantId.IsValid())
	{
		return nullptr;
	}
	UE::GridWorld::Private::PruneOccupantRegistry();
	const UE::GridWorld::Private::FOccupantRegistry* WorldRegistry =
		UE::GridWorld::Private::OccupantsByWorld.Find(const_cast<UWorld*>(&World));
	const TWeakObjectPtr<UGridNavigationOccupancyComponent>* Component = WorldRegistry != nullptr
		? WorldRegistry->Find(InOccupantId)
		: nullptr;
	return Component != nullptr ? Component->Get() : nullptr;
}

void UGridNavigationOccupancyComponent::SetOccupancyEnabled(bool bEnabled)
{
	if (IsActive() != bEnabled)
	{
		SetActive(bEnabled);
		UpdateCachedOccupiedCells();
		NotifyNavigationData();
	}
}

void UGridNavigationOccupancyComponent::RefreshOccupancy()
{
	UpdateCachedOccupiedCells();
	NotifyNavigationData();
}

FBox UGridNavigationOccupancyComponent::GetGridContributionBounds_Implementation() const
{
	return FBox(-BoxExtent, BoxExtent).TransformBy(GetComponentTransform().ToMatrixWithScale());
}

bool UGridNavigationOccupancyComponent::IsGridContributionEnabled_Implementation() const
{
	return IsActive() && IsRegistered();
}

void UGridNavigationOccupancyComponent::EnsureStableId(bool bForceNewId)
{
	if (bForceNewId || !OccupantId.IsValid())
	{
		OccupantId = FGuid::NewGuid();
	}
}

void UGridNavigationOccupancyComponent::PostLoad() { Super::PostLoad(); EnsureStableId(); }
void UGridNavigationOccupancyComponent::OnComponentCreated() { Super::OnComponentCreated(); EnsureStableId(); }
void UGridNavigationOccupancyComponent::PostDuplicate(EDuplicateMode::Type DuplicateMode) { Super::PostDuplicate(DuplicateMode); EnsureStableId(DuplicateMode != EDuplicateMode::PIE); }

void UGridNavigationOccupancyComponent::OnRegister()
{
	Super::OnRegister();
	EnsureStableId();
	if (UWorld* World = GetWorld())
	{
		UE::GridWorld::Private::PruneOccupantRegistry();
		UE::GridWorld::Private::OccupantsByWorld.FindOrAdd(World).Add(OccupantId, this);
	}
	TransformUpdatedHandle = TransformUpdated.AddUObject(this, &UGridNavigationOccupancyComponent::HandleTransformUpdated);
	UpdateCachedOccupiedCells();
	NotifyNavigationData();
}

void UGridNavigationOccupancyComponent::OnUnregister()
{
	TransformUpdated.Remove(TransformUpdatedHandle);
	if (UWorld* World = GetWorld())
	{
		if (UE::GridWorld::Private::FOccupantRegistry* WorldRegistry = UE::GridWorld::Private::OccupantsByWorld.Find(World))
		{
			const TWeakObjectPtr<UGridNavigationOccupancyComponent>* Registered = WorldRegistry->Find(OccupantId);
			if (Registered != nullptr && Registered->Get() == this)
			{
				WorldRegistry->Remove(OccupantId);
			}
			if (WorldRegistry->IsEmpty())
			{
				UE::GridWorld::Private::OccupantsByWorld.Remove(World);
			}
		}
	}
	CachedOccupiedCells.Reset();
	Super::OnUnregister();
	NotifyNavigationData();
}

#if WITH_EDITOR
void UGridNavigationOccupancyComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	BoxExtent = BoxExtent.GetAbs();
	AdditionalCost = FMath::Max(0, AdditionalCost);
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateCachedOccupiedCells();
	NotifyNavigationData();
}
#endif

void UGridNavigationOccupancyComponent::HandleTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	if (UpdateCachedOccupiedCells())
	{
		NotifyNavigationData();
	}
}

bool UGridNavigationOccupancyComponent::UpdateCachedOccupiedCells()
{
	TSet<FGridCellId> NewOccupiedCells;
	if (IsActive() && IsRegistered())
	{
		const APawn* PawnOwner = !bIsReservation ? Cast<APawn>(GetOwner()) : nullptr;
		const FVector PawnNavLocation = PawnOwner != nullptr
			? PawnOwner->GetNavAgentLocation()
			: FNavigationSystem::InvalidLocation;
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AGridNavigationData> It(World); It; ++It)
			{
				const FGridWorldSnapshotPtr Snapshot = It->GetSnapshot();
				if (!Snapshot.IsValid())
				{
					continue;
				}
				const FVector QueryExtent = It->GetDefaultQueryExtent().GetAbs();
				const FGridCellData* ClosestPawnCell = nullptr;
				double ClosestPawnCellDistanceSquared = TNumericLimits<double>::Max();
				for (const FGridCellData& Cell : Snapshot->Cells)
				{
					if (AffectsPoint(Cell.WorldCenter))
					{
						NewOccupiedCells.Add(Cell.Id);
					}
					if (PawnOwner != nullptr
						&& FNavigationSystem::IsValidLocation(PawnNavLocation)
						&& Cell.bWalkable)
					{
						const FVector Delta = Cell.WorldCenter - PawnNavLocation;
						const double DistanceSquared = Delta.SizeSquared();
						if (FMath::Abs(Delta.X) <= QueryExtent.X + UE_KINDA_SMALL_NUMBER
							&& FMath::Abs(Delta.Y) <= QueryExtent.Y + UE_KINDA_SMALL_NUMBER
							&& FMath::Abs(Delta.Z) <= QueryExtent.Z + UE_KINDA_SMALL_NUMBER
							&& DistanceSquared < ClosestPawnCellDistanceSquared)
						{
							ClosestPawnCell = &Cell;
							ClosestPawnCellDistanceSquared = DistanceSquared;
						}
					}
				}
				if (ClosestPawnCell != nullptr)
				{
					// A Character's navigation location is its feet position. Guarantee that its
					// containing logical cell is published even when floor-distance maintenance,
					// an off-center placement, or a cell boundary keeps the floor center just
					// outside the component's physical box.
					NewOccupiedCells.Add(ClosestPawnCell->Id);
				}
			}
		}
	}

	if (NewOccupiedCells.Num() == CachedOccupiedCells.Num()
		&& NewOccupiedCells.Includes(CachedOccupiedCells)
		&& CachedOccupiedCells.Includes(NewOccupiedCells))
	{
		return false;
	}
	CachedOccupiedCells = MoveTemp(NewOccupiedCells);
	return true;
}

bool UGridNavigationOccupancyComponent::AffectsCell(
	const FGridCellId& CellId,
	const FVector& WorldCenter) const
{
	return CachedOccupiedCells.Contains(CellId) || AffectsPoint(WorldCenter);
}

void UGridNavigationOccupancyComponent::NotifyNavigationData() const
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AGridNavigationData> It(World); It; ++It)
		{
			It->RefreshRuntimeOverlay(true);
		}
	}
}
