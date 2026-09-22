#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Oxygen/ParadoxOxygenTypes.h"
#include "TimerManager.h"
#include "ParadoxOxygenComponent.generated.h"

class UDamageType;
class UParadoxHealthComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FParadoxOxygenChangedEvent,
	float, OldRemainingSeconds,
	float, NewRemainingSeconds,
	float, DurationSeconds,
	float, NormalizedOxygen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FParadoxOxygenDepletedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FParadoxOxygenResetEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxOxygenConsumptionSpeedChangedEvent,
	float, OldSpeed,
	float, NewSpeed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxOxygenConsumptionBlockedChangedEvent,
	bool, bIsBlocked);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FParadoxOxygenWholeSecondChangedEvent,
	int32, WholeSecondsRemaining,
	float, RemainingSeconds,
	float, NormalizedOxygen);

/** One source-owned runtime speed multiplier. The source is diagnostic; its owner removes the handle. */
struct FParadoxOxygenSpeedModifierEntry
{
	TWeakObjectPtr<UObject> Source;
	float Multiplier = 1.0f;
};

/** One source-owned runtime consumption blocker. */
struct FParadoxOxygenBlockEntry
{
	TWeakObjectPtr<UObject> Source;
};

/**
 * Authoritative breathable-time resource for one Paradox Character.
 *
 * State advances analytically on Unreal simulation time and is scheduled with one-shot timers.
 * Depletion has no role-specific consequences: it asks the same Character's Health to kill.
 */
UCLASS(ClassGroup = (Paradox), BlueprintType, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxOxygenComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxOxygenComponent();

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen")
	float GetOxygenDurationSeconds() const { return OxygenDurationSeconds; }

	/** Projected value at the current simulation-time sample. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen")
	float GetRemainingOxygenSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen")
	float GetNormalizedOxygen() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen")
	int32 GetWholeSecondsRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen")
	bool IsOxygenDepleted() const { return bIsDepleted; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen")
	bool IsConsumptionBlocked() const { return ConsumptionBlocks.Num() > 0; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Oxygen")
	float GetEffectiveConsumptionSpeed() const { return EffectiveConsumptionSpeed; }

	/** Returns the number of oxygen seconds actually consumed. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen")
	float ConsumeOxygenSeconds(float Seconds);

	/** Returns the number of oxygen seconds actually restored. Depletion is not reversible here. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen")
	float RestoreOxygenSeconds(float Seconds);

	/** Sets and returns the clamped remaining seconds. Depletion is not reversible here. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen")
	float SetRemainingOxygenSeconds(float Seconds);

	/** Restores a living, non-depleted resource to its configured duration. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen")
	float RefillOxygen();

	/** Starts a fresh resource lifetime and invalidates every transient modifier/block handle. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen")
	void ResetOxygen();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen|Speed")
	FParadoxOxygenSpeedModifierHandle AddConsumptionSpeedModifier(
		UObject* Source,
		float Multiplier);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen|Speed")
	bool RemoveConsumptionSpeedModifier(FParadoxOxygenSpeedModifierHandle Handle);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen|Blocking")
	FParadoxOxygenBlockHandle AddConsumptionBlock(UObject* Source);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Oxygen|Blocking")
	bool RemoveConsumptionBlock(FParadoxOxygenBlockHandle Handle);

	/** Time-loop orchestration seam. This is deliberately not a Blueprint gameplay command. */
	void SetRunConsumptionActive(bool bActive);
	bool IsRunConsumptionActive() const { return bRunConsumptionActive; }

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Oxygen|Events")
	FParadoxOxygenChangedEvent OnOxygenChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Oxygen|Events")
	FParadoxOxygenDepletedEvent OnOxygenDepleted;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Oxygen|Events")
	FParadoxOxygenResetEvent OnOxygenReset;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Oxygen|Events")
	FParadoxOxygenConsumptionSpeedChangedEvent OnConsumptionSpeedChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Oxygen|Events")
	FParadoxOxygenConsumptionBlockedChangedEvent OnConsumptionBlockedChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Oxygen|Events")
	FParadoxOxygenWholeSecondChangedEvent OnWholeSecondChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void SanitizeConfiguration();
	float CalculateProjectedRemainingSeconds() const;
	bool CanProgress() const;
	void SynchronizeElapsedTime(bool bBroadcastChanges);
	void BroadcastOxygenChange(float OldRemainingSeconds);
	void RecalculateEffectiveConsumptionSpeed();
	void RescheduleTimers();
	void ClearScheduledTimers();
	void CommitDepletion();
	static int32 CalculateWholeSeconds(float RemainingSeconds);
	FGuid MakeUniqueRuntimeHandle() const;

	void HandleDepletionTimer();
	void HandleWholeSecondTimer();

	UFUNCTION()
	void HandleHealthDeath(
		const UDamageType* DamageType,
		AController* InstigatedBy,
		AActor* DamageCauser);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Oxygen", meta = (AllowPrivateAccess = "true", ClampMin = "0.001", UIMin = "1.0", Units = "s"))
	float OxygenDurationSeconds = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Oxygen", meta = (AllowPrivateAccess = "true", ClampMin = "0.001", UIMin = "0.1"))
	float BaseConsumptionSpeed = 1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Paradox|Oxygen", meta = (AllowPrivateAccess = "true", Units = "s"))
	float RemainingOxygenSeconds = 180.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Paradox|Oxygen", meta = (AllowPrivateAccess = "true"))
	float EffectiveConsumptionSpeed = 1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Paradox|Oxygen", meta = (AllowPrivateAccess = "true"))
	bool bIsDepleted = false;

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxHealthComponent> HealthComponent;

	TMap<FGuid, FParadoxOxygenSpeedModifierEntry> SpeedModifiers;
	TMap<FGuid, FParadoxOxygenBlockEntry> ConsumptionBlocks;
	FTimerHandle DepletionTimerHandle;
	FTimerHandle WholeSecondTimerHandle;
	double LastSynchronizationTimeSeconds = 0.0;
	bool bRunConsumptionActive = false;
};

