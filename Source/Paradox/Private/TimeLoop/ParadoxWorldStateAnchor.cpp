#include "TimeLoop/ParadoxWorldStateAnchor.h"

#include "Components/WorldStateParticipantComponent.h"

AParadoxWorldStateAnchor::AParadoxWorldStateAnchor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	WorldStateParticipant = CreateDefaultSubobject<UWorldStateParticipantComponent>(
		TEXT("WorldStateParticipant"));
	WorldStateParticipant->bCaptureExistence = false;
	WorldStateParticipant->bCaptureActorTransform = false;
	WorldStateParticipant->bCaptureAttachment = false;
	WorldStateParticipant->ExistencePolicy =
		EWorldStateExistencePolicy::ExternallyManaged;
}

