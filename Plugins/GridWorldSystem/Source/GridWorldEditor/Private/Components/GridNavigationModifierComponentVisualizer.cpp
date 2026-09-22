// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/GridNavigationModifierComponentVisualizer.h"

#include "Components/GridNavigationModifierComponent.h"
#include "PrimitiveDrawingUtils.h"

void FGridNavigationModifierComponentVisualizer::DrawVisualization(
	const UActorComponent* Component,
	const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	const UGridNavigationModifierComponent* Modifier = Cast<UGridNavigationModifierComponent>(Component);
	if (!Modifier || !PDI)
	{
		return;
	}

	const FVector Extent = Modifier->BoxExtent.GetAbs();
	const FLinearColor Color = Modifier->bBlockCells
		? FLinearColor(1.0f, 0.15f, 0.05f)
		: FLinearColor(0.1f, 0.8f, 0.35f);
	DrawWireBox(
		PDI,
		Modifier->GetComponentTransform().ToMatrixWithScale(),
		FBox(-Extent, Extent),
		Color,
		SDPG_Foreground,
		2.0f);
}
