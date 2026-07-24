#include "TimeLoop/ParadoxTimeLoopTypes.h"

#include "Recording/IntentReplayTrack.h"
#include "TimeLoop/ParadoxChronoSpawn.h"

bool FParadoxConsolidatedTimeline::IsValid() const
{
	return TemporalIndex >= 0
		&& ::IsValid(ChronoSpawn.Get())
		&& ::IsValid(ReplayTrack.Get())
		&& ReplayTrack->IsFinalized()
		&& ReplayTrack->ValidateTrack().bValid;
}
