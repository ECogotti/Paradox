#include "HUD/ParadoxGameplayHUDComponent.h"

#include "Characters/ParadoxCharacter.h"
#include "Controllers/ParadoxPlayerController.h"
#include "GameModes/ParadoxGameMode.h"
#include "HUD/ParadoxGameplayHUDWidget.h"
#include "Inventory/ParadoxInventoryWidget.h"
#include "Paradox.h"
#include "TimeLoop/ParadoxTimeLoopComponent.h"

UParadoxGameplayHUDComponent::UParadoxGameplayHUDComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
	GameplayHUDWidgetClass = UParadoxGameplayHUDWidget::StaticClass();
}

void UParadoxGameplayHUDComponent::BeginPlay()
{
	Super::BeginPlay();
	AParadoxPlayerController* Controller = Cast<AParadoxPlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalPlayerController())
	{
		return;
	}

	CurrentMode = InitialHUDMode;
	Controller->OnPossessedPawnChanged.AddUniqueDynamic(
		this,
		&ThisClass::HandlePossessedPawnChanged);
	BindTimeLoop();
	CreateGameplayHUD();
	HandlePossessedPawnChanged(nullptr, Controller->GetPawn());
}

void UParadoxGameplayHUDComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	if (AParadoxPlayerController* Controller = Cast<AParadoxPlayerController>(GetOwner()))
	{
		Controller->OnPossessedPawnChanged.RemoveDynamic(
			this,
			&ThisClass::HandlePossessedPawnChanged);
	}
	UnbindTimeLoop();
	DestroyGameplayHUD();
	Super::EndPlay(EndPlayReason);
}

bool UParadoxGameplayHUDComponent::SetHUDMode(
	const EParadoxGameplayHUDMode NewMode)
{
	if (CurrentMode == NewMode)
	{
		return false;
	}
	const EParadoxGameplayHUDMode PreviousMode = CurrentMode;
	CurrentMode = NewMode;
	if (GameplayHUDWidget)
	{
		GameplayHUDWidget->ApplyHUDMode(NewMode);
	}
	OnHUDModeChanged.Broadcast(PreviousMode, NewMode);
	return true;
}

bool UParadoxGameplayHUDComponent::ToggleHUDMode()
{
	return SetHUDMode(
		CurrentMode == EParadoxGameplayHUDMode::Normal
			? EParadoxGameplayHUDMode::Collapsed
			: EParadoxGameplayHUDMode::Normal);
}

void UParadoxGameplayHUDComponent::SetVisibilityOverride(
	const EParadoxGameplayHUDVisibilityOverride NewOverride)
{
	if (VisibilityOverride == NewOverride)
	{
		return;
	}
	VisibilityOverride = NewOverride;
	RefreshHUDVisibility();
}

void UParadoxGameplayHUDComponent::SetSectionVisibility(
	const EParadoxGameplayHUDSection Section,
	const ESlateVisibility Visibility)
{
	ESlateVisibility* StoredVisibility = nullptr;
	switch (Section)
	{
	case EParadoxGameplayHUDSection::TacticalPause:
		StoredVisibility = &TacticalPauseSectionVisibility;
		break;
	case EParadoxGameplayHUDSection::Equipment:
		StoredVisibility = &EquipmentSectionVisibility;
		break;
	case EParadoxGameplayHUDSection::Status:
		StoredVisibility = &StatusSectionVisibility;
		break;
	default:
		return;
	}
	if (*StoredVisibility == Visibility)
	{
		return;
	}
	*StoredVisibility = Visibility;
	if (GameplayHUDWidget)
	{
		GameplayHUDWidget->SetSectionVisibility(Section, Visibility);
	}
	OnHUDSectionVisibilityChanged.Broadcast(Section, Visibility);
}

ESlateVisibility UParadoxGameplayHUDComponent::GetSectionVisibility(
	const EParadoxGameplayHUDSection Section) const
{
	switch (Section)
	{
	case EParadoxGameplayHUDSection::TacticalPause:
		return TacticalPauseSectionVisibility;
	case EParadoxGameplayHUDSection::Equipment:
		return EquipmentSectionVisibility;
	case EParadoxGameplayHUDSection::Status:
		return StatusSectionVisibility;
	default:
		return ESlateVisibility::Collapsed;
	}
}

void UParadoxGameplayHUDComponent::RefreshHUDVisibility()
{
	if (!GameplayHUDWidget || bEndingPlay)
	{
		return;
	}
	bool bShouldShow = false;
	switch (VisibilityOverride)
	{
	case EParadoxGameplayHUDVisibilityOverride::ForcedVisible:
		bShouldShow = true;
		break;
	case EParadoxGameplayHUDVisibilityOverride::ForcedHidden:
		bShouldShow = false;
		break;
	case EParadoxGameplayHUDVisibilityOverride::Automatic:
	default:
		bShouldShow = ShouldHUDBeVisible();
		break;
	}
	if (bHUDVisible == bShouldShow)
	{
		return;
	}
	bHUDVisible = bShouldShow;
	GameplayHUDWidget->SetVisibility(
		bShouldShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	OnHUDVisibilityChanged.Broadcast(bShouldShow);
}

bool UParadoxGameplayHUDComponent::CanToggleHUDModeFromInput() const
{
	return IsValid(GameplayHUDWidget) && bHUDVisible && !bEndingPlay;
}

bool UParadoxGameplayHUDComponent::ShouldHUDBeVisible_Implementation() const
{
	const AParadoxPlayerController* Controller = Cast<AParadoxPlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalPlayerController()
		|| !IsValid(Cast<AParadoxCharacter>(Controller->GetPawn())))
	{
		return false;
	}
	const UParadoxTimeLoopComponent* TimeLoop = BoundTimeLoop
		? BoundTimeLoop.Get()
		: ResolveTimeLoopComponent();
	if (!TimeLoop || !TimeLoop->IsTimeLoopEnabled()
		|| TimeLoop->GetCurrentPhase() == EParadoxTimeLoopPhase::Disabled)
	{
		return true;
	}
	return TimeLoop->GetCurrentPhase() == EParadoxTimeLoopPhase::ActiveRun;
}

void UParadoxGameplayHUDComponent::CreateGameplayHUD()
{
	if (GameplayHUDWidget || bEndingPlay)
	{
		return;
	}
	AParadoxPlayerController* Controller = Cast<AParadoxPlayerController>(GetOwner());
	UClass* RootClass = GameplayHUDWidgetClass.Get();
	if (!Controller || !Controller->IsLocalPlayerController() || !RootClass
		|| !RootClass->IsChildOf(UParadoxGameplayHUDWidget::StaticClass()))
	{
		PARADOX_LOG_WARNING(
			TEXT("Gameplay HUD could not be created for controller '%s': invalid local owner or root widget class."),
			*GetNameSafe(Controller));
		return;
	}
	GameplayHUDWidget = CreateWidget<UParadoxGameplayHUDWidget>(Controller, RootClass);
	if (!GameplayHUDWidget)
	{
		PARADOX_LOG_ERROR(
			TEXT("Gameplay HUD root creation failed for controller '%s' and class '%s'."),
			*GetNameSafe(Controller),
			*GetNameSafe(RootClass));
		return;
	}
	GameplayHUDWidget->AssignHUDContext(this, Controller);
	GameplayHUDWidget->ApplyHUDMode(CurrentMode);
	ResolveEmbeddedSectionWidgets();
	ApplySectionVisibilities();
	GameplayHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (!GameplayHUDWidget->AddToPlayerScreen(HUDZOrder))
	{
		PARADOX_LOG_ERROR(
			TEXT("Gameplay HUD '%s' could not be added to the player screen for '%s'."),
			*GetNameSafe(GameplayHUDWidget),
			*GetNameSafe(Controller));
		DestroyGameplayHUD();
		return;
	}
	ApplyScreenSpaceInputMode(*Controller);
	RefreshHUDVisibility();
}

void UParadoxGameplayHUDComponent::DestroyGameplayHUD()
{
	bHUDVisible = false;
	if (EquipmentWidget.IsValid())
	{
		EquipmentWidget->SetInventoryCharacter(nullptr);
	}
	if (GameplayHUDWidget)
	{
		GameplayHUDWidget->ClearHUDContext();
		GameplayHUDWidget->RemoveFromParent();
	}
	EquipmentWidget.Reset();
	GameplayHUDWidget = nullptr;
}

void UParadoxGameplayHUDComponent::ResolveEmbeddedSectionWidgets()
{
	AParadoxPlayerController* Controller = Cast<AParadoxPlayerController>(GetOwner());
	if (!Controller || !GameplayHUDWidget)
	{
		return;
	}

	if (!GameplayHUDWidget->FindEmbeddedTacticalPauseWidget())
	{
		PARADOX_LOG_WARNING(
			TEXT("Gameplay HUD root '%s' does not contain a Tactical Pause controls widget; authored roots must own all HUD sections."),
			*GetNameSafe(GameplayHUDWidget));
	}

	EquipmentWidget = GameplayHUDWidget->FindEmbeddedEquipmentWidget();
	if (!EquipmentWidget.IsValid())
	{
		PARADOX_LOG_WARNING(
			TEXT("Gameplay HUD root '%s' does not contain an Inventory widget; authored roots must own all HUD sections."),
			*GetNameSafe(GameplayHUDWidget));
	}
	if (EquipmentWidget.IsValid())
	{
		EquipmentWidget->SetInventoryCharacter(Cast<AParadoxCharacter>(Controller->GetPawn()));
	}
}

void UParadoxGameplayHUDComponent::ApplyScreenSpaceInputMode(
	AParadoxPlayerController& Controller) const
{
	if (!bConfigureGameAndUIInputMode)
	{
		return;
	}
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	Controller.SetInputMode(InputMode);
	Controller.bShowMouseCursor = true;
}

void UParadoxGameplayHUDComponent::ApplySectionVisibilities()
{
	if (!GameplayHUDWidget)
	{
		return;
	}
	GameplayHUDWidget->SetSectionVisibility(
		EParadoxGameplayHUDSection::TacticalPause,
		TacticalPauseSectionVisibility);
	GameplayHUDWidget->SetSectionVisibility(
		EParadoxGameplayHUDSection::Equipment,
		EquipmentSectionVisibility);
	GameplayHUDWidget->SetSectionVisibility(
		EParadoxGameplayHUDSection::Status,
		StatusSectionVisibility);
}

void UParadoxGameplayHUDComponent::BindTimeLoop()
{
	UnbindTimeLoop();
	BoundTimeLoop = ResolveTimeLoopComponent();
	if (BoundTimeLoop)
	{
		BoundTimeLoop->OnPhaseChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleTimeLoopPhaseChanged);
	}
}

void UParadoxGameplayHUDComponent::UnbindTimeLoop()
{
	if (BoundTimeLoop)
	{
		BoundTimeLoop->OnPhaseChanged.RemoveDynamic(
			this,
			&ThisClass::HandleTimeLoopPhaseChanged);
	}
	BoundTimeLoop = nullptr;
}

UParadoxTimeLoopComponent* UParadoxGameplayHUDComponent::ResolveTimeLoopComponent() const
{
	const UWorld* World = GetWorld();
	const AParadoxGameMode* GameMode = World
		? Cast<AParadoxGameMode>(World->GetAuthGameMode())
		: nullptr;
	return GameMode ? GameMode->GetTimeLoopComponent() : nullptr;
}

void UParadoxGameplayHUDComponent::HandlePossessedPawnChanged(
	APawn* OldPawn,
	APawn* NewPawn)
{
	if (EquipmentWidget.IsValid())
	{
		EquipmentWidget->SetInventoryCharacter(Cast<AParadoxCharacter>(NewPawn));
	}
	if (!BoundTimeLoop)
	{
		BindTimeLoop();
	}
	RefreshHUDVisibility();
}

void UParadoxGameplayHUDComponent::HandleTimeLoopPhaseChanged(
	const EParadoxTimeLoopPhase PreviousPhase,
	const EParadoxTimeLoopPhase NewPhase)
{
	RefreshHUDVisibility();
}
