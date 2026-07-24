#include "Tests/IntentReplayTestTypes.h"

double UIntentReplayTestTimeSource::CurrentTimeSeconds = 0.0;

double UIntentReplayTestTimeSource::GetTimeSeconds_Implementation(
	UObject* WorldContextObject) const
{
	return CurrentTimeSeconds;
}
