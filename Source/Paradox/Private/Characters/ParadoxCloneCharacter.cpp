#include "Characters/ParadoxCloneCharacter.h"

#include "Behavior/ParadoxCloneBehaviorCoordinatorComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Controllers/ParadoxCloneController.h"
#include "Components/IntentReplayComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayActionTags.h"
#include "Navigation/GridNavigationData.h"
#include "Paradox.h"
#include "Perception/ParadoxTemporalVisionComponent.h"
#include "Investigation/ParadoxCloneInvestigationComponent.h"
#include "Playback/ParadoxCloneReplayExecutionStrategy.h"
#include "Subsystems/GridWorldSubsystem.h"

AParadoxCloneCharacter::AParadoxCloneCharacter()
{
	WorldStateParticipantComponent = CreateDefaultSubobject<UWorldStateParticipantComponent>(
		TEXT("WorldStateParticipantComponent"));
	WorldStateParticipantComponent->bCaptureExistence = false;
	WorldStateParticipantComponent->bCaptureActorTransform = true;
	WorldStateParticipantComponent->bCaptureAttachment = false;
	WorldStateParticipantComponent->ExistencePolicy = EWorldStateExistencePolicy::ExternallyManaged;

	TemporalVisionComponent = CreateDefaultSubobject<UParadoxTemporalVisionComponent>(
		TEXT("TemporalVisionComponent"));
	TemporalVisionComponent->SetupAttachment(GetRootComponent());
	TemporalVisionCandidateSphere = CreateDefaultSubobject<USphereComponent>(
		TEXT("TemporalVisionCandidateSphere"));
	TemporalVisionCandidateSphere->SetupAttachment(TemporalVisionComponent);
	TemporalVisionComponent->SetCandidateSphereComponent(TemporalVisionCandidateSphere);
	InvestigationComponent =
		CreateDefaultSubobject<UParadoxCloneInvestigationComponent>(
			TEXT("ParadoxCloneInvestigationComponent"));
	BehaviorCoordinator =
		CreateDefaultSubobject<UParadoxCloneBehaviorCoordinatorComponent>(
			TEXT("ParadoxCloneBehaviorCoordinatorComponent"));

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	AIControllerClass = AParadoxCloneController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	if (UIntentReplayComponent* Replay = GetIntentReplayComponent())
	{
		Replay->ExecutionStrategyClass =
			UParadoxCloneReplayExecutionStrategy::StaticClass();
	}
}

void AParadoxCloneCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (TemporalVisionComponent)
	{
		TemporalVisionComponent->SetCandidateSphereComponent(
			TemporalVisionCandidateSphere);
		TemporalVisionComponent->SynchronizeCandidateSphere();
	}
}

void AParadoxCloneCharacter::HandleHealthDeath(
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	Super::HandleHealthDeath(DamageType, InstigatedBy, DamageCauser);
	if (BehaviorCoordinator)
	{
		BehaviorCoordinator->StopForDeath();
	}
	if (UGameplayActionComponent* Actions = GetGameplayActionComponent())
	{
		Actions->AbortAllActions(
			GameplayActionTags::Result_Aborted_SystemReset);
	}
	if (AParadoxCloneController* CloneController =
		Cast<AParadoxCloneController>(GetController()))
	{
		CloneController->StopMovement();
		if (UPerceptionKnowledgeListenerComponent* Listener =
			CloneController->GetPerceptionKnowledgeListener())
		{
			const FPerceptionKnowledgeOperationResult Result =
				Listener->SetListenerEnabled(false);
			if (!Result.IsSuccess())
			{
				PARADOX_LOG_WARNING(
					TEXT("Dead clone '%s' could not disable its perception listener: %s"),
					*GetNameSafe(this),
					*Result.Message);
			}
		}
	}
	if (TemporalVisionComponent)
	{
		TemporalVisionComponent->DisableTemporalDetection(true);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	DisableGridPresenceForDeath();
	EnterRagdollCorpseState();
}

void AParadoxCloneCharacter::DisableGridPresenceForDeath()
{
	UGridWorldSubsystem* GridWorld = GetWorld()
		? GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	AGridNavigationData* NavigationData = GridWorld
		? GridWorld->GetNavigationData()
		: nullptr;
	TInlineComponentArray<UGridNavigationOccupancyComponent*> OccupancyComponents(this);
	for (UGridNavigationOccupancyComponent* Occupancy : OccupancyComponents)
	{
		if (!IsValid(Occupancy) || Occupancy->bIsReservation)
		{
			continue;
		}
		if (NavigationData && Occupancy->OccupantId.IsValid())
		{
			NavigationData->ReleaseTrafficCorridor(
				Occupancy->OccupantId,
				nullptr,
				false);
		}
		Occupancy->SetOccupancyEnabled(false);
	}
}

void AParadoxCloneCharacter::EnterRagdollCorpseState()
{
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCanEverAffectNavigation(false);
		Capsule->SetCollisionObjectType(ECC_Pawn);
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Capsule->SetGenerateOverlapEvents(false);
	}
	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		SkeletalMesh->SetCanEverAffectNavigation(false);
		SkeletalMesh->SetCollisionProfileName(TEXT("Ragdoll"));
		SkeletalMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		SkeletalMesh->SetAllBodiesSimulatePhysics(true);
		SkeletalMesh->SetSimulatePhysics(true);
		SkeletalMesh->WakeAllRigidBodies();
	}
}
