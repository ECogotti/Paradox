#include "Oxygen/ParadoxOxygenComponent.h"

#include "Characters/ParadoxCharacter.h"
#include "Engine/World.h"
#include "GameModes/ParadoxGameMode.h"
#include "Health/ParadoxHealthComponent.h"
#include "Oxygen/ParadoxOxygenDepletionDamageType.h"
#include "Oxygen/ParadoxOxygenWorldSubsystem.h"
#include "Paradox.h"
#include "TimeLoop/ParadoxTimeLoopComponent.h"

namespace
{
	constexpr float MinimumTimerDelay = 0.001f;
}

UParadoxOxygenComponent::UParadoxOxygenComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

float UParadoxOxygenComponent::GetOxygenDurationSeconds() const
{
	return IsUsingSharedGlobalOxygen()
		? SharedOxygenSubsystem->GetSharedDurationSeconds()
		: OxygenDurationSeconds;
}

float UParadoxOxygenComponent::GetRemainingOxygenSeconds() const
{
	return IsUsingSharedGlobalOxygen()
		? SharedOxygenSubsystem->GetSharedRemainingSeconds()
		: CalculateProjectedRemainingSeconds();
}

float UParadoxOxygenComponent::GetNormalizedOxygen() const
{
	if (IsUsingSharedGlobalOxygen())
	{
		return SharedOxygenSubsystem->GetSharedNormalizedOxygen();
	}
	return OxygenDurationSeconds > UE_SMALL_NUMBER
		? FMath::Clamp(
			GetRemainingOxygenSeconds() / OxygenDurationSeconds,
			0.0f,
			1.0f)
		: 0.0f;
}

int32 UParadoxOxygenComponent::GetWholeSecondsRemaining() const
{
	return IsUsingSharedGlobalOxygen()
		? SharedOxygenSubsystem->GetSharedWholeSecondsRemaining()
		: CalculateWholeSeconds(GetRemainingOxygenSeconds());
}

bool UParadoxOxygenComponent::IsOxygenDepleted() const
{
	return IsUsingSharedGlobalOxygen()
		? SharedOxygenSubsystem->IsSharedOxygenDepleted()
		: bIsDepleted;
}

bool UParadoxOxygenComponent::IsConsumptionBlocked() const
{
	return IsUsingSharedGlobalOxygen()
		? SharedOxygenSubsystem->IsSharedConsumptionBlocked()
		: !ConsumptionBlocks.IsEmpty();
}

float UParadoxOxygenComponent::GetEffectiveConsumptionSpeed() const
{
	return IsUsingSharedGlobalOxygen()
		? SharedOxygenSubsystem->GetSharedEffectiveConsumptionSpeed()
		: EffectiveConsumptionSpeed;
}

EParadoxOxygenMode UParadoxOxygenComponent::GetOxygenMode() const
{
	return IsUsingSharedGlobalOxygen()
		? EParadoxOxygenMode::SharedGlobal
		: EParadoxOxygenMode::PerPawn;
}

bool UParadoxOxygenComponent::IsUsingSharedGlobalOxygen() const
{
	return SharedOxygenSubsystem.IsValid()
		&& SharedOxygenSubsystem->IsSharedGlobalEnabled();
}

float UParadoxOxygenComponent::ConsumeOxygenSeconds(const float Seconds)
{
	if (IsUsingSharedGlobalOxygen())
	{
		return SharedOxygenSubsystem->ConsumeSharedOxygenSeconds(Seconds);
	}
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
	if (IsUsingSharedGlobalOxygen())
	{
		return SharedOxygenSubsystem->RestoreSharedOxygenSeconds(Seconds);
	}
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
	if (IsUsingSharedGlobalOxygen())
	{
		return SharedOxygenSubsystem->SetSharedRemainingOxygenSeconds(Seconds);
	}
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
	if (IsUsingSharedGlobalOxygen())
	{
		return SharedOxygenSubsystem->RefillSharedOxygen();
	}
	if (bIsDepleted)
	{
		return 0.0f;
	}
	return RestoreOxygenSeconds(OxygenDurationSeconds);
}

void UParadoxOxygenComponent::ResetOxygen()
{
	if (IsUsingSharedGlobalOxygen())
	{
		SharedOxygenSubsystem->ResetSharedOxygen();
		return;
	}
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
	if (IsUsingSharedGlobalOxygen())
	{
		const FParadoxOxygenSpeedModifierHandle Handle =
			SharedOxygenSubsystem->AddSharedConsumptionSpeedModifier(
				Source,
				Multiplier);
		if (Handle.IsValid())
		{
			OwnedSharedSpeedModifierHandles.Add(Handle.Id);
		}
		return Handle;
	}
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
	if (IsUsingSharedGlobalOxygen())
	{
		const bool bRemoved = SharedOxygenSubsystem
			->RemoveSharedConsumptionSpeedModifier(Handle);
		if (bRemoved)
		{
			OwnedSharedSpeedModifierHandles.Remove(Handle.Id);
		}
		return bRemoved;
	}
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
	if (IsUsingSharedGlobalOxygen())
	{
		const FParadoxOxygenBlockHandle Handle =
			SharedOxygenSubsystem->AddSharedConsumptionBlock(Source);
		if (Handle.IsValid())
		{
			OwnedSharedBlockHandles.Add(Handle.Id);
		}
		return Handle;
	}
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
	if (IsUsingSharedGlobalOxygen())
	{
		const bool bRemoved =
			SharedOxygenSubsystem->RemoveSharedConsumptionBlock(Handle);
		if (bRemoved)
		{
			OwnedSharedBlockHandles.Remove(Handle.Id);
		}
		return bRemoved;
	}
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
	if (IsUsingSharedGlobalOxygen())
	{
		bRunConsumptionActive = bShouldActivate;
		SharedOxygenSubsystem->SetParticipantActive(
			*this,
			bShouldActivate);
		return;
	}
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

	SharedOxygenSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UParadoxOxygenWorldSubsystem>()
		: nullptr;
	if (IsUsingSharedGlobalOxygen())
	{
		const float PreviousRemainingSeconds = RemainingOxygenSeconds;
		BindSharedOxygen();
		SharedOxygenSubsystem->RegisterParticipant(*this);

		// Presentation may already observe this component before Actor BeginPlay. Publish the
		// authority handoff immediately so it does not display the per-Pawn fallback until the
		// first shared countdown event.
		OnOxygenChanged.Broadcast(
			PreviousRemainingSeconds,
			SharedOxygenSubsystem->GetSharedRemainingSeconds(),
			SharedOxygenSubsystem->GetSharedDurationSeconds(),
			SharedOxygenSubsystem->GetSharedNormalizedOxygen());
	}
	else
	{
		SharedOxygenSubsystem.Reset();
	}
}

void UParadoxOxygenComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ClearScheduledTimers();
	if (SharedOxygenSubsystem.IsValid())
	{
		SharedOxygenSubsystem->SetParticipantActive(*this, false);
		for (const FGuid& Id : OwnedSharedSpeedModifierHandles)
		{
			FParadoxOxygenSpeedModifierHandle Handle;
			Handle.Id = Id;
			SharedOxygenSubsystem->RemoveSharedConsumptionSpeedModifier(Handle);
		}
		for (const FGuid& Id : OwnedSharedBlockHandles)
		{
			FParadoxOxygenBlockHandle Handle;
			Handle.Id = Id;
			SharedOxygenSubsystem->RemoveSharedConsumptionBlock(Handle);
		}
		SharedOxygenSubsystem->UnregisterParticipant(*this);
	}
	UnbindSharedOxygen();
	SharedOxygenSubsystem.Reset();
	if (HealthComponent.IsValid())
	{
		HealthComponent->OnDeath.RemoveDynamic(
			this,
			&ThisClass::HandleHealthDeath);
	}
	HealthComponent.Reset();
	SpeedModifiers.Reset();
	ConsumptionBlocks.Reset();
	OwnedSharedSpeedModifierHandles.Reset();
	OwnedSharedBlockHandles.Reset();
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

void UParadoxOxygenComponent::BindSharedOxygen()
{
	UnbindSharedOxygen();
	if (!SharedOxygenSubsystem.IsValid())
	{
		return;
	}
	SharedOxygenSubsystem->OnGlobalOxygenChangedNative().AddUObject(
		this,
		&ThisClass::HandleSharedOxygenChanged);
	SharedOxygenSubsystem->OnGlobalWholeSecondChangedNative().AddUObject(
		this,
		&ThisClass::HandleSharedWholeSecondChanged);
	SharedOxygenSubsystem->OnGlobalConsumptionSpeedChangedNative().AddUObject(
		this,
		&ThisClass::HandleSharedConsumptionSpeedChanged);
	SharedOxygenSubsystem->OnGlobalConsumptionBlockedChangedNative().AddUObject(
		this,
		&ThisClass::HandleSharedConsumptionBlockedChanged);
	SharedOxygenSubsystem->OnGlobalOxygenDepletedNative().AddUObject(
		this,
		&ThisClass::HandleSharedOxygenDepleted);
	SharedOxygenSubsystem->OnGlobalOxygenResetNative().AddUObject(
		this,
		&ThisClass::HandleSharedOxygenReset);
}

void UParadoxOxygenComponent::UnbindSharedOxygen()
{
	if (!SharedOxygenSubsystem.IsValid())
	{
		return;
	}
	SharedOxygenSubsystem->OnGlobalOxygenChangedNative().RemoveAll(this);
	SharedOxygenSubsystem->OnGlobalWholeSecondChangedNative().RemoveAll(this);
	SharedOxygenSubsystem->OnGlobalConsumptionSpeedChangedNative().RemoveAll(this);
	SharedOxygenSubsystem->OnGlobalConsumptionBlockedChangedNative().RemoveAll(this);
	SharedOxygenSubsystem->OnGlobalOxygenDepletedNative().RemoveAll(this);
	SharedOxygenSubsystem->OnGlobalOxygenResetNative().RemoveAll(this);
}

void UParadoxOxygenComponent::HandleSharedOxygenChanged(
	const float OldRemainingSeconds,
	const float NewRemainingSeconds,
	const float DurationSeconds,
	const float NormalizedOxygen)
{
	OnOxygenChanged.Broadcast(
		OldRemainingSeconds,
		NewRemainingSeconds,
		DurationSeconds,
		NormalizedOxygen);
}

void UParadoxOxygenComponent::HandleSharedWholeSecondChanged(
	const int32 WholeSecondsRemaining,
	const float RemainingSeconds,
	const float NormalizedOxygen)
{
	OnWholeSecondChanged.Broadcast(
		WholeSecondsRemaining,
		RemainingSeconds,
		NormalizedOxygen);
}

void UParadoxOxygenComponent::HandleSharedConsumptionSpeedChanged(
	const float OldSpeed,
	const float NewSpeed)
{
	OnConsumptionSpeedChanged.Broadcast(OldSpeed, NewSpeed);
}

void UParadoxOxygenComponent::HandleSharedConsumptionBlockedChanged(
	const bool bIsBlocked)
{
	OnConsumptionBlockedChanged.Broadcast(bIsBlocked);
}

void UParadoxOxygenComponent::HandleSharedOxygenDepleted()
{
	OnOxygenDepleted.Broadcast();
	const AParadoxGameMode* GameMode = GetWorld()
		? Cast<AParadoxGameMode>(GetWorld()->GetAuthGameMode())
		: nullptr;
	const UParadoxTimeLoopComponent* TimeLoop = GameMode
		? GameMode->GetTimeLoopComponent()
		: nullptr;
	if (TimeLoop && TimeLoop->IsTimeLoopEnabled())
	{
		return;
	}
	if (HealthComponent.IsValid() && HealthComponent->IsAlive())
	{
		HealthComponent->Kill(
			nullptr,
			GetOwner(),
			UParadoxOxygenDepletionDamageType::StaticClass());
	}
}

void UParadoxOxygenComponent::HandleSharedOxygenReset()
{
	OwnedSharedSpeedModifierHandles.Reset();
	OwnedSharedBlockHandles.Reset();
	OnOxygenReset.Broadcast();
}

void UParadoxOxygenComponent::HandleHealthDeath(
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	if (IsUsingSharedGlobalOxygen())
	{
		bRunConsumptionActive = false;
		SharedOxygenSubsystem->SetParticipantActive(*this, false);
		return;
	}
	SynchronizeElapsedTime(true);
	bRunConsumptionActive = false;
	ClearScheduledTimers();
}
