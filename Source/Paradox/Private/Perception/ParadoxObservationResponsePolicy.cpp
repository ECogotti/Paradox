#include "Perception/ParadoxObservationResponsePolicy.h"

#include "Paradox.h"
#include "PerceptionKnowledgeTags.h"

namespace
{
	bool MatchesRule(
		const FParadoxObservationResponseRule& Rule,
		const FParadoxInvestigationContext& Candidate,
		const EParadoxCloneBehaviorMode CurrentMode)
	{
		return Rule.bEnabled
			&& (Rule.bAnyObservationType || Rule.ObservationType == Candidate.ObservationType)
			&& (Rule.MatchResults.IsEmpty()
				|| Rule.MatchResults.Contains(Candidate.Comparison.Entry.Result))
			&& (Rule.MismatchReasons.IsEmpty()
				|| Rule.MismatchReasons.Contains(Candidate.Comparison.Entry.Reason))
			&& (!Rule.SenseTag.IsValid() || Candidate.SenseTag.MatchesTag(Rule.SenseTag))
			&& (!Rule.SemanticTag.IsValid() || Candidate.SemanticTag.MatchesTag(Rule.SemanticTag))
			&& (Rule.RequiredSourceCategories.IsEmpty()
				|| Candidate.SourceCategories.HasAll(Rule.RequiredSourceCategories))
			&& Candidate.Confidence >= Rule.MinimumConfidence
			&& (Rule.AllowedModes.IsEmpty() || Rule.AllowedModes.Contains(CurrentMode));
	}
}

UParadoxObservationResponsePolicy::UParadoxObservationResponsePolicy()
{
	FParadoxObservationResponseRule ComputerState;
	ComputerState.RuleId = TEXT("Sight.ComputerPowered.High");
	ComputerState.ObservationType = EPerceptionKnowledgeObservationType::State;
	ComputerState.MatchResults = {
		EIntentReplayObservationMatchResult::UnexpectedStateValue,
		EIntentReplayObservationMatchResult::UnexpectedStateStatus
	};
	ComputerState.SenseTag = PerceptionKnowledgeTags::Sense_Sight;
	ComputerState.SemanticTag = ParadoxGameplayTags::State_Computer_Powered;
	ComputerState.InvestigationPriority = 300;
	Rules.Add(ComputerState);

	FParadoxObservationResponseRule SightState;
	SightState.RuleId = TEXT("Sight.UnexpectedState.Default");
	SightState.ObservationType = EPerceptionKnowledgeObservationType::State;
	SightState.MatchResults = {
		EIntentReplayObservationMatchResult::UnexpectedStateValue,
		EIntentReplayObservationMatchResult::UnexpectedStateStatus
	};
	SightState.SenseTag = PerceptionKnowledgeTags::Sense_Sight;
	SightState.InvestigationPriority = 200;
	Rules.Add(SightState);

	FParadoxObservationResponseRule HearingEvent;
	HearingEvent.RuleId = TEXT("Hearing.UnexpectedEvent.Default");
	HearingEvent.ObservationType = EPerceptionKnowledgeObservationType::Event;
	HearingEvent.MatchResults = {
		EIntentReplayObservationMatchResult::UnexpectedObservation
	};
	HearingEvent.SenseTag = PerceptionKnowledgeTags::Sense_Hearing;
	HearingEvent.InvestigationPriority = 100;
	Rules.Add(HearingEvent);
}

FParadoxObservationResponseResult UParadoxObservationResponsePolicy::Evaluate(
	const FParadoxInvestigationContext& Candidate,
	const EParadoxCloneBehaviorMode CurrentMode) const
{
	FParadoxObservationResponseResult Result;
	if (CurrentMode == EParadoxCloneBehaviorMode::Goap)
	{
		Result.DiagnosticMessage = TEXT("GOAP handoff is terminal for this run.");
		return Result;
	}
	if (bIgnoreVerifiedSelfCausedObservations
		&& Candidate.Correlation.Reliability
			== EIntentReplayObservationCorrelationReliability::Verified
		&& Candidate.Correlation.Justification
			== EIntentReplayObservationJustification::ObserverCaused)
	{
		Result.DiagnosticMessage =
			TEXT("Verified observer-caused observation is ignored.");
		return Result;
	}

	const FParadoxObservationResponseRule* BestRule = nullptr;
	for (const FParadoxObservationResponseRule& Rule : Rules)
	{
		if (MatchesRule(Rule, Candidate, CurrentMode)
			&& (!BestRule
				|| Rule.InvestigationPriority > BestRule->InvestigationPriority))
		{
			BestRule = &Rule;
		}
	}
	if (!BestRule || BestRule->InvestigationPriority <= 0)
	{
		Result.DiagnosticMessage =
			TEXT("No enabled project response rule accepted this comparison.");
		return Result;
	}

	Result.Decision = EParadoxObservationResponseDecision::Investigate;
	Result.InvestigationPriority = BestRule->InvestigationPriority;
	Result.RuleId = BestRule->RuleId;
	Result.DiagnosticMessage = FString::Printf(
		TEXT("Rule '%s' assigned investigation priority %d."),
		*BestRule->RuleId.ToString(),
		BestRule->InvestigationPriority);
	return Result;
}
