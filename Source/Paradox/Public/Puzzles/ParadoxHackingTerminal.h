#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Interaction/ParadoxInteractionTypes.h"
#include "Puzzles/ParadoxHackingTypes.h"
#include "Types/WorldStateTypes.h"
#include "ParadoxHackingTerminal.generated.h"

class UStaticMeshComponent;
class UPuzzleEmitterComponent;
class UParadoxSelectableComponent;
class UParadoxInteractionComponent;
class USmartObjectComponent;
class UWorldStateParticipantComponent;
class UParadoxHackTerminalAction;

/** Persistent Puzzle endpoint with independent, non-spatial attempts after the normal approach. */
UCLASS(Blueprintable)
class PARADOX_API AParadoxHackingTerminal : public AActor
{
	GENERATED_BODY()
public:
	AParadoxHackingTerminal();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components") TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components") TObjectPtr<UStaticMeshComponent> TerminalMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components") TObjectPtr<UPuzzleEmitterComponent> Emitter;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components") TObjectPtr<UParadoxSelectableComponent> Selectable;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components") TObjectPtr<UParadoxInteractionComponent> Interaction;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components") TObjectPtr<USmartObjectComponent> SmartObject;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components") TObjectPtr<UWorldStateParticipantComponent> WorldStateParticipant;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hacking") FString Password = TEXT("HEART");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hacking", meta=(ClampMin="0.001")) double TimeLimitSeconds = 30;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hacking", meta=(ClampMin="0.001")) double LetterRefreshInterval = 0.5;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hacking", meta=(ClampMin="3", ClampMax="26")) int32 LettersPerButton = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hacking") bool bAsynchronousRefresh = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hacking", meta=(ClampMin="0", EditCondition="bAsynchronousRefresh")) double MaxAsyncRefreshOffset = 0.5;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hacking", meta=(Categories="Puzzle.Signal")) FGameplayTag OutputSignalTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hacking|Debug") bool bEnableDebug = false;

	UFUNCTION(BlueprintPure, Category="Hacking") EParadoxHackingTerminalState GetTerminalState() const { return PersistentState; }
	UFUNCTION(BlueprintPure, Category="Hacking") bool ValidateConfiguration(FString& OutDiagnostic) const;
	/** Submits the same spatial, journaled request as the selection UI and AI. */
	UFUNCTION(BlueprintCallable, Category="Hacking") FParadoxInteractionRequestResult StartHacking(AActor* InstigatorActor, FGameplayTag OriginTag, UObject* RequestSource);
	UFUNCTION(BlueprintPure, Category="Hacking") bool GetAttemptForInstigator(AActor* InstigatorActor, FParadoxHackingAttemptSnapshot& OutSnapshot) const;
	UFUNCTION(BlueprintPure, Category="Hacking") bool GetAttemptSnapshot(FGuid AttemptId, FParadoxHackingAttemptSnapshot& OutSnapshot) const;
	UFUNCTION(BlueprintCallable, Category="Hacking") bool SubmitLetter(AActor* InstigatorActor, FGuid AttemptId, int32 AttemptGeneration, int32 SlotIndex, const FString& DisplayedLetter);
	UFUNCTION(BlueprintCallable, Category="Hacking") bool CancelAttempt(AActor* InstigatorActor, FGuid AttemptId, int32 AttemptGeneration);
	UFUNCTION(BlueprintPure, Category="Hacking|Debug") TArray<FParadoxHackingAttemptSnapshot> GetDiagnosticSnapshots() const;
	FParadoxHackingAttemptChanged& OnAttemptChanged() { return AttemptChanged; }
	virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	struct FAttempt
	{
		FParadoxHackingAttemptSnapshot View;
		TWeakObjectPtr<AActor> Instigator;
		double StartedAt = 0;
		double EndedAt = 0;
		double Deadline = 0;
		double Interval = 0;
		double CompletionAt = 0;
		bool bSimulatedSuccess = false;
		FRandomStream Random;
	};
	bool BeginAttempt(AActor* InstigatorActor, EParadoxHackingMode Mode, double ReplayDuration, bool bReplaySuccess, FGuid& OutId, FString& OutDiagnostic, FRandomStream* TestRandom = nullptr);
	void FinishAttempt(FGuid Id, EParadoxHackingAttemptState State, bool bUsePlannedCompletionTime = false);
	void HandleGateInvalidationChanged(UPuzzleEmitterComponent* ChangedEmitter, FGameplayTag SignalTag, bool bIsInvalidated);
	void HandleEmitterInvalidated(UPuzzleEmitterComponent* ChangedEmitter);
	void FailActiveAttempts();
	void InvalidateAttempts();
	void Schedule();
	void Advance();
	void PublishSignal();
	FParadoxHackingAttemptSnapshot CopySnapshot(const FAttempt& Attempt) const;
	double Now() const;
	UFUNCTION() void HandlePreRestore(FWorldStateParticipantId ParticipantId);
	UFUNCTION() void HandlePropertiesRestored(FWorldStateParticipantId ParticipantId);
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Hacking", meta=(AllowPrivateAccess="true")) EParadoxHackingTerminalState PersistentState = EParadoxHackingTerminalState::Ready;
	TMap<FGuid, FAttempt> Attempts;
	FParadoxHackingAttemptChanged AttemptChanged;
	FTimerHandle Scheduler;
	int32 Generation = 1;
	bool bInvalidating = false;
	bool bEndingPlay = false;
	friend class UParadoxHackTerminalAction;
	friend struct FParadoxHackingTestAccess;
};
