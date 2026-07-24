#include "StateTree/GameplayActionStateTreeObserver.h"

#include "Components/GameplayActionComponent.h"

void UGameplayActionStateTreeObserver::Bind(UGameplayActionComponent& Component)
{
	Unbind();
	ActionComponent = &Component;
	DelegateHandle = Component.OnActionEndedNative().AddUObject(
		this,
		&ThisClass::HandleActionEnded);
}

void UGameplayActionStateTreeObserver::BeginSubmission()
{
	ObservedHandle = FGameplayActionHandle();
	DeferredEndedEvents.Reset();
	bHasTerminalResult = false;
	bSubmitting = true;
}

void UGameplayActionStateTreeObserver::CompleteSubmission(const FGameplayActionHandle Handle)
{
	ObservedHandle = Handle;
	bSubmitting = false;
	for (const FGameplayActionEvent& Event : DeferredEndedEvents)
	{
		if (Event.Handle == ObservedHandle && Event.bHasResult)
		{
			TerminalResult = Event.Result;
			bHasTerminalResult = true;
			break;
		}
	}
	DeferredEndedEvents.Reset();
}

void UGameplayActionStateTreeObserver::Unbind()
{
	if (UGameplayActionComponent* Component = ActionComponent.Get();
		Component && DelegateHandle.IsValid())
	{
		Component->OnActionEndedNative().Remove(DelegateHandle);
	}
	DelegateHandle.Reset();
	ActionComponent.Reset();
	bSubmitting = false;
}

void UGameplayActionStateTreeObserver::HandleActionEnded(const FGameplayActionEvent& Event)
{
	if (bSubmitting)
	{
		DeferredEndedEvents.Add(Event);
		return;
	}
	if (Event.Handle == ObservedHandle && Event.bHasResult)
	{
		TerminalResult = Event.Result;
		bHasTerminalResult = true;
	}
}
