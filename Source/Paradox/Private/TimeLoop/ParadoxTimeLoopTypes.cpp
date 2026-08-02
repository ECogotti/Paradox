#include "TimeLoop/ParadoxTimeLoopTypes.h"

#include "Recording/IntentReplayTrack.h"
#include "Data/IntentReplayTimelineBundle.h"
#include "TimeLoop/ParadoxChronoSpawn.h"

bool FParadoxConsolidatedTimeline::IsValid() const
{
	const bool bReplayValid = TemporalIndex >= 0
		&& ::IsValid(ChronoSpawn.Get())
		&& ::IsValid(ReplayTrack.Get())
		&& ReplayTrack->IsFinalized()
		&& ReplayTrack->ValidateTrack().bValid;
	if (!bReplayValid)
	{
		return false;
	}
	return !TimelineBundle
		|| (AvatarPerceptionEntityId.IsValid()
			&& TimelineBundle->ValidateBundle().bValid
			&& TimelineBundle->GetActionTrack() == ReplayTrack);
}
