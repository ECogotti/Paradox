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
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IntentReplay_Test_OriginPlayer, "GameplayAction.Origin.TestPlayer");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_IntentReplay_Test_Correlation, "GameplayAction.Correlation.TestIntentReplay");

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

#endif
