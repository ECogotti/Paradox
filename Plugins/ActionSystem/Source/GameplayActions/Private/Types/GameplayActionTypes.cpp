#include "Types/GameplayActionTypes.h"

bool FGameplayActionResult::IsTerminal() const
{
	return TerminalState == EGameplayActionState::Succeeded
		|| TerminalState == EGameplayActionState::Failed
		|| TerminalState == EGameplayActionState::Cancelled
		|| TerminalState == EGameplayActionState::Interrupted
		|| TerminalState == EGameplayActionState::Aborted;
}

bool FGameplayActionSubmissionResult::IsAccepted() const
{
	return Status == EGameplayActionSubmissionStatus::AcceptedStarted
		|| Status == EGameplayActionSubmissionStatus::AcceptedQueued;
}
