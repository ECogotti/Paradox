#pragma once

#include "CoreMinimal.h"
#include "ParadoxOxygenTypes.generated.h"

/** Selects whether Oxygen is owned independently by each Pawn or by one World reservoir. */
UENUM(BlueprintType)
enum class EParadoxOxygenMode : uint8
{
	PerPawn,
	SharedGlobal
};

/** Determines how active temporal avatars contribute to a shared World reservoir. */
UENUM(BlueprintType)
enum class EParadoxSharedOxygenConsumptionPolicy : uint8
{
	/** Consume once at the configured World rate while at least one participant is active. */
	FixedWorldRate,
	/** Multiply the configured World rate by the number of active participants. */
	PerActiveAvatar
};

/** Per-map Oxygen policy supplied by AParadoxWorldInitializer. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxOxygenWorldConfiguration
{
	GENERATED_BODY()

	/** Per-Pawn preserves the original component-owned behavior. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Oxygen")
	EParadoxOxygenMode Mode = EParadoxOxygenMode::PerPawn;

	/** Capacity of the shared reservoir; ignored in Per-Pawn mode. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Oxygen",
		meta = (EditCondition = "Mode == EParadoxOxygenMode::SharedGlobal", EditConditionHides, ClampMin = "0.001", UIMin = "1.0", Units = "s"))
	float SharedDurationSeconds = 180.0f;

	/** Base shared depletion speed; ignored in Per-Pawn mode. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Oxygen",
		meta = (EditCondition = "Mode == EParadoxOxygenMode::SharedGlobal", EditConditionHides, ClampMin = "0.001", UIMin = "0.1"))
	float SharedBaseConsumptionSpeed = 1.0f;

	/** Contribution rule for active Player and Clone avatars. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Oxygen",
		meta = (EditCondition = "Mode == EParadoxOxygenMode::SharedGlobal", EditConditionHides))
	EParadoxSharedOxygenConsumptionPolicy SharedConsumptionPolicy =
		EParadoxSharedOxygenConsumptionPolicy::FixedWorldRate;
};

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
