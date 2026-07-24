#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Characters/ParadoxCharacter.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/EntityIdentityComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Components/TacticalPauseActionQueueComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Controllers/ParadoxCloneController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Perception/ParadoxTemporalVisionComponent.h"
#include "Playback/ParadoxCloneReplayExecutionStrategy.h"
#include "TimeLoop/ParadoxTemporalEntityComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCharacterComponentOwnershipTest,
	"Paradox.Characters.ComponentOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCharacterComponentOwnershipTest::RunTest(const FString& Parameters)
{
	const AParadoxCharacter* SharedDefaults = GetDefault<AParadoxCharacter>();
	if (TestNotNull(TEXT("Shared Paradox Character defaults exist"), SharedDefaults))
	{
		TestNotNull(
			TEXT("Every temporal avatar owns Gameplay Actions"),
			SharedDefaults->GetGameplayActionComponent());
		TestNotNull(
			TEXT("Every temporal avatar owns Intent Replay"),
			SharedDefaults->GetIntentReplayComponent());
		TestNotNull(
			TEXT("Every temporal avatar owns Entity Relations identity"),
			SharedDefaults->GetEntityIdentityComponent());
		TestNotNull(
			TEXT("Every temporal avatar owns Paradox temporal identity"),
			SharedDefaults->GetTemporalEntityComponent());
		TestNull(
			TEXT("The shared character has no player-only planning component"),
			SharedDefaults->FindComponentByClass<UTacticalPauseActionQueueComponent>());
		TestNull(
			TEXT("The shared character has no clone-only World State participant"),
			SharedDefaults->FindComponentByClass<UWorldStateParticipantComponent>());
	}

	const AParadoxPlayerCharacter* PlayerDefaults = GetDefault<AParadoxPlayerCharacter>();
	if (TestNotNull(TEXT("Paradox Player Character defaults exist"), PlayerDefaults))
	{
		TestNotNull(
			TEXT("Player owns its temporary top-down camera"),
			PlayerDefaults->GetTopDownCameraComponent());
		TestNotNull(
			TEXT("Player owns its temporary camera boom"),
			PlayerDefaults->GetCameraBoom());
		const UTacticalPauseActionQueueComponent* Planning =
			PlayerDefaults->GetTacticalPauseActionQueueComponent();
		TestNotNull(TEXT("Player owns Tactical Pause planning"), Planning);
		if (Planning)
		{
			TestTrue(
				TEXT("Player planning targets the shared Gameplay Actions scheduler"),
				Planning->ActionComponentOverride
					== PlayerDefaults->GetGameplayActionComponent());
		}
		TestNull(
			TEXT("Player does not participate in World State"),
			PlayerDefaults->FindComponentByClass<UWorldStateParticipantComponent>());
		TestNull(
			TEXT("Player does not own clone-only Temporal Vision"),
			PlayerDefaults->FindComponentByClass<UParadoxTemporalVisionComponent>());
	}

	const AParadoxCloneCharacter* CloneDefaults = GetDefault<AParadoxCloneCharacter>();
	if (TestNotNull(TEXT("Paradox Clone Character defaults exist"), CloneDefaults))
	{
		const UIntentReplayComponent* CloneReplay =
			CloneDefaults->GetIntentReplayComponent();
		if (TestNotNull(
			TEXT("Clone owns Intent Replay"),
			CloneReplay))
		{
			TestEqual(
				TEXT("Clone replay adapts controller-bound GridWorld paths"),
				CloneReplay->ExecutionStrategyClass.Get(),
				UParadoxCloneReplayExecutionStrategy::StaticClass());
		}
		const UWorldStateParticipantComponent* Participant =
			CloneDefaults->GetWorldStateParticipantComponent();
		TestNotNull(TEXT("Clone owns a World State participant"), Participant);
		if (Participant)
		{
			TestFalse(
				TEXT("World State does not own clone existence"),
				Participant->bCaptureExistence);
			TestTrue(
				TEXT("Clone transform is eligible for World State snapshots"),
				Participant->bCaptureActorTransform);
			TestEqual(
				TEXT("The time loop remains clone existence authority"),
				Participant->ExistencePolicy,
				EWorldStateExistencePolicy::ExternallyManaged);
		}
		const UParadoxTemporalVisionComponent* TemporalVision =
			CloneDefaults->GetTemporalVisionComponent();
		if (TestNotNull(
			TEXT("Clone owns authoritative Temporal Vision"),
			TemporalVision))
		{
			const FParadoxTemporalVisionDebugSnapshot DebugSnapshot =
				TemporalVision->GetDebugSnapshot();
			TestFalse(
				TEXT("Temporal Vision debug is locally disabled by default"),
				DebugSnapshot.bLocalDebugEnabled);
			TestFalse(
				TEXT("Temporal Vision is not authoritative on the CDO"),
				DebugSnapshot.bDetectionAuthoritative);
			TestEqual(
				TEXT("Temporal Vision CDO has no overlap pairs"),
				DebugSnapshot.DeduplicatedActorPairCount,
				0);
		}
		TestNull(
			TEXT("Clone has no player-only Tactical Pause planning"),
			CloneDefaults->FindComponentByClass<UTacticalPauseActionQueueComponent>());
		TestNull(
			TEXT("Clone has no player-only camera"),
			CloneDefaults->FindComponentByClass<UCameraComponent>());
		TestNull(
			TEXT("Clone has no player-only camera boom"),
			CloneDefaults->FindComponentByClass<USpringArmComponent>());
		TestTrue(
			TEXT("Clone defaults to the dedicated controller"),
			CloneDefaults->AIControllerClass->IsChildOf(
				AParadoxCloneController::StaticClass()));
	}

	const AParadoxCloneController* CloneControllerDefaults =
		GetDefault<AParadoxCloneController>();
	if (TestNotNull(TEXT("Paradox Clone Controller defaults exist"), CloneControllerDefaults))
	{
		TestTrue(
			TEXT("Clone controller publishes its Pawn as GridWorld occupancy"),
			CloneControllerDefaults->bAutoRegisterPawnOccupancy);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
