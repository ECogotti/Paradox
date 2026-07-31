#include "Tests/IntentReplayPerceptionTestTypes.h"

double UIntentReplayPerceptionTestTimeSource::CurrentTimeSeconds = 0.0;

double UIntentReplayPerceptionTestTimeSource::GetTimeSeconds_Implementation(
	UObject* WorldContextObject) const
{
	return CurrentTimeSeconds;
}
