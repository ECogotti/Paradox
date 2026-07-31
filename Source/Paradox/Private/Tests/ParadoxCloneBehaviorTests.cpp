#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/BehaviorTree/BTTask_ParadoxGoapPlaceholder.h"
#include "AI/BehaviorTree/BTTask_ParadoxInvestigateObservation.h"
#include "AI/BehaviorTree/BTTask_ParadoxRunIntentReplay.h"
#include "Actions/GameplayActionInstance.h"
#include "Actions/GridMoveToCellActionDefinition.h"
#include "Behavior/ParadoxCloneBehaviorCoordinatorComponent.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "Components/PerceptionKnowledgeHearingRangeRendererComponent.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Controllers/ParadoxCloneController.h"
#include "Data/PerceptionKnowledgeProfile.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Investigation/ParadoxCloneInvestigationComponent.h"
#include "Paradox.h"
#include "Perception/ParadoxObservationResponsePolicy.h"
#include "Perception/ParadoxSemanticNoiseSphere.h"
#include "Perception/ParadoxSemanticStateCube.h"
#include "PerceptionKnowledgeTags.h"

struct FParadoxCloneBehaviorTestAccessor
{
	static void SetInvestigationForArbitration(
		UParadoxCloneBehaviorCoordinatorComponent& Coordinator,
		const FParadoxInvestigationContext& Context,
		UParadoxCloneInvestigationComponent* Investigation = nullptr)
	{
		Coordinator.CurrentMode =
			EParadoxCloneBehaviorMode::Investigating;
		Coordinator.CurrentInvestigation = Context;
		Coordinator.InvestigationComponent = Investigation;
	}

	static FParadoxCloneBehaviorOperationResult ConsiderReplacement(
		UParadoxCloneBehaviorCoordinatorComponent& Coordinator,
		FParadoxInvestigationContext Candidate)
	{
		return Coordinator.ConsiderInvestigationReplacement(
			MoveTemp(Candidate));
	}
};

namespace UE::Paradox::CloneBehavior::Tests
{
	struct FScopedTestWorld
	{
		explicit FScopedTestWorld(const TCHAR* Name)
		{
			Context = GEngine
				? &GEngine->CreateNewWorldContext(EWorldType::Game)
				: nullptr;
			World = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				FName(Name));
			if (World)
			{
				World->AddToRoot();
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		~FScopedTestWorld()
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

		void StartPlay() const
		{
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	FParadoxInvestigationContext MakeCandidate(
		const EPerceptionKnowledgeObservationType Type,
		const EIntentReplayObservationMatchResult MatchResult,
		const FGameplayTag Sense,
		const FGameplayTag SemanticTag)
	{
		FParadoxInvestigationContext Candidate;
		Candidate.ObservationType = Type;
		Candidate.SenseTag = Sense;
		Candidate.SemanticTag = SemanticTag;
		Candidate.Confidence = 1.0f;
		Candidate.Comparison.Entry.Result = MatchResult;
		return Candidate;
	}

	FParadoxInvestigationContext MakeValidRuntimeCandidate(
		const int32 Priority,
		const int32 Revision,
		const FName RuleId,
		const FVector Location)
	{
		FParadoxInvestigationContext Candidate = MakeCandidate(
			EPerceptionKnowledgeObservationType::Event,
			EIntentReplayObservationMatchResult::UnexpectedObservation,
			PerceptionKnowledgeTags::Sense_Hearing.GetTag(),
			ParadoxGameplayTags::Test_Event_Noise.GetTag());
		Candidate.PlaybackSessionId =
			FIntentReplayPlaybackSessionId::NewId();
		Candidate.ObservationTrackId =
			FIntentReplayObservationTrackId::NewId();
		Candidate.JournalId =
			FIntentReplayObservationJournalId::NewId();
		Candidate.JournalEntryId =
			FIntentReplayObservationJournalEntryId::NewId();
		Candidate.InvestigationPriority = Priority;
		Candidate.InvestigationRevision = Revision;
		Candidate.ResponseRuleId = RuleId;
		Candidate.InvestigationLocation = Location;
		return Candidate;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxObservationPriorityPolicyTest,
	"Paradox.CloneBehavior.Policy.DefaultPrioritiesAndFilters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxObservationPriorityPolicyTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::CloneBehavior::Tests;
	const UParadoxObservationResponsePolicy* Policy =
		GetDefault<UParadoxObservationResponsePolicy>();
	const UParadoxCloneBehaviorCoordinatorComponent* CoordinatorDefaults =
		GetDefault<UParadoxCloneBehaviorCoordinatorComponent>();
	TestTrue(
		TEXT("Paradox enables verified causal occurrence identity matching"),
		CoordinatorDefaults->ObservationMatchOptions
			.bTreatVerifiedCausalEventsAsOccurrenceIdentity);
	TestTrue(
		TEXT("Paradox enables ordered persistent State matching"),
		CoordinatorDefaults->ObservationMatchOptions
			.bTreatPersistentStateObservationsAsOrderedSnapshots);
	const FGameplayTag Sight =
		PerceptionKnowledgeTags::Sense_Sight.GetTag();
	const FGameplayTag Hearing =
		PerceptionKnowledgeTags::Sense_Hearing.GetTag();

	FParadoxInvestigationContext HearingCandidate = MakeCandidate(
		EPerceptionKnowledgeObservationType::Event,
		EIntentReplayObservationMatchResult::UnexpectedObservation,
		Hearing,
		ParadoxGameplayTags::Test_Event_Noise.GetTag());
	const FParadoxObservationResponseResult HearingResponse =
		Policy->Evaluate(
			HearingCandidate,
			EParadoxCloneBehaviorMode::Replay);
	TestTrue(
		TEXT("Unexpected Hearing event investigates"),
		HearingResponse.ShouldInvestigate());
	TestEqual(
		TEXT("Unexpected Hearing priority is 100"),
		HearingResponse.InvestigationPriority,
		100);

	FParadoxInvestigationContext SightCandidate = MakeCandidate(
		EPerceptionKnowledgeObservationType::State,
		EIntentReplayObservationMatchResult::UnexpectedStateValue,
		Sight,
		ParadoxGameplayTags::Test_State_Active.GetTag());
	const FParadoxObservationResponseResult SightResponse =
		Policy->Evaluate(
			SightCandidate,
			EParadoxCloneBehaviorMode::Replay);
	TestEqual(
		TEXT("Unexpected Sight state priority is 200"),
		SightResponse.InvestigationPriority,
		200);

	SightCandidate.SemanticTag =
		ParadoxGameplayTags::State_Computer_Powered.GetTag();
	const FParadoxObservationResponseResult ComputerResponse =
		Policy->Evaluate(
			SightCandidate,
			EParadoxCloneBehaviorMode::Investigating);
	TestEqual(
		TEXT("Powered computer state overrides generic Sight"),
		ComputerResponse.InvestigationPriority,
		300);
	TestTrue(
		TEXT("Powered computer state outranks noise"),
		ComputerResponse.InvestigationPriority
			> HearingResponse.InvestigationPriority);

	SightCandidate.Comparison.Entry.Result =
		EIntentReplayObservationMatchResult::Matched;
	TestFalse(
		TEXT("Matched observation is ignored"),
		Policy->Evaluate(
			SightCandidate,
			EParadoxCloneBehaviorMode::Replay)
			.ShouldInvestigate());

	SightCandidate.Comparison.Entry.Result =
		EIntentReplayObservationMatchResult::UnexpectedStateValue;
	SightCandidate.Correlation.Reliability =
		EIntentReplayObservationCorrelationReliability::Verified;
	SightCandidate.Correlation.Justification =
		EIntentReplayObservationJustification::ObserverCaused;
	TestFalse(
		TEXT("Verified self-caused observation is ignored"),
		Policy->Evaluate(
			SightCandidate,
			EParadoxCloneBehaviorMode::Replay)
			.ShouldInvestigate());

	HearingCandidate.Correlation.CausalRecordedIntentId =
		FRecordedIntentId::NewId();
	HearingCandidate.Correlation.Reliability =
		EIntentReplayObservationCorrelationReliability::Verified;
	HearingCandidate.Correlation.Justification =
		EIntentReplayObservationJustification::CorrelatedReplayIntent;
	const FParadoxObservationResponseResult ExternalReplayNoiseResponse =
		Policy->Evaluate(
			HearingCandidate,
			EParadoxCloneBehaviorMode::Replay);
	TestTrue(
		TEXT("Unexpected noise from an external replay Source investigates"),
		ExternalReplayNoiseResponse.ShouldInvestigate());
	TestEqual(
		TEXT("External replay Source noise retains Hearing priority"),
		ExternalReplayNoiseResponse.InvestigationPriority,
		100);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInvestigationArbitrationTest,
	"Paradox.CloneBehavior.Priority.EqualAndLowerNeverReplace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInvestigationArbitrationTest::RunTest(
	const FString& Parameters)
{
	UParadoxCloneBehaviorCoordinatorComponent* Coordinator =
		NewObject<UParadoxCloneBehaviorCoordinatorComponent>(
			GetTransientPackage());
	FParadoxInvestigationContext Current;
	Current.InvestigationPriority = 200;
	Current.InvestigationRevision = 7;
	Current.ResponseRuleId = TEXT("Current");
	FParadoxCloneBehaviorTestAccessor::SetInvestigationForArbitration(
		*Coordinator,
		Current);

	FParadoxInvestigationContext Lower = Current;
	Lower.InvestigationPriority = 100;
	Lower.InvestigationRevision = 8;
	Lower.ResponseRuleId = TEXT("Lower");
	const FParadoxCloneBehaviorOperationResult LowerResult =
		FParadoxCloneBehaviorTestAccessor::ConsiderReplacement(
			*Coordinator,
			Lower);
	TestEqual(
		TEXT("Lower priority is ignored"),
		LowerResult.Status,
		EParadoxCloneBehaviorOperationStatus::Ignored);
	TestEqual(
		TEXT("Lower priority leaves one current target"),
		Coordinator->GetCurrentInvestigation().ResponseRuleId,
		FName(TEXT("Current")));

	FParadoxInvestigationContext Equal = Current;
	Equal.InvestigationRevision = 8;
	Equal.ResponseRuleId = TEXT("Equal");
	const FParadoxCloneBehaviorOperationResult EqualResult =
		FParadoxCloneBehaviorTestAccessor::ConsiderReplacement(
			*Coordinator,
			Equal);
	TestEqual(
		TEXT("Equal priority is ignored"),
		EqualResult.Status,
		EParadoxCloneBehaviorOperationStatus::Ignored);
	TestEqual(
		TEXT("Equal priority cannot oscillate the target"),
		Coordinator->GetCurrentInvestigation().InvestigationRevision,
		7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInvestigationHigherPriorityRetargetTest,
	"Paradox.CloneBehavior.Priority.HigherReplacesSingleActionAndIgnoresStaleCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInvestigationHigherPriorityRetargetTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::CloneBehavior::Tests;
	FScopedTestWorld TestWorld(TEXT("ParadoxInvestigationRetargetWorld"));
	if (!TestNotNull(
		TEXT("Retarget test world exists"),
		TestWorld.World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxCloneCharacter* Clone =
		TestWorld.World->SpawnActor<AParadoxCloneCharacter>(
			AParadoxCloneCharacter::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	AParadoxCloneController* CloneController =
		TestWorld.World->SpawnActor<AParadoxCloneController>(
			AParadoxCloneController::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	if (!TestNotNull(TEXT("Retarget test clone exists"), Clone)
		|| !TestNotNull(
			TEXT("Retarget test controller exists"),
			CloneController))
	{
		return false;
	}
	Clone->AutoPossessAI = EAutoPossessAI::Disabled;
	CloneController->Possess(Clone);
	UGameplayActionComponent* Actions =
		Clone->GetGameplayActionComponent();
	UParadoxCloneInvestigationComponent* Investigation =
		Clone->GetInvestigationComponent();
	UParadoxCloneBehaviorCoordinatorComponent* Coordinator =
		Clone->GetBehaviorCoordinator();
	if (!TestNotNull(TEXT("Retarget scheduler exists"), Actions)
		|| !TestNotNull(TEXT("Retarget executor exists"), Investigation)
		|| !TestNotNull(TEXT("Retarget coordinator exists"), Coordinator))
	{
		return false;
	}
	TestEqual(
		TEXT("Scheduler pauses so the movement request remains deterministic"),
		Actions->PauseActions(),
		EGameplayActionOperationResult::Succeeded);
	TestTrue(
		TEXT("Investigation executor initializes"),
		Investigation->InitializeInvestigation(Actions).IsSuccess());

	const FParadoxInvestigationContext Current =
		MakeValidRuntimeCandidate(
			100,
			1,
			TEXT("Hearing.Current"),
			FVector(100.0, 0.0, 0.0));
	if (!TestTrue(
		TEXT("Initial investigation action is accepted"),
		Investigation->StartInvestigation(Current).IsSuccess()))
	{
		return false;
	}
	const FGameplayActionHandle SupersededHandle =
		Investigation->GetActiveActionHandle();
	TestTrue(
		TEXT("Initial investigation owns one exact action handle"),
		SupersededHandle.IsValid());
	const UGameplayActionInstance* QueuedInvestigation =
		Actions->GetActionInstance(SupersededHandle);
	if (TestNotNull(
		TEXT("Queued investigation exposes its immutable request snapshot"),
		QueuedInvestigation))
	{
		const TValueOrError<
			EGridGoalContentionPolicy,
			EPropertyBagResult> ContentionPolicy =
			QueuedInvestigation->GetParameters()
				.GetValueEnum<EGridGoalContentionPolicy>(
					GridMoveToCellActionParameters::
						GoalContentionPolicy);
		TestTrue(
			TEXT("Investigation request contains a goal-contention policy"),
			ContentionPolicy.HasValue());
		if (ContentionPolicy.HasValue())
		{
			TestEqual(
				TEXT("Investigation does not reject a transiently reserved destination"),
				ContentionPolicy.GetValue(),
				EGridGoalContentionPolicy::RedirectOnCompletion);
		}
	}
	TestEqual(
		TEXT("Only the initial investigation action is queued"),
		Actions->GetQueuedActionHandles().Num(),
		1);
	FParadoxCloneBehaviorTestAccessor::SetInvestigationForArbitration(
		*Coordinator,
		Current,
		Investigation);

	int32 CompletionCount = 0;
	Investigation->OnInvestigationFinishedNative().AddLambda(
		[&CompletionCount](
			const FParadoxInvestigationContext&,
			const FGameplayActionResult&)
		{
			++CompletionCount;
		});
	FParadoxInvestigationContext Higher =
		MakeValidRuntimeCandidate(
			300,
			2,
			TEXT("Sight.ComputerPowered.High"),
			FVector(200.0, 0.0, 0.0));
	const FParadoxCloneBehaviorOperationResult Replacement =
		FParadoxCloneBehaviorTestAccessor::ConsiderReplacement(
			*Coordinator,
			Higher);
	TestEqual(
		TEXT("Higher priority replaces immediately"),
		Replacement.Status,
		EParadoxCloneBehaviorOperationStatus::Replaced);
	TestEqual(
		TEXT("Coordinator exposes only the higher-priority objective"),
		Coordinator->GetCurrentInvestigation().ResponseRuleId,
		FName(TEXT("Sight.ComputerPowered.High")));
	TestEqual(
		TEXT("Retarget advances the authoritative revision"),
		Investigation->GetActiveRevision(),
		2);
	TestEqual(
		TEXT("Retarget retains exactly one queued investigation action"),
		Actions->GetQueuedActionHandles().Num(),
		1);
	TestNotEqual(
		TEXT("Retarget owns a new action handle"),
		Investigation->GetActiveActionHandle(),
		SupersededHandle);

	FGameplayActionResult SupersededResult;
	TestTrue(
		TEXT("Superseded action has a terminal result"),
		Actions->GetActionResult(
			SupersededHandle,
			SupersededResult));
	TestEqual(
		TEXT("Superseded action is Interrupted"),
		SupersededResult.TerminalState,
		EGameplayActionState::Interrupted);
	TestEqual(
		TEXT("Superseded action preserves the dedicated reason"),
		SupersededResult.ReasonTag,
		ParadoxGameplayTags::Result_Interrupted_InvestigationSuperseded.GetTag());
	TestEqual(
		TEXT("Synchronous completion from the replaced revision is ignored"),
		CompletionCount,
		0);
	TestTrue(
		TEXT("Replacement remains active after stale completion"),
		Investigation->IsInvestigationActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCloneControllerAuthoredPerceptionProfileTest,
	"Paradox.CloneBehavior.Assets.CloneControllerPreservesAuthoredPerceptionProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCloneControllerAuthoredPerceptionProfileTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::CloneBehavior::Tests;
	const UPerceptionKnowledgeProfile* AuthoredProfile =
		LoadObject<UPerceptionKnowledgeProfile>(
			nullptr,
			TEXT("/Game/Characters/Astronaut/DataAssets/Perception/DA_ClonePerceptiopnProfile.DA_ClonePerceptiopnProfile"));
	UClass* ControllerClass =
		LoadClass<AParadoxCloneController>(
			nullptr,
			TEXT("/Game/Characters/Astronaut/Blueprints/BP_CloneController.BP_CloneController_C"));
	if (!TestNotNull(
		TEXT("Authored clone perception Profile loads"),
		AuthoredProfile)
		|| !TestNotNull(
			TEXT("Authored clone Controller Blueprint loads"),
			ControllerClass))
	{
		return false;
	}

	const AParadoxCloneController* ControllerDefaults =
		Cast<AParadoxCloneController>(
			ControllerClass->GetDefaultObject());
	if (!TestNotNull(
		TEXT("Authored clone Controller CDO exists"),
		ControllerDefaults)
		|| !TestNotNull(
			TEXT("Authored clone Controller CDO owns a Listener"),
			ControllerDefaults
				? ControllerDefaults->GetPerceptionKnowledgeListener()
				: nullptr))
	{
		return false;
	}
	TestTrue(
		TEXT("Controller Blueprint serializes the authored Profile on its Listener"),
		ControllerDefaults->GetPerceptionKnowledgeListener()
			->GetListenerProfile()
			== AuthoredProfile);
	TestFalse(
		TEXT("Authored Hearing Range differs from the native 3000 cm fallback"),
		FMath::IsNearlyEqual(
			AuthoredProfile->HearingRange,
			3000.0f));

	FScopedTestWorld TestWorld(
		TEXT("ParadoxAuthoredClonePerceptionProfileWorld"));
	if (!TestNotNull(
		TEXT("Authored Profile test world exists"),
		TestWorld.World))
	{
		return false;
	}
	TestWorld.World->CreateAISystem();
	TestWorld.StartPlay();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxCloneController* Controller =
		TestWorld.World->SpawnActor<AParadoxCloneController>(
			ControllerClass,
			FTransform::Identity,
			SpawnParameters);
	APawn* Pawn =
		TestWorld.World->SpawnActor<APawn>(
			APawn::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	if (!TestNotNull(
		TEXT("Authored clone Controller instance spawns"),
		Controller)
		|| !TestNotNull(
			TEXT("Profile test Pawn spawns"),
			Pawn))
	{
		return false;
	}

	UPerceptionKnowledgeListenerComponent* Listener =
		Controller->GetPerceptionKnowledgeListener();
	TestTrue(
		TEXT("Spawned Controller starts with the authored Listener Profile"),
		Listener && Listener->GetListenerProfile() == AuthoredProfile);
	Controller->Possess(Pawn);
	TestTrue(
		TEXT("Possession preserves the authored Listener Profile"),
		Listener && Listener->GetListenerProfile() == AuthoredProfile);
	TestEqual(
		TEXT("Native Hearing retains the authored range after possession"),
		Listener ? Listener->GetEffectiveHearingRange() : 0.0f,
		AuthoredProfile->HearingRange);
	TestEqual(
		TEXT("Hearing renderer retains the authored range after possession"),
		Controller->GetHearingRangeRenderer()
			? Controller->GetHearingRangeRenderer()
				->GetRenderedHearingRange()
			: 0.0f,
		AuthoredProfile->HearingRange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCloneBehaviorNativeCompositionTest,
	"Paradox.CloneBehavior.NativeCompositionAndInactiveGoap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCloneBehaviorNativeCompositionTest::RunTest(
	const FString& Parameters)
{
	const AParadoxCloneCharacter* Clone =
		GetDefault<AParadoxCloneCharacter>();
	TestNotNull(
		TEXT("Clone owns authoritative coordinator"),
		Clone->GetBehaviorCoordinator());
	TestNotNull(
		TEXT("Clone owns investigation executor"),
		Clone->GetInvestigationComponent());
	TestEqual(
		TEXT("Clone investigation defers transient reservations to Reserved Corridor"),
		Clone->GetInvestigationComponent()->MovementGoalContentionPolicy,
		EGridGoalContentionPolicy::RedirectOnCompletion);
	TestEqual(
		TEXT("GOAP is inactive by default"),
		Clone->GetBehaviorCoordinator()->GetCurrentMode(),
		EParadoxCloneBehaviorMode::Replay);
	UClass* AuthoredCloneClass =
		LoadClass<AParadoxCloneCharacter>(
			nullptr,
			TEXT("/Game/Characters/Astronaut/Blueprints/BP_CloneAstronaut.BP_CloneAstronaut_C"));
	const AParadoxCloneCharacter* AuthoredCloneDefaults =
		AuthoredCloneClass
			? Cast<AParadoxCloneCharacter>(
				AuthoredCloneClass->GetDefaultObject())
			: nullptr;
	TestNotNull(
		TEXT("Authored clone Blueprint loads"),
		AuthoredCloneDefaults);
	TestTrue(
		TEXT("Authored clone inherits ordered persistent State matching"),
		AuthoredCloneDefaults
			&& AuthoredCloneDefaults->GetBehaviorCoordinator()
			&& AuthoredCloneDefaults->GetBehaviorCoordinator()
				->ObservationMatchOptions
				.bTreatPersistentStateObservationsAsOrderedSnapshots);

	const AParadoxCloneController* Controller =
		GetDefault<AParadoxCloneController>();
	TestNotNull(
		TEXT("Clone controller owns semantic listener"),
		Controller->GetPerceptionKnowledgeListener());
	TestNotNull(
		TEXT("Clone controller owns hearing renderer"),
		Controller->GetHearingRangeRenderer());
	TestEqual(
		TEXT("Unconfigured renderer has no effective range"),
		Controller->GetHearingRangeRenderer()->GetRenderedHearingRange(),
		0.0f);

	TestTrue(
		TEXT("Replay task is node-instanced"),
		GetDefault<UBTTask_ParadoxRunIntentReplay>()->HasInstance());
	TestTrue(
		TEXT("Investigation task is node-instanced"),
		GetDefault<UBTTask_ParadoxInvestigateObservation>()->HasInstance());
	TestTrue(
		TEXT("GOAP placeholder is node-instanced"),
		GetDefault<UBTTask_ParadoxGoapPlaceholder>()->HasInstance());

	const AParadoxSemanticStateCube* Cube =
		GetDefault<AParadoxSemanticStateCube>();
	const AParadoxSemanticNoiseSphere* Sphere =
		GetDefault<AParadoxSemanticNoiseSphere>();
	TestNotNull(
		TEXT("State cube owns a generic semantic source"),
		Cube->GetPerceptionSource());
	TestNotNull(
		TEXT("Noise sphere owns a generic semantic source"),
		Sphere->GetPerceptionSource());
	TestFalse(
		TEXT("State fixture never ticks"),
		Cube->PrimaryActorTick.bCanEverTick);
	TestFalse(
		TEXT("Noise fixture never ticks"),
		Sphere->PrimaryActorTick.bCanEverTick);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
