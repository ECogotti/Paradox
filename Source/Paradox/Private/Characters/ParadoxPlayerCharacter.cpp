#include "Characters/ParadoxPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/TacticalPauseActionQueueComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameModes/ParadoxGameMode.h"
#include "Paradox.h"
#include "TimeLoop/ParadoxTimeLoopComponent.h"

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

void AParadoxPlayerCharacter::HandleHealthDeath(
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	Super::HandleHealthDeath(DamageType, InstigatedBy, DamageCauser);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	AParadoxGameMode* GameMode = GetWorld()
		? Cast<AParadoxGameMode>(GetWorld()->GetAuthGameMode())
		: nullptr;
	UParadoxTimeLoopComponent* TimeLoop = GameMode
		? GameMode->GetTimeLoopComponent()
		: nullptr;
	if (!TimeLoop || !TimeLoop->IsTimeLoopEnabled())
	{
		PARADOX_LOG_WARNING(
			TEXT("Player '%s' died without an enabled Paradox time-loop authority; the Health death remains committed."),
			*GetNameSafe(this));
		return;
	}

	const FParadoxTimeLoopOperationResult Result = TimeLoop->AcceptPlayerDeath(
		*this,
		DamageType,
		InstigatedBy,
		DamageCauser);
	if (!Result.IsSuccess()
		&& Result.Status != EParadoxTimeLoopOperationStatus::PlayerDeathAccepted)
	{
		PARADOX_LOG_WARNING(
			TEXT("Player-death run failure was rejected for '%s': %s"),
			*GetNameSafe(this),
			*Result.DiagnosticMessage);
	}
}
