#pragma once

#include "CoreMinimal.h"
#include "ParadoxOxygenTypes.generated.h"

/** Opaque ownership token for one transient oxygen countdown-speed modifier. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxOxygenSpeedModifierHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Oxygen")
	FGuid Id;

	bool IsValid() const { return Id.IsValid(); }
};

/** Opaque ownership token for one transient oxygen-consumption block. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxOxygenBlockHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Oxygen")
	FGuid Id;

	bool IsValid() const { return Id.IsValid(); }
};

