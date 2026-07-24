// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/GridPresentationTypes.h"

void FGridCellMaterialDataLayout::Write(
	const FGridCellVisualState& State,
	const FLinearColor& ResolvedColor,
	float (&OutData)[NumFloats])
{
	OutData[InteractionState] = static_cast<float>(State.InteractionState);
	OutData[PathState] = static_cast<float>(State.PathState);
	OutData[NavigationFlags] = static_cast<float>(State.NavigationFlags);
	OutData[Emphasis] = State.Emphasis;
	OutData[ResolvedRed] = ResolvedColor.R;
	OutData[ResolvedGreen] = ResolvedColor.G;
	OutData[ResolvedBlue] = ResolvedColor.B;
	OutData[ResolvedAlpha] = ResolvedColor.A;
	OutData[PathProgress] = State.PathProgress;
	OutData[CustomStyleValue] = State.CustomStyleValue;
}

