// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GridNavigationLinkComponent.h"

#include "EngineUtils.h"
#include "Navigation/GridNavigationData.h"

UGridNavigationLinkComponent::UGridNavigationLinkComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FVector UGridNavigationLinkComponent::GetStartWorldLocation() const { return GetComponentTransform().TransformPosition(StartOffset); }
FVector UGridNavigationLinkComponent::GetEndWorldLocation() const { return GetComponentTransform().TransformPosition(EndOffset); }

void UGridNavigationLinkComponent::SetLinkEnabled(bool bInEnabled)
{
	if (bEnabled != bInEnabled)
	{
		bEnabled = bInEnabled;
		NotifyNavigationData();
	}
}

void UGridNavigationLinkComponent::RefreshLink()
{
	NotifyNavigationData();
}

FBox UGridNavigationLinkComponent::GetGridContributionBounds_Implementation() const
{
	return FBox(GetStartWorldLocation(), GetEndWorldLocation()).ExpandBy(5.0);
}

bool UGridNavigationLinkComponent::IsGridContributionEnabled_Implementation() const
{
	return IsActive() && IsRegistered();
}

void UGridNavigationLinkComponent::EnsureStableId(bool bForceNewId)
{
	if (bForceNewId || !LinkId.IsValid())
	{
		LinkId = FGuid::NewGuid();
	}
}

void UGridNavigationLinkComponent::PostLoad() { Super::PostLoad(); EnsureStableId(); }
void UGridNavigationLinkComponent::OnComponentCreated() { Super::OnComponentCreated(); EnsureStableId(); }
void UGridNavigationLinkComponent::PostDuplicate(EDuplicateMode::Type DuplicateMode) { Super::PostDuplicate(DuplicateMode); EnsureStableId(DuplicateMode != EDuplicateMode::PIE); }

void UGridNavigationLinkComponent::OnRegister()
{
	Super::OnRegister();
	EnsureStableId();
	TransformUpdatedHandle = TransformUpdated.AddUObject(this, &UGridNavigationLinkComponent::HandleTransformUpdated);
	NotifyNavigationData();
}

void UGridNavigationLinkComponent::OnUnregister()
{
	TransformUpdated.Remove(TransformUpdatedHandle);
	Super::OnUnregister();
	NotifyNavigationData();
}

#if WITH_EDITOR
void UGridNavigationLinkComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	TraversalCost = FMath::Max(1, TraversalCost);
	Super::PostEditChangeProperty(PropertyChangedEvent);
	NotifyNavigationData();
}
#endif

void UGridNavigationLinkComponent::HandleTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	NotifyNavigationData();
}

void UGridNavigationLinkComponent::NotifyNavigationData() const
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AGridNavigationData> It(World); It; ++It)
		{
			It->RefreshRuntimeOverlay(false);
		}
	}
}
