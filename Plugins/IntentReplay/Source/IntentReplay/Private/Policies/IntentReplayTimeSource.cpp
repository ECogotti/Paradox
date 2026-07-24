#include "Policies/IntentReplayTimeSource.h"

#include "Engine/World.h"

double UIntentReplayTimeSource::GetTimeSeconds_Implementation(UObject* WorldContextObject) const
{
	// Returning zero for a missing world is deterministic; component commands validate world-dependent
	// playback scheduling separately and report a structured failure there.
	return WorldContextObject && WorldContextObject->GetWorld()
		? WorldContextObject->GetWorld()->GetTimeSeconds()
		: 0.0;
}

double UIntentReplayWorldTimeSource::GetTimeSeconds_Implementation(UObject* WorldContextObject) const
{
	return Super::GetTimeSeconds_Implementation(WorldContextObject);
}
