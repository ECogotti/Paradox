#include "Components/PerceptionKnowledgeHearingRangeRendererComponent.h"

#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "PerceptionKnowledgeModule.h"

UPerceptionKnowledgeHearingRangeRendererComponent::
	UPerceptionKnowledgeHearingRangeRendererComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bCastStaticShadow = false;
	SetVisibility(false);
	SetHiddenInGame(true);
}

void UPerceptionKnowledgeHearingRangeRendererComponent::OnRegister()
{
	Super::OnRegister();
	SetAbsolute(false, false, true);

	if (AController* Controller = Cast<AController>(GetOwner()))
	{
		Controller->OnPossessedPawnChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandlePossessedPawnChanged);
	}
	BindListener(
		Listener
			? Listener.Get()
			: GetOwner()
				? GetOwner()->FindComponentByClass<
					UPerceptionKnowledgeListenerComponent>()
				: nullptr);
	GlobalDebugConfigurationHandle =
		GetPerceptionKnowledgeDebugConfigurationChanged().AddUObject(
			this,
			&ThisClass::HandleGlobalDebugConfigurationChanged);
	RefreshRenderer();
}

void UPerceptionKnowledgeHearingRangeRendererComponent::OnUnregister()
{
	if (AController* Controller = Cast<AController>(GetOwner()))
	{
		Controller->OnPossessedPawnChanged.RemoveDynamic(
			this,
			&ThisClass::HandlePossessedPawnChanged);
	}
	if (GlobalDebugConfigurationHandle.IsValid())
	{
		GetPerceptionKnowledgeDebugConfigurationChanged().Remove(
			GlobalDebugConfigurationHandle);
		GlobalDebugConfigurationHandle.Reset();
	}
	UnbindListener();
	DestroyBodyRenderComponent();
	Super::OnUnregister();
}

void UPerceptionKnowledgeHearingRangeRendererComponent::SetListener(
	UPerceptionKnowledgeListenerComponent* InListener)
{
	BindListener(InListener);
	RefreshRenderer();
}

void UPerceptionKnowledgeHearingRangeRendererComponent::SetGameplayVisible(
	const bool bInVisible)
{
	bVisibleInGameplay = bInVisible;
	RefreshRenderer();
}

void UPerceptionKnowledgeHearingRangeRendererComponent::SetLocalDebugEnabled(
	const bool bInEnabled)
{
	bEnableDebug = bInEnabled;
	RefreshRenderer();
}

void UPerceptionKnowledgeHearingRangeRendererComponent::BindListener(
	UPerceptionKnowledgeListenerComponent* InListener)
{
	if (Listener == InListener)
	{
		return;
	}
	UnbindListener();
	Listener = InListener;
	if (Listener)
	{
		ListenerConfigurationHandle =
			Listener->OnListenerConfigurationChangedNative().AddUObject(
				this,
				&ThisClass::HandleListenerConfigurationChanged);
	}
}

void UPerceptionKnowledgeHearingRangeRendererComponent::UnbindListener()
{
	if (Listener && ListenerConfigurationHandle.IsValid())
	{
		Listener->OnListenerConfigurationChangedNative().Remove(
			ListenerConfigurationHandle);
	}
	ListenerConfigurationHandle.Reset();
	Listener = nullptr;
}

void UPerceptionKnowledgeHearingRangeRendererComponent::
	HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	RefreshRenderer();
}

void UPerceptionKnowledgeHearingRangeRendererComponent::
	HandleListenerConfigurationChanged()
{
	RefreshRenderer();
}

void UPerceptionKnowledgeHearingRangeRendererComponent::
	HandleGlobalDebugConfigurationChanged()
{
	RefreshRenderer();
}

UStaticMeshComponent*
UPerceptionKnowledgeHearingRangeRendererComponent::GetActiveRenderComponent() const
{
	if (IsValid(BodyRenderComponent))
	{
		return BodyRenderComponent.Get();
	}
	const AActor* BodyActor = Listener
		? Listener->GetResolvedBodyActor()
		: nullptr;
	return BodyActor == GetOwner()
		? const_cast<UPerceptionKnowledgeHearingRangeRendererComponent*>(this)
		: nullptr;
}

bool UPerceptionKnowledgeHearingRangeRendererComponent::
	IsHearingRangeVisible() const
{
	const UStaticMeshComponent* RenderComponent =
		GetActiveRenderComponent();
	return IsValid(RenderComponent)
		&& RenderComponent->IsVisible()
		&& !RenderComponent->bHiddenInGame;
}

UStaticMeshComponent*
UPerceptionKnowledgeHearingRangeRendererComponent::ResolveRenderComponent(
	AActor* BodyActor,
	USceneComponent* BodyRoot,
	const bool bCreateIfMissing)
{
	if (!IsValid(BodyActor) || !IsValid(BodyRoot))
	{
		DestroyBodyRenderComponent();
		return nullptr;
	}

	if (BodyActor == GetOwner())
	{
		DestroyBodyRenderComponent();
		if (GetAttachParent() != BodyRoot)
		{
			AttachToComponent(
				BodyRoot,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
		return this;
	}

	if (IsValid(BodyRenderComponent)
		&& BodyRenderComponent->GetOwner() != BodyActor)
	{
		DestroyBodyRenderComponent();
	}
	if (IsValid(BodyRenderComponent) || !bCreateIfMissing)
	{
		return BodyRenderComponent.Get();
	}

	UStaticMeshComponent* NewRenderComponent =
		NewObject<UStaticMeshComponent>(
			BodyActor,
			UStaticMeshComponent::StaticClass(),
			NAME_None,
			RF_Transient);
	if (!NewRenderComponent)
	{
		return nullptr;
	}

	ConfigureRenderComponent(*NewRenderComponent);
	SynchronizeRenderAssets(*NewRenderComponent);
	NewRenderComponent->SetupAttachment(BodyRoot);
	BodyActor->AddInstanceComponent(NewRenderComponent);
	NewRenderComponent->OnComponentCreated();
	NewRenderComponent->RegisterComponent();
	BodyRenderComponent = NewRenderComponent;
	return BodyRenderComponent.Get();
}

void UPerceptionKnowledgeHearingRangeRendererComponent::
	ConfigureRenderComponent(UStaticMeshComponent& Component) const
{
	Component.PrimaryComponentTick.bCanEverTick = false;
	Component.SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component.SetCollisionResponseToAllChannels(ECR_Ignore);
	Component.SetGenerateOverlapEvents(false);
	Component.SetCanEverAffectNavigation(false);
	Component.CastShadow = false;
	Component.bCastDynamicShadow = false;
	Component.bCastStaticShadow = false;
	Component.SetAbsolute(false, false, true);
}

void UPerceptionKnowledgeHearingRangeRendererComponent::
	SynchronizeRenderAssets(UStaticMeshComponent& Component) const
{
	Component.SetStaticMesh(GetStaticMesh());
	for (int32 MaterialIndex = 0;
		MaterialIndex < GetNumMaterials();
		++MaterialIndex)
	{
		Component.SetMaterial(
			MaterialIndex,
			GetMaterial(MaterialIndex));
	}
}

void UPerceptionKnowledgeHearingRangeRendererComponent::
	DestroyBodyRenderComponent()
{
	if (IsValid(BodyRenderComponent))
	{
		BodyRenderComponent->DestroyComponent();
	}
	BodyRenderComponent = nullptr;
}

void UPerceptionKnowledgeHearingRangeRendererComponent::
	SetRendererDiagnostic(
		FString InDiagnostic,
		const bool bLogWarning)
{
	if (RendererDiagnostic == InDiagnostic)
	{
		return;
	}
	RendererDiagnostic = MoveTemp(InDiagnostic);
	if (bLogWarning)
	{
		PERCEPTIONKNOWLEDGE_LOG_WARNING(
			TEXT("Hearing renderer=%s Owner=%s: %s"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			*RendererDiagnostic);
	}
}

void UPerceptionKnowledgeHearingRangeRendererComponent::RefreshRenderer()
{
	AActor* BodyActor = Listener
		? Listener->GetResolvedBodyActor()
		: nullptr;
	USceneComponent* BodyRoot = IsValid(BodyActor)
		? BodyActor->GetRootComponent()
		: nullptr;
	RenderedHearingRange = Listener
		? Listener->GetEffectiveHearingRange()
		: 0.0f;
	const bool bDebugVisible =
		bEnableDebug && IsPerceptionKnowledgeDebugEnabled();
	const bool bVisibilityRequested =
		bVisibleInGameplay || bDebugVisible;
	const bool bHasRequiredState =
		BodyRoot
		&& RenderedHearingRange > 0.0f
		&& GetStaticMesh() != nullptr;
	UStaticMeshComponent* RenderComponent =
		ResolveRenderComponent(
			BodyActor,
			BodyRoot,
			bVisibilityRequested && bHasRequiredState);

	// The Controller-owned authoring component is only a configuration source. Keeping it hidden
	// avoids an unusable primitive owned by Unreal's hidden Controller Actor.
	if (RenderComponent != this)
	{
		SetVisibility(false, true);
		SetHiddenInGame(true);
	}

	const bool bShouldRender =
		bVisibilityRequested
		&& bHasRequiredState
		&& IsValid(RenderComponent);
	if (RenderComponent)
	{
		SynchronizeRenderAssets(*RenderComponent);
		RenderComponent->SetRelativeLocation(FVector::ZeroVector);
		if (RenderedHearingRange > 0.0f)
		{
			const float RangeScale =
				RenderedHearingRange
				/ FMath::Max(0.01f, AuthoredMeshRadius);
			RenderComponent->SetWorldScale3D(
				FVector(
					RangeScale,
					RangeScale,
					RangeScale
						* FMath::Max(
							0.001f,
							VerticalScaleMultiplier)));
		}
		RenderComponent->SetVisibility(bShouldRender, true);
		RenderComponent->SetHiddenInGame(!bShouldRender);
	}

	if (!Listener)
	{
		SetRendererDiagnostic(
			TEXT("No listener is bound."),
			bVisibilityRequested);
	}
	else if (!BodyRoot)
	{
		SetRendererDiagnostic(
			TEXT("The listener has no resolved Body Actor; rendering waits for possession."),
			false);
	}
	else if (RenderedHearingRange <= 0.0f)
	{
		SetRendererDiagnostic(
			TEXT("The effective Hearing Range is zero or Hearing is disabled."),
			bVisibilityRequested);
	}
	else if (!GetStaticMesh())
	{
		SetRendererDiagnostic(
			TEXT("No Static Mesh is assigned. Assign a mesh with a documented authored radius."),
			bVisibilityRequested);
	}
	else if (!bVisibilityRequested)
	{
		SetRendererDiagnostic(
			TEXT("Hidden by policy. Enable gameplay visibility or both local and global debug."),
			false);
	}
	else if (!RenderComponent)
	{
		SetRendererDiagnostic(
			TEXT("The Body Actor render component could not be created."),
			true);
	}
	else
	{
		SetRendererDiagnostic(
			TEXT("Rendering on the resolved Body Actor."),
			false);
	}
}
