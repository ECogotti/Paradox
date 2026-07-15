// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "GridWorldTypes.h"
#include "GridNavigationBoundsVolume.generated.h"

/** One independently addressed grid region. Different regions connect only through explicit Grid links. */
UCLASS(BlueprintType)
class GRIDWORLD_API AGridNavigationBoundsVolume : public ANavMeshBoundsVolume
{
	GENERATED_BODY()

public:
	AGridNavigationBoundsVolume(const FObjectInitializer& ObjectInitializer);

	/** Persistent region identity retained when the level and generated data are saved. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Grid World|Identity")
	FGuid GridId;

	/** Fixed logical X/Y cell dimensions in centimetres; Actor scale changes coverage only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Cells", meta = (ClampMin = "1.0", UIMin = "10.0"))
	FVector2D HorizontalCellSize = FVector2D(100.0, 100.0);

	/** Vertical quantization distance between stable layers, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Cells", meta = (ClampMin = "1.0", UIMin = "10.0"))
	double LayerHeight = 50.0;

	/** Number of cells per X/Y chunk edge used by incremental rebuilds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Cells", meta = (ClampMin = "1", UIMin = "4", UIMax = "64"))
	int32 ChunkSize = 16;

	/** Maximum ordinary-neighbour directions generated for this region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	EGridMovementMode MovementMode = EGridMovementMode::FourDirections;

	/** Allows a diagonal only when both adjacent orthogonal cells are available. The query filter must also allow it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement", meta = (EditCondition = "MovementMode == EGridMovementMode::EightDirections"))
	bool bAllowCornerCutting = false;

	/** Chooses whether standard path following may round cells or must honor their centers physically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement")
	EGridPathFollowingStyle PathFollowingStyle = EGridPathFollowingStyle::Standard;

	/** Chooses acceleration-based or direct-velocity steering for precise path-following styles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement", meta = (EditCondition = "PathFollowingStyle != EGridPathFollowingStyle::Standard"))
	EGridPathDriveMode PathDriveMode = EGridPathDriveMode::DirectVelocity;

	/** Uses acceleration and braking only on the final segment of a Direct Velocity path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement", meta = (EditCondition = "PathFollowingStyle != EGridPathFollowingStyle::Standard && PathDriveMode == EGridPathDriveMode::DirectVelocity"))
	bool bUseAcceleratedFinalApproach = false;

	/** Horizontal distance in centimeters used to accept a required cell center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement", meta = (ClampMin = "0.1", UIMin = "0.5", EditCondition = "PathFollowingStyle != EGridPathFollowingStyle::Standard"))
	float CellCenterTolerance = 2.0f;

	/** Maximum horizontal speed in cm/s required at the final cell center. Intermediate centers never require a stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "PathFollowingStyle != EGridPathFollowingStyle::Standard"))
	float StopSpeedTolerance = 5.0f;

	/** Maximum slope baked into GridWorld. Character Movement > Walkable Floor Angle must be at least this high to walk matching ramps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	double MaxSlopeDegrees = 45.0;

	/** Residual vertical rise allowed beyond the natural slope between neighbouring floor normals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement", meta = (ClampMin = "0.0"))
	double MaxStepHeight = 45.0;

	/** Residual vertical drop allowed beyond the natural slope between neighbouring floor normals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Movement", meta = (ClampMin = "0.0"))
	double MaxDropHeight = 45.0;

	/** Collision profile used by floor, clearance, and obstacle sampling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Collision")
	FName CollisionProfileName = FName(TEXT("Pawn"));

	/** Automatically rebuilds affected chunks after navigation-relevant geometry is added, removed, moved, or changed. Requires the Editor's global Update Navigation Automatically preference. Explicit builds and bounds edits always rebuild. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Collision")
	bool bAutoRebuildOnGeometryChanges = true;

	/** Supported agent horizontal capsule radius used for clearance sampling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Collision", meta = (ClampMin = "1.0"))
	double AgentRadius = 42.0;

	/** Supported agent full capsule height used for upright clearance sampling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Collision", meta = (ClampMin = "1.0"))
	double AgentHeight = 192.0;

	/** Enables local bounds diagnostics when global GridWorld debug and Show Navigation are active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bEnableDebug = false;

	/** @return Logical coordinate frame derived from Actor translation/yaw and authored cell dimensions. */
	FGridTransform GetGridTransform() const;
	/** @return Axis-aligned box in the Actor's unscaled local space. */
	FBox GetLocalGridBounds() const;
	/** @return World-space AABB enclosing the scaled and yaw-rotated volume. */
	FBox GetGridWorldBounds() const;
	/** Validates scale, rotation, box shape, dimensions, and identity. @param OutError Receives a designer-facing reason. */
	bool ValidateGridBounds(FString& OutError) const;
	/** Creates a persistent identity when missing. @param bForceNewId Replaces an existing ID after duplication conflicts. */
	void EnsureStableGridId(bool bForceNewId = false);

	virtual void PostLoad() override;
	virtual void PostActorCreated() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
