#pragma once

#include "Actions/GameplayActionInstance.h"
#include "CoreMinimal.h"
#include "Types/GameplayActionTypes.h"
#include "ParadoxTimeLoopTestTypes.generated.h"

/** Concrete no-op instance used to inspect prepared replay requests without starting gameplay. */
UCLASS()
class UParadoxTimeLoopReplayProbeAction : public UGameplayActionInstance
{
	GENERATED_BODY()
};

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
