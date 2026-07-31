#include "Perception/ParadoxSemanticStateCube.h"

#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Paradox.h"
#include "UObject/ConstructorHelpers.h"

AParadoxSemanticStateCube::AParadoxSemanticStateCube()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	PerceptionSource =
		CreateDefaultSubobject<UPerceptionKnowledgeSourceComponent>(
			TEXT("PerceptionKnowledgeSource"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, BasicMaterial.Object);
	}
	StateTag = ParadoxGameplayTags::State_Computer_Powered.GetTag();
}

void AParadoxSemanticStateCube::BeginPlay()
{
	Super::BeginPlay();
	const FPerceptionKnowledgeOperationResult Result =
		PerceptionSource->SetObservableState(
			StateTag,
			FPerceptionKnowledgeValue::MakeBool(bPowered));
	if (!Result.IsSuccess()
		&& Result.Status != EPerceptionKnowledgeOperationStatus::Unchanged)
	{
		PARADOX_LOG_ERROR(
			TEXT("Semantic state cube '%s' could not publish initial state '%s': %s"),
			*GetNameSafe(this),
			*StateTag.ToString(),
			*Result.Message);
	}
	UpdateVisualFeedback();
}

void AParadoxSemanticStateCube::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateVisualFeedback();
}

FPerceptionKnowledgeOperationResult AParadoxSemanticStateCube::SetPowered(
	const bool bInPowered)
{
	const bool bPreviousPowered = bPowered;
	bPowered = bInPowered;
	UpdateVisualFeedback();
	const FPerceptionKnowledgeOperationResult Result = PerceptionSource
		? PerceptionSource->SetObservableState(
			StateTag,
			FPerceptionKnowledgeValue::MakeBool(bPowered))
		: FPerceptionKnowledgeOperationResult();
	if (bPreviousPowered != bPowered)
	{
		PARADOX_LOG_INFO(
			TEXT("Semantic state cube '%s' changed '%s' from %s to %s; perception result=%s (%s)."),
			*GetNameSafe(this),
			*StateTag.ToString(),
			bPreviousPowered ? TEXT("true") : TEXT("false"),
			bPowered ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(Result.Status),
			*Result.Message);
	}
	return Result;
}

void AParadoxSemanticStateCube::UpdateVisualFeedback()
{
	if (!Mesh)
	{
		return;
	}
	UMaterialInterface* DesiredMaterial =
		bPowered ? PoweredMaterial.Get() : UnpoweredMaterial.Get();
	if (DesiredMaterial)
	{
		Mesh->SetMaterial(0, DesiredMaterial);
	}
	Mesh->SetVectorParameterValueOnMaterials(
		TEXT("Color"),
		FVector(
			bPowered ? PoweredColor.R : UnpoweredColor.R,
			bPowered ? PoweredColor.G : UnpoweredColor.G,
			bPowered ? PoweredColor.B : UnpoweredColor.B));
}
