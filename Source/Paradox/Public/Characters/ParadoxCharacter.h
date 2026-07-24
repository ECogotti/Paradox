// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ParadoxCharacter.generated.h"

class UGameplayActionComponent;
class UIntentReplayComponent;
class UEntityIdentityComponent;
class UParadoxTemporalEntityComponent;

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

	/** Generic runtime identity used by Entity Relations. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UEntityIdentityComponent> EntityIdentityComponent;

	/** Project-specific role, index and immutable replay-track assignment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxTemporalEntityComponent> TemporalEntityComponent;

public:

	/** Constructor */
	AParadoxCharacter();

	/** Returns the authoritative Gameplay Actions component. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UGameplayActionComponent* GetGameplayActionComponent() const { return GameplayActionComponent.Get(); }

	/** Returns the Intent Replay component bound to this character's action component. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UIntentReplayComponent* GetIntentReplayComponent() const { return IntentReplayComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UEntityIdentityComponent* GetEntityIdentityComponent() const { return EntityIdentityComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Components")
	UParadoxTemporalEntityComponent* GetTemporalEntityComponent() const { return TemporalEntityComponent.Get(); }

};

