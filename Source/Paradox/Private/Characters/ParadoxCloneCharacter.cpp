#include "Characters/ParadoxCloneCharacter.h"

#include "Components/WorldStateParticipantComponent.h"
#include "Controllers/ParadoxCloneController.h"
#include "Components/IntentReplayComponent.h"
#include "Perception/ParadoxTemporalVisionComponent.h"
#include "Playback/ParadoxCloneReplayExecutionStrategy.h"

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

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	AIControllerClass = AParadoxCloneController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	if (UIntentReplayComponent* Replay = GetIntentReplayComponent())
	{
		Replay->ExecutionStrategyClass =
			UParadoxCloneReplayExecutionStrategy::StaticClass();
	}
}
