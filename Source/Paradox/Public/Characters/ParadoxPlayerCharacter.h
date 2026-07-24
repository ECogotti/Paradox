#pragma once

#include "Characters/ParadoxCharacter.h"
#include "ParadoxPlayerCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UTacticalPauseActionQueueComponent;

/** Player-owned temporal avatar with presentation and tactical-planning capabilities. */
UCLASS(Blueprintable)
class PARADOX_API AParadoxPlayerCharacter : public AParadoxCharacter
{
	GENERATED_BODY()

public:
	AParadoxPlayerCharacter();

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UCameraComponent* GetTopDownCameraComponent() const { return TopDownCameraComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	USpringArmComponent* GetCameraBoom() const { return CameraBoom.Get(); }

	/** Player-only adapter for the replaceable action planned during Tactical Pause. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UTacticalPauseActionQueueComponent* GetTacticalPauseActionQueueComponent() const
	{
		return TacticalPauseActionQueueComponent.Get();
	}

private:
	/** Temporary character-mounted camera retained until the free camera milestone. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> TopDownCameraComponent;

	/** Temporary top-down camera boom retained until the free camera milestone. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Bridges player planning to the shared Gameplay Actions scheduler. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTacticalPauseActionQueueComponent> TacticalPauseActionQueueComponent;
};
