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
#include "TimeLoop/ParadoxTemporalEntityComponent.h"

AParadoxCharacter::AParadoxCharacter()
{
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
}
