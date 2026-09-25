#pragma once

#include "CoreMinimal.h"
#include "Oxygen/ParadoxOxygenTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "ParadoxOxygenWorldSubsystem.generated.h"

class UParadoxOxygenComponent;

DECLARE_MULTICAST_DELEGATE_FourParams(
	FParadoxGlobalOxygenChangedNativeEvent,
	float,
	float,
	float,
	float);
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FParadoxGlobalOxygenWholeSecondNativeEvent,
	int32,
	float,
	float);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FParadoxGlobalOxygenSpeedChangedNativeEvent,
	float,
	float);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FParadoxGlobalOxygenBlockedChangedNativeEvent,
	bool);
DECLARE_MULTICAST_DELEGATE(FParadoxGlobalOxygenSimpleNativeEvent);

/** Per-World authority for optional shared Oxygen and run-start checkpoints. */
UCLASS(BlueprintType)
class PARADOX_API UParadoxOxygenWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	bool IsConfigurationValid() const { return bConfigurationValid; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	FString GetConfigurationDiagnostic() const { return ConfigurationDiagnostic; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	EParadoxOxygenMode GetOxygenMode() const { return Configuration.Mode; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	bool IsSharedGlobalEnabled() const
	{
		return bConfigurationValid
			&& Configuration.Mode == EParadoxOxygenMode::SharedGlobal;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	FParadoxOxygenWorldConfiguration GetOxygenConfiguration() const
	{
		return Configuration;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	float GetSharedDurationSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	float GetSharedRemainingSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	float GetSharedNormalizedOxygen() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	int32 GetSharedWholeSecondsRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	float GetRunCheckpointRemainingSeconds() const
	{
		return RunCheckpointRemainingSeconds;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	float GetSharedEffectiveConsumptionSpeed() const
	{
		return EffectiveConsumptionSpeed;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	bool IsSharedConsumptionBlocked() const
	{
		return !ConsumptionBlocks.IsEmpty();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	bool IsSharedOxygenDepleted() const { return bIsDepleted; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen|World")
	int32 GetActiveParticipantCount() const;

	void RegisterParticipant(UParadoxOxygenComponent& Component);
	void UnregisterParticipant(UParadoxOxygenComponent& Component);
	void SetParticipantActive(UParadoxOxygenComponent& Component, bool bActive);

	float ConsumeSharedOxygenSeconds(float Seconds);
	float RestoreSharedOxygenSeconds(float Seconds);
	float SetSharedRemainingOxygenSeconds(float Seconds);
	float RefillSharedOxygen();
	void ResetSharedOxygen();

	FParadoxOxygenSpeedModifierHandle AddSharedConsumptionSpeedModifier(
		UObject* Source,
		float Multiplier);
	bool RemoveSharedConsumptionSpeedModifier(
		FParadoxOxygenSpeedModifierHandle Handle);
	FParadoxOxygenBlockHandle AddSharedConsumptionBlock(UObject* Source);
	bool RemoveSharedConsumptionBlock(FParadoxOxygenBlockHandle Handle);

	/** Successful Time Travel preserves the live value as the next run checkpoint. */
	bool CommitCurrentAsRunCheckpoint();
	/** Failed attempts restore the exact value captured for that run. */
	bool RestoreRunCheckpoint();

	FParadoxGlobalOxygenChangedNativeEvent& OnGlobalOxygenChangedNative()
	{
		return GlobalOxygenChangedNative;
	}
	FParadoxGlobalOxygenWholeSecondNativeEvent& OnGlobalWholeSecondChangedNative()
	{
		return GlobalWholeSecondChangedNative;
	}
	FParadoxGlobalOxygenSpeedChangedNativeEvent& OnGlobalConsumptionSpeedChangedNative()
	{
		return GlobalConsumptionSpeedChangedNative;
	}
	FParadoxGlobalOxygenBlockedChangedNativeEvent& OnGlobalConsumptionBlockedChangedNative()
	{
		return GlobalConsumptionBlockedChangedNative;
	}
	FParadoxGlobalOxygenSimpleNativeEvent& OnGlobalOxygenDepletedNative()
	{
		return GlobalOxygenDepletedNative;
	}
	FParadoxGlobalOxygenSimpleNativeEvent& OnGlobalOxygenResetNative()
	{
		return GlobalOxygenResetNative;
	}

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FSpeedModifierEntry
	{
		TWeakObjectPtr<UObject> Source;
		float Multiplier = 1.0f;
	};

	struct FBlockEntry
	{
		TWeakObjectPtr<UObject> Source;
	};

	void ConfigureFromWorld(UWorld& InWorld);
	void InitializeSharedResource();
	void ClearRuntimeState();
	void ClearTransientRunState(bool bBroadcastChanges);
	float CalculateProjectedRemainingSeconds() const;
	bool CanProgress() const;
	void SynchronizeElapsedTime(bool bBroadcastChanges);
	void BroadcastOxygenChange(float OldRemainingSeconds);
	void RecalculateEffectiveConsumptionSpeed();
	void RescheduleTimers();
	void ClearScheduledTimers();
	void CommitDepletion();
	void HandleDepletionTimer();
	void HandleWholeSecondTimer();
	FGuid MakeUniqueRuntimeHandle() const;
	static int32 CalculateWholeSeconds(float RemainingSeconds);

	FParadoxOxygenWorldConfiguration Configuration;
	bool bConfigurationValid = true;
	FString ConfigurationDiagnostic;
	bool bWorldConfigurationResolved = false;

	float RemainingOxygenSeconds = 180.0f;
	float RunCheckpointRemainingSeconds = 180.0f;
	float EffectiveConsumptionSpeed = 1.0f;
	bool bIsDepleted = false;
	double LastSynchronizationTimeSeconds = 0.0;

	TSet<TWeakObjectPtr<UParadoxOxygenComponent>> Participants;
	TSet<TWeakObjectPtr<UParadoxOxygenComponent>> ActiveParticipants;
	TMap<FGuid, FSpeedModifierEntry> SpeedModifiers;
	TMap<FGuid, FBlockEntry> ConsumptionBlocks;
	FTimerHandle DepletionTimerHandle;
	FTimerHandle WholeSecondTimerHandle;

	FParadoxGlobalOxygenChangedNativeEvent GlobalOxygenChangedNative;
	FParadoxGlobalOxygenWholeSecondNativeEvent GlobalWholeSecondChangedNative;
	FParadoxGlobalOxygenSpeedChangedNativeEvent GlobalConsumptionSpeedChangedNative;
	FParadoxGlobalOxygenBlockedChangedNativeEvent GlobalConsumptionBlockedChangedNative;
	FParadoxGlobalOxygenSimpleNativeEvent GlobalOxygenDepletedNative;
	FParadoxGlobalOxygenSimpleNativeEvent GlobalOxygenResetNative;
};
