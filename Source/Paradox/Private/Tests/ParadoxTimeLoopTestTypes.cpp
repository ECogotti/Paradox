#include "Tests/ParadoxTimeLoopTestTypes.h"

void UParadoxTimeLoopActionEventObserver::HandleActionEvent(
	const FGameplayActionEvent& Event)
{
	ObservedEvents.Add(Event);
}
