// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ParadoxCharacter.generated.h"

class UGameplayActionComponent;
class UFootstepComponent;
class UIntentReplayComponent;
class UIntentReplayObservationComponent;
class UNiagaraComponent;
class UEntityIdentityComponent;
class UDamageType;
class UParadoxHealthComponent;
class UParadoxOxygenComponent;
class UParadoxFootstepNoiseComponent;
class UParadoxInventoryComponent;
class UParadoxTemporalEntityComponent;
class UPerceptionKnowledgeSourceComponent;

/**
 * Shared temporal-avatar foundation for player and clone characters.
 *
 * Only capabilities required by both roles live here. Player presentation/planning and clone
 * World State participation are supplied by their dedicated subclasses.
 */
UCLASS(abstract)
class PARADOX_API AParadoxCharacter : public ACharacter
{
	GENERATED_BODY()

private:

	/** Authoritative action scheduler for this character. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGameplayActionComponent> GameplayActionComponent;

	/** Records and replays semantic actions submitted through GameplayActionComponent. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIntentReplayComponent> IntentReplayComponent;

	/** Records/compares PerceptionKnowledge on the same authoritative IntentReplay clock. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIntentReplayObservationComponent> ObservationReplayComponent;

	/** Generic runtime identity used by Entity Relations. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UEntityIdentityComponent> EntityIdentityComponent;

	/** Project-specific role, index and immutable replay-track assignment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxTemporalEntityComponent> TemporalEntityComponent;

	/** Generic animation-synchronized floor contact and cosmetic feedback. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFootstepComponent> FootstepComponent;

	/** Persistent semantic identity and native Hearing stimulus source for this character. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPerceptionKnowledgeSourceComponent> PerceptionKnowledgeSourceComponent;

	/** Converts neutral footstep events into project semantic Hearing noise. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxFootstepNoiseComponent> FootstepNoiseComponent;

	/** Character-local presentation used by the recorded time-travel action. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNiagaraComponent> TimeTravelNiagaraComponent;

	/** Shared authoritative single-slot inventory used identically by players and clones. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxInventoryComponent> InventoryComponent;

public:

	/** Constructor */
	AParadoxCharacter();
	virtual float TakeDamage(
		float DamageAmount,
		const FDamageEvent& DamageEvent,
		AController* EventInstigator,
		AActor* DamageCauser) override;

	/** Returns the authoritative Gameplay Actions component. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UGameplayActionComponent* GetGameplayActionComponent() const { return GameplayActionComponent.Get(); }

	/** Returns the Intent Replay component bound to this character's action component. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UIntentReplayComponent* GetIntentReplayComponent() const { return IntentReplayComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UIntentReplayObservationComponent* GetObservationReplayComponent() const
	{
		return ObservationReplayComponent.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UEntityIdentityComponent* GetEntityIdentityComponent() const { return EntityIdentityComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UParadoxTemporalEntityComponent* GetTemporalEntityComponent() const { return TemporalEntityComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UFootstepComponent* GetFootstepComponent() const { return FootstepComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UPerceptionKnowledgeSourceComponent* GetPerceptionKnowledgeSourceComponent() const
	{
		return PerceptionKnowledgeSourceComponent.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UParadoxFootstepNoiseComponent* GetFootstepNoiseComponent() const
	{
		return FootstepNoiseComponent.Get();
	}

	/** Assign a non-looping Niagara System on the character Blueprint; null uses the immediate fallback. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UNiagaraComponent* GetTimeTravelNiagaraComponent() const
	{
		return TimeTravelNiagaraComponent.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UParadoxInventoryComponent* GetInventoryComponent() const { return InventoryComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UParadoxHealthComponent* GetHealthComponent() const { return HealthComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UParadoxOxygenComponent* GetOxygenComponent() const { return OxygenComponent.Get(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Project-role hook invoked once after Health commits the death transition. */
	virtual void HandleHealthDeath(
		const UDamageType* DamageType,
		AController* InstigatedBy,
		AActor* DamageCauser);

private:
	UFUNCTION()
	void HandleHealthDeathEvent(
		const UDamageType* DamageType,
		AController* InstigatedBy,
		AActor* DamageCauser);

	/** Shared authoritative health and life state for player and clone temporal avatars. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxHealthComponent> HealthComponent;

	/** Shared simulation-time breathable resource for player and clone temporal avatars. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxOxygenComponent> OxygenComponent;
};

