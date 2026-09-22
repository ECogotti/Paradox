#pragma once

#include "CoreMinimal.h"
#include "ParadoxOxygenTestTypes.generated.h"

class UDamageType;

/** Dynamic-delegate recorder used by Oxygen automation tests. */
UCLASS()
class UParadoxOxygenEventRecorder : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleOxygenChanged(
		float OldRemainingSeconds,
		float NewRemainingSeconds,
		float DurationSeconds,
		float NormalizedOxygen);

	UFUNCTION()
	void HandleOxygenDepleted();

	UFUNCTION()
	void HandleOxygenReset();

	UFUNCTION()
	void HandleConsumptionSpeedChanged(float OldSpeed, float NewSpeed);

	UFUNCTION()
	void HandleConsumptionBlockedChanged(bool bIsBlocked);

	UFUNCTION()
	void HandleWholeSecondChanged(
		int32 WholeSecondsRemaining,
		float RemainingSeconds,
		float NormalizedOxygen);

	UFUNCTION()
	void HandleHealthDeath(
		const UDamageType* DamageType,
		AController* InstigatedBy,
		AActor* DamageCauser);

	int32 OxygenChangedCount = 0;
	int32 DepletedCount = 0;
	int32 ResetCount = 0;
	int32 SpeedChangedCount = 0;
	int32 BlockedChangedCount = 0;
	int32 WholeSecondChangedCount = 0;
	int32 DeathCount = 0;
	int32 LastWholeSeconds = INDEX_NONE;
	TSubclassOf<UDamageType> LastDeathDamageTypeClass;
	TArray<FName> EventOrder;
};

