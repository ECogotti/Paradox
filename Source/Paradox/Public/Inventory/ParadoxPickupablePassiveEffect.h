#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ParadoxPickupablePassiveEffect.generated.h"

class AParadoxCharacter;
class AParadoxPickupableActor;

/** Replaceable passive behavior applied only while the owning pickupable is held. */
UCLASS(Abstract, BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class PARADOX_API UParadoxPickupablePassiveEffect : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Inventory|Passive Effect")
	void Apply(AParadoxCharacter* Character, AParadoxPickupableActor* Pickupable);
	virtual void Apply_Implementation(AParadoxCharacter* Character, AParadoxPickupableActor* Pickupable);

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Inventory|Passive Effect")
	void Remove(AParadoxCharacter* Character, AParadoxPickupableActor* Pickupable);
	virtual void Remove_Implementation(AParadoxCharacter* Character, AParadoxPickupableActor* Pickupable);
};
