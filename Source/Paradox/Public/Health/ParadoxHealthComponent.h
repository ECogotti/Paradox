#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "ParadoxHealthComponent.generated.h"

class AController;
class UDamageType;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FParadoxHealthChangedEvent,
	float, OldHealth,
	float, NewHealth,
	float, MaxHealth,
	float, NormalizedHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FParadoxDamageTakenEvent,
	float, DamageApplied,
	const UDamageType*, DamageType,
	AController*, InstigatedBy,
	AActor*, DamageCauser);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxHealedEvent,
	float, HealthRestored,
	float, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FParadoxDeathEvent,
	const UDamageType*, DamageType,
	AController*, InstigatedBy,
	AActor*, DamageCauser);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FParadoxHealthResetEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FParadoxRevivedEvent);

/**
 * Authoritative HP and life-state owner for one Paradox Character.
 *
 * Damage is received exclusively through the owning Actor's native Unreal damage pipeline.
 * This component deliberately exposes no parallel ApplyDamage API.
 */
UCLASS(ClassGroup = (Paradox), BlueprintType, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxHealthComponent();

	UFUNCTION(BlueprintPure, Category = "Paradox|Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Health")
	float GetNormalizedHealth() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Health")
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Health")
	bool IsAlive() const { return !bIsDead; }

	/** Restores health on a living Character and returns the amount actually restored. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Health")
	float Heal(float Amount);

	/**
	 * Sends exactly the remaining HP through Unreal's native damage system.
	 * Returns the amount reported as applied by AActor::TakeDamage.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Health", meta = (AdvancedDisplay = "EventInstigator,DamageCauser,DamageTypeClass"))
	float Kill(
		AController* EventInstigator,
		AActor* DamageCauser,
		TSubclassOf<UDamageType> DamageTypeClass);

	/** Explicit run/life reset. Ordinary healing never revives a dead Character. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Health")
	void ResetHealth();

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Health|Events")
	FParadoxHealthChangedEvent OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Health|Events")
	FParadoxDamageTakenEvent OnDamageTaken;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Health|Events")
	FParadoxHealedEvent OnHealed;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Health|Events")
	FParadoxDeathEvent OnDeath;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Health|Events")
	FParadoxHealthResetEvent OnHealthReset;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Health|Events")
	FParadoxRevivedEvent OnRevived;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void SanitizeConfiguration();

	UFUNCTION()
	void HandleOwnerTakeAnyDamage(
		AActor* DamagedActor,
		float Damage,
		const UDamageType* DamageType,
		AController* InstigatedBy,
		AActor* DamageCauser);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Health", meta = (AllowPrivateAccess = "true", ClampMin = "0.001", UIMin = "1.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Paradox|Health", meta = (AllowPrivateAccess = "true"))
	float CurrentHealth = 100.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Paradox|Health", meta = (AllowPrivateAccess = "true"))
	bool bIsDead = false;
};
