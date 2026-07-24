// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/GridPathLineVisualizationActor.h"

#include "Components/SceneComponent.h"

AGridPathLineVisualizationActor::AGridPathLineVisualizationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);
	PresentationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PresentationRoot"));
	PresentationRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(PresentationRoot);
}
