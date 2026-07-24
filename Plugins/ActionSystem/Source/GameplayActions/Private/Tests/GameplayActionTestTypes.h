#pragma once

#include "CoreMinimal.h"
#include "Actions/GameplayActionInstance.h"
#include "Components/GameplayActionComponent.h"
#include "Interfaces/GameplayActionJournalSink.h"
#include "GameplayActionTestTypes.generated.h"

UENUM()
enum class EGameplayActionTestEnum : uint8
{
	First,
	Second
};

USTRUCT()
struct FGameplayActionTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Number = 0;

	UPROPERTY()
	FName Name;

	friend bool operator==(const FGameplayActionTestPayload& Left, const FGameplayActionTestPayload& Right)
	{
		return Left.Number == Right.Number && Left.Name == Right.Name;
	}
};

UCLASS()
class UGameplayActionTestPropertyHolder : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	bool bBoolValue = false;

	UPROPERTY()
	int32 IntValue = 0;

	UPROPERTY()
	float FloatValue = 0.0f;

	UPROPERTY()
	EGameplayActionTestEnum EnumValue = EGameplayActionTestEnum::First;

	UPROPERTY()
	FGameplayActionTestPayload StructValue;

	UPROPERTY()
	FGameplayTag TagValue;

	UPROPERTY()
	FVector VectorValue = FVector::ZeroVector;

	UPROPERTY()
	FRotator RotatorValue = FRotator::ZeroRotator;

	UPROPERTY()
	FTransform TransformValue = FTransform::Identity;

	UPROPERTY()
	TObjectPtr<UObject> ObjectValue;

	UPROPERTY()
	TSoftObjectPtr<UObject> SoftObjectValue;

	UPROPERTY()
	TSubclassOf<UObject> ClassValue;

	UPROPERTY()
	TSoftClassPtr<UObject> SoftClassValue;
};

UCLASS()
class UGameplayActionTestInstance : public UGameplayActionInstance
{
	GENERATED_BODY()

public:
	static int32 InitCount;
	static int32 StartedCount;
	static int32 PausedCount;
	static int32 ResumedCount;
	static int32 CancelledCount;
	static int32 InterruptedCount;
	static int32 AbortedCount;
	static int32 CleanupCount;
	static int32 TickCount;
	static float LastTickDeltaSeconds;
	static EGameplayActionState LastInitState;
	static bool bSubmitDuringValidation;
	static FGameplayActionRequest ValidationCallbackRequest;
	static EGameplayActionSubmissionStatus ValidationReentrantSubmissionStatus;
	static bool bAttemptOperationsDuringInit;
	static FGameplayActionRequest InitCallbackRequest;
	static EGameplayActionSubmissionStatus InitReentrantSubmissionStatus;
	static EGameplayActionOperationResult InitReentrantCancelResult;
	static EGameplayActionOperationResult InitReentrantPauseResult;

	static void ResetCounters();
	void CompleteForTest();
	void FailForTest();
	void EnableTickForTest(bool bEnabled) { SetActionTickEnabled(bEnabled); }
	void CompleteOnNextTickForTest()
	{
		bCompleteOnNextTick = true;
		SetActionTickEnabled(true);
	}

protected:
	virtual bool CanStartAction_Implementation(FGameplayTag& OutFailureReason, FString& OutDiagnostic) const override;
	virtual void OnActionInit_Implementation() override;
	virtual void OnActionStarted_Implementation() override;
	virtual void OnActionPaused_Implementation() override;
	virtual void OnActionResumed_Implementation() override;
	virtual void OnActionTick_Implementation(float DeltaSeconds) override;
	virtual void OnActionCancelled_Implementation(FGameplayTag ReasonTag) override;
	virtual void OnActionInterrupted_Implementation(FGameplayTag ReasonTag) override;
	virtual void OnActionAborted_Implementation(FGameplayTag ReasonTag) override;
	virtual void OnActionCleanup_Implementation() override;

private:
	bool bCompleteOnNextTick = false;
};

UCLASS()
class UGameplayActionTestComponent : public UGameplayActionComponent
{
	GENERATED_BODY()

public:
	void InvokeTickForTest(float DeltaSeconds)
	{
		TickComponent(DeltaSeconds, LEVELTICK_All, nullptr);
	}

	void InvokeBeginPlayForTest()
	{
		if (!HasBegunPlay())
		{
			BeginPlay();
		}
	}

	void InvokeEndPlayForTest()
	{
		if (HasBegunPlay())
		{
			EndPlay(EEndPlayReason::Destroyed);
		}
	}
};

UCLASS()
class UGameplayActionTestObserver : public UObject, public IGameplayActionJournalSink
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FGameplayActionEvent> ObservedEvents;

	UPROPERTY()
	TArray<FGameplayActionEvent> JournalEvents;

	UPROPERTY()
	TObjectPtr<UGameplayActionComponent> Component;

	UPROPERTY()
	FGameplayActionRequest CallbackRequest;

	UPROPERTY()
	FGameplayActionHandle HandleToCancel;

	UPROPERTY()
	FGameplayActionSubmissionResult CallbackSubmissionResult;

	UPROPERTY()
	EGameplayActionOperationResult CallbackCancelResult = EGameplayActionOperationResult::InvalidState;

	UPROPERTY()
	EGameplayActionSubmissionStatus ReentrantJournalSubmissionStatus = EGameplayActionSubmissionStatus::RejectedInvalidRequest;

	bool bAcceptJournal = true;
	bool bSubmitDuringJournal = false;
	bool bSubmitOnEnded = false;
	bool bCancelOtherOnStarted = false;

	UFUNCTION()
	void HandleActionEvent(const FGameplayActionEvent& Event);

	virtual FGameplayActionJournalResult WriteGameplayActionEvent_Implementation(const FGameplayActionEvent& Event) override;
};
