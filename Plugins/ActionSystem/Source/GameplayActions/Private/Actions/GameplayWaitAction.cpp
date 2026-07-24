#include "Actions/GameplayWaitAction.h"

#include "Engine/World.h"
#include "GameplayActionTags.h"

namespace
{
	const FName DurationParameterName(TEXT("Duration"));
}

void UGameplayWaitAction::BeginDestroy()
{
	if (UWorld* World = GetWorld(); World && WaitTimerHandle.IsValid())
	{
		World->GetTimerManager().ClearTimer(WaitTimerHandle);
	}
	WaitTimerHandle.Invalidate();
	Super::BeginDestroy();
}

bool UGameplayWaitAction::CanStartAction_Implementation(FGameplayTag& OutFailureReason, FString& OutDiagnostic) const
{
	const TValueOrError<double, EPropertyBagResult> DurationResult = GetParameters().GetValueDouble(DurationParameterName);
	if (!DurationResult.HasValue())
	{
		OutFailureReason = GameplayActionTags::Result_Failure_CannotStart;
		OutDiagnostic = TEXT("GameplayWaitAction requires a numeric Property Bag parameter named Duration.");
		return false;
	}
	if (!FMath::IsFinite(DurationResult.GetValue()) || DurationResult.GetValue() < 0.0)
	{
		OutFailureReason = GameplayActionTags::Result_Failure_CannotStart;
		OutDiagnostic = TEXT("GameplayWaitAction Duration must be finite and cannot be negative.");
		return false;
	}
	return true;
}

void UGameplayWaitAction::OnActionInit_Implementation()
{
	// Validation has already guaranteed the field and type. Caching here keeps the start hook focused
	// on execution and demonstrates that Init is safe for both immediate and queued accepted actions.
	const TValueOrError<double, EPropertyBagResult> DurationResult = GetParameters().GetValueDouble(DurationParameterName);
	CachedDurationSeconds = DurationResult.HasValue() ? DurationResult.GetValue() : 0.0;
}

void UGameplayWaitAction::OnActionStarted_Implementation()
{
	if (CachedDurationSeconds <= 0.0)
	{
		SucceedAction(GameplayActionTags::Result_Success, TEXT("Wait completed immediately."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		FailAction(GameplayActionTags::Result_Failure_CannotStart, TEXT("GameplayWaitAction has no valid World."));
		return;
	}

	TWeakObjectPtr<UGameplayWaitAction> WeakThis(this);
	World->GetTimerManager().SetTimer(
		WaitTimerHandle,
		[WeakThis]()
		{
			if (UGameplayWaitAction* Action = WeakThis.Get())
			{
				Action->HandleTimerCompleted();
			}
		},
		static_cast<float>(CachedDurationSeconds),
		false);
}

void UGameplayWaitAction::OnActionPaused_Implementation()
{
	if (UWorld* World = GetWorld(); World && WaitTimerHandle.IsValid())
	{
		World->GetTimerManager().PauseTimer(WaitTimerHandle);
	}
}

void UGameplayWaitAction::OnActionResumed_Implementation()
{
	if (UWorld* World = GetWorld(); World && WaitTimerHandle.IsValid())
	{
		World->GetTimerManager().UnPauseTimer(WaitTimerHandle);
	}
}

void UGameplayWaitAction::OnActionCleanup_Implementation()
{
	if (UWorld* World = GetWorld(); World && WaitTimerHandle.IsValid())
	{
		World->GetTimerManager().ClearTimer(WaitTimerHandle);
	}
	WaitTimerHandle.Invalidate();
	CachedDurationSeconds = 0.0;
}

void UGameplayWaitAction::HandleTimerCompleted()
{
	WaitTimerHandle.Invalidate();
	SucceedAction(GameplayActionTags::Result_Success, TEXT("Wait duration elapsed."));
}
