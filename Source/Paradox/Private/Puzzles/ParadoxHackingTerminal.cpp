#include "Puzzles/ParadoxHackingTerminal.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Paradox.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "Actions/GameplayActionDefinition.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	TAutoConsoleVariable<int32> HackingDebug(TEXT("Paradox.Hacking.Debug"), 0,
		TEXT("Enable hacking transition diagnostics for terminals with local bEnableDebug."), ECVF_Default);

	void ChangeDisplayedLetter(FParadoxHackingLetterSnapshot& Slot, FRandomStream& Random)
	{
		int32 CurrentIndex = INDEX_NONE;
		if (!ensureMsgf(Slot.Pool.Len() > 1 && Slot.CurrentLetter.Len() == 1
			&& Slot.Pool.FindChar(Slot.CurrentLetter[0], CurrentIndex), TEXT("Hacking letter is missing from its generated pool."))) { return; }
		// Map one uniform draw onto the pool while skipping the displayed letter, without retries.
		int32 NextIndex = Random.RandRange(0, Slot.Pool.Len() - 2);
		if (NextIndex >= CurrentIndex) { ++NextIndex; }
		Slot.CurrentLetter = Slot.Pool.Mid(NextIndex, 1);
	}
}

AParadoxHackingTerminal::AParadoxHackingTerminal()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	TerminalMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TerminalMesh"));
	TerminalMesh->SetupAttachment(SceneRoot);
	Emitter = CreateDefaultSubobject<UPuzzleEmitterComponent>(TEXT("Emitter"));
	Selectable = CreateDefaultSubobject<UParadoxSelectableComponent>(TEXT("Selectable"));
	Selectable->bShowInteractionCellsWhenSelected = true;
	Interaction = CreateDefaultSubobject<UParadoxInteractionComponent>(TEXT("Interaction"));
	SmartObject = CreateDefaultSubobject<USmartObjectComponent>(TEXT("SmartObject"));
	SmartObject->SetupAttachment(SceneRoot);
	WorldStateParticipant = CreateDefaultSubobject<UWorldStateParticipantComponent>(TEXT("WorldStateParticipant"));
	WorldStateParticipant->bCaptureActorTransform = true;
	WorldStateParticipant->bCaptureExistence = false;
	WorldStateParticipant->ExistencePolicy = EWorldStateExistencePolicy::ExistingOnly;
	FWorldStatePropertySelection& Selection = WorldStateParticipant->CapturedProperties.AddDefaulted_GetRef();
	Selection.CaptureSourceId = FWorldStateCaptureSourceId::OwnerActor();
	Selection.PropertyName = GET_MEMBER_NAME_CHECKED(AParadoxHackingTerminal, PersistentState);
	OutputSignalTag = ParadoxGameplayTags::Puzzle_Signal_HackingTerminal_Hacked;

	static ConstructorHelpers::FObjectFinder<USmartObjectDefinition> SmartDefinition(
		TEXT("/Game/Data/Puzzles/SOD_ParadoxHackingTerminalSmartObject.SOD_ParadoxHackingTerminalSmartObject"));
	if (SmartDefinition.Succeeded()) { SmartObject->SetDefinition(SmartDefinition.Object); }
	static ConstructorHelpers::FObjectFinder<UGameplayActionDefinition> ActionDefinition(
		TEXT("/Game/Data/GameplayActions/DA_ParadoxHackTerminal.DA_ParadoxHackTerminal"));
	if (ActionDefinition.Succeeded())
	{
		FParadoxInteractionDefinition& Definition = Interaction->InteractionDefinitions.AddDefaulted_GetRef();
		Definition.InteractionTag = ParadoxGameplayTags::Interaction_HackTerminal;
		Definition.GameplayActionDefinition = ActionDefinition.Object;
	}
}

void AParadoxHackingTerminal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Password = Password.ToUpper();
}

bool AParadoxHackingTerminal::ValidateConfiguration(FString& OutDiagnostic) const
{
	OutDiagnostic.Reset();
	if (Password.IsEmpty()) { OutDiagnostic = TEXT("Password must contain at least one A-Z letter."); }
	for (TCHAR Letter : Password.ToUpper())
	{
		if (Letter < TEXT('A') || Letter > TEXT('Z')) { OutDiagnostic = TEXT("Password accepts only A-Z letters, without spaces or accented characters."); break; }
	}
	if (!FMath::IsFinite(TimeLimitSeconds) || TimeLimitSeconds <= 0 || !FMath::IsFinite(LetterRefreshInterval) || LetterRefreshInterval <= 0)
	{
		OutDiagnostic = TEXT("TimeLimitSeconds and LetterRefreshInterval must be finite and positive.");
	}
	if (LettersPerButton < 3 || LettersPerButton > 26) { OutDiagnostic = TEXT("LettersPerButton must be between 3 and 26."); }
	if (bAsynchronousRefresh && (!FMath::IsFinite(MaxAsyncRefreshOffset) || MaxAsyncRefreshOffset < 0)) { OutDiagnostic = TEXT("MaxAsyncRefreshOffset must be finite and non-negative."); }
	if (!OutputSignalTag.IsValid() || !Emitter) { OutDiagnostic = TEXT("A valid OutputSignalTag and native Emitter are required."); }
	if (!SceneRoot || GetRootComponent() != SceneRoot || !TerminalMesh || !Selectable || !Interaction || !SmartObject || !WorldStateParticipant)
	{
		OutDiagnostic = TEXT("Terminal requires its native SceneRoot, mesh, Selectable, Interaction, SmartObject and WorldStateParticipant components.");
	}
	else if (!SmartObject->GetDefinition() || !Interaction->InteractionDefinitions.ContainsByPredicate([](const FParadoxInteractionDefinition& Definition)
	{
		return Definition.InteractionTag == ParadoxGameplayTags::Interaction_HackTerminal && !Definition.GameplayActionDefinition.IsNull();
	}))
	{
		OutDiagnostic = TEXT("Terminal requires a SmartObject Definition and a HackTerminal interaction with an action Definition.");
	}
	else if (!WorldStateParticipant->CapturedProperties.ContainsByPredicate([](const FWorldStatePropertySelection& Selection)
	{
		return Selection.CaptureSourceId == FWorldStateCaptureSourceId::OwnerActor()
			&& Selection.PropertyName == GET_MEMBER_NAME_CHECKED(AParadoxHackingTerminal, PersistentState);
	}))
	{
		OutDiagnostic = TEXT("WorldStateParticipant must capture the owner's PersistentState property.");
	}
	return OutDiagnostic.IsEmpty();
}

#if WITH_EDITOR
EDataValidationResult AParadoxHackingTerminal::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	FString Diagnostic;
	if (!ValidateConfiguration(Diagnostic)) { Context.AddError(FText::FromString(Diagnostic)); return EDataValidationResult::Invalid; }
	if (static_cast<const UObject*>(Interaction.Get())->IsDataValid(Context) == EDataValidationResult::Invalid) { return EDataValidationResult::Invalid; }
	return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}
#endif

void AParadoxHackingTerminal::BeginPlay()
{
	Super::BeginPlay();
	Password = Password.ToUpper();
	if (Emitter)
	{
		Emitter->OnGateInvalidationChangedNative.AddUObject(this, &ThisClass::HandleGateInvalidationChanged);
		Emitter->OnEmitterInvalidatedNative.AddUObject(this, &ThisClass::HandleEmitterInvalidated);
	}
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreRestore.AddUniqueDynamic(this, &ThisClass::HandlePreRestore);
		WorldStateParticipant->OnWorldStatePropertiesRestored.AddUniqueDynamic(this, &ThisClass::HandlePropertiesRestored);
	}
	FString Diagnostic;
	if (!ValidateConfiguration(Diagnostic)) { PARADOX_LOG_ERROR(TEXT("Hacking terminal '%s': %s"), *GetName(), *Diagnostic); }
	else { PublishSignal(); }
}

void AParadoxHackingTerminal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	if (Emitter)
	{
		Emitter->OnGateInvalidationChangedNative.RemoveAll(this);
		Emitter->OnEmitterInvalidatedNative.RemoveAll(this);
	}
	InvalidateAttempts();
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreRestore.RemoveDynamic(this, &ThisClass::HandlePreRestore);
		WorldStateParticipant->OnWorldStatePropertiesRestored.RemoveDynamic(this, &ThisClass::HandlePropertiesRestored);
	}
	AttemptChanged.Clear();
	Super::EndPlay(EndPlayReason);
}

double AParadoxHackingTerminal::Now() const { return GetWorld() ? GetWorld()->GetTimeSeconds() : 0; }

FParadoxInteractionRequestResult AParadoxHackingTerminal::StartHacking(AActor* InstigatorActor, FGameplayTag OriginTag, UObject* RequestSource)
{
	FString Diagnostic;
	if (!ValidateConfiguration(Diagnostic))
	{
		FParadoxInteractionRequestResult Result;
		Result.Status = EParadoxInteractionRequestStatus::InvalidTarget; Result.DiagnosticMessage = Diagnostic; return Result;
	}
	return Interaction->RequestInteraction(InstigatorActor, ParadoxGameplayTags::Interaction_HackTerminal, OriginTag, RequestSource);
}

bool AParadoxHackingTerminal::BeginAttempt(AActor* InstigatorActor, EParadoxHackingMode Mode, double ReplayDuration,
	bool bReplaySuccess, FGuid& OutId, FString& OutDiagnostic, FRandomStream* TestRandom)
{
	OutId.Invalidate();
	if (bEndingPlay || bInvalidating || !IsValid(InstigatorActor) || InstigatorActor->GetWorld() != GetWorld() || !GetWorld())
	{
		OutDiagnostic = TEXT("Terminal or instigator is unavailable, restoring, or belongs to another world."); return false;
	}
	if (!ValidateConfiguration(OutDiagnostic)) { return false; }
	if (Emitter->IsInvalidated(OutputSignalTag))
	{
		OutDiagnostic = TEXT("Terminal emitter is blocked by its Controller gates."); return false;
	}
	if (PersistentState != EParadoxHackingTerminalState::Ready && Mode != EParadoxHackingMode::Replay)
	{
		OutDiagnostic = TEXT("Terminal is already hacked."); return false;
	}
	for (const auto& Pair : Attempts)
	{
		if (Pair.Value.Instigator.Get() == InstigatorActor && Pair.Value.View.State == EParadoxHackingAttemptState::Active)
		{
			OutId = Pair.Key; return true;
		}
	}
	if (Mode == EParadoxHackingMode::Replay && (!FMath::IsFinite(ReplayDuration) || ReplayDuration < 0))
	{
		OutDiagnostic = TEXT("Recorded hacking duration must be finite and non-negative."); return false;
	}
	// Retain only the latest completed attempt per surviving instigator.
	for (auto It = Attempts.CreateIterator(); It; ++It)
	{
		if (It.Value().View.State != EParadoxHackingAttemptState::Active
			&& (!It.Value().Instigator.IsValid() || It.Value().Instigator.Get() == InstigatorActor)) { It.RemoveCurrent(); }
	}
	FAttempt Attempt;
	Attempt.Instigator = InstigatorActor;
	Attempt.Random = TestRandom ? *TestRandom : FRandomStream(FMath::Rand());
	Attempt.View.AttemptId = FGuid::NewGuid();
	Attempt.View.Generation = Generation;
	Attempt.View.State = EParadoxHackingAttemptState::Active;
	Attempt.View.Mode = Mode;
	Attempt.View.Password = Password.ToUpper();
	Attempt.View.TimeLimit = Mode == EParadoxHackingMode::Replay ? ReplayDuration : TimeLimitSeconds;
	Attempt.StartedAt = Now();
	Attempt.Deadline = Attempt.StartedAt + Attempt.View.TimeLimit;
	Attempt.Interval = LetterRefreshInterval;
	double SimulatedDuration = 0;
	for (TCHAR Target : Attempt.View.Password)
	{
		FParadoxHackingLetterSnapshot& Slot = Attempt.View.Letters.AddDefaulted_GetRef();
		Slot.TargetLetter.AppendChar(Target);
		Slot.Pool = Slot.TargetLetter;
		FString Remaining;
		for (TCHAR Candidate = TEXT('A'); Candidate <= TEXT('Z'); ++Candidate) { if (Candidate != Target) { Remaining.AppendChar(Candidate); } }
		while (Slot.Pool.Len() < LettersPerButton)
		{
			const int32 Index = Attempt.Random.RandRange(0, Remaining.Len() - 1);
			Slot.Pool.AppendChar(Remaining[Index]); Remaining.RemoveAt(Index);
		}
		Slot.CurrentLetter = Slot.Pool.Mid(Attempt.Random.RandRange(0, LettersPerButton - 1), 1);
		Slot.RefreshOffset = bAsynchronousRefresh ? Attempt.Random.FRand() * MaxAsyncRefreshOffset : LetterRefreshInterval;
		Slot.NextRefreshTime = Attempt.StartedAt + Slot.RefreshOffset;
		if (Mode == EParadoxHackingMode::Simulated)
		{
			// Initial opportunity is 1/K; subsequent draws exclude the current wrong letter (1/(K-1)).
			if (Slot.CurrentLetter != Slot.TargetLetter)
			{
				const double Uniform = FMath::Max(double(Attempt.Random.FRand()), 1.e-12);
				const double Failures = FMath::FloorToDouble(FMath::Loge(Uniform) / FMath::Loge(1.0 - 1.0 / (LettersPerButton - 1)));
				SimulatedDuration = FMath::Max(SimulatedDuration, Slot.RefreshOffset + Failures * LetterRefreshInterval);
			}
		}
	}
	Attempt.bSimulatedSuccess = Mode == EParadoxHackingMode::Replay ? bReplaySuccess : SimulatedDuration <= TimeLimitSeconds;
	Attempt.CompletionAt = Attempt.StartedAt + (Mode == EParadoxHackingMode::Replay ? ReplayDuration : FMath::Min(SimulatedDuration, TimeLimitSeconds));
	OutId = Attempt.View.AttemptId;
	Attempts.Add(OutId, MoveTemp(Attempt));
	Schedule();
	// Start notification is deferred to the action after it has attached its identity.
	return true;
}

FParadoxHackingAttemptSnapshot AParadoxHackingTerminal::CopySnapshot(const FAttempt& Attempt) const
{
	FParadoxHackingAttemptSnapshot View = Attempt.View;
	View.InstigatorActor = Attempt.Instigator;
	View.bHasPlannedOutcome = View.Mode != EParadoxHackingMode::Interactive;
	View.bPlannedSuccess = View.bHasPlannedOutcome && Attempt.bSimulatedSuccess;
	View.PlannedDurationSeconds = View.bHasPlannedOutcome ? Attempt.CompletionAt - Attempt.StartedAt : 0;
	const bool bActive = View.State == EParadoxHackingAttemptState::Active;
	View.DurationSeconds = FMath::Max(0.0, (bActive ? Now() : Attempt.EndedAt) - Attempt.StartedAt);
	View.TimeRemaining = bActive ? FMath::Max(0.0, Attempt.Deadline - Now()) : 0;
	int32 Frozen = 0;
	for (const auto& Slot : View.Letters) { Frozen += Slot.bFrozen ? 1 : 0; }
	View.Progress = View.Letters.IsEmpty() ? 0 : float(Frozen) / View.Letters.Num();
	return View;
}

bool AParadoxHackingTerminal::GetAttemptSnapshot(FGuid Id, FParadoxHackingAttemptSnapshot& OutSnapshot) const
{
	OutSnapshot = {};
	if (const FAttempt* Attempt = Attempts.Find(Id)) { OutSnapshot = CopySnapshot(*Attempt); return true; }
	return false;
}

bool AParadoxHackingTerminal::GetAttemptForInstigator(AActor* InstigatorActor, FParadoxHackingAttemptSnapshot& OutSnapshot) const
{
	OutSnapshot = {};
	if (!IsValid(InstigatorActor)) { return false; }
	for (const auto& Pair : Attempts)
	{
		if (Pair.Value.Instigator.Get() == InstigatorActor) { OutSnapshot = CopySnapshot(Pair.Value); return true; }
	}
	return false;
}

TArray<FParadoxHackingAttemptSnapshot> AParadoxHackingTerminal::GetDiagnosticSnapshots() const
{
	TArray<FParadoxHackingAttemptSnapshot> Result;
	for (const auto& Pair : Attempts) { Result.Add(CopySnapshot(Pair.Value)); }
	return Result;
}

bool AParadoxHackingTerminal::SubmitLetter(AActor* InstigatorActor, FGuid Id, int32 AttemptGeneration, int32 SlotIndex, const FString& DisplayedLetter)
{
	FAttempt* Attempt = Attempts.Find(Id);
	if (bInvalidating || !IsValid(InstigatorActor) || !Attempt || Attempt->Instigator.Get() != InstigatorActor || AttemptGeneration != Generation
		|| Attempt->View.Generation != AttemptGeneration || Attempt->View.State != EParadoxHackingAttemptState::Active
		|| Attempt->View.Mode != EParadoxHackingMode::Interactive || !Attempt->View.Letters.IsValidIndex(SlotIndex)) { return false; }
	if (!Emitter || Emitter->IsInvalidated(OutputSignalTag)) { FailActiveAttempts(); return false; }
	if (Now() >= Attempt->Deadline) { FinishAttempt(Id, EParadoxHackingAttemptState::TimedOut); return false; }
	FParadoxHackingLetterSnapshot& Slot = Attempt->View.Letters[SlotIndex];
	if (Slot.bFrozen || DisplayedLetter != Slot.CurrentLetter) { return false; }
	if (Slot.CurrentLetter == Slot.TargetLetter) { Slot.bFrozen = true; }
	else
	{
		++Attempt->View.ErrorCount;
		for (auto& Letter : Attempt->View.Letters)
		{
			Letter.bFrozen = false;
			ChangeDisplayedLetter(Letter, Attempt->Random);
			if (Letter.NextRefreshTime <= Now())
			{
				Letter.NextRefreshTime += (FMath::FloorToDouble((Now() - Letter.NextRefreshTime) / Attempt->Interval) + 1) * Attempt->Interval;
			}
		}
	}
	if (Attempt->View.Letters.ContainsByPredicate([](const auto& Letter) { return !Letter.bFrozen; }))
	{
		const auto Snapshot = CopySnapshot(*Attempt); AttemptChanged.Broadcast(Snapshot); Schedule();
	}
	else { FinishAttempt(Id, EParadoxHackingAttemptState::Success); }
	return true;
}

bool AParadoxHackingTerminal::CancelAttempt(AActor* InstigatorActor, FGuid Id, int32 AttemptGeneration)
{
	const FAttempt* Attempt = Attempts.Find(Id);
	if (!Attempt || !IsValid(InstigatorActor) || Attempt->Instigator.Get() != InstigatorActor
		|| Attempt->View.Generation != AttemptGeneration || Attempt->View.State != EParadoxHackingAttemptState::Active) { return false; }
	FinishAttempt(Id, EParadoxHackingAttemptState::Cancelled); return true;
}

void AParadoxHackingTerminal::FinishAttempt(FGuid Id, EParadoxHackingAttemptState State, bool bUsePlannedCompletionTime)
{
	FAttempt* Attempt = Attempts.Find(Id);
	if (!Attempt || Attempt->View.State != EParadoxHackingAttemptState::Active || Attempt->View.Generation != Generation) { return; }
	Attempt->View.State = State;
	Attempt->EndedAt = (State == EParadoxHackingAttemptState::TimedOut) ? Attempt->Deadline : Now();
	if (bUsePlannedCompletionTime && Attempt->View.Mode != EParadoxHackingMode::Interactive
		&& (State == EParadoxHackingAttemptState::Success || State == EParadoxHackingAttemptState::Failed))
	{
		Attempt->EndedAt = Attempt->CompletionAt;
	}
	TArray<FParadoxHackingAttemptSnapshot> Notifications;
	Notifications.Add(CopySnapshot(*Attempt));
	const bool bFirstSuccess = State == EParadoxHackingAttemptState::Success && PersistentState == EParadoxHackingTerminalState::Ready;
	if (bFirstSuccess)
	{
		PersistentState = EParadoxHackingTerminalState::Hacked;
		for (auto& Pair : Attempts)
		{
			if (Pair.Value.View.State == EParadoxHackingAttemptState::Active)
			{
				Pair.Value.View.State = EParadoxHackingAttemptState::Superseded;
				Pair.Value.EndedAt = Now();
				Notifications.Add(CopySnapshot(Pair.Value));
			}
		}
	}
	Schedule();
	for (const auto& Snapshot : Notifications)
	{
		if (Snapshot.Generation != Generation) { break; }
		if (bEnableDebug && HackingDebug.GetValueOnGameThread())
		{
			PARADOX_LOG_INFO(TEXT("Terminal '%s' attempt %s ended %s after %.3fs (generation %d)."), *GetName(), *Snapshot.AttemptId.ToString(),
				*UEnum::GetValueAsString(Snapshot.State), Snapshot.DurationSeconds, Snapshot.Generation);
		}
		AttemptChanged.Broadcast(Snapshot);
	}
	if (bFirstSuccess && !Notifications.IsEmpty()
		&& Notifications[0].Generation == Generation && !bEndingPlay) { PublishSignal(); }
}

void AParadoxHackingTerminal::Schedule()
{
	if (!GetWorld()) { return; }
	GetWorld()->GetTimerManager().ClearTimer(Scheduler);
	if (bEndingPlay || bInvalidating) { return; }
	double Next = TNumericLimits<double>::Max();
	for (const auto& Pair : Attempts)
	{
		const FAttempt& Attempt = Pair.Value;
		if (Attempt.View.State != EParadoxHackingAttemptState::Active) { continue; }
		Next = FMath::Min(Next, Attempt.Deadline);
		if (Attempt.View.Mode != EParadoxHackingMode::Interactive) { Next = FMath::Min(Next, Attempt.CompletionAt); }
		else { for (const auto& Slot : Attempt.View.Letters) { if (!Slot.bFrozen) { Next = FMath::Min(Next, Slot.NextRefreshTime); } } }
	}
	if (Next < TNumericLimits<double>::Max())
	{
		const int32 ScheduledGeneration = Generation;
		GetWorld()->GetTimerManager().SetTimer(Scheduler, FTimerDelegate::CreateWeakLambda(this, [this, ScheduledGeneration]()
		{
			if (ScheduledGeneration == Generation) { Advance(); }
		}), float(FMath::Clamp(Next - Now(), 0.001, double(TNumericLimits<float>::Max()))), false);
	}
}

void AParadoxHackingTerminal::Advance()
{
	if (!Emitter || Emitter->IsInvalidated(OutputSignalTag)) { FailActiveAttempts(); return; }
	TArray<FGuid> Ids;
	Attempts.GetKeys(Ids);
	const int32 CurrentGeneration = Generation;
	for (FGuid Id : Ids)
	{
		if (CurrentGeneration != Generation || bEndingPlay) { break; }
		FAttempt* Attempt = Attempts.Find(Id);
		if (!Attempt || Attempt->View.State != EParadoxHackingAttemptState::Active) { continue; }
		if (!Attempt->Instigator.IsValid()) { FinishAttempt(Id, EParadoxHackingAttemptState::Cancelled); continue; }
		if (Attempt->View.Mode != EParadoxHackingMode::Interactive && Now() >= Attempt->CompletionAt)
		{
			FinishAttempt(Id, Attempt->bSimulatedSuccess ? EParadoxHackingAttemptState::Success
				: (Attempt->View.Mode == EParadoxHackingMode::Replay ? EParadoxHackingAttemptState::Failed : EParadoxHackingAttemptState::TimedOut), true); continue;
		}
		if (Now() >= Attempt->Deadline) { FinishAttempt(Id, EParadoxHackingAttemptState::TimedOut); continue; }
		bool bChanged = false;
		for (auto& Slot : Attempt->View.Letters)
		{
			if (!Slot.bFrozen && Now() >= Slot.NextRefreshTime)
			{
				ChangeDisplayedLetter(Slot, Attempt->Random);
				Slot.NextRefreshTime += (FMath::FloorToDouble((Now() - Slot.NextRefreshTime) / Attempt->Interval) + 1) * Attempt->Interval;
				bChanged = true;
			}
		}
		if (bChanged) { const auto Snapshot = CopySnapshot(*Attempt); AttemptChanged.Broadcast(Snapshot); }
	}
	Schedule();
}

void AParadoxHackingTerminal::HandleGateInvalidationChanged(UPuzzleEmitterComponent* ChangedEmitter, FGameplayTag SignalTag, bool bIsInvalidated)
{
	if (ChangedEmitter != Emitter || SignalTag != OutputSignalTag || bEndingPlay) { return; }
	if (bIsInvalidated) { FailActiveAttempts(); }
	if (Interaction) { Interaction->NotifyInteractionAffordanceChanged(); }
}

void AParadoxHackingTerminal::HandleEmitterInvalidated(UPuzzleEmitterComponent* ChangedEmitter)
{
	if (ChangedEmitter != Emitter || bEndingPlay) { return; }
	FailActiveAttempts();
	if (Interaction) { Interaction->NotifyInteractionAffordanceChanged(); }
}

void AParadoxHackingTerminal::FailActiveAttempts()
{
	if (bEndingPlay || bInvalidating) { return; }
	{
		// Reject new starts/clicks from callbacks until every concurrent attempt has failed.
		TGuardValue<bool> Guard(bInvalidating, true);
		TArray<FGuid> Ids;
		Attempts.GetKeys(Ids);
		const int32 CurrentGeneration = Generation;
		for (FGuid Id : Ids)
		{
			if (CurrentGeneration != Generation || bEndingPlay) { break; }
			FinishAttempt(Id, EParadoxHackingAttemptState::Failed);
		}
	}
	Schedule();
}

void AParadoxHackingTerminal::InvalidateAttempts()
{
	TGuardValue<bool> Guard(bInvalidating, true);
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(Scheduler); }
	TArray<FGuid> Ids;
	Attempts.GetKeys(Ids);
	for (FGuid Id : Ids) { FinishAttempt(Id, EParadoxHackingAttemptState::Cancelled); }
	++Generation;
	Attempts.Reset();
}

void AParadoxHackingTerminal::PublishSignal()
{
	FPuzzleSignalState Existing;
	if (Emitter && Emitter->TryGetSignalState(OutputSignalTag, Existing)
		&& Existing.bIsActive == (PersistentState == EParadoxHackingTerminalState::Hacked)) { return; }
	if (!Emitter || !OutputSignalTag.IsValid() || !Emitter->SetSignalState(OutputSignalTag, PersistentState == EParadoxHackingTerminalState::Hacked, nullptr))
	{
		PARADOX_LOG_ERROR(TEXT("Terminal '%s' cannot publish signal '%s'."), *GetName(), *OutputSignalTag.ToString());
	}
}

void AParadoxHackingTerminal::HandlePreRestore(FWorldStateParticipantId) { InvalidateAttempts(); }
void AParadoxHackingTerminal::HandlePropertiesRestored(FWorldStateParticipantId) { PublishSignal(); }
