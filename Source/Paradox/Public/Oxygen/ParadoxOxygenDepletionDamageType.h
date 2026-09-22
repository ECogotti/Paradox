#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "ParadoxOxygenDepletionDamageType.generated.h"

/** Native damage classification used when a depleted Oxygen component asks Health to kill. */
UCLASS(BlueprintType, Const)
class PARADOX_API UParadoxOxygenDepletionDamageType : public UDamageType
{
	GENERATED_BODY()
};

