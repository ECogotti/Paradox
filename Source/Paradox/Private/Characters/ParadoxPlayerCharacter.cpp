#include "Characters/ParadoxPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/TacticalPauseActionQueueComponent.h"
#include "GameFramework/SpringArmComponent.h"

AParadoxPlayerCharacter::AParadoxPlayerCharacter()
{
	TacticalPauseActionQueueComponent = CreateDefaultSubobject<UTacticalPauseActionQueueComponent>(
		TEXT("TacticalPauseActionQueueComponent"));
	TacticalPauseActionQueueComponent->ActionComponentOverride = GetGameplayActionComponent();

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->TargetArmLength = 800.f;
	CameraBoom->SetRelativeRotation(FRotator(-60.f, 0.f, 0.f));
	CameraBoom->bDoCollisionTest = false;

	TopDownCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCameraComponent->bUsePawnControlRotation = false;

	// BP_PlayerCharacter currently owns presentation/debug work in ReceiveTick.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}
