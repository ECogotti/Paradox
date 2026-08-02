#pragma once

#include "Characters/ParadoxCharacter.h"
#include "ParadoxCloneCharacter.generated.h"

class UWorldStateParticipantComponent;
class UParadoxTemporalVisionComponent;
class UParadoxCloneBehaviorCoordinatorComponent;
class UParadoxCloneInvestigationComponent;

/** Loop-owned replay avatar with explicit World State participation and no player-only components. */
UCLASS(Blueprintable)
class PARADOX_API AParadoxCloneCharacter : public AParadoxCharacter
{
	GENERATED_BODY()

public:
	AParadoxCloneCharacter();

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UWorldStateParticipantComponent* GetWorldStateParticipantComponent() const
	{
		return WorldStateParticipantComponent.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UParadoxTemporalVisionComponent* GetTemporalVisionComponent() const
	{
		return TemporalVisionComponent.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UParadoxCloneBehaviorCoordinatorComponent* GetBehaviorCoordinator() const
	{
		return BehaviorCoordinator.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UParadoxCloneInvestigationComponent* GetInvestigationComponent() const
	{
		return InvestigationComponent.Get();
	}

private:
	/**
	 * Exposes clone state to World State while leaving existence and reconstruction under the
	 * authoritative time loop.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWorldStateParticipantComponent> WorldStateParticipantComponent;

	/** Dynamic LineOfSight mesh used as the clone's authoritative temporal perception geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxTemporalVisionComponent> TemporalVisionComponent;

	/** Sole authority for Replay, Investigating, and terminal future GOAP handoff. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxCloneBehaviorCoordinatorComponent> BehaviorCoordinator;

	/** GameplayActions-only executor for movement, inspection, retarget, and recovery positioning. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxCloneInvestigationComponent> InvestigationComponent;
};
