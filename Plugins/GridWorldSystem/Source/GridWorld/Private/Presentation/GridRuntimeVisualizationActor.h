// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GridRuntimeVisualizationActor.generated.h"

class USceneComponent;

/** Private transient owner for chunked runtime presentation components. */
UCLASS(Transient, NotBlueprintable, NotPlaceable)
class AGridRuntimeVisualizationActor final : public AActor
{
	GENERATED_BODY()

public:
	AGridRuntimeVisualizationActor();

	USceneComponent* GetPresentationRoot() const { return PresentationRoot; }

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> PresentationRoot;
};

