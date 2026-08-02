#pragma once

#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Types/PerceptionKnowledgeTypes.h"
#include "PerceptionKnowledgeSourceComponent.generated.h"

class UPerceptionKnowledgeListenerComponent;
class UPerceptionKnowledgeWorldSubsystem;

/** Semantic observable Source integrated with native AI Perception stimulus registration. */
UCLASS(
	ClassGroup = (PerceptionKnowledge),
	BlueprintType,
	Blueprintable,
	meta = (BlueprintSpawnableComponent))
class PERCEPTIONKNOWLEDGE_API UPerceptionKnowledgeSourceComponent
	: public UAIPerceptionStimuliSourceComponent
{
	GENERATED_BODY()

public:
	UPerceptionKnowledgeSourceComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Identity")
	FPerceptionKnowledgeEntityId GetEntityId() const { return EntityId; }

	/**
	 * Assigns an authoritative identity while this runtime Source is disabled and unregistered.
	 *
	 * The previous identity is preserved on every failure, including a live registry collision.
	 */
	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Identity")
	FPerceptionKnowledgeOperationResult AssignEntityId(
		FPerceptionKnowledgeEntityId InEntityId);

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Registration")
	bool IsSemanticallyRegistered() const { return bSemanticRegistered; }

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Registration")
	bool IsSourceEnabled() const { return bSourceEnabled; }

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Registration")
	bool IsNativeStimuliSourceRegistered() const { return bSuccessfullyRegistered; }

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Registration")
	FPerceptionKnowledgeOperationResult GetLastRegistrationResult() const { return LastRegistrationResult; }

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|State")
	int32 GetExposedStateCount() const { return RuntimeStates.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|State")
	FPerceptionKnowledgeOperationResult SetObservableState(
		FGameplayTag StateTag,
		const FPerceptionKnowledgeValue& Value);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|State")
	FPerceptionKnowledgeOperationResult SetObservableStateUnknown(FGameplayTag StateTag);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|State")
	FPerceptionKnowledgeOperationResult InvalidateObservableState(FGameplayTag StateTag);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|State")
	FPerceptionKnowledgeOperationResult RemoveObservableState(FGameplayTag StateTag);

	/** Re-gathers providers for currently observing listeners without scanning the world. */
	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|State")
	FPerceptionKnowledgeOperationResult NotifyProviderStatesChanged();

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|State")
	FPerceptionKnowledgeOperationResult GetObservableStatesForSense(
		UPerceptionKnowledgeListenerComponent* Observer,
		FGameplayTag SenseTag,
		TArray<FPerceptionKnowledgeExposedState>& OutStates) const;

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Event")
	FPerceptionKnowledgeOperationResult EmitObservableEvent(
		const FPerceptionKnowledgeEventRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Hearing")
	FPerceptionKnowledgeOperationResult EmitSemanticNoise(
		const FPerceptionKnowledgeNoiseRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Registration")
	FPerceptionKnowledgeOperationResult SetSourceEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Registration")
	FPerceptionKnowledgeOperationResult RetryRegistration();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Perception Knowledge|Identity")
	bool RegenerateEntityId();

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Debug")
	void SetDebugEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Debug")
	void DumpSourceToLog() const;

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLoad() override;
	virtual void OnComponentCreated() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
#if WITH_EDITOR
	virtual void PostEditImport() override;
#endif

private:
	void EnsureStableId(bool bForceNewId = false);
	void SynchronizeNativeSenseRegistration();
	void InitializeRuntimeStates();
	FPerceptionKnowledgeOperationResult RegisterSemanticSource();
	void UnregisterSemanticSource();
	FPerceptionKnowledgeOperationResult SetStateInternal(
		FGameplayTag StateTag,
		const FPerceptionKnowledgeValue& Value,
		EPerceptionKnowledgeFactStatus Status);
	void UpdateDebugTimer();
	void DrawSourceDebug();

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Perception Knowledge|Identity", meta = (AllowPrivateAccess = "true"))
	FPerceptionKnowledgeEntityId EntityId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Registration", meta = (AllowPrivateAccess = "true"))
	bool bRegisterForSight = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Registration", meta = (AllowPrivateAccess = "true"))
	bool bRegisterForHearing = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Registration", meta = (AllowPrivateAccess = "true"))
	bool bSourceEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|State", meta = (AllowPrivateAccess = "true"))
	TArray<FPerceptionKnowledgeExposedState> InitialObservableStates;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Debug", meta = (AllowPrivateAccess = "true"))
	bool bEnableDebug = false;

	UPROPERTY(Transient)
	FPerceptionKnowledgeOperationResult LastRegistrationResult;

	TMap<FGameplayTag, FPerceptionKnowledgeExposedState> RuntimeStates;
	FTimerHandle DebugTimerHandle;
	bool bSemanticRegistered = false;
	bool bRuntimeStatesInitialized = false;

	friend class UPerceptionKnowledgeWorldSubsystem;
	friend struct FPerceptionKnowledgeTestAccessor;
};
