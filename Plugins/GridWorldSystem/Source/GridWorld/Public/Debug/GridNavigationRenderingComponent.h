// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debug/DebugDrawComponent.h"
#include "GridNavigationRenderingComponent.generated.h"

/** Batched debug renderer used by Grid Navigation Data. */
UCLASS(Transient, ClassGroup = Debug)
class GRIDWORLD_API UGridNavigationRenderingComponent final : public UDebugDrawComponent
{
	GENERATED_BODY()

public:
	UGridNavigationRenderingComponent();

protected:
#if UE_ENABLE_DEBUG_DRAWING
	/** Captures immutable snapshot/debug copies into one batched render-thread scene proxy. */
	virtual FDebugRenderSceneProxy* CreateDebugSceneProxy() override;
#endif
	/** @return Bounds of the authoritative Grid nav data so Unreal can cull debug drawing. */
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
};
