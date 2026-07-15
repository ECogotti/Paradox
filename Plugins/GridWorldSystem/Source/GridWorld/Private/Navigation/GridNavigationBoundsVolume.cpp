// Copyright Epic Games, Inc. All Rights Reserved.

#include "Navigation/GridNavigationBoundsVolume.h"

#include "Components/BrushComponent.h"
#include "Model.h"
#include "Engine/Polys.h"
#include "NavigationSystem.h"
#include "PhysicsEngine/BodySetup.h"

AGridNavigationBoundsVolume::AGridNavigationBoundsVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAllowPhysicsOverlap = false;
}

FGridTransform AGridNavigationBoundsVolume::GetGridTransform() const
{
	FGridTransform Result;
	Result.Origin = GetActorLocation();
	Result.Rotation = GetActorRotation();
	Result.CellSize = FVector(HorizontalCellSize.X, HorizontalCellSize.Y, LayerHeight);
	return Result;
}

FBox AGridNavigationBoundsVolume::GetLocalGridBounds() const
{
	const UBrushComponent* LocalBrushComponent = GetBrushComponent();
	if (LocalBrushComponent == nullptr)
	{
		return FBox(ForceInit);
	}
	const FBox UnscaledBounds = LocalBrushComponent->CalcBounds(FTransform::Identity).GetBox();
	return UnscaledBounds.IsValid
		? UnscaledBounds.TransformBy(FScaleMatrix(GetActorScale3D()))
		: FBox(ForceInit);
}

FBox AGridNavigationBoundsVolume::GetGridWorldBounds() const
{
	const FBox LocalBounds = GetLocalGridBounds();
	const FTransform GridFrame(GetActorQuat(), GetActorLocation(), FVector::OneVector);
	return LocalBounds.IsValid ? LocalBounds.TransformBy(GridFrame.ToMatrixWithScale()) : FBox(ForceInit);
}

bool AGridNavigationBoundsVolume::ValidateGridBounds(FString& OutError) const
{
	auto Fail = [&OutError](const TCHAR* Message)
	{
		OutError = Message;
		return false;
	};

	if (!GridId.IsValid())
	{
		return Fail(TEXT("GridId is invalid."));
	}
	if (HorizontalCellSize.X <= UE_SMALL_NUMBER || HorizontalCellSize.Y <= UE_SMALL_NUMBER || LayerHeight <= UE_SMALL_NUMBER)
	{
		return Fail(TEXT("Cell dimensions must be positive."));
	}
	if (ChunkSize <= 0 || AgentRadius <= UE_SMALL_NUMBER || AgentHeight < AgentRadius * 2.0)
	{
		return Fail(TEXT("Chunk size or Supported Agent dimensions are invalid."));
	}
	if (!FMath::IsFinite(CellCenterTolerance) || CellCenterTolerance < 0.1f
		|| !FMath::IsFinite(StopSpeedTolerance) || StopSpeedTolerance < 0.0f)
	{
		return Fail(TEXT("Path-following center and stop tolerances are invalid."));
	}
	if (GetActorLocation().ContainsNaN() || GetActorRotation().ContainsNaN())
	{
		return Fail(TEXT("Grid bounds location or rotation is invalid."));
	}
	const FVector ActorScale = GetActorScale3D();
	if (ActorScale.ContainsNaN()
		|| FMath::Abs(ActorScale.X) <= UE_SMALL_NUMBER
		|| FMath::Abs(ActorScale.Y) <= UE_SMALL_NUMBER
		|| FMath::Abs(ActorScale.Z) <= UE_SMALL_NUMBER)
	{
		return Fail(TEXT("Grid bounds scale components must be finite and non-zero."));
	}

	const UBrushComponent* LocalBrushComponent = GetBrushComponent();
	if (LocalBrushComponent == nullptr)
	{
		return Fail(TEXT("Grid bounds has no brush component."));
	}
#if WITH_EDITORONLY_DATA
	const UModel* BrushModel = LocalBrushComponent != nullptr ? LocalBrushComponent->Brush : nullptr;
	if (BrushModel == nullptr || BrushModel->Polys == nullptr || BrushModel->Polys->Element.Num() != 6)
	{
		return Fail(TEXT("Grid bounds must use a six-face box brush."));
	}
	for (const FPoly& Polygon : BrushModel->Polys->Element)
	{
		if (Polygon.Vertices.Num() != 4)
		{
			return Fail(TEXT("Grid bounds must use a box brush with four vertices per face."));
		}
	}
#else
	const UBodySetup* BodySetup = LocalBrushComponent->BrushBodySetup;
	if (BodySetup == nullptr
		|| BodySetup->AggGeom.ConvexElems.Num() != 1
		|| BodySetup->AggGeom.ConvexElems[0].VertexData.Num() != 8)
	{
		return Fail(TEXT("Grid bounds must use a box convex collision shape."));
	}
#endif
	if (!GetLocalGridBounds().IsValid)
	{
		return Fail(TEXT("Grid bounds brush has invalid bounds."));
	}
	return true;
}

void AGridNavigationBoundsVolume::EnsureStableGridId(bool bForceNewId)
{
	if (bForceNewId || !GridId.IsValid())
	{
		GridId = FGuid::NewGuid();
	}
}

void AGridNavigationBoundsVolume::PostLoad()
{
	Super::PostLoad();
	EnsureStableGridId();
}

void AGridNavigationBoundsVolume::PostActorCreated()
{
	Super::PostActorCreated();
	EnsureStableGridId();
}

void AGridNavigationBoundsVolume::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	EnsureStableGridId(DuplicateMode != EDuplicateMode::PIE);
}

#if WITH_EDITOR
void AGridNavigationBoundsVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	HorizontalCellSize.X = FMath::Max(1.0, HorizontalCellSize.X);
	HorizontalCellSize.Y = FMath::Max(1.0, HorizontalCellSize.Y);
	LayerHeight = FMath::Max(1.0, LayerHeight);
	ChunkSize = FMath::Max(1, ChunkSize);
	CellCenterTolerance = FMath::Max(0.1f, CellCenterTolerance);
	StopSpeedTolerance = FMath::Max(0.0f, StopSpeedTolerance);
	MaxSlopeDegrees = FMath::Clamp(MaxSlopeDegrees, 0.0, 89.0);
	MaxStepHeight = FMath::Max(0.0, MaxStepHeight);
	MaxDropHeight = FMath::Max(0.0, MaxDropHeight);
	AgentRadius = FMath::Max(1.0, AgentRadius);
	AgentHeight = FMath::Max(AgentRadius * 2.0, AgentHeight);
	EnsureStableGridId();
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberName = PropertyChangedEvent.MemberProperty != nullptr
		? PropertyChangedEvent.MemberProperty->GetFName()
		: FName();
	const bool bGenerationPropertyChanged =
		MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, HorizontalCellSize)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, LayerHeight)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, ChunkSize)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, MovementMode)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, bAllowCornerCutting)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, PathFollowingStyle)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, PathDriveMode)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, bUseAcceleratedFinalApproach)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, CellCenterTolerance)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, StopSpeedTolerance)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, MaxSlopeDegrees)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, MaxStepHeight)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, MaxDropHeight)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, CollisionProfileName)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, AgentRadius)
		|| MemberName == GET_MEMBER_NAME_CHECKED(AGridNavigationBoundsVolume, AgentHeight);
	if (bGenerationPropertyChanged)
	{
		if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			NavigationSystem->OnNavigationBoundsUpdated(this);
		}
	}
}
#endif
