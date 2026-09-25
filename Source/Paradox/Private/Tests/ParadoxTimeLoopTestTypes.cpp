#include "Tests/ParadoxTimeLoopTestTypes.h"

void UParadoxTimeLoopActionEventObserver::HandleActionEvent(
	const FGameplayActionEvent& Event)
{
	ObservedEvents.Add(Event);
}

void AParadoxChronoSpawnStateInitializationProbe::ReceiveStateInitialized_Implementation(
	const EParadoxChronoSpawnState InitialState)
{
	++InitializationCount;
	LastInitializedState = InitialState;
}
