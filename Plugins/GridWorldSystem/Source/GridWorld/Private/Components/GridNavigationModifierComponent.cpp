// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GridNavigationModifierComponent.h"

#include "EngineUtils.h"
#include "Navigation/GridNavigationData.h"

UGridNavigationModifierComponent::UGridNavigationModifierComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UGridNavigationModifierComponent::AffectsPoint(const FVector& WorldPoint) const
{
	const FVector LocalPoint = GetComponentTransform().InverseTransformPosition(WorldPoint).GetAbs();
	return LocalPoint.X <= BoxExtent.X && LocalPoint.Y <= BoxExtent.Y && LocalPoint.Z <= BoxExtent.Z;
}

void UGridNavigationModifierComponent::SetBlockingEnabled(bool bInBlockCells)
{
	if (bBlockCells != bInBlockCells)
	{
		bBlockCells = bInBlockCells;
		NotifyNavigationData();
	}
}

void UGridNavigationModifierComponent::RefreshModifier()
{
	NotifyNavigationData();
}

FBox UGridNavigationModifierComponent::GetGridContributionBounds_Implementation() const
{
	return FBox(-BoxExtent, BoxExtent).TransformBy(GetComponentTransform().ToMatrixWithScale());
}

bool UGridNavigationModifierComponent::IsGridContributionEnabled_Implementation() const
{
	return IsActive() && IsRegistered();
}

void UGridNavigationModifierComponent::EnsureStableId(bool bForceNewId)
{
	if (bForceNewId || !ModifierId.IsValid())
	{
		ModifierId = FGuid::NewGuid();
	}
}

void UGridNavigationModifierComponent::PostLoad() { Super::PostLoad(); EnsureStableId(); }
void UGridNavigationModifierComponent::OnComponentCreated() { Super::OnComponentCreated(); EnsureStableId(); }
void UGridNavigationModifierComponent::PostDuplicate(EDuplicateMode::Type DuplicateMode) { Super::PostDuplicate(DuplicateMode); EnsureStableId(DuplicateMode != EDuplicateMode::PIE); }

void UGridNavigationModifierComponent::OnRegister()
{
	Super::OnRegister();
	EnsureStableId();
	TransformUpdatedHandle = TransformUpdated.AddUObject(this, &UGridNavigationModifierComponent::HandleTransformUpdated);
	NotifyNavigationData();
}

void UGridNavigationModifierComponent::OnUnregister()
{
	TransformUpdated.Remove(TransformUpdatedHandle);
	Super::OnUnregister();
	NotifyNavigationData();
}

#if WITH_EDITOR
void UGridNavigationModifierComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	BoxExtent = BoxExtent.GetAbs();
	CostMultiplier = FMath::Max(0.001, CostMultiplier);
	OverrideCost = FMath::Max(1, OverrideCost);
	Super::PostEditChangeProperty(PropertyChangedEvent);
	NotifyNavigationData();
}
#endif

void UGridNavigationModifierComponent::HandleTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	NotifyNavigationData();
}

void UGridNavigationModifierComponent::NotifyNavigationData() const
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AGridNavigationData> It(World); It; ++It)
		{
			It->RefreshRuntimeOverlay(false);
		}
	}
}
