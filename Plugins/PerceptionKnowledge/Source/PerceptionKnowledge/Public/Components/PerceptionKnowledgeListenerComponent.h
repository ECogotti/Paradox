#pragma once

#include "Perception/AIPerceptionComponent.h"
#include "Types/PerceptionKnowledgeTypes.h"
#include "PerceptionKnowledgeListenerComponent.generated.h"

class APawn;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class UPerceptionKnowledgeProfile;
class UPerceptionKnowledgeSourceComponent;
class UPerceptionKnowledgeWorldSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FPerceptionKnowledgeObservationEvent,
	const FPerceptionKnowledgeObservation&, Observation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FPerceptionKnowledgeKnownStateChangedEvent,
	const FPerceptionKnowledgeKnownState&, PreviousState,
	const FPerceptionKnowledgeKnownState&, CurrentState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FPerceptionKnowledgeKnownStateInvalidatedEvent,
	const FPerceptionKnowledgeKnownState&, InvalidatedState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FPerceptionKnowledgeRecentEventAddedEvent,
	const FPerceptionKnowledgeEventObservation&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FPerceptionKnowledgeEntityPerceptionChangedEvent,
	FPerceptionKnowledgeEntityId, EntityId,
	FGameplayTag, SenseTag,
	bool, bCurrentlyPerceived);

DECLARE_MULTICAST_DELEGATE_OneParam(
	FPerceptionKnowledgeObservationNativeEvent,
	const FPerceptionKnowledgeObservation&);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FPerceptionKnowledgeKnownStateChangedNativeEvent,
	const FPerceptionKnowledgeKnownState&,
	const FPerceptionKnowledgeKnownState&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FPerceptionKnowledgeKnownStateInvalidatedNativeEvent,
	const FPerceptionKnowledgeKnownState&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FPerceptionKnowledgeRecentEventAddedNativeEvent,
	const FPerceptionKnowledgeEventObservation&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FPerceptionKnowledgeEntityPerceptionChangedNativeEvent,
	FPerceptionKnowledgeEntityId,
	FGameplayTag,
	bool);
DECLARE_MULTICAST_DELEGATE(
	FPerceptionKnowledgeListenerConfigurationChangedNativeEvent);

/**
 * AI Perception listener that translates native stimuli into semantic observations and owns
 * the authoritative current knowledge for its observer.
 */
UCLASS(
	ClassGroup = (PerceptionKnowledge),
	BlueprintType,
	Blueprintable,
	meta = (BlueprintSpawnableComponent))
class PERCEPTIONKNOWLEDGE_API UPerceptionKnowledgeListenerComponent : public UAIPerceptionComponent
{
	GENERATED_BODY()

public:
	UPerceptionKnowledgeListenerComponent(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable, Category = "Perception Knowledge|Events")
	FPerceptionKnowledgeObservationEvent OnObservationProduced;

	UPROPERTY(BlueprintAssignable, Category = "Perception Knowledge|Events")
	FPerceptionKnowledgeKnownStateChangedEvent OnKnownStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Perception Knowledge|Events")
	FPerceptionKnowledgeKnownStateInvalidatedEvent OnKnownStateInvalidated;

	UPROPERTY(BlueprintAssignable, Category = "Perception Knowledge|Events")
	FPerceptionKnowledgeRecentEventAddedEvent OnRecentEventAdded;

	UPROPERTY(BlueprintAssignable, Category = "Perception Knowledge|Events")
	FPerceptionKnowledgeEntityPerceptionChangedEvent OnEntityPerceptionChanged;

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Knowledge")
	int64 GetKnowledgeRevision() const { return KnowledgeRevision; }

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Knowledge")
	bool GetKnownState(
		FPerceptionKnowledgeEntityId EntityId,
		FGameplayTag StateTag,
		FPerceptionKnowledgeKnownState& OutState) const;

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Knowledge")
	TArray<FPerceptionKnowledgeKnownState> GetKnownStatesForEntity(
		FPerceptionKnowledgeEntityId EntityId) const;

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Knowledge")
	TArray<FPerceptionKnowledgeEventObservation> GetRecentEvents() const;

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Knowledge")
	FPerceptionKnowledgeSnapshot BuildKnowledgeSnapshot(
		const FPerceptionKnowledgeSnapshotFilter& Filter) const;

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Knowledge")
	bool IsEntityCurrentlyPerceived(
		FPerceptionKnowledgeEntityId EntityId,
		FGameplayTag SenseTag) const;

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Knowledge")
	FPerceptionKnowledgeOperationResult ForgetEntity(FPerceptionKnowledgeEntityId EntityId);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Knowledge")
	FPerceptionKnowledgeOperationResult InvalidateKnownState(
		FPerceptionKnowledgeEntityId EntityId,
		FGameplayTag StateTag);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Registration")
	FPerceptionKnowledgeOperationResult SetListenerEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Registration")
	FPerceptionKnowledgeOperationResult SetListenerProfile(UPerceptionKnowledgeProfile* InProfile);

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Registration")
	FPerceptionKnowledgeOperationResult GetLastRegistrationResult() const { return LastRegistrationResult; }

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Registration")
	bool IsObservationSuspended() const { return bObservationSuspended; }

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Listener")
	AActor* GetResolvedBodyActor() const;

	/** Read-only access to the profile currently applied to native AI Perception. */
	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Listener")
	UPerceptionKnowledgeProfile* GetListenerProfile() const { return Profile.Get(); }

	/** Hearing range after listener/profile enablement has been applied; zero means disabled. */
	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Listener")
	float GetEffectiveHearingRange() const;

	/** Sight radius currently applied to native AI Perception; zero means disabled. */
	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Listener")
	float GetEffectiveSightRadius() const;

	/** Lose Sight radius currently applied to native AI Perception; zero means disabled. */
	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Listener")
	float GetEffectiveLoseSightRadius() const;

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Listener")
	bool GetListenerViewpoint(FVector& OutLocation, FVector& OutDirection) const;

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Debug")
	void SetDebugEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Debug")
	FPerceptionKnowledgeDebugFrame BuildDebugFrame() const;

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Debug")
	void DumpKnowledgeToLog() const;

	FPerceptionKnowledgeObservationNativeEvent& OnObservationProducedNative()
	{
		return ObservationProducedNative;
	}
	FPerceptionKnowledgeKnownStateChangedNativeEvent& OnKnownStateChangedNative()
	{
		return KnownStateChangedNative;
	}
	FPerceptionKnowledgeKnownStateInvalidatedNativeEvent& OnKnownStateInvalidatedNative()
	{
		return KnownStateInvalidatedNative;
	}
	FPerceptionKnowledgeRecentEventAddedNativeEvent& OnRecentEventAddedNative()
	{
		return RecentEventAddedNative;
	}
	FPerceptionKnowledgeEntityPerceptionChangedNativeEvent& OnEntityPerceptionChangedNative()
	{
		return EntityPerceptionChangedNative;
	}
	FPerceptionKnowledgeListenerConfigurationChangedNativeEvent&
	OnListenerConfigurationChangedNative()
	{
		return ListenerConfigurationChangedNative;
	}

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FRecentEventEntry
	{
		FPerceptionKnowledgeEventObservation Observation;
		double ExpirationWorldTime = 0.0;
	};

	struct FCorrelationFailureEntry
	{
		FVector Location = FVector::ZeroVector;
		double ExpirationWorldTime = 0.0;
	};

	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	UFUNCTION()
	void HandleTargetPerceptionForgotten(AActor* Actor);

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	void ApplyProfileBeforeRegistration();
	void ApplyProfileRuntimeConfiguration();
	void BindProfileChanges();
	void UnbindProfileChanges();
	void HandleProfileChanged();
	bool HasValidObservationBody() const;
	void RegisterSemanticListener();
	void UnregisterSemanticListener();
	void ClearCurrentPerception();
	void RefreshSourceStates(
		UPerceptionKnowledgeSourceComponent* Source,
		FGameplayTag SenseTag,
		float Confidence,
		const FVector& ObservationLocation,
		bool bAcquisition);
	void HandleSourceStateChanged(
		UPerceptionKnowledgeSourceComponent* Source,
		const FPerceptionKnowledgeExposedState& State,
		bool bRemoved);
	void HandleSourceUnregistered(UPerceptionKnowledgeSourceComponent* Source);
	void ReceiveEventObservation(const FPerceptionKnowledgeEventObservation& Event);
	bool StoreStateObservation(
		const FPerceptionKnowledgeStateObservation& Observation,
		bool bAcquisition);
	void SetPerceptionRelationship(
		UPerceptionKnowledgeSourceComponent* Source,
		FGameplayTag SenseTag,
		bool bPerceived);
	void CleanupRecentEvents();
	void RefreshVisibleSources();
	void UpdateTimers();
	void DrawListenerDebug();
	void BroadcastObservation(const FPerceptionKnowledgeObservation& Observation);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Configuration", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPerceptionKnowledgeProfile> Profile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Registration", meta = (AllowPrivateAccess = "true"))
	bool bListenerEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Debug", meta = (AllowPrivateAccess = "true"))
	bool bEnableDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Debug", meta = (AllowPrivateAccess = "true"))
	FPerceptionKnowledgeDebugFilter DebugFilter;

	UPROPERTY(Transient)
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(Transient)
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	UPROPERTY(Transient)
	FPerceptionKnowledgeOperationResult LastRegistrationResult;

	TMap<FPerceptionKnowledgeStateKey, FPerceptionKnowledgeKnownState> KnownStates;
	TMap<FPerceptionKnowledgeEntityId, FGameplayTagContainer> CurrentlyPerceivedSenses;
	TArray<FRecentEventEntry> RecentEvents;
	TArray<FCorrelationFailureEntry> CorrelationFailures;
	FPerceptionKnowledgeObservationNativeEvent ObservationProducedNative;
	FPerceptionKnowledgeKnownStateChangedNativeEvent KnownStateChangedNative;
	FPerceptionKnowledgeKnownStateInvalidatedNativeEvent KnownStateInvalidatedNative;
	FPerceptionKnowledgeRecentEventAddedNativeEvent RecentEventAddedNative;
	FPerceptionKnowledgeEntityPerceptionChangedNativeEvent EntityPerceptionChangedNative;
	FPerceptionKnowledgeListenerConfigurationChangedNativeEvent
		ListenerConfigurationChangedNative;
	FTimerHandle RecentEventCleanupTimerHandle;
	FTimerHandle VisibleStateRefreshTimerHandle;
	FTimerHandle DebugTimerHandle;
	FDelegateHandle ProfileChangedHandle;
	int64 KnowledgeRevision = 0;
	bool bSemanticRegistered = false;
	bool bObservationSuspended = true;

	friend class UPerceptionKnowledgeWorldSubsystem;
	friend struct FPerceptionKnowledgeTestAccessor;
};
