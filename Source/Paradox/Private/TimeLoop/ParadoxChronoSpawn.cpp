#include "TimeLoop/ParadoxChronoSpawn.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ParadoxChronoSpawn"

AParadoxChronoSpawn::AParadoxChronoSpawn()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SelectionMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectionMesh"));
	SelectionMesh->SetupAttachment(SceneRoot);
	SelectionMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SelectionMesh->SetCollisionObjectType(ECC_WorldDynamic);
	SelectionMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	SelectionMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SelectionMesh->SetGenerateOverlapEvents(false);
	SelectionMesh->SetCastShadow(false);
	SelectionMesh->SetRelativeScale3D(FVector(1.25f, 1.25f, 0.08f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMeshFinder.Succeeded())
	{
		SelectionMesh->SetStaticMesh(CylinderMeshFinder.Object);
	}

	StateLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StateLabel"));
	StateLabel->SetupAttachment(SceneRoot);
	StateLabel->SetHorizontalAlignment(EHTA_Center);
	StateLabel->SetVerticalAlignment(EVRTA_TextCenter);
	StateLabel->SetWorldSize(28.0f);
	StateLabel->SetRelativeLocation(FVector(0.0f, 0.0f, 28.0f));
	StateLabel->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));

	SelectableComponent = CreateDefaultSubobject<UParadoxSelectableComponent>(TEXT("SelectableComponent"));
}

void AParadoxChronoSpawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!bChronoSpawnEnabled)
	{
		ChronoSpawnState = EParadoxChronoSpawnState::Disabled;
	}
	else if (ChronoSpawnState == EParadoxChronoSpawnState::Disabled)
	{
		ChronoSpawnState = EParadoxChronoSpawnState::Available;
	}
	ReceiveVisualStateChanged(ChronoSpawnState);
}

void AParadoxChronoSpawn::BeginPlay()
{
	Super::BeginPlay();
	SetRuntimeState(
		bChronoSpawnEnabled
			? EParadoxChronoSpawnState::Available
			: EParadoxChronoSpawnState::Disabled);
}

#if WITH_EDITOR
EDataValidationResult AParadoxChronoSpawn::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	auto AddError = [&Context, &Result](const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	};

	if (!SceneRoot || GetRootComponent() != SceneRoot)
	{
		AddError(LOCTEXT("MissingSceneRoot", "Chrono Spawn requires its native Scene Root."));
	}
	if (!SelectionMesh || SelectionMesh->GetAttachParent() != SceneRoot)
	{
		AddError(LOCTEXT("MissingSelectionMesh", "Chrono Spawn requires Selection Mesh attached to Scene Root."));
	}
	if (!StateLabel || StateLabel->GetAttachParent() != SceneRoot)
	{
		AddError(LOCTEXT("MissingStateLabel", "Chrono Spawn requires State Label attached to Scene Root."));
	}
	if (!SelectableComponent)
	{
		AddError(LOCTEXT("MissingSelectableComponent", "Chrono Spawn requires its native Selectable Component."));
	}
	return Result;
}
#endif

bool AParadoxChronoSpawn::IsAvailableForSelection() const
{
	return bChronoSpawnEnabled
		&& (ChronoSpawnState == EParadoxChronoSpawnState::Available
			|| ChronoSpawnState == EParadoxChronoSpawnState::Hovered);
}

void AParadoxChronoSpawn::SetChronoSpawnEnabled(const bool bEnabled)
{
	if (bChronoSpawnEnabled == bEnabled)
	{
		return;
	}

	bChronoSpawnEnabled = bEnabled;
	SetRuntimeState(
		bChronoSpawnEnabled
			? EParadoxChronoSpawnState::Available
			: EParadoxChronoSpawnState::Disabled);
}

void AParadoxChronoSpawn::SetRuntimeState(const EParadoxChronoSpawnState NewState)
{
	if (ChronoSpawnState == NewState)
	{
		return;
	}

	ChronoSpawnState = NewState;
	ReceiveVisualStateChanged(ChronoSpawnState);
}

void AParadoxChronoSpawn::ReceiveVisualStateChanged_Implementation(
	const EParadoxChronoSpawnState NewState)
{
	if (!SelectionMesh || !StateLabel)
	{
		return;
	}

	FColor LabelColor = FColor::White;
	FText Label = FText::FromString(TEXT("AVAILABLE"));
	FVector Scale(1.25f, 1.25f, 0.08f);
	bool bVisible = true;

	switch (NewState)
	{
	case EParadoxChronoSpawnState::Hovered:
		LabelColor = FColor::Cyan;
		Label = FText::FromString(TEXT("HOVER"));
		Scale = FVector(1.4f, 1.4f, 0.1f);
		break;
	case EParadoxChronoSpawnState::Selected:
		LabelColor = FColor::Yellow;
		Label = FText::FromString(TEXT("SELECTED"));
		Scale = FVector(1.5f, 1.5f, 0.12f);
		break;
	case EParadoxChronoSpawnState::Occupied:
		LabelColor = FColor::Red;
		Label = FText::FromString(TEXT("OCCUPIED"));
		break;
	case EParadoxChronoSpawnState::Disabled:
		LabelColor = FColor(96, 96, 96);
		Label = FText::FromString(TEXT("DISABLED"));
		bVisible = false;
		break;
	case EParadoxChronoSpawnState::Available:
	default:
		LabelColor = FColor::Green;
		break;
	}

	SelectionMesh->SetRelativeScale3D(Scale);
	SelectionMesh->SetVisibility(bVisible, true);
	SelectionMesh->SetCollisionEnabled(
		bVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	StateLabel->SetText(Label);
	StateLabel->SetTextRenderColor(LabelColor);
	StateLabel->SetVisibility(true);
}

#undef LOCTEXT_NAMESPACE
