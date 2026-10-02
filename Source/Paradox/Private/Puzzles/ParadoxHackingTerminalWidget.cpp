#include "Puzzles/ParadoxHackingTerminalWidget.h"
#include "Puzzles/ParadoxHackingLetterWidget.h"
#include "Puzzles/ParadoxHackingTerminal.h"
#include "Puzzles/ParadoxHackTerminalAction.h"
#include "CommonButtonBase.h"
#include "CommonTextBlock.h"
#include "Components/GameplayActionComponent.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "GameFramework/Pawn.h"
#include "Paradox.h"
#include "TimerManager.h"

bool UParadoxHackingTerminalWidget::ValidateBindings()
{
	const bool bValid = StartHackingButton && PasswordText && TimeRemainingProgress && LetterContainer
		&& HackingStateSwitcher && HackingStateSwitcher->GetNumWidgets() >= 3
		&& LetterContainer->CanHaveMultipleChildren() && LetterWidgetClass
		&& !LetterWidgetClass->HasAnyClassFlags(CLASS_Abstract) && InvalidLetterClass.Get() != LetterWidgetClass.Get();
	if (!bValid && !bReportedInvalidBindings)
	{
		bReportedInvalidBindings = true;
		const FString Diagnostic = FString::Printf(TEXT("Widget '%s' requires BindWidget StartHackingButton (CommonButtonBase), PasswordText (CommonTextBlock), TimeRemainingProgress (ProgressBar), LetterContainer (multi-child PanelWidget), HackingStateSwitcher (WidgetSwitcher with at least three pages) and a concrete LetterWidgetClass."), *GetName());
		PARADOX_LOG_ERROR(TEXT("%s"), *Diagnostic);
		PARADOX_LOG_ERROR(TEXT("Widget '%s' bindings: Start=%d Password=%d Time=%d Container=%d SwitcherPages=%d LetterClass='%s' Abstract=%d."), *GetName(),
			StartHackingButton != nullptr, PasswordText != nullptr, TimeRemainingProgress != nullptr, LetterContainer != nullptr,
			HackingStateSwitcher ? HackingStateSwitcher->GetNumWidgets() : 0,
			*GetNameSafe(LetterWidgetClass.Get()), LetterWidgetClass ? LetterWidgetClass->HasAnyClassFlags(CLASS_Abstract) : false);
		OnHackingViewUnavailable(Diagnostic);
	}
	if (!bValid)
	{
		if (StartHackingButton) { StartHackingButton->SetIsInteractionEnabled(false); }
		for (UParadoxHackingLetterWidget* Letter : LetterViews) { if (Letter) { Letter->UpdateLetter(INDEX_NONE, FString(), false, false); } }
	}
	return bValid;
}

void UParadoxHackingTerminalWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (StartHackingButton)
	{
		StartHackingButton->OnClicked().RemoveAll(this);
		StartHackingButton->OnClicked().AddUObject(this, &ThisClass::HandleStartClicked);
	}
	RefreshHackingView();
}

void UParadoxHackingTerminalWidget::NativeDestruct()
{
	if (StartHackingButton) { StartHackingButton->OnClicked().RemoveAll(this); }
	NativeSelectionContextCleared();
	Super::NativeDestruct();
}

void UParadoxHackingTerminalWidget::NativeSelectionContextAssigned()
{
	NativeSelectionContextCleared();
	Terminal = Cast<AParadoxHackingTerminal>(GetSelectedActor());
	if (Terminal.IsValid())
	{
		if (APawn* Requester = GetCurrentRequester()) { RequesterActions = Requester->FindComponentByClass<UGameplayActionComponent>(); }
		if (UGameplayActionComponent* Actions = RequesterActions.Get())
		{
			// Reconstruct an accepted approach as well as an already active attempt after reselection.
			TArray<FGameplayActionHandle> Handles = Actions->GetActiveActionHandles();
			Handles.Append(Actions->GetQueuedActionHandles());
			for (FGameplayActionHandle Handle : Handles)
			{
				const auto* Action = Cast<UParadoxHackTerminalAction>(Actions->GetActionInstance(Handle));
				if (Action && Action->GetInteractionTarget() == Terminal.Get()) { ViewedActionHandle = Handle; break; }
			}
			ActionEndedHandle = Actions->OnActionEndedNative().AddUObject(this, &ThisClass::HandleActionEnded);
		}
		AttemptChangedHandle = Terminal->OnAttemptChanged().AddUObject(this, &ThisClass::HandleAttemptChanged);
		if (GetWorld()) { GetWorld()->GetTimerManager().SetTimer(ViewTimer, this, &ThisClass::RefreshHackingView,
			FMath::IsFinite(ViewUpdateInterval) ? FMath::Max(0.01f, ViewUpdateInterval) : 0.05f, true); }
	}
	RefreshHackingView();
}

void UParadoxHackingTerminalWidget::ClearLetterViews()
{
	for (UParadoxHackingLetterWidget* Letter : LetterViews) { if (Letter) { Letter->OnLetterClicked.RemoveAll(this); Letter->RemoveFromParent(); } }
	LetterViews.Reset(); BuiltAttemptId.Invalidate();
}

void UParadoxHackingTerminalWidget::NativeSelectionContextCleared()
{
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(ViewTimer); }
	if (Terminal.IsValid()) { Terminal->OnAttemptChanged().Remove(AttemptChangedHandle); }
	if (RequesterActions.IsValid()) { RequesterActions->OnActionEndedNative().Remove(ActionEndedHandle); }
	ActionEndedHandle.Reset(); RequesterActions.Reset(); ViewedActionHandle = {};
	AttemptChangedHandle.Reset(); Terminal.Reset(); ViewedAttempt = {};
	ClearLetterViews();
	if (StartHackingButton) { StartHackingButton->SetIsInteractionEnabled(false); }
	if (HackingStateSwitcher) { HackingStateSwitcher->SetActiveWidgetIndex(0); }
}

void UParadoxHackingTerminalWidget::RefreshHackingView()
{
	if (!ValidateBindings()) { return; }
	AParadoxHackingTerminal* Target = Terminal.Get();
	if (!Target) { StartHackingButton->SetIsInteractionEnabled(false); HackingStateSwitcher->SetActiveWidgetIndex(0); return; }
	const FParadoxHackingAttemptSnapshot Previous = ViewedAttempt;
	const bool bHasAttempt = Target->GetAttemptForInstigator(GetCurrentRequester(), ViewedAttempt);
	const bool bActive = bHasAttempt && ViewedAttempt.State == EParadoxHackingAttemptState::Active;
	const bool bInProgress = bActive || IsApproachingTerminal();
	const bool bHacked = Target->GetTerminalState() == EParadoxHackingTerminalState::Hacked;
	const int32 PageIndex = bHacked ? 2 : bInProgress ? 1 : 0;
	if (HackingStateSwitcher->GetActiveWidgetIndex() != PageIndex) { HackingStateSwitcher->SetActiveWidgetIndex(PageIndex); }
	PasswordText->SetText(FText::FromString(bHasAttempt ? ViewedAttempt.Password : Target->Password.ToUpper()));
	TimeRemainingProgress->SetPercent(bActive && ViewedAttempt.TimeLimit > 0 ? float(ViewedAttempt.TimeRemaining / ViewedAttempt.TimeLimit) : 0);
	StartHackingButton->SetIsInteractionEnabled(!bInProgress && !bHacked
		&& Target->Emitter && !Target->Emitter->IsInvalidated(Target->OutputSignalTag)
		&& CanRequestInteraction(ParadoxGameplayTags::Interaction_HackTerminal));
	if (bHasAttempt && BuiltAttemptId != ViewedAttempt.AttemptId)
	{
		ClearLetterViews(); BuiltAttemptId = ViewedAttempt.AttemptId;
		TArray<UParadoxHackingLetterWidget*> NewViews;
		for (int32 Index = 0; Index < ViewedAttempt.Letters.Num(); ++Index)
		{
			UParadoxHackingLetterWidget* Letter = CreateWidget<UParadoxHackingLetterWidget>(this, LetterWidgetClass);
			if (!Letter || !Letter->HasRequiredBindings())
			{
				InvalidLetterClass = LetterWidgetClass.Get();
				const FString Diagnostic = TEXT("LetterWidgetClass requires BindWidget LetterButton and LetterText. Hacking continues; the view is unavailable.");
				PARADOX_LOG_ERROR(TEXT("Hacking widget '%s': %s"), *GetName(), *Diagnostic);
				OnHackingViewUnavailable(Diagnostic); ClearLetterViews(); StartHackingButton->SetIsInteractionEnabled(false); return;
			}
			if (!LetterContainer->AddChild(Letter))
			{
				const FString Diagnostic = FString::Printf(TEXT("LetterContainer '%s' could not accept slot %d. Hacking continues; the view is unavailable."), *GetNameSafe(LetterContainer), Index);
				PARADOX_LOG_ERROR(TEXT("Hacking widget '%s': %s"), *GetName(), *Diagnostic);
				OnHackingViewUnavailable(Diagnostic); ClearLetterViews(); StartHackingButton->SetIsInteractionEnabled(false); return;
			}
			Letter->OnLetterClicked.AddUObject(this, &ThisClass::HandleLetterClicked);
			LetterViews.Add(Letter); NewViews.Add(Letter);
		}
		OnLetterWidgetsRebuilt(NewViews);
	}
	if (!bHasAttempt) { ClearLetterViews(); }
	for (int32 Index = 0; Index < LetterViews.Num(); ++Index)
	{
		const auto& LetterState = ViewedAttempt.Letters[Index];
		LetterViews[Index]->UpdateLetter(Index, LetterState.CurrentLetter, LetterState.bFrozen, bActive && ViewedAttempt.Mode == EParadoxHackingMode::Interactive);
	}
	if (Previous.AttemptId == ViewedAttempt.AttemptId && ViewedAttempt.ErrorCount > Previous.ErrorCount) { OnHackingError(); }
	if (Previous.AttemptId != ViewedAttempt.AttemptId || Previous.State != ViewedAttempt.State)
	{
		if (ViewedAttempt.State == EParadoxHackingAttemptState::Success) { OnHackingSucceeded(); }
		if (ViewedAttempt.State == EParadoxHackingAttemptState::TimedOut) { OnHackingTimedOut(); }
		if (ViewedAttempt.State == EParadoxHackingAttemptState::Failed) { OnHackingFailed(); }
	}
}

void UParadoxHackingTerminalWidget::HandleStartClicked()
{
	if (ValidateBindings() && Terminal.IsValid() && StartHackingButton->GetIsEnabled() && StartHackingButton->IsInteractionEnabled())
	{
		const FParadoxInteractionRequestResult Result = RequestInteraction(ParadoxGameplayTags::Interaction_HackTerminal);
		if (Result.IsAccepted()) { ViewedActionHandle = Result.SubmissionResult.Handle; }
		RefreshHackingView();
	}
}

void UParadoxHackingTerminalWidget::HandleLetterClicked(int32 SlotIndex, const FString& Letter)
{
	if (Terminal.IsValid()) { Terminal->SubmitLetter(GetCurrentRequester(), ViewedAttempt.AttemptId, ViewedAttempt.Generation, SlotIndex, Letter); }
}

void UParadoxHackingTerminalWidget::HandleAttemptChanged(const FParadoxHackingAttemptSnapshot&) { RefreshHackingView(); }

bool UParadoxHackingTerminalWidget::IsApproachingTerminal() const
{
	const UGameplayActionComponent* Actions = RequesterActions.Get();
	const auto* Action = Actions ? Cast<UParadoxHackTerminalAction>(Actions->GetActionInstance(ViewedActionHandle)) : nullptr;
	if (!Action || Action->IsBackgroundExecution() || Action->GetInteractionTarget() != Terminal.Get()) { return false; }
	const EGameplayActionState State = Action->GetState();
	return State == EGameplayActionState::Queued || State == EGameplayActionState::Starting
		|| State == EGameplayActionState::Running || State == EGameplayActionState::Paused;
}

void UParadoxHackingTerminalWidget::HandleActionEnded(const FGameplayActionEvent& Event)
{
	if (Event.Handle == ViewedActionHandle) { ViewedActionHandle = {}; RefreshHackingView(); }
}
