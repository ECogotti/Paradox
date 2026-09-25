#include "Oxygen/ParadoxOxygenWorldSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Oxygen/ParadoxOxygenComponent.h"
#include "Paradox.h"
#include "World/ParadoxWorldInitializer.h"

namespace
{
	constexpr float MinimumTimerDelay = 0.001f;
}

void UParadoxOxygenWorldSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Configuration = FParadoxOxygenWorldConfiguration();
	bConfigurationValid = true;
	bWorldConfigurationResolved = false;
	ConfigurationDiagnostic = TEXT("No Paradox World Initializer is present; Per-Pawn Oxygen is active.");
	InitializeSharedResource();
}

void UParadoxOxygenWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	ConfigureFromWorld(InWorld);
	Super::OnWorldBeginPlay(InWorld);
}

void UParadoxOxygenWorldSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	ClearRuntimeState();
	Super::OnWorldEndPlay(InWorld);
}

void UParadoxOxygenWorldSubsystem::Deinitialize()
{
	ClearRuntimeState();
	Super::Deinitialize();
}

bool UParadoxOxygenWorldSubsystem::DoesSupportWorldType(
	const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UParadoxOxygenWorldSubsystem::ConfigureFromWorld(UWorld& InWorld)
{
	TArray<AParadoxWorldInitializer*> Initializers;
	for (TActorIterator<AParadoxWorldInitializer> It(&InWorld); It; ++It)
	{
		if (IsValid(*It))
		{
			Initializers.Add(*It);
		}
	}

	bWorldConfigurationResolved = true;
	bConfigurationValid = true;
	Configuration = FParadoxOxygenWorldConfiguration();
	if (Initializers.IsEmpty())
	{
		ConfigurationDiagnostic =
			TEXT("No Paradox World Initializer is present; Per-Pawn Oxygen is active.");
		InitializeSharedResource();
		return;
	}
	if (Initializers.Num() != 1)
	{
		bConfigurationValid = false;
		ConfigurationDiagnostic = FString::Printf(
			TEXT("World '%s' contains %d Paradox World Initializers; exactly one is allowed."),
			*GetNameSafe(&InWorld),
			Initializers.Num());
		PARADOX_LOG_ERROR(TEXT("%s"), *ConfigurationDiagnostic);
		InitializeSharedResource();
		return;
	}

	Configuration = Initializers[0]->GetOxygenConfiguration();
	if (Configuration.Mode == EParadoxOxygenMode::SharedGlobal
		&& (!FMath::IsFinite(Configuration.SharedDurationSeconds)
			|| Configuration.SharedDurationSeconds <= 0.0f
			|| !FMath::IsFinite(Configuration.SharedBaseConsumptionSpeed)
			|| Configuration.SharedBaseConsumptionSpeed <= 0.0f))
	{
		bConfigurationValid = false;
		ConfigurationDiagnostic = FString::Printf(
			TEXT("Paradox World Initializer '%s' has invalid shared Oxygen duration %.3f or base speed %.3f."),
			*GetNameSafe(Initializers[0]),
			Configuration.SharedDurationSeconds,
			Configuration.SharedBaseConsumptionSpeed);
		PARADOX_LOG_ERROR(TEXT("%s"), *ConfigurationDiagnostic);
		Configuration = FParadoxOxygenWorldConfiguration();
		InitializeSharedResource();
		return;
	}

	ConfigurationDiagnostic = FString::Printf(
		TEXT("Paradox World Initializer '%s' selected Oxygen mode %s."),
		*GetNameSafe(Initializers[0]),
		*UEnum::GetValueAsString(Configuration.Mode));
	InitializeSharedResource();
	PARADOX_LOG_INFO(TEXT("%s"), *ConfigurationDiagnostic);
}

void UParadoxOxygenWorldSubsystem::InitializeSharedResource()
{
	ClearScheduledTimers();
	Participants.Reset();
	ActiveParticipants.Reset();
	SpeedModifiers.Reset();
	ConsumptionBlocks.Reset();
	const float Duration = FMath::Max(
		Configuration.SharedDurationSeconds,
		MinimumTimerDelay);
	RemainingOxygenSeconds = Duration;
	RunCheckpointRemainingSeconds = Duration;
	bIsDepleted = false;
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: 0.0;
	RecalculateEffectiveConsumptionSpeed();
}

void UParadoxOxygenWorldSubsystem::ClearRuntimeState()
{
	ClearScheduledTimers();
	Participants.Reset();
	ActiveParticipants.Reset();
	SpeedModifiers.Reset();
	ConsumptionBlocks.Reset();
	GlobalOxygenChangedNative.Clear();
	GlobalWholeSecondChangedNative.Clear();
	GlobalConsumptionSpeedChangedNative.Clear();
	GlobalConsumptionBlockedChangedNative.Clear();
	GlobalOxygenDepletedNative.Clear();
	GlobalOxygenResetNative.Clear();
}

float UParadoxOxygenWorldSubsystem::GetSharedDurationSeconds() const
{
	return Configuration.SharedDurationSeconds;
}

float UParadoxOxygenWorldSubsystem::GetSharedRemainingSeconds() const
{
	return CalculateProjectedRemainingSeconds();
}

float UParadoxOxygenWorldSubsystem::GetSharedNormalizedOxygen() const
{
	const float Duration = GetSharedDurationSeconds();
	return Duration > UE_SMALL_NUMBER
		? FMath::Clamp(GetSharedRemainingSeconds() / Duration, 0.0f, 1.0f)
		: 0.0f;
}

int32 UParadoxOxygenWorldSubsystem::GetSharedWholeSecondsRemaining() const
{
	return CalculateWholeSeconds(GetSharedRemainingSeconds());
}

int32 UParadoxOxygenWorldSubsystem::GetActiveParticipantCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<UParadoxOxygenComponent>& Participant :
		ActiveParticipants)
	{
		if (Participant.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

void UParadoxOxygenWorldSubsystem::RegisterParticipant(
	UParadoxOxygenComponent& Component)
{
	if (IsSharedGlobalEnabled())
	{
		Participants.Add(&Component);
	}
}

void UParadoxOxygenWorldSubsystem::UnregisterParticipant(
	UParadoxOxygenComponent& Component)
{
	if (!Participants.Contains(&Component))
	{
		return;
	}
	SynchronizeElapsedTime(true);
	const float OldSpeed = EffectiveConsumptionSpeed;
	Participants.Remove(&Component);
	ActiveParticipants.Remove(&Component);
	RecalculateEffectiveConsumptionSpeed();
	if (!FMath::IsNearlyEqual(OldSpeed, EffectiveConsumptionSpeed))
	{
		GlobalConsumptionSpeedChangedNative.Broadcast(
			OldSpeed,
			EffectiveConsumptionSpeed);
	}
	RescheduleTimers();
}

void UParadoxOxygenWorldSubsystem::SetParticipantActive(
	UParadoxOxygenComponent& Component,
	const bool bActive)
{
	if (!IsSharedGlobalEnabled() || !Participants.Contains(&Component))
	{
		return;
	}
	const bool bWasActive = ActiveParticipants.Contains(&Component);
	if (bWasActive == bActive)
	{
		if (bActive)
		{
			RescheduleTimers();
		}
		return;
	}
	SynchronizeElapsedTime(true);
	const float OldSpeed = EffectiveConsumptionSpeed;
	if (bActive)
	{
		ActiveParticipants.Add(&Component);
	}
	else
	{
		ActiveParticipants.Remove(&Component);
	}
	RecalculateEffectiveConsumptionSpeed();
	if (!FMath::IsNearlyEqual(OldSpeed, EffectiveConsumptionSpeed))
	{
		GlobalConsumptionSpeedChangedNative.Broadcast(
			OldSpeed,
			EffectiveConsumptionSpeed);
	}
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	RescheduleTimers();
}

float UParadoxOxygenWorldSubsystem::ConsumeSharedOxygenSeconds(
	const float Seconds)
{
	if (!IsSharedGlobalEnabled() || Seconds <= 0.0f
		|| !FMath::IsFinite(Seconds) || bIsDepleted)
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
		GetSharedDurationSeconds());
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

float UParadoxOxygenWorldSubsystem::RestoreSharedOxygenSeconds(
	const float Seconds)
{
	if (!IsSharedGlobalEnabled() || Seconds <= 0.0f
		|| !FMath::IsFinite(Seconds) || bIsDepleted)
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
		GetSharedDurationSeconds());
	BroadcastOxygenChange(OldRemaining);
	RescheduleTimers();
	return RemainingOxygenSeconds - OldRemaining;
}

float UParadoxOxygenWorldSubsystem::SetSharedRemainingOxygenSeconds(
	const float Seconds)
{
	if (!IsSharedGlobalEnabled() || !FMath::IsFinite(Seconds) || bIsDepleted)
	{
		return GetSharedRemainingSeconds();
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
		GetSharedDurationSeconds());
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

float UParadoxOxygenWorldSubsystem::RefillSharedOxygen()
{
	return bIsDepleted
		? 0.0f
		: RestoreSharedOxygenSeconds(GetSharedDurationSeconds());
}

void UParadoxOxygenWorldSubsystem::ResetSharedOxygen()
{
	if (!IsSharedGlobalEnabled())
	{
		return;
	}
	ClearScheduledTimers();
	const float OldRemaining = CalculateProjectedRemainingSeconds();
	const float OldSpeed = EffectiveConsumptionSpeed;
	const bool bWasBlocked = IsSharedConsumptionBlocked();
	SpeedModifiers.Reset();
	ConsumptionBlocks.Reset();
	bIsDepleted = false;
	RemainingOxygenSeconds = GetSharedDurationSeconds();
	RunCheckpointRemainingSeconds = RemainingOxygenSeconds;
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: 0.0;
	RecalculateEffectiveConsumptionSpeed();
	if (!FMath::IsNearlyEqual(OldSpeed, EffectiveConsumptionSpeed))
	{
		GlobalConsumptionSpeedChangedNative.Broadcast(
			OldSpeed,
			EffectiveConsumptionSpeed);
	}
	if (bWasBlocked)
	{
		GlobalConsumptionBlockedChangedNative.Broadcast(false);
	}
	BroadcastOxygenChange(OldRemaining);
	GlobalOxygenResetNative.Broadcast();
	RescheduleTimers();
}

FParadoxOxygenSpeedModifierHandle
UParadoxOxygenWorldSubsystem::AddSharedConsumptionSpeedModifier(
	UObject* Source,
	const float Multiplier)
{
	FParadoxOxygenSpeedModifierHandle Handle;
	if (!IsSharedGlobalEnabled() || !IsValid(Source)
		|| !FMath::IsFinite(Multiplier) || Multiplier <= 0.0f)
	{
		return Handle;
	}
	SynchronizeElapsedTime(true);
	if (bIsDepleted)
	{
		return Handle;
	}
	const float OldSpeed = EffectiveConsumptionSpeed;
	Handle.Id = MakeUniqueRuntimeHandle();
	FSpeedModifierEntry& Entry = SpeedModifiers.Add(Handle.Id);
	Entry.Source = Source;
	Entry.Multiplier = Multiplier;
	RecalculateEffectiveConsumptionSpeed();
	if (!FMath::IsNearlyEqual(OldSpeed, EffectiveConsumptionSpeed))
	{
		GlobalConsumptionSpeedChangedNative.Broadcast(
			OldSpeed,
			EffectiveConsumptionSpeed);
	}
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	RescheduleTimers();
	return Handle;
}

bool UParadoxOxygenWorldSubsystem::RemoveSharedConsumptionSpeedModifier(
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
	if (!FMath::IsNearlyEqual(OldSpeed, EffectiveConsumptionSpeed))
	{
		GlobalConsumptionSpeedChangedNative.Broadcast(
			OldSpeed,
			EffectiveConsumptionSpeed);
	}
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	RescheduleTimers();
	return true;
}

FParadoxOxygenBlockHandle
UParadoxOxygenWorldSubsystem::AddSharedConsumptionBlock(UObject* Source)
{
	FParadoxOxygenBlockHandle Handle;
	if (!IsSharedGlobalEnabled() || !IsValid(Source))
	{
		return Handle;
	}
	SynchronizeElapsedTime(true);
	if (bIsDepleted)
	{
		return Handle;
	}
	const bool bWasBlocked = IsSharedConsumptionBlocked();
	Handle.Id = MakeUniqueRuntimeHandle();
	ConsumptionBlocks.Add(Handle.Id).Source = Source;
	if (!bWasBlocked)
	{
		GlobalConsumptionBlockedChangedNative.Broadcast(true);
	}
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	RescheduleTimers();
	return Handle;
}

bool UParadoxOxygenWorldSubsystem::RemoveSharedConsumptionBlock(
	const FParadoxOxygenBlockHandle Handle)
{
	if (!Handle.IsValid() || !ConsumptionBlocks.Contains(Handle.Id))
	{
		return false;
	}
	SynchronizeElapsedTime(true);
	const bool bWasBlocked = IsSharedConsumptionBlocked();
	ConsumptionBlocks.Remove(Handle.Id);
	if (bWasBlocked && !IsSharedConsumptionBlocked())
	{
		GlobalConsumptionBlockedChangedNative.Broadcast(false);
	}
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: LastSynchronizationTimeSeconds;
	RescheduleTimers();
	return true;
}

bool UParadoxOxygenWorldSubsystem::CommitCurrentAsRunCheckpoint()
{
	if (!IsSharedGlobalEnabled())
	{
		return true;
	}
	SynchronizeElapsedTime(true);
	if (bIsDepleted)
	{
		return false;
	}
	RunCheckpointRemainingSeconds = RemainingOxygenSeconds;
	ClearTransientRunState(true);
	GlobalOxygenResetNative.Broadcast();
	return true;
}

bool UParadoxOxygenWorldSubsystem::RestoreRunCheckpoint()
{
	if (!IsSharedGlobalEnabled())
	{
		return true;
	}
	ClearScheduledTimers();
	const float OldRemaining = CalculateProjectedRemainingSeconds();
	ClearTransientRunState(true);
	bIsDepleted = false;
	RemainingOxygenSeconds = FMath::Clamp(
		RunCheckpointRemainingSeconds,
		0.0f,
		GetSharedDurationSeconds());
	LastSynchronizationTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: 0.0;
	BroadcastOxygenChange(OldRemaining);
	GlobalOxygenResetNative.Broadcast();
	return true;
}

void UParadoxOxygenWorldSubsystem::ClearTransientRunState(
	const bool bBroadcastChanges)
{
	const float OldSpeed = EffectiveConsumptionSpeed;
	const bool bWasBlocked = IsSharedConsumptionBlocked();
	ActiveParticipants.Reset();
	SpeedModifiers.Reset();
	ConsumptionBlocks.Reset();
	RecalculateEffectiveConsumptionSpeed();
	if (bBroadcastChanges
		&& !FMath::IsNearlyEqual(OldSpeed, EffectiveConsumptionSpeed))
	{
		GlobalConsumptionSpeedChangedNative.Broadcast(
			OldSpeed,
			EffectiveConsumptionSpeed);
	}
	if (bBroadcastChanges && bWasBlocked)
	{
		GlobalConsumptionBlockedChangedNative.Broadcast(false);
	}
	ClearScheduledTimers();
}

float UParadoxOxygenWorldSubsystem::CalculateProjectedRemainingSeconds() const
{
	if (!CanProgress())
	{
		return FMath::Clamp(
			RemainingOxygenSeconds,
			0.0f,
			GetSharedDurationSeconds());
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
		GetSharedDurationSeconds());
}

bool UParadoxOxygenWorldSubsystem::CanProgress() const
{
	return IsSharedGlobalEnabled()
		&& !bIsDepleted
		&& !IsSharedConsumptionBlocked()
		&& GetActiveParticipantCount() > 0
		&& EffectiveConsumptionSpeed > UE_SMALL_NUMBER;
}

void UParadoxOxygenWorldSubsystem::SynchronizeElapsedTime(
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

void UParadoxOxygenWorldSubsystem::BroadcastOxygenChange(
	const float OldRemainingSeconds)
{
	if (FMath::IsNearlyEqual(OldRemainingSeconds, RemainingOxygenSeconds))
	{
		return;
	}
	const float Duration = GetSharedDurationSeconds();
	const float Normalized = Duration > UE_SMALL_NUMBER
		? FMath::Clamp(RemainingOxygenSeconds / Duration, 0.0f, 1.0f)
		: 0.0f;
	GlobalOxygenChangedNative.Broadcast(
		OldRemainingSeconds,
		RemainingOxygenSeconds,
		Duration,
		Normalized);
	const int32 OldWholeSeconds = CalculateWholeSeconds(OldRemainingSeconds);
	const int32 NewWholeSeconds = CalculateWholeSeconds(RemainingOxygenSeconds);
	if (OldWholeSeconds != NewWholeSeconds)
	{
		GlobalWholeSecondChangedNative.Broadcast(
			NewWholeSeconds,
			RemainingOxygenSeconds,
			Normalized);
	}
}

void UParadoxOxygenWorldSubsystem::RecalculateEffectiveConsumptionSpeed()
{
	double CalculatedSpeed = Configuration.SharedBaseConsumptionSpeed;
	for (const TPair<FGuid, FSpeedModifierEntry>& Pair : SpeedModifiers)
	{
		CalculatedSpeed *= Pair.Value.Multiplier;
		if (!FMath::IsFinite(CalculatedSpeed)
			|| CalculatedSpeed >= static_cast<double>(MAX_flt))
		{
			CalculatedSpeed = static_cast<double>(MAX_flt);
			break;
		}
	}
	if (Configuration.SharedConsumptionPolicy
		== EParadoxSharedOxygenConsumptionPolicy::PerActiveAvatar)
	{
		CalculatedSpeed *= static_cast<double>(GetActiveParticipantCount());
	}
	EffectiveConsumptionSpeed = static_cast<float>(CalculatedSpeed);
}

void UParadoxOxygenWorldSubsystem::RescheduleTimers()
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
		const float UntilBoundary = FMath::Max(
			Remaining - static_cast<float>(WholeSeconds - 1),
			MinimumTimerDelay * EffectiveConsumptionSpeed);
		World->GetTimerManager().SetTimer(
			WholeSecondTimerHandle,
			this,
			&ThisClass::HandleWholeSecondTimer,
			FMath::Max(
				UntilBoundary / EffectiveConsumptionSpeed,
				MinimumTimerDelay),
			false);
	}
}

void UParadoxOxygenWorldSubsystem::ClearScheduledTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DepletionTimerHandle);
		World->GetTimerManager().ClearTimer(WholeSecondTimerHandle);
	}
}

void UParadoxOxygenWorldSubsystem::CommitDepletion()
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
	GlobalOxygenDepletedNative.Broadcast();
}

void UParadoxOxygenWorldSubsystem::HandleDepletionTimer()
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

void UParadoxOxygenWorldSubsystem::HandleWholeSecondTimer()
{
	SynchronizeElapsedTime(true);
	if (!bIsDepleted)
	{
		RescheduleTimers();
	}
}

FGuid UParadoxOxygenWorldSubsystem::MakeUniqueRuntimeHandle() const
{
	FGuid Id = FGuid::NewGuid();
	while (SpeedModifiers.Contains(Id) || ConsumptionBlocks.Contains(Id))
	{
		Id = FGuid::NewGuid();
	}
	return Id;
}

int32 UParadoxOxygenWorldSubsystem::CalculateWholeSeconds(
	const float RemainingSeconds)
{
	return FMath::Max(
		0,
		FMath::CeilToInt(RemainingSeconds - KINDA_SMALL_NUMBER));
}
