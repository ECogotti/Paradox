#pragma once

#include "Behavior/ParadoxCloneBehaviorTypes.h"
#include "Components/ActorComponent.h"
#include "Types/GameplayActionTypes.h"
#include "Types/IntentReplayPerceptionTypes.h"
#include "ParadoxCloneBehaviorCoordinatorComponent.generated.h"

class UBehaviorTreeComponent;
class UBlackboardComponent;
class UIntentReplayComponent;
class UIntentReplayObservationComponent;
class UIntentReplayTimelineBundle;
class UParadoxCloneInvestigationComponent;
class UParadoxObservationResponsePolicy;
class UParadoxReplayRecoveryPolicy;
class UPerceptionKnowledgeListenerComponent;

namespace ParadoxCloneBlackboardKeys
{
	PARADOX_API extern const FName BehaviorMode;
	PARADOX_API extern const FName InvestigationLocation;
	PARADOX_API extern const FName InvestigationSourceActor;
	PARADOX_API extern const FName InvestigationSourceEntityId;
	PARADOX_API extern const FName InvestigationJournalEntryId;
	PARADOX_API extern const FName InvestigationObservationType;
	PARADOX_API extern const FName InvestigationSemanticTag;
	PARADOX_API extern const FName InvestigationSense;
	PARADOX_API extern const FName LastModeTransitionReason;
	PARADOX_API extern const FName InvestigationResponseRuleId;
	PARADOX_API extern const FName InvestigationPriority;
	PARADOX_API extern const FName InvestigationRevision;
	PARADOX_API extern const FName HasValidInvestigation;
	PARADOX_API extern const FName ReplayResumeAvailable;
	PARADOX_API extern const FName InvestigationConfidence;
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FParadoxCloneBehaviorModeChangedEvent,
	EParadoxCloneBehaviorMode, PreviousMode,
	EParadoxCloneBehaviorMode, NewMode,
	int32, ModeRevision);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FParadoxReplayContinuityFailedEvent,
	const FParadoxInvestigationContext&, Investigation,
	const FString&, DiagnosticMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FParadoxGoapHandoffRequestedEvent);

DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FParadoxCloneBehaviorModeChangedNativeDelegate,
	EParadoxCloneBehaviorMode,
	EParadoxCloneBehaviorMode,
	int32);
DECLARE_MULTICAST_DELEGATE(
	FParadoxReplayAuthorizedNativeDelegate);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FParadoxReplayContinuityNativeDelegate,
	bool);

/**
 * Single authoritative owner of a clone's Replay / Investigating / terminal GOAP mode.
 *
 * Blackboard is a write-only mirror. Comparison, investigation, and replay components publish
 * immutable results; only this component commits a high-level state transition.
 */
UCLASS(ClassGroup = (Paradox), meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxCloneBehaviorCoordinatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxCloneBehaviorCoordinatorComponent();

	/**
	 * Initializes one playback run and, when a full bundle is supplied, starts comparison before
	 * synchronized replay authorization. A null bundle enables explicit legacy action-only mode.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Clone Behavior")
	FParadoxCloneBehaviorOperationResult InitializeForRun(
		UIntentReplayTimelineBundle* TimelineBundle);

	/** Called by the synchronized time-loop barrier; it does not itself start IntentReplay. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Clone Behavior")
	FParadoxCloneBehaviorOperationResult AuthorizeReplayStart();

	/** The sole StartReplay/ResumeReplay seam used by UBTTask_ParadoxRunIntentReplay. */
	FParadoxCloneBehaviorOperationResult StartAuthorizedReplayFromBehaviorTree();

	/** Exact-revision completion report used by the native Investigating Behavior Tree task. */
	FParadoxCloneBehaviorOperationResult CompleteInvestigation(
		const FParadoxInvestigationContext& CompletedContext,
		const FGameplayActionResult& Result);

	/** Explicit retry after continuity recovery failed; never activates GOAP. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Clone Behavior|Recovery")
	FParadoxCloneBehaviorOperationResult RetryReplayContinuity();

	/**
	 * Future irreversible handoff seam. Stops replay/investigation and the Behavior Tree safely
	 * before notifying the external system. Milestone 3 adds no gameplay caller.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Clone Behavior|GOAP")
	FParadoxCloneBehaviorOperationResult RequestEnterGoapMode();

	/** Supplies controller-owned BT/Blackboard instances; Blackboard remains a mirror only. */
	void SetBehaviorTreeContext(
		UBehaviorTreeComponent* InBehaviorTree,
		UBlackboardComponent* InBlackboard);

	UFUNCTION(BlueprintPure, Category = "Paradox|Clone Behavior")
	EParadoxCloneBehaviorMode GetCurrentMode() const { return CurrentMode; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Clone Behavior")
	int32 GetModeRevision() const { return ModeRevision; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Clone Behavior")
	bool IsReplayStartAuthorized() const { return bReplayStartAuthorized; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Clone Behavior")
	bool HasValidInvestigation() const { return CurrentInvestigation.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Clone Behavior")
	FParadoxInvestigationContext GetCurrentInvestigation() const
	{
		return CurrentInvestigation;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Clone Behavior")
	FParadoxReplayResumeContext GetReplayResumeContext() const
	{
		return ReplayResumeContext;
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Clone Behavior")
	FParadoxCloneBehaviorDebugSnapshot GetDebugSnapshot() const;

	UIntentReplayComponent* GetReplayComponent() const { return ReplayComponent; }
	UParadoxCloneInvestigationComponent* GetInvestigationComponent() const
	{
		return InvestigationComponent;
	}

	FParadoxCloneBehaviorModeChangedNativeDelegate& OnModeChangedNative()
	{
		return ModeChangedNative;
	}
	FParadoxReplayAuthorizedNativeDelegate& OnReplayAuthorizedNative()
	{
		return ReplayAuthorizedNative;
	}
	FParadoxReplayContinuityNativeDelegate& OnReplayContinuityNative()
	{
		return ReplayContinuityNative;
	}

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Clone Behavior|Events")
	FParadoxCloneBehaviorModeChangedEvent OnModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Clone Behavior|Events")
	FParadoxReplayContinuityFailedEvent OnReplayContinuityCannotBeRestored;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Clone Behavior|Events")
	FParadoxGoapHandoffRequestedEvent OnGoapHandoffRequested;

	/** Optional project Data Asset; the native CDO supplies the documented defaults when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Clone Behavior|Policy")
	TObjectPtr<UParadoxObservationResponsePolicy> ObservationResponsePolicy;

	/** Optional semantic recovery policy; the native CDO is used when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Clone Behavior|Policy")
	TObjectPtr<UParadoxReplayRecoveryPolicy> ReplayRecoveryPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Clone Behavior|Comparison")
	FIntentReplayObservationMatchOptions ObservationMatchOptions;

	/** Local half of the Paradox.CloneBehavior.Debug AND local debug gate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Clone Behavior|Debug")
	bool bEnableDebug = false;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleUnexpectedObservation(
		const FIntentReplayObservationComparisonEvent& Event);
	FParadoxInvestigationContext BuildInvestigationCandidate(
		const FIntentReplayObservationComparisonEvent& Event) const;
	FParadoxCloneBehaviorOperationResult EnterInvestigation(
		FParadoxInvestigationContext Candidate);
	FParadoxCloneBehaviorOperationResult ConsiderInvestigationReplacement(
		FParadoxInvestigationContext Candidate);
	FParadoxCloneBehaviorOperationResult ContinueReplayRecovery();
	void HandleRecoveryMoveFinished(int32 InvestigationRevision, bool bSucceeded);
	void SetMode(EParadoxCloneBehaviorMode NewMode, FName Reason);
	void UpdateBlackboardMirror();
	void BroadcastContinuityFailure(const FString& Diagnostic);
	bool IsComparisonAuthoritative(
		const FIntentReplayObservationComparisonEvent& Event) const;
	bool IsDetailedDebugEnabled() const;
	FParadoxCloneBehaviorOperationResult MakeResult(
		EParadoxCloneBehaviorOperationStatus Status,
		FString Diagnostic,
		FName Reason = NAME_None) const;
	void UnbindRuntimeDelegates();

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayComponent> ReplayComponent;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayObservationComponent> ObservationComponent;

	UPROPERTY(Transient)
	TObjectPtr<UPerceptionKnowledgeListenerComponent> PerceptionListener;

	UPROPERTY(Transient)
	TObjectPtr<UParadoxCloneInvestigationComponent> InvestigationComponent;

	UPROPERTY(Transient)
	TObjectPtr<UIntentReplayTimelineBundle> ActiveTimelineBundle;

	UPROPERTY(Transient)
	TObjectPtr<UBehaviorTreeComponent> BehaviorTreeComponent;

	UPROPERTY(Transient)
	TObjectPtr<UBlackboardComponent> BlackboardComponent;

	UPROPERTY(Transient)
	EParadoxCloneBehaviorMode CurrentMode = EParadoxCloneBehaviorMode::Replay;

	UPROPERTY(Transient)
	FParadoxInvestigationContext CurrentInvestigation;

	UPROPERTY(Transient)
	FParadoxReplayResumeContext ReplayResumeContext;

	FIntentReplayPlaybackSessionId ExpectedPlaybackSessionId;
	FIntentReplayObservationTrackId ExpectedObservationTrackId;
	FIntentReplayObservationJournalId ExpectedObservationJournalId;
	FRecordedIntentId PendingRecoveryIntentId;
	FDelegateHandle UnexpectedObservationHandle;
	FDelegateHandle RecoveryMoveFinishedHandle;
	int32 ModeRevision = 0;
	FName LastModeTransitionReason;
	bool bInitializedForRun = false;
	bool bReplayStartAuthorized = false;
	bool bReplayResumeAvailable = false;
	bool bWaitingForRecoveryMove = false;
	bool bRecoveryBlocked = false;
	bool bGoapHandoffTerminal = false;

	FParadoxCloneBehaviorModeChangedNativeDelegate ModeChangedNative;
	FParadoxReplayAuthorizedNativeDelegate ReplayAuthorizedNative;
	FParadoxReplayContinuityNativeDelegate ReplayContinuityNative;

	friend struct FParadoxCloneBehaviorTestAccessor;
};
