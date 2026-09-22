#include "Oxygen/ParadoxOxygenComponent.h"

#include "Characters/ParadoxCharacter.h"
#include "Engine/World.h"
#include "Health/ParadoxHealthComponent.h"
#include "Oxygen/ParadoxOxygenDepletionDamageType.h"
#include "Paradox.h"

namespace
{
	constexpr float MinimumTimerDelay = 0.001f;
}

UParadoxOxygenComponent::UParadoxOxygenComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

float UParadoxOxygenComponent::GetRemainingOxygenSeconds() const
{
	return CalculateProjectedRemainingSeconds();
}

float UParadoxOxygenComponent::GetNormalizedOxygen() const
{
	return OxygenDurationSeconds > UE_SMALL_NUMBER
		? FMath::Clamp(
			GetRemainingOxygenSeconds() / OxygenDurationSeconds,
			0.0f,
			1.0f)
		: 0.0f;
}

int32 UParadoxOxygenComponent::GetWholeSecondsRemaining() const
{
	return CalculateWholeSeconds(GetRemainingOxygenSeconds());
}

float UParadoxOxygenComponent::ConsumeOxygenSeconds(const float Seconds)
{
	if (Seconds <= 0.0f || !FMath::IsFinite(Seconds) || bIsDepleted)
	{
		return 0.0f;
	}

	SynchronizeElapsedTime(true);
	if (bIsDepleted)
	{
		return 0.0f;
	}
	const float OldRemaining = RemainingOxygenSeconds;
	RemainingOxygenSeconds = FMath::Clamp(
		RemainingOxygenSeconds - Seconds,
		0.0f,
		OxygenDurationSeconds);
	BroadcastOxygenChange(OldRemaining);
	if (RemainingOxygenSeconds <= UE_SMALL_NUMBER)
	{
		CommitDepletion();
	}
	else
	{
		RescheduleTimers();
	}
	return OldRemaining - RemainingOxygenSeconds;
}

float UParadoxOxygenComponent::RestoreOxygenSeconds(const float Seconds)
{
	if (Seconds <= 0.0f || !FMath::IsFinite(Seconds) || bIsDepleted)
	{
		return 0.0f;
	}

	SynchronizeElapsedTime(true);
	if (bIsDepleted)
	{
		return 0.0f;
	}
	const float OldRemaining = RemainingOxygenSeconds;
	RemainingOxygenSeconds = FMath::Clamp(
		RemainingOxygenSeconds + Seconds,
		0.0f,
		OxygenDurationSeconds);
	BroadcastOxygenChange(OldRemaining);
	RescheduleTimers();
	return RemainingOxygenSeconds - OldRemaining;
}

float UParadoxOxygenComponent::SetRemainingOxygenSeconds(const float Seconds)
{
	if (!FMath::IsFinite(Seconds) || bIsDepleted)
	{
		return GetRemainingOxygenSeconds();
	}

	SynchronizeElapsedTime(true);
	if (bIsDepleted)
	{
		return RemainingOxygenSeconds;
	}
	const float OldRemaining = RemainingOxygenSeconds;
	RemainingOxygenSeconds = FMath::Clamp(
		Seconds,
		0.0f,
		OxygenDurationSeconds);
	BroadcastOxygenChange(OldRemaining);
	if (RemainingOxygenSeconds <= UE_SMALL_NUMBER)
	{
		CommitDepletion();
	}
	else
	{
		RescheduleTimers();
	}
	return RemainingOxygenSeconds;
}

float UParadoxOxygenComponent::RefillOxygen()
{
	if (bIsDepleted)
	{
		return 0.0f;
	}
	return RestoreOxygenSeconds(OxygenDurationSeconds);
}

void UParadoxOxygenComponent::ResetOxygen()
{
	ClearScheduledTimers();
	const float OldRemaining = CalculateProjectedRemainingSeconds();
	const float OldSpeed = EffectiveConsumptionSpeed;
	const bool bWasBlocked = IsConsumptionBlocked();

	SpeedModifiers.Reset();
	ConsumptionBlocks.Reset();
	bIsDepleted = false;
	RemainingOxygenSeconds = OxygenDurationSeconds;
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: 0.0;
	RecalculateEffectiveConsumptionSpeed();

	if (!FMath::IsNearlyEqual(OldSpeed, EffectiveConsumptionSpeed))
	{
		OnConsumptionSpeedChanged.Broadcast(
			OldSpeed,
			EffectiveConsumptionSpeed);
	}
	if (bWasBlocked)
	{
		OnConsumptionBlockedChanged.Broadcast(false);
	}
	BroadcastOxygenChange(OldRemaining);
	OnOxygenReset.Broadcast();
	RescheduleTimers();
}

FParadoxOxygenSpeedModifierHandle
UParadoxOxygenComponent::AddConsumptionSpeedModifier(
	UObject* Source,
	const float Multiplier)
{
	FParadoxOxygenSpeedModifierHandle Handle;
	if (!IsValid(Source)
		|| !FMath::IsFinite(Multiplier)
		|| Multiplier <= 0.0f)
	{
		PARADOX_LOG_WARNING(
			TEXT("Oxygen component '%s' on '%s' rejected speed modifier source '%s' with multiplier %.3f."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(Source),
			Multiplier);
		return Handle;
	}

	SynchronizeElapsedTime(true);
	if (bIsDepleted)
	{
		return Handle;
	}
	const float OldSpeed = EffectiveConsumptionSpeed;
	Handle.Id = MakeUniqueRuntimeHandle();
	FParadoxOxygenSpeedModifierEntry& Entry = SpeedModifiers.Add(Handle.Id);
	Entry.Source = Source;
	Entry.Multiplier = Multiplier;
	RecalculateEffectiveConsumptionSpeed();
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	if (!FMath::IsNearlyEqual(OldSpeed, EffectiveConsumptionSpeed))
	{
		OnConsumptionSpeedChanged.Broadcast(
			OldSpeed,
			EffectiveConsumptionSpeed);
	}
	RescheduleTimers();
	return Handle;
}

bool UParadoxOxygenComponent::RemoveConsumptionSpeedModifier(
	const FParadoxOxygenSpeedModifierHandle Handle)
{
	if (!Handle.IsValid() || !SpeedModifiers.Contains(Handle.Id))
	{
		return false;
	}

	SynchronizeElapsedTime(true);
	const float OldSpeed = EffectiveConsumptionSpeed;
	SpeedModifiers.Remove(Handle.Id);
	RecalculateEffectiveConsumptionSpeed();
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	if (!FMath::IsNearlyEqual(OldSpeed, EffectiveConsumptionSpeed))
	{
		OnConsumptionSpeedChanged.Broadcast(
			OldSpeed,
			EffectiveConsumptionSpeed);
	}
	RescheduleTimers();
	return true;
}

FParadoxOxygenBlockHandle UParadoxOxygenComponent::AddConsumptionBlock(
	UObject* Source)
{
	FParadoxOxygenBlockHandle Handle;
	if (!IsValid(Source))
	{
		PARADOX_LOG_WARNING(
			TEXT("Oxygen component '%s' on '%s' rejected an invalid consumption-block source."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
		return Handle;
	}

	SynchronizeElapsedTime(true);
	if (bIsDepleted)
	{
		return Handle;
	}
	const bool bWasBlocked = IsConsumptionBlocked();
	Handle.Id = MakeUniqueRuntimeHandle();
	FParadoxOxygenBlockEntry& Entry = ConsumptionBlocks.Add(Handle.Id);
	Entry.Source = Source;
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	if (!bWasBlocked)
	{
		OnConsumptionBlockedChanged.Broadcast(true);
	}
	RescheduleTimers();
	return Handle;
}

bool UParadoxOxygenComponent::RemoveConsumptionBlock(
	const FParadoxOxygenBlockHandle Handle)
{
	if (!Handle.IsValid() || !ConsumptionBlocks.Contains(Handle.Id))
	{
		return false;
	}

	SynchronizeElapsedTime(true);
	const bool bWasBlocked = IsConsumptionBlocked();
	ConsumptionBlocks.Remove(Handle.Id);
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	if (bWasBlocked && !IsConsumptionBlocked())
	{
		OnConsumptionBlockedChanged.Broadcast(false);
	}
	RescheduleTimers();
	return true;
}

void UParadoxOxygenComponent::SetRunConsumptionActive(const bool bActive)
{
	const bool bShouldActivate = bActive
		&& HealthComponent.IsValid()
		&& HealthComponent->IsAlive();
	if (bRunConsumptionActive == bShouldActivate)
	{
		if (bShouldActivate)
		{
			RescheduleTimers();
		}
		return;
	}

	if (bRunConsumptionActive)
	{
		SynchronizeElapsedTime(true);
	}
	bRunConsumptionActive = bShouldActivate;
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	RescheduleTimers();
}

void UParadoxOxygenComponent::BeginPlay()
{
	Super::BeginPlay();
	SanitizeConfiguration();
	RemainingOxygenSeconds = OxygenDurationSeconds;
	EffectiveConsumptionSpeed = BaseConsumptionSpeed;
	bIsDepleted = false;
	bRunConsumptionActive = false;
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: 0.0;

	AParadoxCharacter* Character = Cast<AParadoxCharacter>(GetOwner());
	HealthComponent = Character ? Character->GetHealthComponent() : nullptr;
	if (!HealthComponent.IsValid())
	{
		PARADOX_LOG_ERROR(
			TEXT("Oxygen component '%s' on '%s' requires the owning Paradox Character's Health component; consumption remains inactive."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
		return;
	}
	HealthComponent->OnDeath.AddUniqueDynamic(
		this,
		&ThisClass::HandleHealthDeath);
}

void UParadoxOxygenComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ClearScheduledTimers();
	if (HealthComponent.IsValid())
	{
		HealthComponent->OnDeath.RemoveDynamic(
			this,
			&ThisClass::HandleHealthDeath);
	}
	HealthComponent.Reset();
	SpeedModifiers.Reset();
	ConsumptionBlocks.Reset();
	bRunConsumptionActive = false;
	Super::EndPlay(EndPlayReason);
}

void UParadoxOxygenComponent::PostLoad()
{
	Super::PostLoad();
	SanitizeConfiguration();
}

#if WITH_EDITOR
void UParadoxOxygenComponent::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SanitizeConfiguration();
	if (!HasBegunPlay())
	{
		RemainingOxygenSeconds = OxygenDurationSeconds;
		EffectiveConsumptionSpeed = BaseConsumptionSpeed;
	}
}
#endif

void UParadoxOxygenComponent::SanitizeConfiguration()
{
	if (!FMath::IsFinite(OxygenDurationSeconds)
		|| OxygenDurationSeconds <= 0.0f)
	{
		PARADOX_LOG_WARNING(
			TEXT("Oxygen component '%s' on '%s' had invalid duration %.3f; using 1 second."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			OxygenDurationSeconds);
		OxygenDurationSeconds = 1.0f;
	}
	if (!FMath::IsFinite(BaseConsumptionSpeed)
		|| BaseConsumptionSpeed <= 0.0f)
	{
		PARADOX_LOG_WARNING(
			TEXT("Oxygen component '%s' on '%s' had invalid base speed %.3f; using x1."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			BaseConsumptionSpeed);
		BaseConsumptionSpeed = 1.0f;
	}
	RemainingOxygenSeconds = FMath::Clamp(
		RemainingOxygenSeconds,
		0.0f,
		OxygenDurationSeconds);
}

float UParadoxOxygenComponent::CalculateProjectedRemainingSeconds() const
{
	if (!CanProgress())
	{
		return FMath::Clamp(
			RemainingOxygenSeconds,
			0.0f,
			OxygenDurationSeconds);
	}

	const UWorld* World = GetWorld();
	const double CurrentTime = World
		? World->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	const double Elapsed = FMath::Max(
		0.0,
		CurrentTime - LastSynchronizationTimeSeconds);
	return FMath::Clamp(
		RemainingOxygenSeconds
			- static_cast<float>(Elapsed) * EffectiveConsumptionSpeed,
		0.0f,
		OxygenDurationSeconds);
}

bool UParadoxOxygenComponent::CanProgress() const
{
	return bRunConsumptionActive
		&& !bIsDepleted
		&& !IsConsumptionBlocked()
		&& HealthComponent.IsValid()
		&& EffectiveConsumptionSpeed > UE_SMALL_NUMBER;
}

void UParadoxOxygenComponent::SynchronizeElapsedTime(
	const bool bBroadcastChanges)
{
	const float OldRemaining = RemainingOxygenSeconds;
	RemainingOxygenSeconds = CalculateProjectedRemainingSeconds();
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	if (bBroadcastChanges)
	{
		BroadcastOxygenChange(OldRemaining);
	}
	if (!bIsDepleted && RemainingOxygenSeconds <= UE_SMALL_NUMBER)
	{
		CommitDepletion();
	}
}

void UParadoxOxygenComponent::BroadcastOxygenChange(
	const float OldRemainingSeconds)
{
	if (FMath::IsNearlyEqual(OldRemainingSeconds, RemainingOxygenSeconds))
	{
		return;
	}

	const float Normalized = OxygenDurationSeconds > UE_SMALL_NUMBER
		? FMath::Clamp(
			RemainingOxygenSeconds / OxygenDurationSeconds,
			0.0f,
			1.0f)
		: 0.0f;
	OnOxygenChanged.Broadcast(
		OldRemainingSeconds,
		RemainingOxygenSeconds,
		OxygenDurationSeconds,
		Normalized);
	const int32 OldWholeSeconds = CalculateWholeSeconds(OldRemainingSeconds);
	const int32 NewWholeSeconds = CalculateWholeSeconds(RemainingOxygenSeconds);
	if (OldWholeSeconds != NewWholeSeconds)
	{
		OnWholeSecondChanged.Broadcast(
			NewWholeSeconds,
			RemainingOxygenSeconds,
			Normalized);
	}
}

void UParadoxOxygenComponent::RecalculateEffectiveConsumptionSpeed()
{
	double CalculatedSpeed = BaseConsumptionSpeed;
	for (const TPair<FGuid, FParadoxOxygenSpeedModifierEntry>& Pair : SpeedModifiers)
	{
		CalculatedSpeed *= Pair.Value.Multiplier;
		if (!FMath::IsFinite(CalculatedSpeed)
			|| CalculatedSpeed >= static_cast<double>(MAX_flt))
		{
			CalculatedSpeed = static_cast<double>(MAX_flt);
			break;
		}
	}
	EffectiveConsumptionSpeed = static_cast<float>(CalculatedSpeed);
}

void UParadoxOxygenComponent::RescheduleTimers()
{
	ClearScheduledTimers();
	if (!CanProgress())
	{
		return;
	}

	const float Remaining = RemainingOxygenSeconds;
	if (Remaining <= UE_SMALL_NUMBER)
	{
		CommitDepletion();
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float DepletionDelay = FMath::Max(
		Remaining / EffectiveConsumptionSpeed,
		MinimumTimerDelay);
	World->GetTimerManager().SetTimer(
		DepletionTimerHandle,
		this,
		&ThisClass::HandleDepletionTimer,
		DepletionDelay,
		false);

	const int32 WholeSeconds = CalculateWholeSeconds(Remaining);
	if (WholeSeconds > 0)
	{
		const float OxygenUntilNextWholeSecond = FMath::Max(
			Remaining - static_cast<float>(WholeSeconds - 1),
			MinimumTimerDelay * EffectiveConsumptionSpeed);
		const float BoundaryDelay = FMath::Max(
			OxygenUntilNextWholeSecond / EffectiveConsumptionSpeed,
			MinimumTimerDelay);
		World->GetTimerManager().SetTimer(
			WholeSecondTimerHandle,
			this,
			&ThisClass::HandleWholeSecondTimer,
			BoundaryDelay,
			false);
	}
}

void UParadoxOxygenComponent::ClearScheduledTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DepletionTimerHandle);
		World->GetTimerManager().ClearTimer(WholeSecondTimerHandle);
	}
}

void UParadoxOxygenComponent::CommitDepletion()
{
	if (bIsDepleted)
	{
		return;
	}

	const float OldRemaining = RemainingOxygenSeconds;
	RemainingOxygenSeconds = 0.0f;
	bIsDepleted = true;
	ClearScheduledTimers();
	BroadcastOxygenChange(OldRemaining);
	OnOxygenDepleted.Broadcast();

	if (!HealthComponent.IsValid())
	{
		PARADOX_LOG_ERROR(
			TEXT("Oxygen component '%s' on '%s' depleted without a valid Health component; no alternate death path exists."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
		return;
	}
	HealthComponent->Kill(
		nullptr,
		GetOwner(),
		UParadoxOxygenDepletionDamageType::StaticClass());
}

int32 UParadoxOxygenComponent::CalculateWholeSeconds(
	const float RemainingSeconds)
{
	return FMath::Max(
		0,
		FMath::CeilToInt(RemainingSeconds - KINDA_SMALL_NUMBER));
}

FGuid UParadoxOxygenComponent::MakeUniqueRuntimeHandle() const
{
	FGuid Id = FGuid::NewGuid();
	while (SpeedModifiers.Contains(Id) || ConsumptionBlocks.Contains(Id))
	{
		Id = FGuid::NewGuid();
	}
	return Id;
}

void UParadoxOxygenComponent::HandleDepletionTimer()
{
	SynchronizeElapsedTime(true);
	if (!bIsDepleted && RemainingOxygenSeconds <= UE_SMALL_NUMBER)
	{
		CommitDepletion();
	}
	else if (!bIsDepleted)
	{
		RescheduleTimers();
	}
}

void UParadoxOxygenComponent::HandleWholeSecondTimer()
{
	SynchronizeElapsedTime(true);
	if (!bIsDepleted)
	{
		RescheduleTimers();
	}
}

void UParadoxOxygenComponent::HandleHealthDeath(
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	SynchronizeElapsedTime(true);
	bRunConsumptionActive = false;
	ClearScheduledTimers();
}
