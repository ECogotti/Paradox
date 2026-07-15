// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GridNavigationOccupancyComponent.h"

#include "EngineUtils.h"
#include "Navigation/GridNavigationData.h"

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
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AGridNavigationData> It(World); It; ++It)
			{
				const FGridWorldSnapshotPtr Snapshot = It->GetSnapshot();
				if (!Snapshot.IsValid())
				{
					continue;
				}
				for (const FGridCellData& Cell : Snapshot->Cells)
				{
					if (AffectsPoint(Cell.WorldCenter))
					{
						NewOccupiedCells.Add(Cell.Id);
					}
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
