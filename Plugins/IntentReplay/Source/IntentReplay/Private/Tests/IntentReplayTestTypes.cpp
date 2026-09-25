#include "Tests/IntentReplayTestTypes.h"

#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayActionTags.h"

void UIntentReplayPauseOnStartTestAction::OnActionStarted_Implementation()
{
	const UGameplayActionComponent* Actions = GetOwningComponent();
	AActor* Owner = Actions ? Actions->GetOwner() : nullptr;
	if (UIntentReplayComponent* Replay =
		Owner ? Owner->FindComponentByClass<UIntentReplayComponent>() : nullptr;
		Replay
			&& Replay->GetPlaybackState()
				== EIntentReplayPlaybackState::Playing)
	{
		Replay->PauseReplay();
	}
	SucceedAction(
		GameplayActionTags::Result_Success,
		TEXT("Test action completed after requesting a replay pause."));
}

double UIntentReplayTestTimeSource::CurrentTimeSeconds = 0.0;

double UIntentReplayTestTimeSource::GetTimeSeconds_Implementation(
	UObject* WorldContextObject) const
{
	return CurrentTimeSeconds;
}
