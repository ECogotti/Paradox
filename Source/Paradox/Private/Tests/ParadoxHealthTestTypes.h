#pragma once

#include "Characters/ParadoxCharacter.h"
#include "Health/ParadoxHealthComponent.h"
#include "ParadoxHealthTestTypes.generated.h"

/** Concrete shared Character used to isolate Health from Player/Clone death consequences. */
UCLASS()
class AParadoxHealthTestCharacter : public AParadoxCharacter
{
	GENERATED_BODY()
};

/** Dynamic-delegate recorder used by Health automation tests. */
UCLASS()
class UParadoxHealthEventRecorder : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleHealthChanged(
		float OldHealth,
		float NewHealth,
		float MaxHealth,
		float NormalizedHealth);

	UFUNCTION()
	void HandleDamageTaken(
		float DamageApplied,
		const UDamageType* DamageType,
		AController* InstigatedBy,
		AActor* DamageCauser);

	UFUNCTION()
	void HandleDeath(
		const UDamageType* DamageType,
		AController* InstigatedBy,
		AActor* DamageCauser);

	UFUNCTION()
	void HandleHealed(float HealthRestored, float NewHealth);

	UFUNCTION()
	void HandleReset();

	UFUNCTION()
	void HandleRevived();

	int32 HealthChangedCount = 0;
	int32 DamageTakenCount = 0;
	int32 DeathCount = 0;
	int32 ResetCount = 0;
	int32 RevivedCount = 0;
	float LastDamageApplied = 0.0f;
	TArray<FName> EventOrder;
};
