#include "TimeLoop/ParadoxTemporalEntityComponent.h"

#include "Recording/IntentReplayTrack.h"

UParadoxTemporalEntityComponent::UParadoxTemporalEntityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UParadoxTemporalEntityComponent::AssignPlayer(const int32 InTemporalIndex)
{
	if (InTemporalIndex < 0)
	{
		return false;
	}

	TemporalRole = EParadoxTemporalEntityRole::Player;
	TemporalIndex = InTemporalIndex;
	AssignedReplayTrack = nullptr;
	return true;
}

bool UParadoxTemporalEntityComponent::AssignClone(
	const int32 InTemporalIndex,
	UIntentReplayTrack* InReplayTrack)
{
	if (InTemporalIndex < 0
		|| !IsValid(InReplayTrack)
		|| !InReplayTrack->IsFinalized()
		|| !InReplayTrack->ValidateTrack().bValid)
	{
		return false;
	}

	TemporalRole = EParadoxTemporalEntityRole::Clone;
	TemporalIndex = InTemporalIndex;
	AssignedReplayTrack = InReplayTrack;
	return true;
}

void UParadoxTemporalEntityComponent::ClearTemporalAssignment()
{
	TemporalRole = EParadoxTemporalEntityRole::Unassigned;
	TemporalIndex = INDEX_NONE;
	AssignedReplayTrack = nullptr;
}

