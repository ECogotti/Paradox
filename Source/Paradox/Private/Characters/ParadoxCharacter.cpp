// Copyright Epic Games, Inc. All Rights Reserved.

#include "Characters/ParadoxCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/EntityIdentityComponent.h"
#include "Components/FootstepComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Components/IntentReplayObservationComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Footsteps/ParadoxFootstepNoiseComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Health/ParadoxHealthComponent.h"
#include "NiagaraComponent.h"
#include "Oxygen/ParadoxOxygenComponent.h"
#include "TimeLoop/ParadoxTemporalEntityComponent.h"

AParadoxCharacter::AParadoxCharacter()
{
	SetCanBeDamaged(true);

	// Set size for player capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Don't rotate character to camera direction
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);
	GetCharacterMovement()->bConstrainToPlane = true;
	GetCharacterMovement()->bSnapToPlaneAtStart = true;
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;

	// Create semantic action and replay components on the entity whose behavior is recorded.
	GameplayActionComponent = CreateDefaultSubobject<UGameplayActionComponent>(TEXT("GameplayActionComponent"));
	InventoryComponent = CreateDefaultSubobject<UParadoxInventoryComponent>(TEXT("InventoryComponent"));
	HealthComponent = CreateDefaultSubobject<UParadoxHealthComponent>(TEXT("HealthComponent"));
	OxygenComponent = CreateDefaultSubobject<UParadoxOxygenComponent>(TEXT("OxygenComponent"));
	IntentReplayComponent = CreateDefaultSubobject<UIntentReplayComponent>(TEXT("IntentReplayComponent"));
	IntentReplayComponent->ActionComponentOverride = GameplayActionComponent;
	ObservationReplayComponent =
		CreateDefaultSubobject<UIntentReplayObservationComponent>(
			TEXT("IntentReplayObservationComponent"));
	ObservationReplayComponent->IntentReplaySourceOverride =
		IntentReplayComponent;
	EntityIdentityComponent = CreateDefaultSubobject<UEntityIdentityComponent>(
		TEXT("EntityIdentityComponent"));
	TemporalEntityComponent = CreateDefaultSubobject<UParadoxTemporalEntityComponent>(
		TEXT("TemporalEntityComponent"));
	FootstepComponent = CreateDefaultSubobject<UFootstepComponent>(
		TEXT("FootstepComponent"));
	PerceptionKnowledgeSourceComponent =
		CreateDefaultSubobject<UPerceptionKnowledgeSourceComponent>(
			TEXT("PerceptionKnowledgeSourceComponent"));
	FootstepNoiseComponent =
		CreateDefaultSubobject<UParadoxFootstepNoiseComponent>(
			TEXT("ParadoxFootstepNoiseComponent"));
	TimeTravelNiagaraComponent =
		CreateDefaultSubobject<UNiagaraComponent>(
			TEXT("TimeTravelNiagaraComponent"));
	TimeTravelNiagaraComponent->SetupAttachment(GetRootComponent());
	TimeTravelNiagaraComponent->SetAutoActivate(false);
}

float AParadoxCharacter::TakeDamage(
	const float DamageAmount,
	const FDamageEvent& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser)
{
	const float HealthBeforeDamage = HealthComponent
		? HealthComponent->GetCurrentHealth()
		: 0.0f;
	const float EngineDamage = Super::TakeDamage(
		DamageAmount,
		DamageEvent,
		EventInstigator,
		DamageCauser);
	return HealthComponent
		? FMath::Max(0.0f, HealthBeforeDamage - HealthComponent->GetCurrentHealth())
		: EngineDamage;
}

void AParadoxCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddUniqueDynamic(
			this,
			&ThisClass::HandleHealthDeathEvent);
	}
}

void AParadoxCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HealthComponent)
	{
		HealthComponent->OnDeath.RemoveDynamic(
			this,
			&ThisClass::HandleHealthDeathEvent);
	}
	Super::EndPlay(EndPlayReason);
}

void AParadoxCharacter::HandleHealthDeath(
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
}

void AParadoxCharacter::HandleHealthDeathEvent(
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	HandleHealthDeath(DamageType, InstigatedBy, DamageCauser);
}
