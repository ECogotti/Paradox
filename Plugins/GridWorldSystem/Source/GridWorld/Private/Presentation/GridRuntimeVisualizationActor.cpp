// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/GridRuntimeVisualizationActor.h"

#include "Components/SceneComponent.h"

AGridRuntimeVisualizationActor::AGridRuntimeVisualizationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);
	PresentationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PresentationRoot"));
	PresentationRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(PresentationRoot);
}

