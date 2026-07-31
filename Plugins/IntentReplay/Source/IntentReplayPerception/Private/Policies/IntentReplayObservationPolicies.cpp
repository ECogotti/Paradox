#include "Policies/IntentReplayObservationPolicies.h"

#include "PerceptionKnowledgeTags.h"
#include "Types/IntentReplayPerceptionValueComparison.h"

namespace
{
	bool MatchesTagQuery(const FGameplayTagQuery& Query, const FGameplayTag Tag)
	{
		if (Query.IsEmpty())
		{
			return true;
		}
		FGameplayTagContainer Container;
		if (Tag.IsValid())
		{
			Container.AddTag(Tag);
		}
		return Query.Matches(Container);
	}
}

bool UIntentReplayObservationRecordPolicy::IsObservationAllowed_Implementation(
	const FPerceptionKnowledgeObservation& Observation,
	const FIntentReplayObservationRecordOptions& Options) const
{
	if (Observation.Type == EPerceptionKnowledgeObservationType::State)
	{
		const FPerceptionKnowledgeStateObservation& State = Observation.State;
		if (!State.Key.EntityId.IsValid()
			|| !State.Key.StateTag.IsValid()
			|| !State.SenseTag.IsValid()
			|| (State.Status == EPerceptionKnowledgeFactStatus::Known && !State.Value.IsValid())
			|| (State.Status == EPerceptionKnowledgeFactStatus::Unknown && !Options.bRecordUnknown)
			|| (State.Status == EPerceptionKnowledgeFactStatus::Invalidated
				&& !Options.bRecordInvalidated))
		{
			return false;
		}
		return MatchesTagQuery(Options.StateTagQuery, State.Key.StateTag)
			&& MatchesTagQuery(Options.SenseTagQuery, State.SenseTag);
	}

	const FPerceptionKnowledgeEventObservation& Event = Observation.Event;
	if (!Event.ObservationId.IsValid()
		|| !Event.EventTag.IsValid()
		|| !Event.SenseTag.IsValid()
		|| (Options.bRequireEventSourceIdentity && !Event.SourceEntityId.IsValid()))
	{
		return false;
	}
	return MatchesTagQuery(Options.EventTagQuery, Event.EventTag)
		&& MatchesTagQuery(Options.SenseTagQuery, Event.SenseTag)
		&& MatchesTagQuery(Options.CauseTagQuery, Event.CauseTag);
}

bool UIntentReplayObservationMatchPolicy::AreStateValuesEquivalent_Implementation(
	const FPerceptionKnowledgeValue& Expected,
	const FPerceptionKnowledgeValue& Current,
	const FIntentReplayObservationMatchOptions& Options) const
{
	if (Expected.GetType() != Current.GetType())
	{
		return false;
	}
	if (Expected.GetType() == EPerceptionKnowledgeValueType::Float)
	{
		double ExpectedValue = 0.0;
		double CurrentValue = 0.0;
		return Expected.GetFloat(ExpectedValue)
			&& Current.GetFloat(CurrentValue)
			&& FMath::IsNearlyEqual(ExpectedValue, CurrentValue, Options.FloatTolerance);
	}
	if (Expected.GetType() == EPerceptionKnowledgeValueType::Vector)
	{
		FVector ExpectedValue = FVector::ZeroVector;
		FVector CurrentValue = FVector::ZeroVector;
		return Expected.GetVector(ExpectedValue)
			&& Current.GetVector(CurrentValue)
			&& ExpectedValue.Equals(CurrentValue, Options.VectorTolerance);
	}
	return IntentReplayPerception::AreValuesExactlyEqual(Expected, Current);
}

void UIntentReplayObservationMatchPolicy::GetTimeWindow(
	const EIntentReplayRecordedObservationType Type,
	const FGameplayTag SenseTag,
	const FIntentReplayObservationMatchOptions& Options,
	double& OutEarlyTolerance,
	double& OutLateTolerance) const
{
	if (Type == EIntentReplayRecordedObservationType::State)
	{
		OutEarlyTolerance = Options.StateEarlyTolerance;
		OutLateTolerance = Options.StateLateTolerance;
		return;
	}
	if (SenseTag == PerceptionKnowledgeTags::Sense_Hearing)
	{
		OutEarlyTolerance = Options.HearingEarlyTolerance;
		OutLateTolerance = Options.HearingLateTolerance;
		return;
	}
	OutEarlyTolerance = Options.EventEarlyTolerance;
	OutLateTolerance = Options.EventLateTolerance;
}
