// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Interfaces/GridNavigationContributor.h"
#include "GridNavigationLinkComponent.generated.h"

/** Authored special transition between two projected GridWorld cells. */
UCLASS(ClassGroup = (GridWorld), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class GRIDWORLD_API UGridNavigationLinkComponent : public USceneComponent, public IGridNavigationContributor
{
	GENERATED_BODY()

public:
	UGridNavigationLinkComponent();

	/** Stable link identity preserved across topology rebuilds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Identity")
	FGuid LinkId;

	/** Component-local start point projected to a source cell during generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Link")
	FVector StartOffset = FVector::ZeroVector;

	/** Component-local end point projected to a destination cell during generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Link")
	FVector EndOffset = FVector(100.0, 0.0, 0.0);

	/** Controls whether the generated link is available to queries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Link")
	bool bEnabled = true;

	/** Adds a reverse transition from end to start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Link")
	bool bBidirectional = true;

	/** Fixed-point link cost; an ordinary orthogonal cell step costs 1000. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Link", meta = (ClampMin = "1"))
	int32 TraversalCost = 1000;

	/** Bit mask of traversal channels allowed to use this link. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Link", meta = (Bitmask))
	int32 TraversalChannels = MAX_uint16;

	/** Enables local link diagnostics when global GridWorld debug is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Debug")
	bool bEnableDebug = false;

	/** @return StartOffset transformed into world space. */
	FVector GetStartWorldLocation() const;
	/** @return EndOffset transformed into world space. */
	FVector GetEndWorldLocation() const;
	/** Enables or disables the link and republishes the overlay. @param bInEnabled New enabled state. */
	UFUNCTION(BlueprintCallable, Category = "Grid World")
	void SetLinkEnabled(bool bInEnabled);
	UFUNCTION(BlueprintCallable, Category = "Grid World")
	void RefreshLink();
	virtual FBox GetGridContributionBounds_Implementation() const override;
	virtual bool IsGridContributionEnabled_Implementation() const override;
	virtual void PostLoad() override;
	virtual void OnComponentCreated() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** Binding removed during component unregistration. */
	FDelegateHandle TransformUpdatedHandle;
	/** Maintains deterministic link identity. @param bForceNewId Generates a replacement even when already valid. */
	void EnsureStableId(bool bForceNewId = false);
	/** Responds to transform changes without a Tick. */
	void HandleTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);
	/** Requests regeneration of the affected navigation contribution. */
	void NotifyNavigationData() const;
};
	/** Rebuilds the link contribution after programmatic property changes. */
