#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/IntentReplayTypes.h"
#include "IntentRecordabilityPolicy.generated.h"

/**
 * Recursive boundary policy that rejects request parameters tied to runtime world identity.
 *
 * The default accepts deterministic values, classes, soft references, and hard asset references;
 * it rejects Actors, components, transient objects, non-assets, and unknown reflected types.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class INTENTREPLAY_API UIntentRecordabilityPolicy : public UObject
{
	GENERATED_BODY()

public:
	/** Validates the whole bag and returns the first failing nested path. */
	virtual FIntentRecordabilityResult ValidatePropertyBag(const FInstancedPropertyBag& PropertyBag) const;

protected:
	/** Recursive implementation for scalar, struct, array, set, and map values. */
	FIntentRecordabilityResult ValidateProperty(
		const FProperty* Property,
		const void* ValueAddress,
		const FString& PropertyPath) const;
};
