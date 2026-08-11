#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Characters/ParadoxCharacter.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/EntityIdentityComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Components/SphereComponent.h"
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
			const USphereComponent* CandidateSphere =
				CloneDefaults->GetTemporalVisionCandidateSphere();
			if (TestNotNull(
				TEXT("Clone owns the Temporal Vision Pawn candidate sphere"),
				CandidateSphere))
			{
				TestTrue(
					TEXT("Candidate sphere is attached to Temporal Vision"),
					CandidateSphere->GetAttachParent() == TemporalVision);
				TestEqual(
					TEXT("Candidate sphere follows the configured outer cone radius"),
					CandidateSphere->GetUnscaledSphereRadius(),
					900.0f);
				TestEqual(
					TEXT("Candidate sphere retains the Pawn-only query mask"),
					CandidateSphere->GetCollisionResponseToChannel(ECC_Pawn),
					ECR_Overlap);
				TestEqual(
					TEXT("Candidate sphere ignores WorldStatic"),
					CandidateSphere->GetCollisionResponseToChannel(ECC_WorldStatic),
					ECR_Ignore);
				TestEqual(
					TEXT("Candidate sphere owns no persistent moving physics body"),
					CandidateSphere->GetCollisionEnabled(),
					ECollisionEnabled::NoCollision);
				TestFalse(
					TEXT("Candidate sphere does not generate component overlap maintenance"),
					CandidateSphere->GetGenerateOverlapEvents());
			}
			TestFalse(
				TEXT("Temporal Vision procedural mesh has no dynamic collision"),
				TemporalVision->IsDynamicMeshCollisionEnabled());
			TestEqual(
				TEXT("Temporal Vision procedural mesh uses NoCollision"),
				TemporalVision->GetCollisionEnabled(),
				ECollisionEnabled::NoCollision);
			TestTrue(
				TEXT("A point in front and inside the configured radius passes the cone filter"),
				TemporalVision->IsWorldLocationWithinConfiguredCone(FVector(450.0, 0.0, 0.0)));
			TestFalse(
				TEXT("A point outside the configured radius fails the cone filter"),
				TemporalVision->IsWorldLocationWithinConfiguredCone(FVector(901.0, 0.0, 0.0)));
			TestFalse(
				TEXT("A point outside the configured half-angle fails the cone filter"),
				TemporalVision->IsWorldLocationWithinConfiguredCone(FVector(450.0, 450.0, 0.0)));
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

		UParadoxTemporalVisionComponent* RuntimeVision =
			NewObject<UParadoxTemporalVisionComponent>();
		USphereComponent* RuntimeCandidateSphere = NewObject<USphereComponent>();
		if (TestNotNull(TEXT("Runtime Temporal Vision test component exists"), RuntimeVision)
			&& TestNotNull(TEXT("Runtime candidate sphere test component exists"), RuntimeCandidateSphere))
		{
			RuntimeVision->SetCandidateSphereComponent(RuntimeCandidateSphere);
			RuntimeVision->SetRadius2(1234.0f);
			RuntimeVision->SynchronizeCandidateSphere();
			TestEqual(
				TEXT("Runtime cone radius changes resize the candidate sphere"),
				RuntimeCandidateSphere->GetUnscaledSphereRadius(),
				1234.0f);
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
