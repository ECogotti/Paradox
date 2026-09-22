#include "Tests/ParadoxHealthTestTypes.h"

void UParadoxHealthEventRecorder::HandleHealthChanged(
	float OldHealth,
	float NewHealth,
	float MaxHealth,
	float NormalizedHealth)
{
	++HealthChangedCount;
	EventOrder.Add(TEXT("HealthChanged"));
}

void UParadoxHealthEventRecorder::HandleDamageTaken(
	const float DamageApplied,
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	++DamageTakenCount;
	LastDamageApplied = DamageApplied;
	EventOrder.Add(TEXT("DamageTaken"));
}

void UParadoxHealthEventRecorder::HandleDeath(
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	++DeathCount;
	EventOrder.Add(TEXT("Death"));
}

void UParadoxHealthEventRecorder::HandleHealed(
	const float HealthRestored,
	const float NewHealth)
{
	EventOrder.Add(TEXT("Healed"));
}

void UParadoxHealthEventRecorder::HandleReset()
{
	++ResetCount;
	EventOrder.Add(TEXT("HealthReset"));
}

void UParadoxHealthEventRecorder::HandleRevived()
{
	++RevivedCount;
	EventOrder.Add(TEXT("Revived"));
}
