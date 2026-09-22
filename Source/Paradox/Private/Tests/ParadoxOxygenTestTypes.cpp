#include "Tests/ParadoxOxygenTestTypes.h"

#include "GameFramework/DamageType.h"

void UParadoxOxygenEventRecorder::HandleOxygenChanged(
	float OldRemainingSeconds,
	float NewRemainingSeconds,
	float DurationSeconds,
	float NormalizedOxygen)
{
	++OxygenChangedCount;
	EventOrder.Add(TEXT("OxygenChanged"));
}

void UParadoxOxygenEventRecorder::HandleOxygenDepleted()
{
	++DepletedCount;
	EventOrder.Add(TEXT("OxygenDepleted"));
}

void UParadoxOxygenEventRecorder::HandleOxygenReset()
{
	++ResetCount;
	EventOrder.Add(TEXT("OxygenReset"));
}

void UParadoxOxygenEventRecorder::HandleConsumptionSpeedChanged(
	float OldSpeed,
	float NewSpeed)
{
	++SpeedChangedCount;
}

void UParadoxOxygenEventRecorder::HandleConsumptionBlockedChanged(bool bIsBlocked)
{
	++BlockedChangedCount;
}

void UParadoxOxygenEventRecorder::HandleWholeSecondChanged(
	const int32 WholeSecondsRemaining,
	float RemainingSeconds,
	float NormalizedOxygen)
{
	++WholeSecondChangedCount;
	LastWholeSeconds = WholeSecondsRemaining;
	EventOrder.Add(TEXT("WholeSecondChanged"));
}

void UParadoxOxygenEventRecorder::HandleHealthDeath(
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	++DeathCount;
	LastDeathDamageTypeClass = DamageType
		? DamageType->GetClass()
		: nullptr;
	EventOrder.Add(TEXT("HealthDeath"));
}

