// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridPathLineVisualizationActor.generated.h"

class USceneComponent;

/** Private transient owner for the optional runtime path-line components. */
UCLASS(Transient, NotBlueprintable, NotPlaceable)
class AGridPathLineVisualizationActor final : public AActor
{
	GENERATED_BODY()

public:
	AGridPathLineVisualizationActor();

	USceneComponent* GetPresentationRoot() const { return PresentationRoot; }

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> PresentationRoot;
};
