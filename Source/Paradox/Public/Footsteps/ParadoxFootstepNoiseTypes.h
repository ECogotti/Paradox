#pragma once

#include "Chaos/ChaosEngineInterface.h"
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ParadoxFootstepNoiseTypes.generated.h"

/** Stable classification produced whenever the Paradox footstep adapter handles an event. */
UENUM(BlueprintType)
enum class EParadoxFootstepNoiseResult : uint8
{
	Emitted,
	SuppressedByCrouch,
	DisabledBySurface,
	MissingNoiseProfile,
	MissingSurfaceResponse,
	InvalidEvent,
	InvalidOwner,
	EmissionFailed
};

/** Project-owned semantic hearing response for one physical surface. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxFootstepNoiseResponse
{
	GENERATED_BODY()

	/** Semantic event recorded by PerceptionKnowledge when the noise is heard. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Footsteps|Perception")
	FGameplayTag EventTag;

	/** Optional cause metadata used for observation correlation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Footsteps|Perception")
	FGameplayTag CauseTag;

	/** Multiplied by the normalized intensity from the generic footstep event. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Footsteps|Perception", meta = (ClampMin = "0.0"))
	float BaseLoudness = 1.0f;

	/** Zero lets each listener's Hearing profile determine the effective range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Footsteps|Perception", meta = (ClampMin = "0.0", Units = "Centimeters"))
	float MaxRange = 0.0f;

	/** Disables AI noise for this response without affecting generic audio or VFX. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Footsteps|Perception")
	bool bEmitNoise = true;
};
