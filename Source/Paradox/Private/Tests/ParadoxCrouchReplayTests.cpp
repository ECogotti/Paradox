#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actions/GameplayActionDefinition.h"
#include "Actions/GameplayWaitAction.h"
#include "Actions/ParadoxSetCrouchedActionDefinition.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayActionTags.h"
#include "Paradox.h"
#include "Recording/IntentReplayTrack.h"
#include "StructUtils/PropertyBag.h"
#include "Types/IntentReplayTypes.h"

namespace UE::Paradox::CrouchReplay::Tests
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
		UGameplayActionDefinition& Definition,
		const FGameplayTag Origin)
	{
		FGameplayActionRequestCreationResult Creation =
			UGameplayActionBlueprintLibrary::CreateActionRequest(&Definition);
		if (!Creation.WasCreated())
		{
			return FGameplayActionRequest();
		}
		UGameplayActionBlueprintLibrary::SetRequestContext(
			Creation.Request,
			Origin,
			nullptr,
			FGameplayActionCorrelationData());
		return MoveTemp(Creation.Request);
	}

	UGameplayActionDefinition* MakeLongMovementDefinition(UObject& Outer)
	{
		UGameplayActionDefinition* Definition =
			NewObject<UGameplayActionDefinition>(&Outer);
		Definition->InstanceClass = UGameplayWaitAction::StaticClass();
		Definition->ActionTag = ParadoxGameplayTags::Action_InvestigationInspect;
		Definition->ExecutionLocks.AddTag(GameplayActionTags::Lock_Movement);
		Definition->BlockedPolicy = EGameplayActionBlockedPolicy::Queue;
		Definition->JournalRequirement =
			EGameplayActionJournalRequirement::Disabled;
		const TArray<FPropertyBagPropertyDesc> Descriptors = {
			{ TEXT("Duration"), EPropertyBagPropertyType::Double }
		};
		Definition->DefaultParameters.InitializeFromBagStruct(
			UPropertyBag::GetOrCreateFromDescs(Descriptors));
		Definition->DefaultParameters.SetValueDouble(TEXT("Duration"), 60.0);
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCrouchActionReplayAndConcurrencyTest,
	"Paradox.Crouch.ActionReplayAndMovementConcurrency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCrouchActionReplayAndConcurrencyTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::CrouchReplay::Tests;

	FScopedTestWorld TestWorld(TEXT("ParadoxCrouchReplayWorld"));
	if (!TestNotNull(TEXT("Transient crouch test world exists"), TestWorld.World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AParadoxPlayerCharacter* SourceCharacter =
		TestWorld.World->SpawnActor<AParadoxPlayerCharacter>(
			AParadoxPlayerCharacter::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	AParadoxPlayerCharacter* ReplayCharacter =
		TestWorld.World->SpawnActor<AParadoxPlayerCharacter>(
			AParadoxPlayerCharacter::StaticClass(),
			FTransform(FVector(300.0, 0.0, 0.0)),
			SpawnParameters);
	if (!TestNotNull(TEXT("Source Character exists"), SourceCharacter)
		|| !TestNotNull(TEXT("Replay Character exists"), ReplayCharacter))
	{
		return false;
	}
	TestWorld.StartPlay();

	UGameplayActionComponent* SourceActions =
		SourceCharacter->GetGameplayActionComponent();
	UIntentReplayComponent* SourceReplay =
		SourceCharacter->GetIntentReplayComponent();
	UIntentReplayComponent* RecipientReplay =
		ReplayCharacter->GetIntentReplayComponent();
	if (!TestNotNull(TEXT("Source action scheduler exists"), SourceActions)
		|| !TestNotNull(TEXT("Source replay exists"), SourceReplay)
		|| !TestNotNull(TEXT("Recipient replay exists"), RecipientReplay))
	{
		return false;
	}
	if (!SourceReplay->IsIntentReplayInitialized())
	{
		TestTrue(
			TEXT("Source replay initializes"),
			SourceReplay->InitializeIntentReplay().Succeeded());
	}
	if (!RecipientReplay->IsIntentReplayInitialized())
	{
		TestTrue(
			TEXT("Recipient replay initializes"),
			RecipientReplay->InitializeIntentReplay().Succeeded());
	}

	UGameplayActionDefinition* MovementDefinition =
		MakeLongMovementDefinition(*TestWorld.World);
	const FGameplayActionSubmissionResult MovementSubmission =
		SourceActions->SubmitAction(
			MakeRequest(*MovementDefinition, ParadoxGameplayTags::Origin_Player));
	if (!TestEqual(
		TEXT("Long movement starts"),
		MovementSubmission.Status,
		EGameplayActionSubmissionStatus::AcceptedStarted))
	{
		return false;
	}

	FIntentRecordingOptions RecordingOptions;
	RecordingOptions.SourceLabel = TEXT("ParadoxCrouchActionReplay");
	if (!TestTrue(
		TEXT("Crouch recording starts"),
		SourceReplay->StartRecording(RecordingOptions).Succeeded()))
	{
		return false;
	}

	UParadoxSetCrouchedActionDefinition* CrouchDefinition =
		NewObject<UParadoxSetCrouchedActionDefinition>(TestWorld.World);
	CrouchDefinition->DefaultParameters.SetValueBool(
		ParadoxSetCrouchedActionParameters::DesiredCrouched,
		true);
	const FGameplayActionSubmissionResult CrouchSubmission =
		SourceActions->SubmitAction(
			MakeRequest(*CrouchDefinition, ParadoxGameplayTags::Origin_Player));
	TestEqual(
		TEXT("Crouch starts immediately while Movement is locked"),
		CrouchSubmission.Status,
		EGameplayActionSubmissionStatus::AcceptedStarted);
	EGameplayActionState MovementState = EGameplayActionState::Created;
	TestTrue(
		TEXT("Movement handle remains queryable"),
		SourceActions->GetActionState(
			MovementSubmission.Handle,
			MovementState));
	TestEqual(
		TEXT("Crouch does not interrupt movement"),
		MovementState,
		EGameplayActionState::Running);
	TestTrue(
		TEXT("Crouch action immediately sets the persistent stance desire"),
		SourceCharacter->GetCharacterMovement()->bWantsToCrouch);

	CrouchDefinition->DefaultParameters.SetValueBool(
		ParadoxSetCrouchedActionParameters::DesiredCrouched,
		false);
	const FGameplayActionSubmissionResult UnCrouchSubmission =
		SourceActions->SubmitAction(
			MakeRequest(*CrouchDefinition, ParadoxGameplayTags::Origin_Player));
	TestEqual(
		TEXT("A rapid uncrouch request also starts immediately"),
		UnCrouchSubmission.Status,
		EGameplayActionSubmissionStatus::AcceptedStarted);
	TestTrue(
		TEXT("Movement remains running after both rapid stance requests"),
		SourceActions->GetActionState(
			MovementSubmission.Handle,
			MovementState)
			&& MovementState == EGameplayActionState::Running);
	TestFalse(
		TEXT("Uncrouch immediately clears the persistent stance desire"),
		SourceCharacter->GetCharacterMovement()->bWantsToCrouch);

	TestTrue(
		TEXT("Crouch recording finalizes"),
		SourceReplay
			->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate)
			.Succeeded());
	UIntentReplayTrack* Track = SourceReplay->GetLastFinalizedTrack();
	if (!TestNotNull(TEXT("Crouch track exists"), Track)
		|| !TestEqual(TEXT("Crouch and uncrouch were recorded"), Track->GetEntryCount(), 2))
	{
		return false;
	}
	FRecordedIntent RecordedCrouch;
	if (TestTrue(
		TEXT("Recorded crouch entry is readable"),
		Track->GetEntryByIndex(0, RecordedCrouch)))
	{
		TestTrue(
			TEXT("Recorded action uses the crouch semantic tag"),
			RecordedCrouch.ActionTag == ParadoxGameplayTags::Action_SetCrouched);
		const TValueOrError<bool, EPropertyBagResult> RecordedDesired =
			RecordedCrouch.GetParameters().GetValueBool(
				ParadoxSetCrouchedActionParameters::DesiredCrouched);
		TestTrue(
			TEXT("Recorded crouch preserves DesiredCrouched=true"),
			RecordedDesired.HasValue() && RecordedDesired.GetValue());
	}
	FRecordedIntent RecordedUnCrouch;
	if (TestTrue(
		TEXT("Recorded uncrouch entry is readable"),
		Track->GetEntryByIndex(1, RecordedUnCrouch)))
	{
		const TValueOrError<bool, EPropertyBagResult> RecordedDesired =
			RecordedUnCrouch.GetParameters().GetValueBool(
				ParadoxSetCrouchedActionParameters::DesiredCrouched);
		TestTrue(
			TEXT("Recorded uncrouch preserves DesiredCrouched=false"),
			RecordedDesired.HasValue() && !RecordedDesired.GetValue());
	}

	FIntentReplayPlaybackOptions PlaybackOptions;
	const FIntentReplayPrepareResult PrepareResult =
		RecipientReplay->PrepareReplay(Track, PlaybackOptions);
	if (!TestTrue(
		TEXT("Recipient prepares the crouch track"),
		PrepareResult.WasAccepted()))
	{
		AddError(PrepareResult.Failure.DiagnosticMessage);
		return false;
	}
	TestTrue(
		TEXT("Recipient starts the crouch replay"),
		RecipientReplay->StartReplay().Succeeded());
	TestWorld.World->Tick(LEVELTICK_All, 0.1f);
	TestFalse(
		TEXT("Replay applies the final recorded uncrouched desire"),
		ReplayCharacter->GetCharacterMovement()->bWantsToCrouch);

	SourceActions->AbortAllActions(
		GameplayActionTags::Result_Aborted_SystemReset);
	return true;
}

#endif
