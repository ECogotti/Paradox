#include "Interaction/ParadoxSelectableComponent.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/ParadoxInteractionWidgetBase.h"
#include "Interaction/ParadoxSelectionComponent.h"
#include "Misc/DataValidation.h"
#include "Paradox.h"

#if WITH_EDITOR
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"

namespace UE::Paradox::Selection::Private
{
	template <typename TComponent>
	void GatherDirectComponentValidationCandidates(
		const AActor& OwnerActor,
		TArray<const TComponent*>& OutComponents)
	{
		OutComponents.Reset();

		TArray<TComponent*> InstancedComponents;
		OwnerActor.GetComponents<TComponent>(InstancedComponents);
		for (const TComponent* Component : InstancedComponents)
		{
			if (IsValid(Component) && Component->GetOwner() == &OwnerActor)
			{
				OutComponents.AddUnique(Component);
			}
		}

		// Blueprint CDOs do not instantiate SCS components. Inspect their templates so Blueprint
		// compilation validates the same authored components that the runtime Actor will create.
		// Placed/runtime Actors already own the instantiated SCS components; adding the templates
		// there would count every authored component twice.
		if (OwnerActor.HasAnyFlags(RF_ClassDefaultObject))
		{
			for (const UClass* Class = OwnerActor.GetClass(); Class; Class = Class->GetSuperClass())
			{
				const UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(Class);
				if (!BlueprintClass)
				{
					continue;
				}

				if (const USimpleConstructionScript* ConstructionScript =
					BlueprintClass->SimpleConstructionScript)
				{
					for (const USCS_Node* Node : ConstructionScript->GetAllNodes())
					{
						if (const TComponent* Component = Node
							? Cast<TComponent>(Node->ComponentTemplate.Get())
							: nullptr)
						{
							OutComponents.AddUnique(Component);
						}
					}
				}

				for (const UActorComponent* Template : BlueprintClass->ComponentTemplates)
				{
					if (const TComponent* Component = Cast<TComponent>(Template))
					{
						OutComponents.AddUnique(Component);
					}
				}
			}
		}

		OutComponents.Sort([](const TComponent& A, const TComponent& B)
		{
			return A.GetPathName() < B.GetPathName();
		});
	}
}
#endif

UParadoxSelectableComponent::UParadoxSelectableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

EParadoxSelectionPresentationState UParadoxSelectableComponent::GetSelectionPresentationState() const
{
	if (bIsSelected)
	{
		return EParadoxSelectionPresentationState::Selected;
	}
	return bIsHovered
		? EParadoxSelectionPresentationState::Hovered
		: EParadoxSelectionPresentationState::None;
}

#if WITH_EDITOR
EDataValidationResult UParadoxSelectableComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!bShowPuzzleConnectionsWhenSelected)
	{
		return Result;
	}
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return Result;
	}
	TArray<const UBoxComponent*> WireTargets;
	UE::Paradox::Selection::Private::GatherDirectComponentValidationCandidates(
		*OwnerActor,
		WireTargets);
	WireTargets.RemoveAll([](const UBoxComponent* Box)
	{
		return !IsValid(Box) || !Box->ComponentHasTag(TEXT("WireTarget"));
	});
	if (WireTargets.Num() > 1)
	{
		Context.AddWarning(NSLOCTEXT(
			"ParadoxSelectable",
			"MultipleWireTargets",
			"Multiple UBoxComponent instances are tagged WireTarget. Runtime uses the first component in stable component-path order; keep a single tag to avoid ambiguous authoring."));
	}
	if (!WireTargets.IsEmpty())
	{
		const FVector Extent = WireTargets[0]->GetUnscaledBoxExtent()
			* WireTargets[0]->GetComponentScale().GetAbs();
		if (Extent.ContainsNaN()
			|| Extent.X <= KINDA_SMALL_NUMBER
			|| Extent.Y <= KINDA_SMALL_NUMBER
			|| Extent.Z <= KINDA_SMALL_NUMBER)
		{
			Context.AddError(NSLOCTEXT(
				"ParadoxSelectable",
				"InvalidWireTargetExtent",
				"The WireTarget box must have finite, positive extent on every axis. Invalid tagged boxes are ignored at runtime."));
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			return Result;
		}
	}

	bool bHasVisualBounds = false;
	TArray<const UStaticMeshComponent*> StaticMeshes;
	UE::Paradox::Selection::Private::GatherDirectComponentValidationCandidates(
		*OwnerActor,
		StaticMeshes);
	for (const UStaticMeshComponent* Mesh : StaticMeshes)
	{
		FVector LocalMin = FVector::ZeroVector;
		FVector LocalMax = FVector::ZeroVector;
		if (IsValid(Mesh))
		{
			Mesh->GetLocalBounds(LocalMin, LocalMax);
		}
		if (IsValid(Mesh) && Mesh->GetStaticMesh()
			&& Mesh->IsVisible() && !Mesh->bHiddenInGame
			&& (LocalMax.X - LocalMin.X) > KINDA_SMALL_NUMBER
			&& (LocalMax.Y - LocalMin.Y) > KINDA_SMALL_NUMBER)
		{
			bHasVisualBounds = true;
			break;
		}
	}
	if (!bHasVisualBounds)
	{
		TArray<const USkeletalMeshComponent*> SkeletalMeshes;
		UE::Paradox::Selection::Private::GatherDirectComponentValidationCandidates(
			*OwnerActor,
			SkeletalMeshes);
		for (const USkeletalMeshComponent* Mesh : SkeletalMeshes)
		{
			if (IsValid(Mesh) && Mesh->GetSkeletalMeshAsset()
				&& Mesh->IsVisible() && !Mesh->bHiddenInGame
				&& Mesh->GetLocalBounds().BoxExtent.X > KINDA_SMALL_NUMBER
				&& Mesh->GetLocalBounds().BoxExtent.Y > KINDA_SMALL_NUMBER)
			{
				bHasVisualBounds = true;
				break;
			}
		}
	}
	if (!bHasVisualBounds)
	{
		Context.AddWarning(NSLOCTEXT(
			"ParadoxSelectable",
			"MissingPuzzleWireBounds",
			"Puzzle connections are enabled but the Actor has neither one valid WireTarget box nor usable direct Static/Skeletal Mesh bounds. Runtime rendering will use Actor Location as a point fallback."));
	}
	return Result;
}
#endif

void UParadoxSelectableComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateInteractionWidgetFacing();
}

void UParadoxSelectableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UParadoxSelectionComponent* Selection = ActiveSelectionComponent.Get())
	{
		Selection->HandleSelectableEndingPlay(this);
	}
	ResetPresentationState();
	DestroyInteractionWidget();
	Super::EndPlay(EndPlayReason);
}

void UParadoxSelectableComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	if (UParadoxSelectionComponent* Selection = ActiveSelectionComponent.Get())
	{
		Selection->HandleSelectableEndingPlay(this);
	}
	ResetPresentationState();
	DestroyInteractionWidget();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UParadoxSelectableComponent::HandleHoverChanged_Implementation(const bool bNewHovered)
{
}

void UParadoxSelectableComponent::HandleSelectionChanged_Implementation(const bool bNewSelected)
{
}

void UParadoxSelectableComponent::SetHoveredFromSelection(const bool bNewHovered)
{
	const bool bEffectiveHovered = bCanBeHovered && bNewHovered;
	if (bIsHovered == bEffectiveHovered)
	{
		return;
	}

	bIsHovered = bEffectiveHovered;
	ApplyPresentationState();
	OnHoverChanged.Broadcast(this, bIsHovered);
	HandleHoverChanged(bIsHovered);
	if (bEnableDebug)
	{
		PARADOX_LOG_INFO(
			TEXT("Selectable '%s' hover changed to %s."),
			*GetNameSafe(GetOwner()),
			bIsHovered ? TEXT("true") : TEXT("false"));
	}
}

void UParadoxSelectableComponent::SetSelectedFromSelection(
	const bool bNewSelected,
	UParadoxSelectionComponent* InSelectionComponent)
{
	const bool bEffectiveSelected = bCanBeSelected && bNewSelected;
	if (bIsSelected == bEffectiveSelected)
	{
		return;
	}

	bIsSelected = bEffectiveSelected;
	ActiveSelectionComponent = bIsSelected ? InSelectionComponent : nullptr;
	ApplyPresentationState();
	if (bIsSelected)
	{
		ShowInteractionWidget(InSelectionComponent);
	}
	else
	{
		HideInteractionWidget();
	}
	OnSelectionChanged.Broadcast(this, bIsSelected);
	HandleSelectionChanged(bIsSelected);
	if (bEnableDebug)
	{
		PARADOX_LOG_INFO(
			TEXT("Selectable '%s' selection changed to %s."),
			*GetNameSafe(GetOwner()),
			bIsSelected ? TEXT("true") : TEXT("false"));
	}
}

void UParadoxSelectableComponent::ResetPresentationState()
{
	const bool bWasHovered = bIsHovered;
	const bool bWasSelected = bIsSelected;
	bIsHovered = false;
	bIsSelected = false;
	ActiveSelectionComponent.Reset();
	RestoreOutline();
	HideInteractionWidget();

	if (bWasHovered)
	{
		OnHoverChanged.Broadcast(this, false);
		HandleHoverChanged(false);
	}
	if (bWasSelected)
	{
		OnSelectionChanged.Broadcast(this, false);
		HandleSelectionChanged(false);
	}
}

void UParadoxSelectableComponent::ApplyPresentationState()
{
	switch (GetSelectionPresentationState())
	{
	case EParadoxSelectionPresentationState::Selected:
		ApplyOutline(UE::Paradox::Selection::SelectedStencilValue);
		break;
	case EParadoxSelectionPresentationState::Hovered:
		ApplyOutline(UE::Paradox::Selection::HoverStencilValue);
		break;
	case EParadoxSelectionPresentationState::None:
	default:
		RestoreOutline();
		break;
	}
}

void UParadoxSelectableComponent::ApplyOutline(const int32 StencilValue)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return;
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	Owner->GetComponents(PrimitiveComponents, false);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!IsValid(PrimitiveComponent)
			|| PrimitiveComponent->GetOwner() != Owner
			|| (!PrimitiveComponent->IsA<UStaticMeshComponent>()
				&& !PrimitiveComponent->IsA<USkeletalMeshComponent>()))
		{
			continue;
		}

		const TWeakObjectPtr<UPrimitiveComponent> ComponentKey(PrimitiveComponent);
		if (!CachedPrimitiveRenderStates.Contains(ComponentKey))
		{
			FCachedPrimitiveRenderState& CachedState = CachedPrimitiveRenderStates.Add(ComponentKey);
			CachedState.bRenderCustomDepth = PrimitiveComponent->bRenderCustomDepth;
			CachedState.CustomDepthStencilValue = PrimitiveComponent->CustomDepthStencilValue;
			CachedState.CustomDepthStencilWriteMask = PrimitiveComponent->CustomDepthStencilWriteMask;
		}

		PrimitiveComponent->SetRenderCustomDepth(true);
		PrimitiveComponent->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
		PrimitiveComponent->SetCustomDepthStencilValue(StencilValue);
	}
}

void UParadoxSelectableComponent::RestoreOutline()
{
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FCachedPrimitiveRenderState>& Pair
		: CachedPrimitiveRenderStates)
	{
		if (UPrimitiveComponent* PrimitiveComponent = Pair.Key.Get())
		{
			PrimitiveComponent->SetCustomDepthStencilWriteMask(Pair.Value.CustomDepthStencilWriteMask);
			PrimitiveComponent->SetCustomDepthStencilValue(Pair.Value.CustomDepthStencilValue);
			PrimitiveComponent->SetRenderCustomDepth(Pair.Value.bRenderCustomDepth);
		}
	}
	CachedPrimitiveRenderStates.Reset();
}

bool UParadoxSelectableComponent::EnsureInteractionWidget(
	UParadoxSelectionComponent* InSelectionComponent)
{
	if (IsValid(InteractionWidgetComponent))
	{
		return true;
	}
	if (!SelectionWidgetClass || !IsValid(GetOwner()) || !GetWorld())
	{
		return false;
	}

	AActor* Owner = GetOwner();
	USceneComponent* Anchor = Cast<USceneComponent>(WidgetAnchor.GetComponent(Owner));
	if (IsValid(Anchor) && Anchor->GetOwner() != Owner)
	{
		PARADOX_LOG_WARNING(
			TEXT("Selectable '%s' ignored widget anchor '%s' because it belongs to Actor '%s'; widget anchors must belong to the selected Actor."),
			*GetNameSafe(Owner),
			*GetNameSafe(Anchor),
			*GetNameSafe(Anchor->GetOwner()));
		Anchor = nullptr;
	}
	if (!IsValid(Anchor))
	{
		Anchor = Owner->GetRootComponent();
	}
	if (!IsValid(Anchor))
	{
		PARADOX_LOG_WARNING(
			TEXT("Selectable '%s' cannot create its interaction widget because no Scene Component anchor is available."),
			*GetNameSafe(Owner));
		return false;
	}

	const FName ComponentName = MakeUniqueObjectName(
		Owner,
		UWidgetComponent::StaticClass(),
		TEXT("ParadoxInteractionWidget"));
	InteractionWidgetComponent = NewObject<UWidgetComponent>(Owner, ComponentName);
	Owner->AddInstanceComponent(InteractionWidgetComponent);
	InteractionWidgetComponent->SetupAttachment(Anchor);
	InteractionWidgetComponent->SetRelativeLocation(WidgetRelativeOffset);
	InteractionWidgetComponent->SetRelativeRotation(WidgetRelativeRotation);
	InteractionWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	InteractionWidgetComponent->SetDrawSize(FVector2D(WidgetDrawSize));
	InteractionWidgetComponent->SetTwoSided(true);
	InteractionWidgetComponent->SetCollisionProfileName(TEXT("UI"));
	InteractionWidgetComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionWidgetComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionWidgetComponent->SetGenerateOverlapEvents(false);
	InteractionWidgetComponent->SetCanEverAffectNavigation(false);
	InteractionWidgetComponent->SetWidgetClass(SelectionWidgetClass);
	InteractionWidgetComponent->SetVisibility(false, true);
	if (const APlayerController* PlayerController = InSelectionComponent
		? Cast<APlayerController>(InSelectionComponent->GetOwner())
		: nullptr)
	{
		InteractionWidgetComponent->SetOwnerPlayer(PlayerController->GetLocalPlayer());
	}
	InteractionWidgetComponent->RegisterComponent();
	InteractionWidgetComponent->InitWidget();

	if (!Cast<UParadoxInteractionWidgetBase>(InteractionWidgetComponent->GetUserWidgetObject()))
	{
		PARADOX_LOG_ERROR(
			TEXT("Selectable '%s' failed to create a UParadoxInteractionWidgetBase from widget class '%s'."),
			*GetNameSafe(Owner),
			*GetNameSafe(SelectionWidgetClass.Get()));
		DestroyInteractionWidget();
		return false;
	}
	return true;
}

void UParadoxSelectableComponent::ShowInteractionWidget(
	UParadoxSelectionComponent* InSelectionComponent)
{
	if (!EnsureInteractionWidget(InSelectionComponent))
	{
		return;
	}

	APlayerController* PlayerController = InSelectionComponent
		? Cast<APlayerController>(InSelectionComponent->GetOwner())
		: nullptr;
	if (UParadoxInteractionWidgetBase* Widget = Cast<UParadoxInteractionWidgetBase>(
		InteractionWidgetComponent->GetUserWidgetObject()))
	{
		Widget->AssignSelectionContext(GetOwner(), this, InSelectionComponent, PlayerController);
		InteractionWidgetComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		InteractionWidgetComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		InteractionWidgetComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		InteractionWidgetComponent->SetVisibility(true, true);
		UpdateInteractionWidgetFacing();
		SetComponentTickEnabled(bFaceOwningPlayerCamera);
	}
}

void UParadoxSelectableComponent::HideInteractionWidget()
{
	SetComponentTickEnabled(false);
	if (!IsValid(InteractionWidgetComponent))
	{
		return;
	}
	if (UParadoxInteractionWidgetBase* Widget = Cast<UParadoxInteractionWidgetBase>(
		InteractionWidgetComponent->GetUserWidgetObject()))
	{
		Widget->ClearSelectionContext();
	}
	InteractionWidgetComponent->SetVisibility(false, true);
	InteractionWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UParadoxSelectableComponent::DestroyInteractionWidget()
{
	if (!IsValid(InteractionWidgetComponent))
	{
		InteractionWidgetComponent = nullptr;
		return;
	}
	HideInteractionWidget();
	InteractionWidgetComponent->DestroyComponent();
	InteractionWidgetComponent = nullptr;
}

void UParadoxSelectableComponent::UpdateInteractionWidgetFacing()
{
	if (!bFaceOwningPlayerCamera
		|| !bIsSelected
		|| !IsValid(InteractionWidgetComponent)
		|| !InteractionWidgetComponent->IsVisible())
	{
		return;
	}

	const UParadoxSelectionComponent* SelectionComponent = ActiveSelectionComponent.Get();
	const APlayerController* PlayerController = SelectionComponent
		? Cast<APlayerController>(SelectionComponent->GetOwner())
		: nullptr;
	if (!IsValid(PlayerController))
	{
		return;
	}

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	InteractionWidgetComponent->SetWorldRotation((-CameraRotation.Vector()).Rotation());
}
