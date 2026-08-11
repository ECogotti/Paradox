#pragma once

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Controllers/ParadoxPlayerController.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/ParadoxInteractionWidgetBase.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Interaction/ParadoxSelectionComponent.h"
#include "SmartObjectComponent.h"
#include "ParadoxSelectionTestTypes.generated.h"

UCLASS()
class AParadoxSelectionTestActor : public AActor
{
	GENERATED_BODY()

public:
	AParadoxSelectionTestActor()
	{
		Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
		SetRootComponent(Root);

		StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
		StaticMesh->SetupAttachment(Root);
		StaticMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		StaticMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		StaticMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

		SkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMesh"));
		SkeletalMesh->SetupAttachment(Root);
		SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		NonMeshPrimitive = CreateDefaultSubobject<UBoxComponent>(TEXT("NonMeshPrimitive"));
		NonMeshPrimitive->SetupAttachment(Root);
		NonMeshPrimitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		Selectable = CreateDefaultSubobject<UParadoxSelectableComponent>(TEXT("Selectable"));
		SmartObject = CreateDefaultSubobject<USmartObjectComponent>(TEXT("SmartObject"));
		SmartObject->SetupAttachment(Root);
		Interaction = CreateDefaultSubobject<UParadoxInteractionComponent>(TEXT("Interaction"));
	}

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> StaticMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> SkeletalMesh;

	UPROPERTY()
	TObjectPtr<UBoxComponent> NonMeshPrimitive;

	UPROPERTY()
	TObjectPtr<UParadoxSelectableComponent> Selectable;

	UPROPERTY()
	TObjectPtr<USmartObjectComponent> SmartObject;

	UPROPERTY()
	TObjectPtr<UParadoxInteractionComponent> Interaction;
};

UCLASS()
class UParadoxSelectionTestWidget : public UParadoxInteractionWidgetBase
{
	GENERATED_BODY()
};

UCLASS()
class AParadoxSelectionTestController : public APlayerController
{
	GENERATED_BODY()

public:
	AParadoxSelectionTestController()
	{
		Selection = CreateDefaultSubobject<UParadoxSelectionComponent>(TEXT("Selection"));
	}

	UPROPERTY()
	TObjectPtr<UParadoxSelectionComponent> Selection;
};

/** Concrete native controller used to exercise ParadoxPlayerController default subobjects in runtime tests. */
UCLASS()
class AParadoxPuzzleOverlayTestController : public AParadoxPlayerController
{
	GENERATED_BODY()
};
