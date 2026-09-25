#pragma once

#include "Actions/GameplayActionInstance.h"
#include "CoreMinimal.h"
#include "TimeLoop/ParadoxChronoSpawn.h"
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

/** Records native dispatches of the Blueprint state-initialization seam. */
UCLASS()
class AParadoxChronoSpawnStateInitializationProbe : public AParadoxChronoSpawn
{
	GENERATED_BODY()

public:
	virtual void ReceiveStateInitialized_Implementation(
		EParadoxChronoSpawnState InitialState) override;

	int32 GetInitializationCount() const { return InitializationCount; }
	EParadoxChronoSpawnState GetLastInitializedState() const
	{
		return LastInitializedState;
	}

private:
	int32 InitializationCount = 0;
	EParadoxChronoSpawnState LastInitializedState =
		EParadoxChronoSpawnState::Available;
};
