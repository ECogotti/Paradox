#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actions/GameplayActionDefinition.h"
#include "Actions/GameplayWaitAction.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "IntentReplayTags.h"
#include "Journal/IntentExecutionJournal.h"
#include "NativeGameplayTags.h"
#include "Playback/IntentReplayPlaybackSession.h"
#include "Recording/IntentRecordingSession.h"
#include "Recording/IntentReplayTrack.h"
#include "StructUtils/PropertyBag.h"
#include "Tests/IntentReplayTestTypes.h"
#include "TimerManager.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IntentReplay_Test_Action, "GameplayAction.Test.IntentReplay.Action");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IntentReplay_Test_ActionChanged, "GameplayAction.Test.IntentReplay.ActionChanged");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IntentReplay_Test_OriginPlayer, "GameplayAction.Origin.Test.IntentReplay.Player");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IntentReplay_Test_Correlation, "IntentReplay.Correlation.Test.Intent");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IntentReplay_Test_MovementLock, "GameplayAction.Lock.Test.IntentReplay.Movement");
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	TAG_IntentReplay_Test_ExternalRecovery,
	"GameplayAction.Result.Interrupted.Test.IntentReplay.ExternalRecovery");

namespace IntentReplayTests
{
	struct FEntity
	{
		AActor* Actor = nullptr;
		UGameplayActionComponent* Actions = nullptr;
		UIntentReplayComponent* Replay = nullptr;
	};

	UWorld* MakeWorld(const TCHAR* Name)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
		if (World)
		{
			World->AddToRoot();
		}
		UIntentReplayTestTimeSource::ResetTime();
		return World;
	}

	void DestroyWorld(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		World->RemoveFromRoot();
		World->DestroyWorld(false);
	}

	void Advance(UWorld& World, const double DeltaSeconds)
	{
		++GFrameCounter;
		World.GetTimerManager().Tick(0.0f);
		UIntentReplayTestTimeSource::AdvanceTime(DeltaSeconds);
		++GFrameCounter;
		World.GetTimerManager().Tick(static_cast<float>(DeltaSeconds));
	}

	FEntity MakeEntity(UWorld& World)
	{
		FEntity Entity;
		Entity.Actor = World.SpawnActor<AActor>();
		if (!Entity.Actor)
		{
			return Entity;
		}

		Entity.Actions = NewObject<UGameplayActionComponent>(Entity.Actor, NAME_None, RF_Transient);
		Entity.Actor->AddInstanceComponent(Entity.Actions);
		Entity.Actions->RegisterComponent();

		Entity.Replay = NewObject<UIntentReplayComponent>(Entity.Actor, NAME_None, RF_Transient);
		Entity.Replay->ActionComponentOverride = Entity.Actions;
		Entity.Replay->TimeSourceClass = UIntentReplayTestTimeSource::StaticClass();
		Entity.Actor->AddInstanceComponent(Entity.Replay);
		Entity.Replay->RegisterComponent();
		Entity.Replay->InitializeIntentReplay();
		return Entity;
	}

	UGameplayActionDefinition* MakeWaitDefinition(
		const double DurationSeconds,
		const FGameplayTag ActionTag = TAG_IntentReplay_Test_Action)
	{
		UGameplayActionDefinition* Definition =
			NewObject<UGameplayActionDefinition>(GetTransientPackage(), NAME_None, RF_Transient);
		Definition->InstanceClass = UGameplayWaitAction::StaticClass();
		Definition->ActionTag = ActionTag;
		Definition->JournalRequirement = EGameplayActionJournalRequirement::Required;
		Definition->DefaultParameters.InitializeFromBagStruct(
			UPropertyBag::GetOrCreateFromDescs(
				{ { TEXT("Duration"), EPropertyBagPropertyType::Double } }));
		Definition->DefaultParameters.SetValueDouble(TEXT("Duration"), DurationSeconds);
		return Definition;
	}

	FGameplayActionRequest MakeRequest(
		UGameplayActionDefinition& Definition,
		const int32 Priority = 0,
		const EGameplayActionBlockedPolicy BlockedPolicy = EGameplayActionBlockedPolicy::Queue)
	{
		FGameplayActionRequestCreationResult Creation =
			UGameplayActionBlueprintLibrary::CreateActionRequest(&Definition);
		if (!Creation.WasCreated())
		{
			return FGameplayActionRequest();
		}
		UGameplayActionBlueprintLibrary::SetRequestPriority(Creation.Request, Priority);
		UGameplayActionBlueprintLibrary::SetRequestBlockedPolicy(Creation.Request, BlockedPolicy);
		FGameplayActionCorrelationData Correlation;
		Correlation.Type = TAG_IntentReplay_Test_Correlation;
		Correlation.Id = FGuid::NewGuid();
		UGameplayActionBlueprintLibrary::SetRequestContext(
			Creation.Request,
			TAG_IntentReplay_Test_OriginPlayer,
			&Definition,
			Correlation);
		return Creation.Request;
	}

	bool FindAcceptedReplayEvent(
		const UIntentExecutionJournal& Journal,
		FIntentExecutionEvent& OutEvent)
	{
		for (const FIntentExecutionEvent& Event : Journal.GetEvents())
		{
			if (Event.bHasActionEvent
				&& Event.ActionEvent.EventType == EGameplayActionEventType::Accepted
				&& Event.ActionEvent.OriginTag == IntentReplayTags::Origin_Replay)
			{
				OutEvent = Event;
				return true;
			}
		}
		return false;
	}
}

struct FIntentReplayCoreTestAccessor
{
	static UIntentReplayTrack* MakeLegacyEmptyTrack()
	{
		UIntentReplayTrack* Track = NewObject<UIntentReplayTrack>(
			GetTransientPackage(),
			NAME_None,
			RF_Transient);
		TArray<FRecordedIntent> Entries;
		Track->InitializeFinalized(
			FIntentReplayTrackId::NewId(),
			FIntentRecordingSessionId::NewId(),
			MoveTemp(Entries),
			0.0,
			TEXT("LegacyCompatibility"),
			FGameplayTagContainer());
		Track->FormatVersion = 1;
		Track->SourceRecordingSessionId = FIntentRecordingSessionId();
		return Track;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplayRecordingTest,
	"IntentReplay.Recording.TransactionAsyncStopDeepCopyAndNewSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplayRecordingTest::RunTest(const FString& Parameters)
{
	using namespace IntentReplayTests;
	UWorld* World = MakeWorld(TEXT("IntentReplayRecordingTestWorld"));
	TestNotNull(TEXT("Test world exists"), World);
	if (!World)
	{
		return false;
	}

	const FEntity Source = MakeEntity(*World);
	TestNotNull(TEXT("Source replay component exists"), Source.Replay);
	TestTrue(
		TEXT("Source replay component initialized"),
		Source.Replay && Source.Replay->IsIntentReplayInitialized());
	UGameplayActionDefinition* Definition = MakeWaitDefinition(0.1);

	FIntentRecordingOptions RecordingOptions;
	RecordingOptions.SourceLabel = TEXT("PlayerIterationOne");
	const FIntentRecordingStartResult Start = Source.Replay->StartRecording(RecordingOptions);
	TestTrue(TEXT("Recording starts"), Start.Succeeded());
	UIntentRecordingSession* FirstSession = Source.Replay->GetActiveRecordingSession();
	TestNotNull(TEXT("First session exists"), FirstSession);

	const FGameplayActionSubmissionResult Submission =
		Source.Actions->SubmitAction(MakeRequest(*Definition, 17, EGameplayActionBlockedPolicy::Reject));
	TestTrue(TEXT("Source action is accepted"), Submission.IsAccepted());
	Definition->DefaultParameters.SetValueDouble(TEXT("Duration"), 0.9);

	TestTrue(
		TEXT("Async stop request succeeds"),
		Source.Replay->RequestStopRecording(EIntentRecordingFinalizeMode::AsyncStop).Succeeded());
	TestEqual(TEXT("Recording enters Draining"), Source.Replay->GetRecordingState(), EIntentRecordingState::Draining);
	TestNull(TEXT("Track is not finalized before the action ends"), Source.Replay->GetLastFinalizedTrack());

	Advance(*World, 0.11);
	UIntentReplayTrack* Track = Source.Replay->GetLastFinalizedTrack();
	TestNotNull(TEXT("Async stop finalizes a track"), Track);
	if (Track)
	{
		TestTrue(TEXT("Track preserves the session id"), Track->GetTrackId() == Start.TrackId);
		TestEqual(TEXT("Track has one entry"), Track->GetEntryCount(), 1);
		TestTrue(TEXT("Track is independent from the source actor"), Track->GetOuter() == GetTransientPackage());
		FRecordedIntent Entry;
		TestTrue(TEXT("Recorded entry is readable"), Track->GetEntryByIndex(0, Entry));
		TestEqual(TEXT("Recorded priority is preserved"), Entry.EffectivePriority, 17);
		TestEqual(
			TEXT("Recorded blocked policy is preserved"),
			Entry.EffectiveBlockedPolicy,
			EGameplayActionBlockedPolicy::Reject);
		TestTrue(TEXT("Original origin is preserved"), Entry.OriginalOriginTag == TAG_IntentReplay_Test_OriginPlayer);
		TestTrue(TEXT("Original result is captured after async stop"), Entry.bHasOriginalResult);
		TestEqual(TEXT("Original result succeeded"), Entry.OriginalResult.TerminalState, EGameplayActionState::Succeeded);
		const TValueOrError<double, EPropertyBagResult> Duration =
			Entry.GetParameters().GetValueDouble(TEXT("Duration"));
		TestTrue(TEXT("Recorded Duration is readable"), Duration.IsValid());
		if (Duration.IsValid())
		{
			TestEqual(TEXT("Recorded bag is a deep copy"), Duration.GetValue(), 0.1);
		}
	}
	TestTrue(
		TEXT("Recording journal contains lifecycle events"),
		FirstSession && FirstSession->GetExecutionJournal()->GetEntryCount() >= 3);

	const FIntentRecordingStartResult SecondStart =
		Source.Replay->StartRecording(FIntentRecordingOptions());
	TestTrue(TEXT("A fresh player recording starts"), SecondStart.Succeeded());
	TestTrue(TEXT("Fresh recording has a new Track ID"), SecondStart.TrackId != Start.TrackId);
	TestTrue(
		TEXT("Last finalized track remains the previous immutable track until a new finalization"),
		Source.Replay->GetLastFinalizedTrack() == Track);
	TestEqual(TEXT("Transferred track remains immutable"), Track ? Track->GetEntryCount() : 0, 1);
	TestTrue(TEXT("Fresh recording can be cancelled"), Source.Replay->CancelRecording().Succeeded());

	Source.Actor->Destroy();
	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplayAuthoritativeTimelineTest,
	"IntentReplay.Timeline.SessionClockSequenceLifecycleAndLegacyFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplayAuthoritativeTimelineTest::RunTest(const FString& Parameters)
{
	using namespace IntentReplayTests;
	UWorld* World = MakeWorld(TEXT("IntentReplayAuthoritativeTimelineWorld"));
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}

	const FEntity Source = MakeEntity(*World);
	UGameplayActionDefinition* Definition = MakeWaitDefinition(2.0);
	TArray<FIntentReplayTimelineLifecycleEvent> Lifecycle;
	Source.Replay->OnTimelineLifecycleChangedNative().AddLambda(
		[&Lifecycle](const FIntentReplayTimelineLifecycleEvent& Event)
		{
			Lifecycle.Add(Event);
		});

	const FIntentRecordingStartResult Start =
		Source.Replay->StartRecording(FIntentRecordingOptions());
	TestTrue(TEXT("Recording Session ID is valid"), Start.SessionId.IsValid());
	const FIntentReplayTimelinePointResult ExternalPoint =
		Source.Replay->CaptureRecordingTimelinePoint(Start.SessionId);
	TestTrue(TEXT("External recording point is captured"), ExternalPoint.Succeeded());
	TestEqual(TEXT("First shared sequence is zero"), ExternalPoint.TimelineSequence, int64(0));
	TestTrue(
		TEXT("Action accepted after external point"),
		Source.Actions->SubmitAction(MakeRequest(*Definition)).IsAccepted());

	Advance(*World, 0.25);
	TestTrue(TEXT("Recording pauses"), Source.Replay->PauseRecording().Succeeded());
	const FIntentReplayTimelineClockSnapshot Paused =
		Source.Replay->GetRecordingClockSnapshot();
	Advance(*World, 0.5);
	const FIntentReplayTimelineClockSnapshot StillPaused =
		Source.Replay->GetRecordingClockSnapshot();
	TestTrue(
		TEXT("Paused recording clock is frozen"),
		FMath::IsNearlyEqual(
			Paused.RelativeTimeSeconds,
			StillPaused.RelativeTimeSeconds,
			UE_SMALL_NUMBER));
	TestEqual(
		TEXT("Capture while paused is explicit"),
		Source.Replay->CaptureRecordingTimelinePoint(Start.SessionId).Status,
		EIntentReplayTimelineCaptureStatus::Paused);
	TestTrue(TEXT("Recording resumes"), Source.Replay->ResumeRecording().Succeeded());
	const FIntentReplayTimelinePointResult ResumedPoint =
		Source.Replay->CaptureRecordingTimelinePoint(Start.SessionId);
	TestEqual(TEXT("Shared allocator advanced past action"), ResumedPoint.TimelineSequence, int64(2));
	TestTrue(
		TEXT("Recording finalizes"),
		Source.Replay->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate).Succeeded());

	UIntentReplayTrack* Track = Source.Replay->GetLastFinalizedTrack();
	TestNotNull(TEXT("Format 2 track is published"), Track);
	if (Track)
	{
		TestEqual(TEXT("Core track format is 2"), Track->GetFormatVersion(), 2);
		TestTrue(
			TEXT("Track carries recording session identity"),
			Track->GetSourceRecordingSessionId() == Start.SessionId);
		FRecordedIntent Entry;
		TestTrue(TEXT("Recorded action is readable"), Track->GetEntryByIndex(0, Entry));
		TestEqual(TEXT("Action uses shared sequence one"), Entry.TimelineSequence, int64(1));
	}

	const FIntentReplayTimelineClockSnapshot FinalClock =
		Source.Replay->GetRecordingClockSnapshot();
	Advance(*World, 1.0);
	TestTrue(
		TEXT("Terminal recording clock remains frozen"),
		FMath::IsNearlyEqual(
			FinalClock.RelativeTimeSeconds,
			Source.Replay->GetRecordingClockSnapshot().RelativeTimeSeconds,
			UE_SMALL_NUMBER));
	TestEqual(TEXT("Four recording transitions emitted once"), Lifecycle.Num(), 4);
	if (Lifecycle.Num() == 4)
	{
		TestEqual(TEXT("Transition 0"), Lifecycle[0].NewRecordingState, EIntentRecordingState::Recording);
		TestEqual(TEXT("Transition 1"), Lifecycle[1].NewRecordingState, EIntentRecordingState::Paused);
		TestEqual(TEXT("Transition 2"), Lifecycle[2].NewRecordingState, EIntentRecordingState::Recording);
		TestEqual(TEXT("Transition 3"), Lifecycle[3].NewRecordingState, EIntentRecordingState::Finalized);
	}

	Lifecycle.Reset();
	const FIntentReplayPrepareResult Prepare =
		Source.Replay->PrepareReplay(Track, FIntentReplayPlaybackOptions());
	TestTrue(TEXT("Playback preparation is accepted"), Prepare.WasAccepted());
	TestEqual(
		TEXT("Playback reaches Ready synchronously for loaded definitions"),
		Source.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Ready);
	TestTrue(TEXT("Playback starts"), Source.Replay->StartReplay().Succeeded());
	const FIntentReplayTimelinePointResult PlaybackPoint =
		Source.Replay->CapturePlaybackTimelinePoint(Prepare.SessionId);
	TestEqual(TEXT("First playback sequence is zero"), PlaybackPoint.TimelineSequence, int64(0));
	TestTrue(TEXT("Playback pauses"), Source.Replay->PauseReplay().Succeeded());
	const FIntentReplayTimelineClockSnapshot PausedPlayback =
		Source.Replay->GetPlaybackClockSnapshot();
	Advance(*World, 0.5);
	TestTrue(
		TEXT("Paused playback clock is frozen"),
		FMath::IsNearlyEqual(
			PausedPlayback.RelativeTimeSeconds,
			Source.Replay->GetPlaybackClockSnapshot().RelativeTimeSeconds,
			UE_SMALL_NUMBER));
	TestTrue(TEXT("Playback resumes"), Source.Replay->ResumeReplay().Succeeded());
	TestEqual(
		TEXT("Playback allocator resumes without reset"),
		Source.Replay->CapturePlaybackTimelinePoint(Prepare.SessionId).TimelineSequence,
		int64(1));
	Advance(*World, 2.1);
	TestEqual(
		TEXT("Playback completes"),
		Source.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Completed);
	const FIntentReplayTimelineClockSnapshot CompletedPlayback =
		Source.Replay->GetPlaybackClockSnapshot();
	Advance(*World, 1.0);
	TestTrue(
		TEXT("Terminal playback clock remains frozen"),
		FMath::IsNearlyEqual(
			CompletedPlayback.RelativeTimeSeconds,
			Source.Replay->GetPlaybackClockSnapshot().RelativeTimeSeconds,
			UE_SMALL_NUMBER));
	TestEqual(TEXT("Six playback transitions emitted once"), Lifecycle.Num(), 6);

	UIntentReplayTrack* LegacyTrack =
		FIntentReplayCoreTestAccessor::MakeLegacyEmptyTrack();
	TestTrue(
		TEXT("Legacy format 1 validation remains supported"),
		LegacyTrack->ValidateTrack().bValid);

	Source.Actor->Destroy();
	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplayRecordabilityTest,
	"IntentReplay.Recordability.RecursiveRuntimeReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplayRecordabilityTest::RunTest(const FString& Parameters)
{
	using namespace IntentReplayTests;
	UWorld* World = MakeWorld(TEXT("IntentReplayRecordabilityTestWorld"));
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}
	const FEntity Source = MakeEntity(*World);
	UGameplayActionDefinition* Definition = MakeWaitDefinition(0.01);
	Definition->DefaultParameters.InitializeFromBagStruct(
		UPropertyBag::GetOrCreateFromDescs(
			{
				{ TEXT("Duration"), EPropertyBagPropertyType::Double },
				{ TEXT("Nested"), EPropertyBagPropertyType::Struct, FIntentReplayTestNestedPayload::StaticStruct() }
			}));
	Definition->DefaultParameters.SetValueDouble(TEXT("Duration"), 0.01);

	FIntentReplayTestNestedPayload UnsafePayload;
	UnsafePayload.Objects.Add(Source.Actor);
	UnsafePayload.ObjectMap.Add(TEXT("Actor"), Source.Actor);
	TestEqual(
		TEXT("Nested payload is assigned"),
		Definition->DefaultParameters.SetValueStruct(TEXT("Nested"), UnsafePayload),
		EPropertyBagResult::Success);

	TestTrue(TEXT("Recording starts"), Source.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	UIntentRecordingSession* RejectedSession = Source.Replay->GetActiveRecordingSession();
	const FGameplayActionSubmissionResult Rejected =
		Source.Actions->SubmitAction(MakeRequest(*Definition));
	TestEqual(
		TEXT("Runtime Actor inside a recursive container rejects the Accepted transaction"),
		Rejected.Status,
		EGameplayActionSubmissionStatus::RejectedJournal);
	TestTrue(
		TEXT("Rejected action remains observable in the execution journal"),
		RejectedSession && RejectedSession->GetExecutionJournal()->GetEntryCount() > 0);
	TestTrue(
		TEXT("Rejected recording finalizes"),
		Source.Replay->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate).Succeeded());
	TestEqual(TEXT("Rejected runtime value never enters the track"), Source.Replay->GetLastFinalizedTrack()->GetEntryCount(), 0);

	FIntentReplayTestNestedPayload SafePayload;
	SafePayload.Objects.Add(nullptr);
	SafePayload.ObjectMap.Add(TEXT("Null"), nullptr);
	SafePayload.StableAssetPath = FSoftObjectPath(UGameplayWaitAction::StaticClass());
	Definition->DefaultParameters.SetValueStruct(TEXT("Nested"), SafePayload);
	TestTrue(TEXT("Second recording starts"), Source.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	TestTrue(
		TEXT("Null runtime references and UTF-8 soft paths are deterministic and accepted"),
		Source.Actions->SubmitAction(MakeRequest(*Definition)).IsAccepted());
	TestTrue(
		TEXT("Immediate finalization succeeds"),
		Source.Replay->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate).Succeeded());
	TestEqual(TEXT("Safe nested payload enters the track"), Source.Replay->GetLastFinalizedTrack()->GetEntryCount(), 1);
	Source.Actions->AbortAllActions(FGameplayTag());
	Source.Replay->NoRecordingSessionPolicy = EIntentNoRecordingSessionPolicy::RejectAcceptedActions;
	TestEqual(
		TEXT("RejectAcceptedActions enforces the journal contract without a recording"),
		Source.Actions->SubmitAction(MakeRequest(*Definition)).Status,
		EGameplayActionSubmissionStatus::RejectedJournal);

	Source.Actor->Destroy();
	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplayTransferTest,
	"IntentReplay.Transfer.PlayerCloneSharedTrackIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplayTransferTest::RunTest(const FString& Parameters)
{
	using namespace IntentReplayTests;
	UWorld* World = MakeWorld(TEXT("IntentReplayTransferTestWorld"));
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}

	FEntity Source = MakeEntity(*World);
	UGameplayActionDefinition* Definition = MakeWaitDefinition(0.1);
	TestTrue(TEXT("Player recording starts"), Source.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	TestTrue(TEXT("Player action is accepted"), Source.Actions->SubmitAction(MakeRequest(*Definition)).IsAccepted());
	TestTrue(
		TEXT("Player recording starts asynchronous stop"),
		Source.Replay->RequestStopRecording(EIntentRecordingFinalizeMode::AsyncStop).Succeeded());
	Advance(*World, 0.11);
	UIntentReplayTrack* Track = Source.Replay->GetLastFinalizedTrack();
	TestNotNull(TEXT("Player track finalized"), Track);
	if (!Track)
	{
		Source.Actor->Destroy();
		DestroyWorld(World);
		return false;
	}

	UIntentReplayTestTrackHolder* Coordinator =
		NewObject<UIntentReplayTestTrackHolder>(GetTransientPackage(), NAME_None, RF_Transient);
	Coordinator->RetainedTrack = Track;
	Coordinator->RetainedDefinition = Definition;
	Coordinator->AddToRoot();

	Source.Actor->Destroy();
	Source = FEntity();
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("External UPROPERTY keeps the finalized track alive"), IsValid(Coordinator->RetainedTrack));
	TestEqual(TEXT("Retained track remains finalized"), Coordinator->RetainedTrack->GetEntryCount(), 1);

	const FEntity CloneA = MakeEntity(*World);
	const FEntity CloneB = MakeEntity(*World);
	TestTrue(
		TEXT("Clone A may record its own non-replay intents"),
		CloneA.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());

	FIntentReplayPlaybackOptions PlaybackOptions;
	const FIntentReplayPrepareResult PrepareA =
		CloneA.Replay->PrepareReplay(Coordinator->RetainedTrack, PlaybackOptions);
	const FIntentReplayPrepareResult PrepareB =
		CloneB.Replay->PrepareReplay(Coordinator->RetainedTrack, PlaybackOptions);
	TestEqual(TEXT("Clone A preparation is ready"), PrepareA.Status, EIntentReplayPrepareStatus::Ready);
	TestEqual(TEXT("Clone B preparation is ready"), PrepareB.Status, EIntentReplayPrepareStatus::Ready);
	TestTrue(TEXT("Playback sessions have distinct IDs"), PrepareA.SessionId != PrepareB.SessionId);

	UIntentReplayPlaybackSession* SessionA = CloneA.Replay->GetActivePlaybackSession();
	UIntentReplayPlaybackSession* SessionB = CloneB.Replay->GetActivePlaybackSession();
	TestNotNull(TEXT("Clone A playback session exists"), SessionA);
	TestNotNull(TEXT("Clone B playback session exists"), SessionB);
	TestTrue(
		TEXT("Clone A reads the shared track"),
		SessionA && SessionA->GetSourceTrack() == Coordinator->RetainedTrack.Get());
	TestTrue(
		TEXT("Clone B reads the shared track"),
		SessionB && SessionB->GetSourceTrack() == Coordinator->RetainedTrack.Get());
	TestTrue(
		TEXT("Execution journals are isolated"),
		SessionA && SessionB && SessionA->GetExecutionJournal() != SessionB->GetExecutionJournal());

	TestTrue(TEXT("Clone A replay starts"), CloneA.Replay->StartReplay().Succeeded());
	TestTrue(TEXT("Clone B replay starts"), CloneB.Replay->StartReplay().Succeeded());
	TestEqual(TEXT("Clone A owns one replay action"), SessionA ? SessionA->GetReplayOwnedActionCount() : 0, 1);
	TestEqual(TEXT("Clone B owns one replay action"), SessionB ? SessionB->GetReplayOwnedActionCount() : 0, 1);
	Advance(*World, 0.11);
	TestEqual(TEXT("Clone A completes"), CloneA.Replay->GetPlaybackState(), EIntentReplayPlaybackState::Completed);
	TestEqual(TEXT("Clone B completes"), CloneB.Replay->GetPlaybackState(), EIntentReplayPlaybackState::Completed);

	FRecordedIntent RecordedIntent;
	Coordinator->RetainedTrack->GetEntryByIndex(0, RecordedIntent);
	FIntentExecutionEvent ReplayAcceptedEvent;
	TestTrue(
		TEXT("Clone replay journal contains Accepted"),
		SessionA && FindAcceptedReplayEvent(*SessionA->GetExecutionJournal(), ReplayAcceptedEvent));
	TestTrue(
		TEXT("Replay origin is explicit"),
		ReplayAcceptedEvent.ActionEvent.OriginTag == IntentReplayTags::Origin_Replay);
	TestTrue(
		TEXT("Replay correlation type is explicit"),
		ReplayAcceptedEvent.ActionEvent.Correlation.Type == IntentReplayTags::Correlation_RecordedIntent);
	TestEqual(
		TEXT("Replay correlation carries RecordedIntentId"),
		ReplayAcceptedEvent.ActionEvent.Correlation.Id,
		RecordedIntent.RecordedIntentId.GetGuid());

	TestTrue(
		TEXT("Clone A recording finalizes independently"),
		CloneA.Replay->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate).Succeeded());
	TestEqual(
		TEXT("Replay-origin requests do not recursively enter a recording"),
		CloneA.Replay->GetLastFinalizedTrack()->GetEntryCount(),
		0);

	CloneA.Actor->Destroy();
	CloneB.Actor->Destroy();
	Coordinator->RemoveFromRoot();
	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplayCompatibilityTimingTest,
	"IntentReplay.Playback.CompatibilityTimingPauseStopAndIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplayCompatibilityTimingTest::RunTest(const FString& Parameters)
{
	using namespace IntentReplayTests;
	UWorld* World = MakeWorld(TEXT("IntentReplayCompatibilityTimingTestWorld"));
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}

	const FEntity Source = MakeEntity(*World);
	UGameplayActionDefinition* Definition = MakeWaitDefinition(1.0);
	TestTrue(TEXT("Timeline recording starts"), Source.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	TestTrue(TEXT("First intent is accepted"), Source.Actions->SubmitAction(MakeRequest(*Definition)).IsAccepted());
	Advance(*World, 0.05);
	TestTrue(TEXT("Second intent is accepted"), Source.Actions->SubmitAction(MakeRequest(*Definition)).IsAccepted());
	TestTrue(
		TEXT("Default finalization is immediate"),
		Source.Replay->RequestStopRecording().Succeeded());
	UIntentReplayTrack* Track = Source.Replay->GetLastFinalizedTrack();
	TestEqual(TEXT("Timeline has two entries"), Track ? Track->GetEntryCount() : 0, 2);
	Source.Actions->AbortAllActions(FGameplayTag());
	FRecordedIntent ImmediateEntry;
	TestTrue(TEXT("Immediate track entry remains readable"), Track && Track->GetEntryByIndex(0, ImmediateEntry));
	TestFalse(TEXT("Late Ended cannot mutate an immediately finalized track"), ImmediateEntry.bHasOriginalResult);

	const FEntity Clone = MakeEntity(*World);
	const FGameplayActionSubmissionResult ForeignSubmission =
		Clone.Actions->SubmitAction(MakeRequest(*Definition));
	TestTrue(TEXT("Unrelated clone action starts"), ForeignSubmission.IsAccepted());

	Definition->ActionTag = TAG_IntentReplay_Test_ActionChanged;
	AddExpectedError(
		TEXT("Strict replay compatibility rejected Definition changes"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	const FIntentReplayPrepareResult StrictPrepare =
		Clone.Replay->PrepareReplay(Track, FIntentReplayPlaybackOptions());
	TestEqual(
		TEXT("Strict compatibility rejects critical Definition changes"),
		StrictPrepare.Status,
		EIntentReplayPrepareStatus::Rejected);
	TestEqual(TEXT("Rejected preparation leaves a failed session"), Clone.Replay->GetPlaybackState(), EIntentReplayPlaybackState::Failed);

	FIntentReplayPlaybackOptions CompatibleOptions;
	CompatibleOptions.CompatibilityPolicy =
		EIntentReplayCompatibilityPolicy::CopyCompatibleValuesUseCurrentDefaults;
	const FIntentReplayPrepareResult CompatiblePrepare =
		Clone.Replay->PrepareReplay(Track, CompatibleOptions);
	TestEqual(TEXT("Compatible preparation succeeds"), CompatiblePrepare.Status, EIntentReplayPrepareStatus::Ready);
	UIntentReplayPlaybackSession* Session = Clone.Replay->GetActivePlaybackSession();
	TestEqual(TEXT("Compatibility reports match entries"), Session ? Session->GetCompatibilityReportCount() : 0, 2);
	FIntentReplayCompatibilityReport CompatibilityReport;
	TestTrue(
		TEXT("Compatibility report is readable"),
		Session && Session->GetCompatibilityReportByIndex(0, CompatibilityReport));
	TestTrue(
		TEXT("Compatible mode reports the ActionTag divergence"),
		CompatibilityReport.ConfigurationChanges.Contains(TEXT("ActionTag changed")));

	TestTrue(TEXT("Compatible replay starts"), Clone.Replay->StartReplay().Succeeded());
	TestEqual(TEXT("First entry submits immediately"), Session ? Session->GetNextEntryIndex() : 0, 1);
	TestTrue(TEXT("Replay pauses"), Clone.Replay->PauseReplay().Succeeded());
	Advance(*World, 0.2);
	TestEqual(TEXT("Paused timeline does not submit the second entry"), Session ? Session->GetNextEntryIndex() : 0, 1);
	TestTrue(TEXT("Replay resumes"), Clone.Replay->ResumeReplay().Succeeded());
	Advance(*World, 0.049);
	TestEqual(TEXT("Absolute scheduling waits for the remaining delay"), Session ? Session->GetNextEntryIndex() : 0, 1);
	Advance(*World, 0.002);
	TestEqual(TEXT("Second entry submits after the remaining delay"), Session ? Session->GetNextEntryIndex() : 0, 2);

	const TArray<FGameplayActionHandle> ReplayHandles =
		Session ? Session->GetReplayOwnedActionHandles() : TArray<FGameplayActionHandle>();
	TestEqual(TEXT("Two replay-owned handles are active"), ReplayHandles.Num(), 2);
	TestTrue(TEXT("Stopping replay succeeds"), Clone.Replay->StopReplay().Succeeded());
	TestEqual(TEXT("Replay enters Cancelled"), Clone.Replay->GetPlaybackState(), EIntentReplayPlaybackState::Cancelled);
	for (const FGameplayActionHandle Handle : ReplayHandles)
	{
		FGameplayActionResult Result;
		TestTrue(TEXT("Stopped replay result is retained"), Clone.Actions->GetActionResult(Handle, Result));
		TestEqual(TEXT("Stopped replay action is cancelled"), Result.TerminalState, EGameplayActionState::Cancelled);
	}
	EGameplayActionState ForeignState = EGameplayActionState::Created;
	TestTrue(TEXT("Unrelated action remains queryable"), Clone.Actions->GetActionState(ForeignSubmission.Handle, ForeignState));
	TestEqual(TEXT("Stop cancels only session-owned handles"), ForeignState, EGameplayActionState::Running);
	Clone.Actions->CancelAction(ForeignSubmission.Handle, FGameplayTag());

	const FIntentReplayPrepareResult TeardownPrepare =
		Clone.Replay->PrepareReplay(Track, CompatibleOptions);
	TestEqual(TEXT("A new playback session prepares after cancellation"), TeardownPrepare.Status, EIntentReplayPrepareStatus::Ready);
	TestTrue(TEXT("Teardown replay starts"), Clone.Replay->StartReplay().Succeeded());
	const TWeakObjectPtr<UIntentReplayComponent> DestroyedReplayComponent = Clone.Replay;
	Clone.Actor->Destroy();
	CollectGarbage(RF_NoFlags);
	Advance(*World, 2.0);
	TestFalse(TEXT("Destroyed component is no longer a callback target"), DestroyedReplayComponent.IsValid());
	Source.Actor->Destroy();
	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplayIdleTailCompletionTest,
	"IntentReplay.Playback.PreservesRecordedIdleTail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplayIdleTailCompletionTest::RunTest(const FString& Parameters)
{
	using namespace IntentReplayTests;
	UWorld* World = MakeWorld(TEXT("IntentReplayIdleTailCompletionTestWorld"));
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}

	const FEntity Source = MakeEntity(*World);
	UGameplayActionDefinition* Definition = MakeWaitDefinition(0.01);
	TestTrue(
		TEXT("Recording starts"),
		Source.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	TestTrue(
		TEXT("Short action is accepted"),
		Source.Actions->SubmitAction(MakeRequest(*Definition)).IsAccepted());
	Advance(*World, 0.02);
	Advance(*World, 1.0);
	TestTrue(
		TEXT("Recording finalizes after an idle tail"),
		Source.Replay->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate).Succeeded());
	UIntentReplayTrack* Track = Source.Replay->GetLastFinalizedTrack();
	TestNotNull(TEXT("Idle-tail track exists"), Track);
	TestTrue(
		TEXT("Recorded duration includes the idle tail"),
		Track && Track->GetRecordedDurationSeconds() >= 1.0);

	const FEntity Clone = MakeEntity(*World);
	const FIntentReplayPrepareResult Prepare =
		Clone.Replay->PrepareReplay(Track, FIntentReplayPlaybackOptions());
	TestEqual(TEXT("Idle-tail replay prepares"), Prepare.Status, EIntentReplayPrepareStatus::Ready);
	TestTrue(TEXT("Idle-tail replay starts"), Clone.Replay->StartReplay().Succeeded());
	Advance(*World, 0.02);
	TestEqual(
		TEXT("Replay remains Playing after its last action ends"),
		Clone.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Playing);
	Advance(*World, 0.5);
	TestEqual(
		TEXT("Replay remains Playing throughout the recorded idle tail"),
		Clone.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Playing);
	TestTrue(TEXT("Idle-tail replay pauses"), Clone.Replay->PauseReplay().Succeeded());
	Advance(*World, 1.0);
	TestEqual(
		TEXT("Paused idle-tail clock does not complete replay"),
		Clone.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Paused);
	TestTrue(TEXT("Idle-tail replay resumes"), Clone.Replay->ResumeReplay().Succeeded());
	Advance(*World, 0.51);
	TestEqual(
		TEXT("Replay completes only after the full active recorded duration"),
		Clone.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Completed);

	Source.Actor->Destroy();
	Clone.Actor->Destroy();
	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplaySameSessionPreemptionTest,
	"IntentReplay.Playback.SameSessionPreemptionContinues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplaySameSessionPreemptionTest::RunTest(const FString& Parameters)
{
	using namespace IntentReplayTests;
	UWorld* World = MakeWorld(TEXT("IntentReplaySameSessionPreemptionTestWorld"));
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}

	const FEntity Source = MakeEntity(*World);
	UGameplayActionDefinition* Definition = MakeWaitDefinition(1.0);
	Definition->ExecutionLocks.AddTag(TAG_IntentReplay_Test_MovementLock);
	TestTrue(
		TEXT("Player recording starts"),
		Source.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	const FGameplayActionSubmissionResult FirstPlayerAction =
		Source.Actions->SubmitAction(MakeRequest(*Definition, 0));
	TestTrue(TEXT("First player movement starts"), FirstPlayerAction.IsAccepted());
	Advance(*World, 0.05);
	const FGameplayActionSubmissionResult SecondPlayerAction =
		Source.Actions->SubmitAction(MakeRequest(*Definition, 10));
	TestTrue(TEXT("Replacement player movement starts"), SecondPlayerAction.IsAccepted());
	FGameplayActionResult FirstPlayerResult;
	TestTrue(
		TEXT("First player movement has a terminal result"),
		Source.Actions->GetActionResult(FirstPlayerAction.Handle, FirstPlayerResult));
	TestEqual(
		TEXT("Player replacement interrupts the first movement"),
		FirstPlayerResult.TerminalState,
		EGameplayActionState::Interrupted);
	TestTrue(
		TEXT("Player interruption identifies its replacement"),
		FirstPlayerResult.CausingActionHandle == SecondPlayerAction.Handle);
	TestTrue(
		TEXT("Player track finalizes immediately"),
		Source.Replay->RequestStopRecording().Succeeded());
	UIntentReplayTrack* Track = Source.Replay->GetLastFinalizedTrack();
	TestEqual(TEXT("Replacement pattern contains two intents"), Track ? Track->GetEntryCount() : 0, 2);
	Source.Actions->AbortAllActions(FGameplayTag());

	FIntentReplayPlaybackOptions StrictFailureOptions;
	StrictFailureOptions.TerminalFailurePolicy =
		EIntentReplayTerminalFailurePolicy::StopPlayback;

	const FEntity Clone = MakeEntity(*World);
	TestEqual(
		TEXT("Clone prepares the replacement pattern"),
		Clone.Replay->PrepareReplay(Track, StrictFailureOptions).Status,
		EIntentReplayPrepareStatus::Ready);
	TestTrue(TEXT("Clone replay starts"), Clone.Replay->StartReplay().Succeeded());
	Advance(*World, 0.051);
	TestEqual(
		TEXT("Same-session preemption does not fail playback"),
		Clone.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Playing);
	UIntentReplayPlaybackSession* CloneSession =
		Clone.Replay->GetActivePlaybackSession();
	TestEqual(
		TEXT("Only the replacement replay action remains active"),
		CloneSession ? CloneSession->GetReplayOwnedActionCount() : 0,
		1);
	Advance(*World, 1.01);
	TestEqual(
		TEXT("Clone completes after executing the replacement movement"),
		Clone.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Completed);

	const FEntity ExternallyPreemptedClone = MakeEntity(*World);
	TestEqual(
		TEXT("Second clone prepares the same track"),
		ExternallyPreemptedClone.Replay
			->PrepareReplay(Track, StrictFailureOptions)
			.Status,
		EIntentReplayPrepareStatus::Ready);
	TestTrue(
		TEXT("Second clone replay starts"),
		ExternallyPreemptedClone.Replay->StartReplay().Succeeded());
	AddExpectedError(
		TEXT("ended with EGameplayActionState::Interrupted"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	const FGameplayActionSubmissionResult ExternalAction =
		ExternallyPreemptedClone.Actions->SubmitAction(
			MakeRequest(*Definition, 100));
	TestTrue(TEXT("External preempting action starts"), ExternalAction.IsAccepted());
	TestEqual(
		TEXT("External preemption still fails strict playback"),
		ExternallyPreemptedClone.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Failed);

	ExternallyPreemptedClone.Actions->CancelAction(
		ExternalAction.Handle,
		FGameplayTag());
	Source.Actor->Destroy();
	Clone.Actor->Destroy();
	ExternallyPreemptedClone.Actor->Destroy();
	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplayExternalRecoveryTest,
	"IntentReplay.Playback.ExternalInterruptionRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplayExternalRecoveryTest::RunTest(
	const FString& Parameters)
{
	using namespace IntentReplayTests;
	UWorld* World =
		MakeWorld(TEXT("IntentReplayExternalRecoveryTestWorld"));
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}

	const FEntity Source = MakeEntity(*World);
	UGameplayActionDefinition* Definition = MakeWaitDefinition(0.2);
	TestTrue(
		TEXT("Recovery source recording starts"),
		Source.Replay->StartRecording(
			FIntentRecordingOptions()).Succeeded());
	TestTrue(
		TEXT("Recovery source action starts"),
		Source.Actions->SubmitAction(
			MakeRequest(*Definition)).IsAccepted());
	Advance(*World, 0.21);
	TestTrue(
		TEXT("Recovery source track finalizes"),
		Source.Replay->RequestStopRecording(
			EIntentRecordingFinalizeMode::Immediate).Succeeded());
	UIntentReplayTrack* Track = Source.Replay->GetLastFinalizedTrack();
	FRecordedIntent ImmutableEntryBefore;
	TestTrue(
		TEXT("Recovery track has one immutable entry"),
		Track && Track->GetEntryByIndex(0, ImmutableEntryBefore));

	const FEntity SatisfiedClone = MakeEntity(*World);
	TestEqual(
		TEXT("Satisfied clone prepares"),
		SatisfiedClone.Replay
			->PrepareReplay(Track, FIntentReplayPlaybackOptions())
			.Status,
		EIntentReplayPrepareStatus::Ready);
	TestTrue(
		TEXT("Satisfied clone starts"),
		SatisfiedClone.Replay->StartReplay().Succeeded());
	const FIntentReplayExternalInterruptionResult Interruption =
		SatisfiedClone.Replay->BeginExternalReplayInterruption(
			TAG_IntentReplay_Test_ExternalRecovery);
	TestTrue(
		TEXT("External interruption is atomic"),
		Interruption.Succeeded());
	TestEqual(
		TEXT("Exactly one replay-owned intent is suspended"),
		Interruption.SuspendedIntents.Num(),
		1);
	TestEqual(
		TEXT("Replay is paused before interruption returns"),
		SatisfiedClone.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Paused);
	TestTrue(
		TEXT("Pending recovery is observable"),
		SatisfiedClone.Replay->HasPendingExternalReplayRecovery());
	if (Interruption.SuspendedIntents.Num() == 1)
	{
		const FIntentReplaySuspendedIntent& Suspended =
			Interruption.SuspendedIntents[0];
		FGameplayActionResult InterruptedResult;
		TestTrue(
			TEXT("Interrupted runtime handle retains terminal result"),
			SatisfiedClone.Actions->GetActionResult(
				Suspended.InterruptedRuntimeHandle,
				InterruptedResult));
		TestEqual(
			TEXT("External recovery interruption is terminal Interrupted"),
			InterruptedResult.TerminalState,
			EGameplayActionState::Interrupted);
		TestTrue(
			TEXT("Journal reason remains the requested interruption reason"),
			InterruptedResult.ReasonTag
				== TAG_IntentReplay_Test_ExternalRecovery);
		TestEqual(
			TEXT("Resume is rejected until reconciliation"),
			SatisfiedClone.Replay->ResumeReplay().Status,
			EIntentReplayOperationStatus::PendingExternalRecovery);
		TestTrue(
			TEXT("Already-satisfied reconciliation succeeds"),
			SatisfiedClone.Replay
				->ResolveExternallyInterruptedIntentAsSatisfied(
					Suspended.RecordedIntent.RecordedIntentId)
				.Succeeded());
	}
	TestFalse(
		TEXT("Satisfied reconciliation clears pending recovery"),
		SatisfiedClone.Replay->HasPendingExternalReplayRecovery());
	TestTrue(
		TEXT("Replay resumes only after reconciliation"),
		SatisfiedClone.Replay->ResumeReplay().Succeeded());

	FRecordedIntent ImmutableEntryAfter;
	TestTrue(
		TEXT("Source track remains readable"),
		Track->GetEntryByIndex(0, ImmutableEntryAfter));
	TestTrue(
		TEXT("External recovery does not mutate Recorded Intent identity"),
		ImmutableEntryBefore.RecordedIntentId
			== ImmutableEntryAfter.RecordedIntentId);
	TestTrue(
		TEXT("External recovery does not mutate original correlation"),
		ImmutableEntryBefore.OriginalCorrelation.Id
			== ImmutableEntryAfter.OriginalCorrelation.Id);

	const FEntity ReissuedClone = MakeEntity(*World);
	TestEqual(
		TEXT("Reissued clone prepares"),
		ReissuedClone.Replay
			->PrepareReplay(Track, FIntentReplayPlaybackOptions())
			.Status,
		EIntentReplayPrepareStatus::Ready);
	TestTrue(
		TEXT("Reissued clone starts"),
		ReissuedClone.Replay->StartReplay().Succeeded());
	const FIntentReplayExternalInterruptionResult ReissueInterruption =
		ReissuedClone.Replay->BeginExternalReplayInterruption(
			TAG_IntentReplay_Test_ExternalRecovery);
	if (TestEqual(
		TEXT("Reissue captures exactly one intent"),
		ReissueInterruption.SuspendedIntents.Num(),
		1))
	{
		const FIntentReplayRecoveryResult Reissue =
			ReissuedClone.Replay->ReissueExternallyInterruptedIntent(
				ReissueInterruption.SuspendedIntents[0]
					.RecordedIntent.RecordedIntentId);
		TestTrue(
			TEXT("Non-movement intent can be reissued immediately"),
			Reissue.Succeeded());
		TestTrue(
			TEXT("Reissue receives a new runtime handle"),
			Reissue.SubmissionResult.Handle.IsValid()
				&& Reissue.SubmissionResult.Handle
					!= ReissueInterruption.SuspendedIntents[0]
						.InterruptedRuntimeHandle);
	}
	TestFalse(
		TEXT("Successful reissue clears pending recovery"),
		ReissuedClone.Replay->HasPendingExternalReplayRecovery());
	TestTrue(
		TEXT("Reissued replay resumes"),
		ReissuedClone.Replay->ResumeReplay().Succeeded());

	Source.Actor->Destroy();
	SatisfiedClone.Actor->Destroy();
	ReissuedClone.Actor->Destroy();
	DestroyWorld(World);
	return true;
}

#endif
