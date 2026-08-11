#pragma once

#include "Characters/ParadoxCharacter.h"
#include "ParadoxCloneCharacter.generated.h"

class UWorldStateParticipantComponent;
class UParadoxTemporalVisionComponent;
class UParadoxCloneBehaviorCoordinatorComponent;
class UParadoxCloneInvestigationComponent;
class USphereComponent;

/** Loop-owned replay avatar with explicit World State participation and no player-only components. */
UCLASS(Blueprintable)
class PARADOX_API AParadoxCloneCharacter : public AParadoxCharacter
{
	GENERATED_BODY()

public:
	AParadoxCloneCharacter();
	virtual void OnConstruction(const FTransform& Transform) override;

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
	USphereComponent* GetTemporalVisionCandidateSphere() const
	{
		return TemporalVisionCandidateSphere.Get();
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

	/** Collisionless dynamic LineOfSight mesh used as the clone's temporal view geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxTemporalVisionComponent> TemporalVisionComponent;

	/** Pawn-only broad phase; its radius follows the Temporal Vision cone settings automatically. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> TemporalVisionCandidateSphere;

	/** Sole authority for Replay, Investigating, and terminal future GOAP handoff. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxCloneBehaviorCoordinatorComponent> BehaviorCoordinator;

	/** GameplayActions-only executor for movement, inspection, retarget, and recovery positioning. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxCloneInvestigationComponent> InvestigationComponent;
};
