#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actions/GameplayActionDefinition.h"
#include "Actions/GameplayWaitAction.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Components/IntentReplayObservationComponent.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Data/IntentReplayObservationTrack.h"
#include "Data/IntentReplayTimelineBundle.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "IntentReplayTags.h"
#include "Journal/IntentReplayObservationJournal.h"
#include "NativeGameplayTags.h"
#include "PerceptionKnowledgeTags.h"
#include "Recording/IntentReplayTrack.h"
#include "StructUtils/PropertyBag.h"
#include "Tests/IntentReplayPerceptionTestTypes.h"
#include "TimerManager.h"
#include "UObject/GarbageCollection.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(
	TAG_IntentReplayPerception_TestAction,
	"GameplayAction.Test.IntentReplayPerception.Action");
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	TAG_IntentReplayPerception_StatePrimary,
	"PerceptionKnowledge.State.Test.IntentReplayPerception.Primary");
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	TAG_IntentReplayPerception_StateSecondary,
	"PerceptionKnowledge.State.Test.IntentReplayPerception.Secondary");
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	TAG_IntentReplayPerception_Event,
	"PerceptionKnowledge.Event.Test.IntentReplayPerception.Semantic");
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	TAG_IntentReplayPerception_AmbiguousEvent,
	"PerceptionKnowledge.Event.Test.IntentReplayPerception.Ambiguous");
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	TAG_IntentReplayPerception_IgnoredEvent,
	"PerceptionKnowledge.Event.Test.IntentReplayPerception.Ignored");
UE_DEFINE_GAMEPLAY_TAG_STATIC(
	TAG_IntentReplayPerception_Cause,
	"PerceptionKnowledge.Cause.Test.IntentReplayPerception");

namespace IntentReplayPerceptionTests
{
	struct FEntity
	{
		AActor* Actor = nullptr;
		UGameplayActionComponent* Actions = nullptr;
		UIntentReplayComponent* Replay = nullptr;
		UPerceptionKnowledgeListenerComponent* Listener = nullptr;
		UIntentReplayObservationComponent* Observations = nullptr;
	};

	UWorld* MakeWorld(const TCHAR* Name)
	{
		FWorldContext* Context = GEngine
			? &GEngine->CreateNewWorldContext(EWorldType::Game)
			: nullptr;
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
		if (World)
		{
			World->AddToRoot();
		}
		if (Context)
		{
			Context->SetCurrentWorld(World);
		}
		UIntentReplayPerceptionTestTimeSource::ResetTime();
		return World;
	}

	void DestroyWorld(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		World->DestroyWorld(true);
		if (GEngine)
		{
			GEngine->DestroyWorldContext(World);
		}
		World->RemoveFromRoot();
	}

	void Advance(UWorld& World, const double DeltaSeconds)
	{
		++GFrameCounter;
		World.GetTimerManager().Tick(0.0f);
		UIntentReplayPerceptionTestTimeSource::AdvanceTime(DeltaSeconds);
		++GFrameCounter;
		World.GetTimerManager().Tick(static_cast<float>(DeltaSeconds));
	}

	void AddAndRegister(AActor& Owner, UActorComponent& Component)
	{
		Owner.AddInstanceComponent(&Component);
		Component.RegisterComponent();
	}

	UPerceptionKnowledgeSourceComponent* AddPerceptionSource(AActor& Owner)
	{
		UPerceptionKnowledgeSourceComponent* Source =
			NewObject<UPerceptionKnowledgeSourceComponent>(
				&Owner,
				NAME_None,
				RF_Transient);
		AddAndRegister(Owner, *Source);
		return Source;
	}

	FEntity MakeEntity(UWorld& World)
	{
		FEntity Entity;
		Entity.Actor = World.SpawnActor<AActor>();
		if (!Entity.Actor)
		{
			return Entity;
		}

		Entity.Actions = NewObject<UGameplayActionComponent>(
			Entity.Actor,
			NAME_None,
			RF_Transient);
		AddAndRegister(*Entity.Actor, *Entity.Actions);

		Entity.Replay = NewObject<UIntentReplayComponent>(
			Entity.Actor,
			NAME_None,
			RF_Transient);
		Entity.Replay->ActionComponentOverride = Entity.Actions;
		Entity.Replay->TimeSourceClass =
			UIntentReplayPerceptionTestTimeSource::StaticClass();
		AddAndRegister(*Entity.Actor, *Entity.Replay);
		Entity.Replay->InitializeIntentReplay();

		Entity.Listener = NewObject<UPerceptionKnowledgeListenerComponent>(
			Entity.Actor,
			NAME_None,
			RF_Transient);
		Entity.Actor->AddInstanceComponent(Entity.Listener);

		Entity.Observations = NewObject<UIntentReplayObservationComponent>(
			Entity.Actor,
			NAME_None,
			RF_Transient);
		Entity.Observations->IntentReplaySourceOverride = Entity.Replay;
		Entity.Observations->PerceptionListenerOverride = Entity.Listener;
		AddAndRegister(*Entity.Actor, *Entity.Observations);
		Entity.Observations->InitializeObservationReplay();
		return Entity;
	}

	UGameplayActionDefinition* MakeWaitDefinition(const double Duration)
	{
		UGameplayActionDefinition* Definition =
			NewObject<UGameplayActionDefinition>(
				GetTransientPackage(),
				NAME_None,
				RF_Transient);
		Definition->InstanceClass = UGameplayWaitAction::StaticClass();
		Definition->ActionTag = TAG_IntentReplayPerception_TestAction;
		Definition->JournalRequirement = EGameplayActionJournalRequirement::Required;
		Definition->DefaultParameters.InitializeFromBagStruct(
			UPropertyBag::GetOrCreateFromDescs(
				{ { TEXT("Duration"), EPropertyBagPropertyType::Double } }));
		Definition->DefaultParameters.SetValueDouble(TEXT("Duration"), Duration);
		return Definition;
	}

	FGameplayActionRequest MakeRequest(UGameplayActionDefinition& Definition)
	{
		const FGameplayActionRequestCreationResult Creation =
			UGameplayActionBlueprintLibrary::CreateActionRequest(&Definition);
		return Creation.WasCreated()
			? Creation.Request
			: FGameplayActionRequest();
	}

	FPerceptionKnowledgeStateObservation MakeState(
		const FPerceptionKnowledgeEntityId EntityId,
		const FGameplayTag StateTag,
		const EPerceptionKnowledgeFactStatus Status,
		const FPerceptionKnowledgeValue& Value,
		const FVector Location = FVector(100.0, 0.0, 0.0))
	{
		FPerceptionKnowledgeStateObservation State;
		State.Key.EntityId = EntityId;
		State.Key.StateTag = StateTag;
		State.Status = Status;
		State.Value = Value;
		State.SenseTag = PerceptionKnowledgeTags::Sense_Sight;
		State.Confidence = 0.8f;
		State.ObservationLocation = Location;
		return State;
	}

	FPerceptionKnowledgeEventObservation MakeEvent(
		const FPerceptionKnowledgeEntityId SourceId,
		const FPerceptionKnowledgeEntityId InstigatorId,
		const FGameplayTag EventTag,
		const FVector Location,
		const FGuid ObservationId = FGuid::NewGuid())
	{
		FPerceptionKnowledgeEventObservation Event;
		Event.ObservationId = ObservationId;
		Event.EventTag = EventTag;
		Event.SenseTag = PerceptionKnowledgeTags::Sense_Hearing;
		Event.SourceEntityId = SourceId;
		Event.InstigatorEntityId = InstigatorId;
		Event.WorldLocation = Location;
		Event.Strength = 1.0f;
		Event.Loudness = 0.75f;
		Event.Confidence = 0.9f;
		Event.CauseTag = TAG_IntentReplayPerception_Cause;
		return Event;
	}

	void Broadcast(
		UPerceptionKnowledgeListenerComponent& Listener,
		const FPerceptionKnowledgeStateObservation& State)
	{
		Listener.OnObservationProducedNative().Broadcast(
			FPerceptionKnowledgeObservation::FromState(State));
	}

	void Broadcast(
		UPerceptionKnowledgeListenerComponent& Listener,
		const FPerceptionKnowledgeEventObservation& Event)
	{
		Listener.OnObservationProducedNative().Broadcast(
			FPerceptionKnowledgeObservation::FromEvent(Event));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplayPerceptionRecordingAndComparisonTest,
	"IntentReplayPerception.EndToEnd.RecordingBundleMatchingJournalAndDebug",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplayPerceptionRecordingAndComparisonTest::RunTest(
	const FString& Parameters)
{
	using namespace IntentReplayPerceptionTests;
	UWorld* World = MakeWorld(TEXT("IntentReplayPerceptionEndToEndWorld"));
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}

	const FEntity Source = MakeEntity(*World);
	TestTrue(
		TEXT("Observation adapter initializes"),
		Source.Observations
			&& Source.Observations->IsObservationReplayInitialized());
	UGameplayActionDefinition* Definition = MakeWaitDefinition(2.0);
	const FIntentRecordingStartResult Start =
		Source.Replay->StartRecording(FIntentRecordingOptions());
	TestTrue(TEXT("Core recording starts"), Start.Succeeded());
	TestNotNull(
		TEXT("Observation recording auto-starts"),
		Source.Observations->GetActiveObservationRecordingSession());
	TestTrue(
		TEXT("Action occupies the replay during comparison"),
		Source.Actions->SubmitAction(MakeRequest(*Definition)).IsAccepted());

	const FPerceptionKnowledgeEntityId EntityId(FGuid::NewGuid());
	const FPerceptionKnowledgeEntityId InstigatorId(FGuid::NewGuid());
	FPerceptionKnowledgeStateObservation Primary = MakeState(
		EntityId,
		TAG_IntentReplayPerception_StatePrimary,
		EPerceptionKnowledgeFactStatus::Known,
		FPerceptionKnowledgeValue::MakeBool(false));
	Broadcast(*Source.Listener, Primary);
	Broadcast(*Source.Listener, Primary);
	Primary.Value = FPerceptionKnowledgeValue::MakeBool(true);

	Advance(*World, 0.1);
	Broadcast(
		*Source.Listener,
		MakeState(
			EntityId,
			TAG_IntentReplayPerception_StatePrimary,
			EPerceptionKnowledgeFactStatus::Unknown,
			FPerceptionKnowledgeValue()));
	Advance(*World, 0.1);
	Broadcast(
		*Source.Listener,
		MakeState(
			EntityId,
			TAG_IntentReplayPerception_StatePrimary,
			EPerceptionKnowledgeFactStatus::Invalidated,
			FPerceptionKnowledgeValue()));
	Advance(*World, 0.1);
	Source.Listener->OnEntityPerceptionChangedNative().Broadcast(
		EntityId,
		PerceptionKnowledgeTags::Sense_Sight,
		true);
	Broadcast(
		*Source.Listener,
		MakeState(
			EntityId,
			TAG_IntentReplayPerception_StatePrimary,
			EPerceptionKnowledgeFactStatus::Known,
			FPerceptionKnowledgeValue::MakeBool(false)));

	Advance(*World, 0.1);
	const FPerceptionKnowledgeEventObservation FirstEvent = MakeEvent(
		EntityId,
		InstigatorId,
		TAG_IntentReplayPerception_Event,
		FVector(200.0, 0.0, 0.0));
	Broadcast(*Source.Listener, FirstEvent);
	Broadcast(*Source.Listener, FirstEvent);
	Advance(*World, 0.05);
	Broadcast(
		*Source.Listener,
		MakeEvent(
			EntityId,
			InstigatorId,
			TAG_IntentReplayPerception_Event,
			FVector(260.0, 0.0, 0.0)));
	Advance(*World, 0.05);
	Broadcast(
		*Source.Listener,
		MakeState(
			EntityId,
			TAG_IntentReplayPerception_StateSecondary,
			EPerceptionKnowledgeFactStatus::Known,
			FPerceptionKnowledgeValue::MakeBool(true)));
	Advance(*World, 0.05);
	Broadcast(
		*Source.Listener,
		MakeEvent(
			EntityId,
			InstigatorId,
			TAG_IntentReplayPerception_AmbiguousEvent,
			FVector(300.0, 0.0, 0.0)));
	Broadcast(
		*Source.Listener,
		MakeEvent(
			EntityId,
			InstigatorId,
			TAG_IntentReplayPerception_AmbiguousEvent,
			FVector(300.0, 0.0, 0.0)));

	TestTrue(
		TEXT("Core immediate finalization succeeds"),
		Source.Replay->RequestStopRecording(
			EIntentRecordingFinalizeMode::Immediate).Succeeded());
	UIntentReplayObservationTrack* ObservationTrack =
		Source.Observations->GetLastFinalizedObservationTrack();
	UIntentReplayTimelineBundle* Bundle =
		Source.Observations->GetLastTimelineBundle();
	TestNotNull(TEXT("Observation Track publishes only after Action Track"), ObservationTrack);
	TestNotNull(TEXT("Validated Timeline Bundle is published"), Bundle);
	if (!ObservationTrack || !Bundle)
	{
		Source.Actor->Destroy();
		DestroyWorld(World);
		return false;
	}
	TestTrue(TEXT("Observation Track validates"), ObservationTrack->ValidateTrack().bValid);
	TestTrue(TEXT("Timeline Bundle validates"), Bundle->ValidateBundle().bValid);
	TestEqual(TEXT("Deduplication preserves nine semantic records"), ObservationTrack->GetEntryCount(), 9);
	FIntentReplayRecordedObservation FirstRecorded;
	TestTrue(TEXT("First state copy is readable"), ObservationTrack->GetEntryByIndex(0, FirstRecorded));
	bool RecordedBool = true;
	TestTrue(TEXT("Recorded bool copy can be read"), FirstRecorded.State.Value.GetBool(RecordedBool));
	TestFalse(TEXT("Recorded value is a deep copy"), RecordedBool);
	TestEqual(
		TEXT("Runtime event duplicate is counted once"),
		Source.Observations->GetRuntimeStats().DuplicateObservations,
		int64(1));

	const FEntity Unrelated = MakeEntity(*World);
	TestTrue(
		TEXT("Unrelated core recording starts"),
		Unrelated.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	TestTrue(
		TEXT("Unrelated core track finalizes"),
		Unrelated.Replay->RequestStopRecording(
			EIntentRecordingFinalizeMode::Immediate).Succeeded());
	FIntentReplayObservationOperationResult WrongPairing;
	TestNull(
		TEXT("Tracks from different sessions cannot be paired"),
		Source.Observations->CreateTimelineBundle(
			Unrelated.Replay->GetLastFinalizedTrack(),
			ObservationTrack,
			WrongPairing));
	TestEqual(
		TEXT("Wrong pairing is diagnosed"),
		WrongPairing.Status,
		EIntentReplayObservationOperationStatus::BundleInvalid);
	Unrelated.Actor->Destroy();

	UIntentReplayPerceptionTestBundleHolder* Holder =
		NewObject<UIntentReplayPerceptionTestBundleHolder>(
			GetTransientPackage(),
			NAME_None,
			RF_Transient);
	Holder->Bundle = Bundle;
	Holder->Definition = Definition;
	Holder->AddToRoot();
	Source.Actor->Destroy();
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("Bundle survives source destruction through reflected ownership"), IsValid(Holder->Bundle));
	TestTrue(TEXT("Value-only Observation Track still validates"), Holder->Bundle->ValidateBundle().bValid);

	const FEntity Clone = MakeEntity(*World);
	const FIntentReplayPrepareResult Prepare =
		Clone.Replay->PrepareReplay(
			Holder->Bundle->GetActionTrack(),
			FIntentReplayPlaybackOptions());
	TestTrue(TEXT("Clone accepts Action Track"), Prepare.WasAccepted());
	FIntentReplayObservationMatchOptions MatchOptions;
	MatchOptions.JournalOptions.MaxEntries = 3;
	TestTrue(
		TEXT("Comparison arms in Ready"),
		Clone.Observations->StartObservationComparison(
			Holder->Bundle,
			MatchOptions).Succeeded());
	TestTrue(TEXT("Clone replay starts"), Clone.Replay->StartReplay().Succeeded());

	Broadcast(
		*Clone.Listener,
		MakeState(
			EntityId,
			TAG_IntentReplayPerception_StatePrimary,
			EPerceptionKnowledgeFactStatus::Known,
			FPerceptionKnowledgeValue::MakeBool(false)));
	TestTrue(TEXT("Core replay pauses"), Clone.Replay->PauseReplay().Succeeded());
	Broadcast(
		*Clone.Listener,
		MakeEvent(
			EntityId,
			InstigatorId,
			TAG_IntentReplayPerception_IgnoredEvent,
			FVector::ZeroVector));
	TestTrue(TEXT("Core replay resumes"), Clone.Replay->ResumeReplay().Succeeded());

	Advance(*World, 0.1);
	Broadcast(
		*Clone.Listener,
		MakeState(
			EntityId,
			TAG_IntentReplayPerception_StatePrimary,
			EPerceptionKnowledgeFactStatus::Unknown,
			FPerceptionKnowledgeValue()));
	Advance(*World, 0.1);
	Broadcast(
		*Clone.Listener,
		MakeState(
			EntityId,
			TAG_IntentReplayPerception_StatePrimary,
			EPerceptionKnowledgeFactStatus::Invalidated,
			FPerceptionKnowledgeValue()));
	Advance(*World, 0.1);
	Clone.Listener->OnEntityPerceptionChangedNative().Broadcast(
		EntityId,
		PerceptionKnowledgeTags::Sense_Sight,
		true);
	Broadcast(
		*Clone.Listener,
		MakeState(
			EntityId,
			TAG_IntentReplayPerception_StatePrimary,
			EPerceptionKnowledgeFactStatus::Known,
			FPerceptionKnowledgeValue::MakeBool(false)));
	Advance(*World, 0.1);
	FPerceptionKnowledgeEventObservation CurrentEvent = MakeEvent(
		EntityId,
		InstigatorId,
		TAG_IntentReplayPerception_Event,
		FVector(200.0, 0.0, 0.0));
	Broadcast(*Clone.Listener, CurrentEvent);
	Broadcast(*Clone.Listener, CurrentEvent);
	Advance(*World, 0.05);
	Broadcast(
		*Clone.Listener,
		MakeEvent(
			EntityId,
			InstigatorId,
			TAG_IntentReplayPerception_Event,
			FVector(260.0, 0.0, 0.0)));
	Advance(*World, 0.05);
	Broadcast(
		*Clone.Listener,
		MakeState(
			EntityId,
			TAG_IntentReplayPerception_StateSecondary,
			EPerceptionKnowledgeFactStatus::Known,
			FPerceptionKnowledgeValue::MakeBool(false)));
	Advance(*World, 0.05);
	Broadcast(
		*Clone.Listener,
		MakeEvent(
			EntityId,
			InstigatorId,
			TAG_IntentReplayPerception_AmbiguousEvent,
			FVector(300.0, 0.0, 0.0)));

	FIntentReplayObservationComparisonSummary Summary =
		Clone.Observations->GetComparisonSummary();
	TestEqual(TEXT("Six observations match"), Summary.Matched, 6);
	TestEqual(TEXT("Value discrepancy is journalized"), Summary.Unexpected, 1);
	TestEqual(TEXT("Equivalent candidates are ambiguous"), Summary.Ambiguous, 1);
	TestEqual(TEXT("Repeated runtime event is duplicate"), Summary.Duplicate, 1);
	TestEqual(TEXT("Paused callback is ignored"), Summary.Ignored, 1);
	TestEqual(TEXT("Ambiguity consumes neither expected event"), Summary.PendingExpected, 2);

	UIntentReplayObservationJournal* Journal =
		Clone.Observations->GetActiveObservationJournal();
	TArray<FIntentReplayObservationJournalEntry> Copy = Journal->GetEntries();
	const int32 AuthoritativeEntryCount = Journal->GetEntryCount();
	TestEqual(TEXT("Journal uses the configured ring capacity"), AuthoritativeEntryCount, 3);
	Copy.Reset();
	TestEqual(
		TEXT("Journal collection queries return copies"),
		Journal->GetEntryCount(),
		AuthoritativeEntryCount);

	Advance(*World, 2.0);
	Summary = Clone.Observations->GetComparisonSummary();
	TestEqual(TEXT("Replay completion expires unresolved expected events"), Summary.ExpiredUnobserved, 2);
	TestEqual(TEXT("Completed journal has no pending expected entries"), Summary.PendingExpected, 0);
	TestTrue(TEXT("Comparison journal is terminal"), Journal->IsTerminal());
	TestEqual(
		TEXT("Journal retains terminal comparison status"),
		Journal->GetTerminalState(),
		EIntentReplayObservationComparisonState::Completed);
	TestFalse(
		TEXT("Journal retains policy identity"),
		Journal->GetMatchPolicyIdentity().IsNone());

	IConsoleVariable* DebugCVar =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("IntentReplayPerception.Debug"));
	TestNotNull(TEXT("Global debug CVar exists"), DebugCVar);
	const int32 OriginalDebugValue = DebugCVar ? DebugCVar->GetInt() : 0;
	Clone.Observations->SetDebugEnabled(false);
	TestFalse(
		TEXT("Local debug off exits before expensive work"),
		Clone.Observations->BuildDebugFrame().bExpensiveDataBuilt);
	Clone.Observations->SetDebugEnabled(true);
	if (DebugCVar)
	{
		DebugCVar->Set(0, ECVF_SetByCode);
	}
	TestFalse(
		TEXT("Global debug off exits before expensive work"),
		Clone.Observations->BuildDebugFrame().bExpensiveDataBuilt);
	if (DebugCVar)
	{
		DebugCVar->Set(1, ECVF_SetByCode);
	}
	const FIntentReplayObservationComparisonSummary BeforeDebug =
		Clone.Observations->GetComparisonSummary();
	const FIntentReplayObservationDebugFrame DebugFrame =
		Clone.Observations->BuildDebugFrame();
	TestTrue(TEXT("Both debug gates build a value-only frame"), DebugFrame.bExpensiveDataBuilt);
	TestEqual(
		TEXT("Debug does not mutate comparison results"),
		Clone.Observations->GetComparisonSummary().Compared,
		BeforeDebug.Compared);
	if (DebugCVar)
	{
		DebugCVar->Set(OriginalDebugValue, ECVF_SetByCode);
	}

	Clone.Actor->Destroy();
	Holder->RemoveFromRoot();
	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplayPerceptionCrossOwnerLifecycleTest,
	"IntentReplayPerception.Lifecycle.PlayerControllerPawnCrossOwnerAndRebind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplayPerceptionCrossOwnerLifecycleTest::RunTest(
	const FString& Parameters)
{
	using namespace IntentReplayPerceptionTests;
	UWorld* World = MakeWorld(TEXT("IntentReplayPerceptionCrossOwnerWorld"));
	if (!World)
	{
		return false;
	}

	APawn* Pawn = World->SpawnActor<APawn>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	TestNotNull(TEXT("Pawn exists"), Pawn);
	TestNotNull(TEXT("Player Controller exists"), Controller);
	if (!Pawn || !Controller)
	{
		DestroyWorld(World);
		return false;
	}
	Controller->Possess(Pawn);

	UGameplayActionComponent* Actions =
		NewObject<UGameplayActionComponent>(Pawn, NAME_None, RF_Transient);
	AddAndRegister(*Pawn, *Actions);
	UIntentReplayComponent* Replay =
		NewObject<UIntentReplayComponent>(Pawn, NAME_None, RF_Transient);
	Replay->ActionComponentOverride = Actions;
	Replay->TimeSourceClass =
		UIntentReplayPerceptionTestTimeSource::StaticClass();
	AddAndRegister(*Pawn, *Replay);
	Replay->InitializeIntentReplay();

	UPerceptionKnowledgeListenerComponent* Listener =
		NewObject<UPerceptionKnowledgeListenerComponent>(
			Controller,
			NAME_None,
			RF_Transient);
	Controller->AddInstanceComponent(Listener);
	UIntentReplayObservationComponent* Adapter =
		NewObject<UIntentReplayObservationComponent>(
			Pawn,
			NAME_None,
			RF_Transient);
	Adapter->IntentReplaySourceOverride = Replay;
	Adapter->PerceptionListenerOverride = Listener;
	AddAndRegister(*Pawn, *Adapter);
	Adapter->InitializeObservationReplay();
	TestTrue(TEXT("Cross-owner explicit binding initializes"), Adapter->IsObservationReplayInitialized());
	TestTrue(TEXT("Replay binding stays on Pawn"), Adapter->GetBoundIntentReplaySource() == Replay);
	TestTrue(TEXT("Listener binding stays on Controller"), Adapter->GetBoundPerceptionKnowledgeListener() == Listener);

	TestTrue(
		TEXT("Core recording starts"),
		Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	UPerceptionKnowledgeListenerComponent* Replacement =
		NewObject<UPerceptionKnowledgeListenerComponent>(
			Controller,
			NAME_None,
			RF_Transient);
	Controller->AddInstanceComponent(Replacement);
	TestEqual(
		TEXT("Rebinding is rejected while a synchronized session is active"),
		Adapter->SetPerceptionKnowledgeListener(Replacement).Status,
		EIntentReplayObservationOperationStatus::InvalidState);
	TestTrue(TEXT("Core cancellation succeeds"), Replay->CancelRecording().Succeeded());
	TestTrue(
		TEXT("Rebinding succeeds after terminal transition"),
		Adapter->SetPerceptionKnowledgeListener(Replacement).Succeeded());

	Controller->UnPossess();
	Pawn->Destroy();
	Controller->Destroy();
	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplayPerceptionLateStateReacquisitionTest,
	"IntentReplayPerception.EndToEnd.PersistentStateReacquisitionDetectsLateValueMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplayPerceptionLateStateReacquisitionTest::RunTest(
	const FString& Parameters)
{
	using namespace IntentReplayPerceptionTests;
	UWorld* World = MakeWorld(TEXT("IntentReplayPerceptionLateStateWorld"));
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}

	const FEntity Recorder = MakeEntity(*World);
	UGameplayActionDefinition* DurationDefinition = MakeWaitDefinition(30.0);
	TestTrue(
		TEXT("State recording starts"),
		Recorder.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	TestTrue(
		TEXT("State timeline receives a long-running intent"),
		Recorder.Actions
			->SubmitAction(MakeRequest(*DurationDefinition))
			.IsAccepted());

	const FPerceptionKnowledgeEntityId CubeEntityId(FGuid::NewGuid());
	const FPerceptionKnowledgeStateObservation PoweredOff = MakeState(
		CubeEntityId,
		TAG_IntentReplayPerception_StatePrimary,
		EPerceptionKnowledgeFactStatus::Known,
		FPerceptionKnowledgeValue::MakeBool(false));
	Broadcast(*Recorder.Listener, PoweredOff);
	Advance(*World, 5.0);
	Recorder.Listener->OnEntityPerceptionChangedNative().Broadcast(
		CubeEntityId,
		PerceptionKnowledgeTags::Sense_Sight,
		true);
	Broadcast(*Recorder.Listener, PoweredOff);

	TestTrue(
		TEXT("State timeline finalizes"),
		Recorder.Replay
			->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate)
			.Succeeded());
	UIntentReplayTimelineBundle* Bundle =
		Recorder.Observations->GetLastTimelineBundle();
	TestNotNull(TEXT("State timeline bundle exists"), Bundle);
	if (!Bundle)
	{
		Recorder.Actor->Destroy();
		DestroyWorld(World);
		return false;
	}
	TestEqual(
		TEXT("Initial sight and reacquisition are separate State snapshots"),
		Bundle->GetObservationTrack()->GetEntryCount(),
		2);

	const FEntity Clone = MakeEntity(*World);
	TestTrue(
		TEXT("Clone prepares the State action track"),
		Clone.Replay
			->PrepareReplay(
				Bundle->GetActionTrack(),
				FIntentReplayPlaybackOptions())
			.WasAccepted());
	FIntentReplayObservationMatchOptions MatchOptions;
	MatchOptions.bTreatPersistentStateObservationsAsOrderedSnapshots = true;
	TestTrue(
		TEXT("Clone arms ordered persistent State comparison"),
		Clone.Observations
			->StartObservationComparison(Bundle, MatchOptions)
			.Succeeded());
	TestTrue(
		TEXT("Clone starts State replay"),
		Clone.Replay->StartReplay().Succeeded());

	Broadcast(*Clone.Listener, PoweredOff);
	Advance(*World, 8.0);
	Clone.Listener->OnEntityPerceptionChangedNative().Broadcast(
		CubeEntityId,
		PerceptionKnowledgeTags::Sense_Sight,
		true);
	Broadcast(
		*Clone.Listener,
		MakeState(
			CubeEntityId,
			TAG_IntentReplayPerception_StatePrimary,
			EPerceptionKnowledgeFactStatus::Known,
			FPerceptionKnowledgeValue::MakeBool(true)));

	const FIntentReplayObservationComparisonSummary Summary =
		Clone.Observations->GetComparisonSummary();
	TestEqual(TEXT("Initial powered state matches"), Summary.Matched, 1);
	TestEqual(
		TEXT("Late reacquisition detects one unexpected value"),
		Summary.Unexpected,
		1);
	TestEqual(
		TEXT("Ordered State snapshots leave no expected record pending"),
		Summary.PendingExpected,
		0);
	const FIntentReplayObservationJournalEntry LastEntry =
		Clone.Observations->GetActiveObservationJournal()->GetEntries().Last();
	TestEqual(
		TEXT("Late State is classified as a value mismatch"),
		LastEntry.Result,
		EIntentReplayObservationMatchResult::UnexpectedStateValue);
	TestEqual(
		TEXT("Late State keeps the structured value mismatch reason"),
		LastEntry.Reason,
		EIntentReplayObservationMismatchReason::StateValueMismatch);
	TestTrue(
		TEXT("The test exceeds the default State time window"),
		FMath::Abs(LastEntry.TimeDelta)
			> MatchOptions.StateLateTolerance);

	Clone.Actor->Destroy();
	Recorder.Actor->Destroy();
	DestroyWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIntentReplayPerceptionReplaySourceCausalFootstepTest,
	"IntentReplayPerception.EndToEnd.ReplaySourceCausalEventSurvivesReconstructionDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIntentReplayPerceptionReplaySourceCausalFootstepTest::RunTest(
	const FString& Parameters)
{
	using namespace IntentReplayPerceptionTests;
	UWorld* World = MakeWorld(TEXT("IntentReplayPerceptionCausalReplayWorld"));
	if (!TestNotNull(TEXT("Test world exists"), World))
	{
		return false;
	}
	const FIntentReplayObservationMatchOptions GenericDefaults;
	TestFalse(
		TEXT("Verified causal occurrence identity remains a generic opt-in"),
		GenericDefaults.bTreatVerifiedCausalEventsAsOccurrenceIdentity);

	UGameplayActionDefinition* LongMoveDefinition = MakeWaitDefinition(30.0);
	const FEntity T0Recorder = MakeEntity(*World);
	TestTrue(
		TEXT("T0 recording starts"),
		T0Recorder.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	TestTrue(
		TEXT("T0 records a long-running intent"),
		T0Recorder.Actions
			->SubmitAction(MakeRequest(*LongMoveDefinition))
			.IsAccepted());
	TestTrue(
		TEXT("T0 track finalizes"),
		T0Recorder.Replay
			->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate)
			.Succeeded());
	UIntentReplayTrack* T0Track = T0Recorder.Replay->GetLastFinalizedTrack();
	TestNotNull(TEXT("T0 action track exists"), T0Track);
	FRecordedIntent T0Intent;
	TestTrue(
		TEXT("T0 causal intent is readable"),
		T0Track && T0Track->GetEntryByIndex(0, T0Intent));

	const FEntity T0FirstClone = MakeEntity(*World);
	UPerceptionKnowledgeSourceComponent* T0FirstSource =
		AddPerceptionSource(*T0FirstClone.Actor);
	const FPerceptionKnowledgeEntityId StableT0EntityId =
		T0FirstSource->GetEntityId();
	TestTrue(TEXT("T0 first clone has a stable Source ID"), StableT0EntityId.IsValid());
	TestTrue(
		TEXT("T0 first clone prepares replay"),
		T0FirstClone.Replay
			->PrepareReplay(T0Track, FIntentReplayPlaybackOptions())
			.WasAccepted());
	TestTrue(
		TEXT("T0 first clone starts replay"),
		T0FirstClone.Replay->StartReplay().Succeeded());

	const FEntity T1Recorder = MakeEntity(*World);
	UGameplayActionDefinition* T1DurationDefinition = MakeWaitDefinition(30.0);
	TestTrue(
		TEXT("T1 observation recording starts"),
		T1Recorder.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	TestTrue(
		TEXT("T1 has enough duration for delayed comparison"),
		T1Recorder.Actions
			->SubmitAction(MakeRequest(*T1DurationDefinition))
			.IsAccepted());
	Broadcast(
		*T1Recorder.Listener,
		MakeEvent(
			StableT0EntityId,
			StableT0EntityId,
			TAG_IntentReplayPerception_Event,
			FVector(100.0, 0.0, 0.0)));
	TestTrue(
		TEXT("T1 timeline finalizes"),
		T1Recorder.Replay
			->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate)
			.Succeeded());
	UIntentReplayTimelineBundle* T1Bundle =
		T1Recorder.Observations->GetLastTimelineBundle();
	TestNotNull(TEXT("T1 timeline bundle exists"), T1Bundle);
	FIntentReplayRecordedObservation RecordedNoise;
	TestTrue(
		TEXT("T1 recorded the T0 noise"),
		T1Bundle
			&& T1Bundle->GetObservationTrack()->GetEntryByIndex(
				0,
				RecordedNoise));
	TestEqual(
		TEXT("Recorded noise carries the T0 Recorded Intent ID"),
		RecordedNoise.Event.Correlation.CausalRecordedIntentId,
		T0Intent.RecordedIntentId);
	TestEqual(
		TEXT("Recorded causal identity is verified"),
		RecordedNoise.Event.Correlation.Reliability,
		EIntentReplayObservationCorrelationReliability::Verified);

	T0FirstClone.Actor->Destroy();
	const FEntity T0RebuiltClone = MakeEntity(*World);
	UPerceptionKnowledgeSourceComponent* T0RebuiltSource =
		AddPerceptionSource(*T0RebuiltClone.Actor);
	TestTrue(
		TEXT("Rebuilt T0 Source can be disabled"),
		T0RebuiltSource->SetSourceEnabled(false).IsSuccess());
	TestTrue(
		TEXT("Rebuilt T0 accepts its timeline-stable Source ID"),
		T0RebuiltSource->AssignEntityId(StableT0EntityId).IsSuccess());
	TestTrue(
		TEXT("Rebuilt T0 Source can be registered"),
		T0RebuiltSource->SetSourceEnabled(true).IsSuccess());
	TestTrue(
		TEXT("Rebuilt T0 prepares the same replay"),
		T0RebuiltClone.Replay
			->PrepareReplay(T0Track, FIntentReplayPlaybackOptions())
			.WasAccepted());
	TestTrue(
		TEXT("Rebuilt T0 starts the same replay"),
		T0RebuiltClone.Replay->StartReplay().Succeeded());

	const FEntity T1Clone = MakeEntity(*World);
	TestTrue(
		TEXT("T1 clone prepares its action replay"),
		T1Clone.Replay
			->PrepareReplay(
				T1Bundle->GetActionTrack(),
				FIntentReplayPlaybackOptions())
			.WasAccepted());
	FIntentReplayObservationMatchOptions MatchOptions;
	MatchOptions.bTreatVerifiedCausalEventsAsOccurrenceIdentity = true;
	TestTrue(
		TEXT("T1 clone starts perceptual comparison"),
		T1Clone.Observations
			->StartObservationComparison(T1Bundle, MatchOptions)
			.Succeeded());
	TestTrue(
		TEXT("T1 clone starts action replay"),
		T1Clone.Replay->StartReplay().Succeeded());

	Advance(*World, 2.0);
	FPerceptionKnowledgeEventObservation DelayedDisplacedNoise = MakeEvent(
		StableT0EntityId,
		StableT0EntityId,
		TAG_IntentReplayPerception_Event,
		FVector(1500.0, 0.0, 0.0));
	DelayedDisplacedNoise.Strength = 0.25f;
	DelayedDisplacedNoise.Loudness = 0.2f;
	Broadcast(*T1Clone.Listener, DelayedDisplacedNoise);

	FIntentReplayObservationComparisonSummary Summary =
		T1Clone.Observations->GetComparisonSummary();
	TestEqual(
		TEXT("The reconstructed replay-owned noise matches despite recovery drift"),
		Summary.Matched,
		1);
	TestEqual(
		TEXT("The matched historical noise does not become unexpected"),
		Summary.Unexpected,
		0);
	TestEqual(
		TEXT("The matched occurrence consumes its single expected record"),
		Summary.PendingExpected,
		0);

	Broadcast(
		*T1Clone.Listener,
		MakeEvent(
			StableT0EntityId,
			StableT0EntityId,
			TAG_IntentReplayPerception_Event,
			FVector(1600.0, 0.0, 0.0)));
	Summary = T1Clone.Observations->GetComparisonSummary();
	TestEqual(
		TEXT("An extra occurrence from the same replay intent remains unexpected"),
		Summary.Unexpected,
		1);
	const UIntentReplayObservationJournal* ComparisonJournal =
		T1Clone.Observations->GetActiveObservationJournal();
	TestEqual(
		TEXT("The extra occurrence is rejected because its expected record was consumed"),
		ComparisonJournal->GetEntries().Last().Reason,
		EIntentReplayObservationMismatchReason::AllCandidatesAlreadyConsumed);

	const FEntity NewReplaySource = MakeEntity(*World);
	UPerceptionKnowledgeSourceComponent* NewSource =
		AddPerceptionSource(*NewReplaySource.Actor);
	TestTrue(
		TEXT("A novel replay Source prepares its action track"),
		NewReplaySource.Replay
			->PrepareReplay(T0Track, FIntentReplayPlaybackOptions())
			.WasAccepted());
	TestTrue(
		TEXT("A novel replay Source starts its action"),
		NewReplaySource.Replay->StartReplay().Succeeded());
	Broadcast(
		*T1Clone.Listener,
		MakeEvent(
			NewSource->GetEntityId(),
			NewSource->GetEntityId(),
			TAG_IntentReplayPerception_Event,
			FVector(1500.0, 0.0, 0.0)));
	Summary = T1Clone.Observations->GetComparisonSummary();
	TestEqual(
		TEXT("The same semantic noise from a new Source remains unexpected"),
		Summary.Unexpected,
		2);
	TestEqual(
		TEXT("The new Source does not borrow the historical causal occurrence"),
		ComparisonJournal->GetEntries().Last().CurrentObservation.Event.SourceEntityId,
		NewSource->GetEntityId());
	TestEqual(
		TEXT("The novel Source event is still identified as replay-correlated"),
		ComparisonJournal->GetEntries().Last().CurrentCorrelation.Justification,
		EIntentReplayObservationJustification::CorrelatedReplayIntent);
	TestEqual(
		TEXT("The novel Source replay correlation is verified"),
		ComparisonJournal->GetEntries().Last().CurrentCorrelation.Reliability,
		EIntentReplayObservationCorrelationReliability::Verified);

	NewReplaySource.Actor->Destroy();
	T1Clone.Actor->Destroy();
	T0RebuiltClone.Actor->Destroy();
	T1Recorder.Actor->Destroy();
	T0Recorder.Actor->Destroy();
	DestroyWorld(World);
	return true;
}

#endif
