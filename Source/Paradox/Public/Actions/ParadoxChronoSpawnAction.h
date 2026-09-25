#pragma once

#include "Interaction/ParadoxInteractionActionBase.h"
#include "ParadoxChronoSpawnAction.generated.h"

class AParadoxCharacter;
class AParadoxChronoSpawn;
class UIntentReplayComponent;
class UParadoxTimeLoopComponent;

/** Materializes one temporal avatar, waiting on an inactive puzzle Receiver when replay requires it. */
UCLASS()
class PARADOX_API UParadoxChronoSpawnAction : public UParadoxInteractionActionBase
{
	GENERATED_BODY()

protected:
	virtual bool CanSatisfyInteractionPreconditions_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual void OnActionInit_Implementation() override;
	virtual void ExecuteInteraction_Implementation() override;
	virtual void OnActionCleanup_Implementation() override;

private:
	void TryMaterialize();

	UFUNCTION()
	void HandleChronoSpawnActivationChanged(
		AParadoxChronoSpawn* ChronoSpawn,
		bool bIsActive);

	UFUNCTION()
	void HandleChronoSpawnDestroyed(AActor* DestroyedActor);

	TWeakObjectPtr<AParadoxCharacter> TemporalAvatar;
	TWeakObjectPtr<AParadoxChronoSpawn> TargetChronoSpawn;
	TWeakObjectPtr<UParadoxTimeLoopComponent> TimeLoop;
	TWeakObjectPtr<UIntentReplayComponent> PausedReplay;
	bool bWaitingForActivation = false;
	bool bMaterialized = false;
};
