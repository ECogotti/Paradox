#include "Subsystems/TacticalPauseLocalPlayerSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Settings/TacticalPauseSettings.h"
#include "TacticalPause.h"
#include "Widgets/TacticalPauseControlsWidget.h"

void UTacticalPauseLocalPlayerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	CreateControlsWidget(LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr);
}

void UTacticalPauseLocalPlayerSubsystem::Deinitialize()
{
	RemoveControlsWidget();
	Super::Deinitialize();
}

void UTacticalPauseLocalPlayerSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);
	// Widget ownership follows the local player's current controller exactly.
	RemoveControlsWidget();
	CreateControlsWidget(NewPlayerController);
}

void UTacticalPauseLocalPlayerSubsystem::CreateControlsWidget(APlayerController* PlayerController)
{
	if (ControlsWidget || !PlayerController || !PlayerController->IsLocalController() || !IsEligibleLocalPlayer())
	{
		return;
	}
	UWorld* World = PlayerController->GetWorld();
	if (!World || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
	{
		return;
	}
	const UTacticalPauseSettings* Settings = GetDefault<UTacticalPauseSettings>();
	if (!Settings || !Settings->bCreateDefaultWidgetAutomatically)
	{
		return;
	}

	// Soft loading keeps the replaceable presentation out of module startup.
	UClass* WidgetClass = Settings->DefaultWidgetClass.LoadSynchronous();
	if (!WidgetClass || !WidgetClass->IsChildOf(UTacticalPauseControlsWidget::StaticClass()))
	{
		if (!Settings->DefaultWidgetClass.IsNull())
		{
			TACTICALPAUSE_LOG_WARNING("Configured Tactical Pause Common UI class '%s' could not be loaded; using the native logic shell.",
				*Settings->DefaultWidgetClass.ToSoftObjectPath().ToString());
		}
		WidgetClass = UTacticalPauseControlsWidget::StaticClass();
	}

	ControlsWidget = CreateWidget<UTacticalPauseControlsWidget>(PlayerController, WidgetClass);
	if (!ControlsWidget)
	{
		TACTICALPAUSE_LOG_ERROR("Failed to create Tactical Pause widget for PlayerController %s in World %s.",
			*GetNameSafe(PlayerController), *GetNameSafe(World));
		return;
	}
	if (!ControlsWidget->AddToPlayerScreen(Settings->WidgetZOrder))
	{
		TACTICALPAUSE_LOG_ERROR("Failed to add Tactical Pause widget %s to the player screen for %s.",
			*GetNameSafe(ControlsWidget), *GetNameSafe(PlayerController));
		ControlsWidget->RemoveFromParent();
		ControlsWidget = nullptr;
		return;
	}
	TACTICALPAUSE_LOG_INFO("Created Tactical Pause widget %s for PlayerController %s in World %s.",
		*GetNameSafe(ControlsWidget), *GetNameSafe(PlayerController), *GetNameSafe(World));
}

void UTacticalPauseLocalPlayerSubsystem::RemoveControlsWidget()
{
	if (ControlsWidget)
	{
		ControlsWidget->RemoveFromParent();
		ControlsWidget = nullptr;
	}
}

bool UTacticalPauseLocalPlayerSubsystem::IsEligibleLocalPlayer() const
{
	const UTacticalPauseSettings* Settings = GetDefault<UTacticalPauseSettings>();
	if (Settings && Settings->WidgetPlayerPolicy == ETacticalPauseWidgetPlayerPolicy::EveryLocalPlayer)
	{
		return true;
	}
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	return GameInstance && GameInstance->GetFirstGamePlayer() == LocalPlayer;
}
