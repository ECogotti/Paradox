#pragma once

#include "Actions/GameplayActionDefinition.h"
#include "CoreMinimal.h"
#include "ParadoxInteractionActionDefinition.generated.h"

namespace ParadoxInteractionActionParameters
{
	PARADOX_API extern const FName Target;
	PARADOX_API extern const FName InteractionTag;
	PARADOX_API extern const FName NavigationFilter;
	PARADOX_API extern const FName AcceptanceRadius;
	PARADOX_API extern const FName AllowStrafe;
	PARADOX_API extern const FName ReceiverComponentName;
	PARADOX_API extern const FName EmitterComponentName;
	PARADOX_API extern const FName SignalTag;
	PARADOX_API extern const FName Command;
}

/** Shared replay-safe schema and scheduler defaults for native Paradox interactions. */
UCLASS(Abstract, BlueprintType)
class PARADOX_API UParadoxInteractionActionDefinition : public UGameplayActionDefinition
{
	GENERATED_BODY()

public:
	UParadoxInteractionActionDefinition();
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** Ready-to-author Definition class for manual Puzzle Receiver activation/deactivation. */
UCLASS(BlueprintType)
class PARADOX_API UParadoxReceiverInteractionActionDefinition
	: public UParadoxInteractionActionDefinition
{
	GENERATED_BODY()

public:
	UParadoxReceiverInteractionActionDefinition();
};

/** Ready-to-author Definition class for Puzzle Emitter On/Off signals. */
UCLASS(BlueprintType)
class PARADOX_API UParadoxEmitterInteractionActionDefinition
	: public UParadoxInteractionActionDefinition
{
	GENERATED_BODY()

public:
	UParadoxEmitterInteractionActionDefinition();
};
