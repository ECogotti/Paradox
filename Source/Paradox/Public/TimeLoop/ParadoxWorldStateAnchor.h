#pragma once

#include "GameFramework/Info.h"
#include "ParadoxWorldStateAnchor.generated.h"

class UWorldStateParticipantComponent;

/**
 * Loop-owned participant that keeps an otherwise empty World State baseline structurally valid.
 *
 * It captures no gameplay properties, transform, attachment, or existence. Puzzle participants
 * remain the actual source of resettable level state.
 */
UCLASS(NotBlueprintable, Transient)
class PARADOX_API AParadoxWorldStateAnchor : public AInfo
{
	GENERATED_BODY()

public:
	AParadoxWorldStateAnchor();

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UWorldStateParticipantComponent> WorldStateParticipant;
};

