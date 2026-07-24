#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Types/GameplayActionTypes.h"
#include "GameplayActionJournalSink.generated.h"

UINTERFACE(BlueprintType)
class GAMEPLAYACTIONS_API UGameplayActionJournalSink : public UInterface
{
	GENERATED_BODY()
};

/**
 * Synchronous structured journal contract. A component can register at most one implementing object.
 *
 * Accepted is written transactionally before the new instance enters scheduler collections, acquires
 * locks, or interrupts older actions. Required journals may atomically reject only that first event.
 * Later rejections remain observable through logging but cannot roll back an already committed
 * lifecycle transition. Implementations execute on the Game Thread and must not call back into the
 * component during the initial Accepted transaction.
 */
class GAMEPLAYACTIONS_API IGameplayActionJournalSink
{
	GENERATED_BODY()

public:
	/** Consumes one self-contained event snapshot and returns an immediate accepted/rejected decision. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Gameplay Actions|Journal")
	FGameplayActionJournalResult WriteGameplayActionEvent(const FGameplayActionEvent& Event);
};
