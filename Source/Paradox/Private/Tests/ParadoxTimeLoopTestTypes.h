#pragma once

#include "CoreMinimal.h"
#include "Types/GameplayActionTypes.h"
#include "ParadoxTimeLoopTestTypes.generated.h"

/** Retains immutable action-event snapshots after transient instances have completed. */
UCLASS()
class UParadoxTimeLoopActionEventObserver : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FGameplayActionEvent> ObservedEvents;

	UFUNCTION()
	void HandleActionEvent(const FGameplayActionEvent& Event);
};
