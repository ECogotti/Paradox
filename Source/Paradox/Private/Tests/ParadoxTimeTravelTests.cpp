#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actions/GameplayActionDefinition.h"
#include "Actions/GameplayWaitAction.h"
#include "Actions/ParadoxTimeTravelActionDefinition.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayActionTags.h"
#include "NiagaraComponent.h"
#include "Paradox.h"
#include "Recording/IntentReplayTrack.h"
#include "StructUtils/PropertyBag.h"
#include "Tests/ParadoxTimeTravelTestTypes.h"

namespace UE::Paradox::TimeTravel::Tests
{
	struct FScopedTestWorld
	{
		explicit FScopedTestWorld(const TCHAR* Name)
		{
			Context = GEngine
				? &GEngine->CreateNewWorldContext(EWorldType::Game)
				: nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
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

	FGameplayActionRequest MakeRequest(
		UGameplayActionDefinition& Definition)
	{
		FGameplayActionRequestCreationResult Creation =
			UGameplayActionBlueprintLibrary::CreateActionRequest(&Definition);
		if (Creation.WasCreated())
		{
			UGameplayActionBlueprintLibrary::SetRequestContext(
				Creation.Request,
				ParadoxGameplayTags::Origin_Player,
				nullptr,
				FGameplayActionCorrelationData());
		}
		return MoveTemp(Creation.Request);
	}

	UGameplayActionDefinition* MakeMovementDefinition(UObject& Outer)
	{
		UGameplayActionDefinition* Definition =
			NewObject<UGameplayActionDefinition>(&Outer);
		Definition->InstanceClass = UGameplayWaitAction::StaticClass();
		Definition->ActionTag = ParadoxGameplayTags::Action_InvestigationInspect;
		Definition->ExecutionLocks.AddTag(GameplayActionTags::Lock_Movement);
		Definition->bInterruptible = true;
		Definition->JournalRequirement =
			EGameplayActionJournalRequirement::Optional;
		Definition->DefaultParameters.InitializeFromBagStruct(
			UPropertyBag::GetOrCreateFromDescs(
				{ { TEXT("Duration"), EPropertyBagPropertyType::Double } }));
		Definition->DefaultParameters.SetValueDouble(TEXT("Duration"), 60.0);
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxTimeTravelRecordedActionTest,
	"Paradox.TimeTravel.RecordedActionPreemptsMovementAndSupportsNoVfx",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxTimeTravelRecordedActionTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::TimeTravel::Tests;
	UParadoxTimeTravelActionDefinition* TimeTravelDefinition =
		LoadObject<UParadoxTimeTravelActionDefinition>(
			nullptr,
			TEXT("/Game/Data/GameplayActions/DA_ParadoxTimeTravel.DA_ParadoxTimeTravel"));
	if (!TestNotNull(TEXT("Ready-to-use Time Travel Definition loads"), TimeTravelDefinition))
	{
		return false;
	}
	TestTrue(
		TEXT("Time Travel owns Movement"),
		TimeTravelDefinition->ExecutionLocks.HasTagExact(
			GameplayActionTags::Lock_Movement));
	TestTrue(
		TEXT("Time Travel owns Stance"),
		TimeTravelDefinition->ExecutionLocks.HasTagExact(
			ParadoxGameplayTags::Lock_Stance));
	TestTrue(
		TEXT("Time Travel has its dedicated lock"),
		TimeTravelDefinition->ExecutionLocks.HasTagExact(
			ParadoxGameplayTags::Lock_TimeTravel));
	TestFalse(TEXT("Time Travel is not interruptible"), TimeTravelDefinition->bInterruptible);
	TestEqual(
		TEXT("Time Travel rejects unresolved equal-or-higher lock conflicts"),
		TimeTravelDefinition->BlockedPolicy,
		EGameplayActionBlockedPolicy::Reject);
	TestTrue(
		TEXT("Time Travel has terminal scheduling priority"),
		TimeTravelDefinition->DefaultPriority > 0);
	TestEqual(
		TEXT("Time Travel journaling is required"),
		TimeTravelDefinition->JournalRequirement,
		EGameplayActionJournalRequirement::Required);

	FScopedTestWorld TestWorld(TEXT("ParadoxTimeTravelRecordedActionWorld"));
	if (!TestNotNull(TEXT("Transient Time Travel world exists"), TestWorld.World))
	{
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxPlayerCharacter* Character =
		TestWorld.World->SpawnActor<AParadoxPlayerCharacter>(
			AParadoxPlayerCharacter::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	AParadoxTimeTravelTestPlayerController* Controller =
		TestWorld.World->SpawnActor<AParadoxTimeTravelTestPlayerController>(
			AParadoxTimeTravelTestPlayerController::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	if (!TestNotNull(TEXT("Time Travel Character exists"), Character)
		|| !TestNotNull(TEXT("Time Travel Controller exists"), Controller))
	{
		return false;
	}
	Controller->Possess(Character);
	TestWorld.StartPlay();
	UNiagaraComponent* NiagaraComponent =
		Character->GetTimeTravelNiagaraComponent();
	TestNotNull(TEXT("Character owns the Time Travel Niagara component"), NiagaraComponent);
	TestTrue(
		TEXT("No authored Niagara asset selects the immediate fallback"),
		NiagaraComponent && !NiagaraComponent->GetAsset());
	TestTrue(
		TEXT("Time Travel Niagara never auto-activates"),
		NiagaraComponent && !NiagaraComponent->bAutoActivate);

	UGameplayActionComponent* Actions = Character->GetGameplayActionComponent();
	UIntentReplayComponent* Replay = Character->GetIntentReplayComponent();
	if (!TestNotNull(TEXT("Action scheduler exists"), Actions)
		|| !TestNotNull(TEXT("Intent Replay exists"), Replay))
	{
		return false;
	}
	if (!Replay->IsIntentReplayInitialized())
	{
		TestTrue(
			TEXT("Intent Replay initializes"),
			Replay->InitializeIntentReplay().Succeeded());
	}
	TestTrue(
		TEXT("Recording starts"),
		Replay->StartRecording(FIntentRecordingOptions()).Succeeded());

	UGameplayActionDefinition* MovementDefinition =
		MakeMovementDefinition(*TestWorld.World);
	const FGameplayActionSubmissionResult MovementSubmission =
		Actions->SubmitAction(MakeRequest(*MovementDefinition));
	TestEqual(
		TEXT("Movement begins before Time Travel"),
		MovementSubmission.Status,
		EGameplayActionSubmissionStatus::AcceptedStarted);
	const FGameplayActionSubmissionResult TimeTravelSubmission =
		Actions->SubmitAction(MakeRequest(*TimeTravelDefinition));
	TestTrue(
		TEXT("Time Travel is accepted during movement"),
		TimeTravelSubmission.IsAccepted());

	FGameplayActionResult MovementResult;
	TestTrue(
		TEXT("Movement becomes terminal"),
		Actions->GetActionResult(
			MovementSubmission.Handle,
			MovementResult));
	TestEqual(
		TEXT("Time Travel preempts movement"),
		MovementResult.TerminalState,
		EGameplayActionState::Interrupted);
	FGameplayActionResult TimeTravelResult;
	TestTrue(
		TEXT("No-VFX Time Travel completes synchronously"),
		Actions->GetActionResult(
			TimeTravelSubmission.Handle,
			TimeTravelResult));
	TestEqual(
		TEXT("No-VFX fallback succeeds"),
		TimeTravelResult.TerminalState,
		EGameplayActionState::Succeeded);

	TestTrue(
		TEXT("Recording finalizes"),
		Replay->RequestStopRecording(
			EIntentRecordingFinalizeMode::Immediate).Succeeded());
	UIntentReplayTrack* Track = Replay->GetLastFinalizedTrack();
	TestEqual(
		TEXT("Movement and Time Travel are both recorded"),
		Track ? Track->GetEntryCount() : 0,
		2);
	FRecordedIntent RecordedTimeTravel;
	TestTrue(
		TEXT("Recorded Time Travel entry is readable"),
		Track && Track->GetEntryByIndex(1, RecordedTimeTravel));
	TestTrue(
		TEXT("Track preserves the Time Travel semantic tag"),
		RecordedTimeTravel.ActionTag == ParadoxGameplayTags::Action_TimeTravel);
	return true;
}

#endif
