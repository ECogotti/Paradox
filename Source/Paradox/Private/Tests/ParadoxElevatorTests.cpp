#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationModifierComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Controllers/PuzzleController.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayActionTags.h"
#include "Interfaces/MovementBaseInterface.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "NiagaraComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "ParadoxElevatorTestTypes.h"
#include "PressurePlateTestTypes.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "UObject/StrongObjectPtr.h"

namespace UE::Paradox::Elevator::Tests
{
	struct FScopedWorld
	{
		explicit FScopedWorld(const TCHAR* Name, const EWorldType::Type WorldType = EWorldType::Game)
		{
			Context = GEngine ? &GEngine->CreateNewWorldContext(WorldType) : nullptr;
			World = UWorld::CreateWorld(WorldType, false, FName(Name));
			if (World)
			{
				World->AddToRoot();
				World->SetShouldTick(true);
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		~FScopedWorld()
		{
			if (World)
			{
				World->DestroyWorld(true);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World->RemoveFromRoot();
			}
		}

		void StartPlay()
		{
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			for (TActorIterator<AActor> Iterator(World); Iterator; ++Iterator)
			{
				if (!Iterator->HasActorBegunPlay())
				{
					Iterator->DispatchBeginPlay();
				}
			}
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
		TStrongObjectPtr<APuzzleController> Controller;
	};

	template <typename T>
	T* Spawn(UWorld& World, const FName Name, const FVector& Location = FVector::ZeroVector)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = Name;
		SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<T>(T::StaticClass(), FTransform(FRotator::ZeroRotator, Location), SpawnParameters);
	}

	AParadoxElevatorTestActor* SpawnElevator(FScopedWorld& Scope, const FName Name,
		const bool bControllerActive = true)
	{
		AParadoxElevatorTestActor* Elevator = Spawn<AParadoxElevatorTestActor>(*Scope.World, Name);
		if (Elevator)
		{
			Scope.Controller.Reset(NewObject<APuzzleController>(GetTransientPackage()));
			Elevator->PuzzleReceiver->SetControllerRequest(Scope.Controller.Get(), bControllerActive);
			Elevator->PressDuration = 0.0f;
			Elevator->ReleaseDuration = 0.0f;
			Elevator->ForwardMovementTime = 1.0f;
			Elevator->bEmitNoiseOnRaiseStart = false;
			Elevator->bEmitNoiseOnLowerStart = false;
			Elevator->bEmitNoiseOnReachedEndpoint = false;
		}
		return Elevator;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxElevatorArchitectureTest,
	"Paradox.Elevator.Architecture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxElevatorArchitectureTest::RunTest(const FString& Parameters)
{
	const AParadoxElevator* Defaults = GetDefault<AParadoxElevator>();
	if (!TestNotNull(TEXT("Elevator native defaults exist"), Defaults))
	{
		return false;
	}
	TestTrue(TEXT("Elevator derives from the vertical barrier"),
		AParadoxElevator::StaticClass()->IsChildOf(AParadoxVerticalBarrier::StaticClass()));
	TestTrue(TEXT("Button visual rides the platform"),
		Defaults->ButtonMesh && Defaults->ButtonMesh->GetAttachParent() == Defaults->BarrierMesh.Get());
	TestTrue(TEXT("Button audio follows the button and does not auto-activate"),
		Defaults->ButtonMovementAudio
			&& Defaults->ButtonMovementAudio->GetAttachParent() == Defaults->ButtonMesh.Get()
			&& !Defaults->ButtonMovementAudio->bAutoActivate);
	TestTrue(TEXT("Button Niagara follows the button and does not auto-activate"),
		Defaults->ButtonMovementVFX
			&& Defaults->ButtonMovementVFX->GetAttachParent() == Defaults->ButtonMesh.Get()
			&& !Defaults->ButtonMovementVFX->bAutoActivate);
	for (const FName EventName : { FName(TEXT("HandleButtonPressed")),
		FName(TEXT("HandleButtonReleased")), FName(TEXT("HandleButtonMovementCompleted")) })
	{
		const UFunction* Event = AParadoxElevator::StaticClass()->FindFunctionByName(EventName);
		TestTrue(*FString::Printf(TEXT("Button event %s is available to Blueprint"), *EventName.ToString()),
			Event && Event->HasAnyFunctionFlags(FUNC_BlueprintEvent));
	}
	TestTrue(TEXT("Button trigger rides the platform independently of button animation"),
		Defaults->ButtonOccupancyVolume
			&& Defaults->ButtonOccupancyVolume->GetAttachParent() == Defaults->BarrierMesh.Get());
	TestTrue(TEXT("Passenger detector rides the whole platform"),
		Defaults->PassageOccupancyVolume
			&& Defaults->PassageOccupancyVolume->GetAttachParent() == Defaults->BarrierMesh.Get());
	TestFalse(TEXT("Button visual does not affect navigation"), Defaults->ButtonMesh->CanEverAffectNavigation());
	TestFalse(TEXT("Elevator uses passenger transport"), Defaults->bWaitForClearPassage);
	TestTrue(TEXT("Platform contributes navigation at stable endpoints"), Defaults->bGenerateNavigationAtStableEndpoints);
	TestTrue(TEXT("Selecting the elevator displays its puzzle connections"),
		Defaults->SelectableComponent->bShowPuzzleConnectionsWhenSelected);
	TestEqual(TEXT("Receiver requires manual activation after prerequisites"),
		Defaults->PuzzleReceiver->ActivationMode, EPuzzleReceiverActivationMode::Manual);
	TestFalse(TEXT("An unconnected Receiver does not enable the button"),
		Defaults->PuzzleReceiver->bActivateWhenUncontrolled);
	TestTrue(TEXT("An unconnected Manual Receiver permits button activation"),
		Defaults->PuzzleReceiver->bAllowManualActivationWithoutController);
	TestEqual(TEXT("Default first trip starts at Start"), Defaults->InitialPosition, EPuzzleTransformMoverInitialPosition::Start);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxElevatorReceiverGateTest,
	"Paradox.Elevator.ReceiverEnablesButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxElevatorReceiverGateTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Elevator::Tests;
	FScopedWorld Scope(TEXT("ParadoxElevatorReceiverGateWorld"));
	if (!TestNotNull(TEXT("Receiver gate world exists"), Scope.World))
	{
		return false;
	}
	AParadoxElevatorTestActor* Elevator = SpawnElevator(Scope, TEXT("ReceiverGateElevator"), false);
	APuzzleController* Controller = Scope.Controller.Get();
	AParadoxElevatorTestOccupant* Occupant = Spawn<AParadoxElevatorTestOccupant>(
		*Scope.World, TEXT("ReceiverGateOccupant"), FVector(10000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Receiver gate elevator exists"), Elevator)
		|| !TestNotNull(TEXT("Receiver gate Controller exists"), Controller)
		|| !TestNotNull(TEXT("Receiver gate occupant exists"), Occupant))
	{
		return false;
	}
	Elevator->PressDuration = 0.2f;
	Elevator->ReleaseDuration = 0.1f;
	Scope.StartPlay();
	TestFalse(TEXT("Inactive Controller leaves manual Receiver gated"), Elevator->PuzzleReceiver->IsReceiverActive());
	TestFalse(TEXT("Inactive puzzle Controller disables the button"), Elevator->IsButtonEnabled());
	Occupant->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation());
	Occupant->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("Disabled button does not descend"), Elevator->IsButtonPressed());
	TestFalse(TEXT("Disabled button does not move the elevator"), Elevator->IsMoving());
	Elevator->PuzzleReceiver->SetControllerRequest(Controller, true);
	TestTrue(TEXT("Active puzzle Controller enables the button"), Elevator->IsButtonEnabled());
	TestFalse(TEXT("Prerequisite alone does not activate the Receiver"), Elevator->PuzzleReceiver->IsReceiverActive());
	TestTrue(TEXT("Enabling while occupied starts the button press"), Elevator->IsButtonPressed());
	TestFalse(TEXT("Receiver signal itself does not start the trip"), Elevator->IsMoving());
	Elevator->Tick(0.1f);
	Elevator->PuzzleReceiver->SetControllerRequest(Controller, false);
	TestFalse(TEXT("Losing enablement cancels an incomplete press"), Elevator->IsButtonPressed());
	Elevator->Tick(0.1f);
	TestTrue(TEXT("Canceled Receiver press stays at Start"), Elevator->IsAtStart());
	TestEqual(TEXT("Canceled Receiver press raises the button"), Elevator->GetButtonMovementAlpha(), 0.0f);
	Occupant->SetActorLocation(FVector(10000.0f, 0.0f, 0.0f));
	Occupant->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	Elevator->PuzzleReceiver->SetControllerRequest(Controller, true);
	Occupant->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation());
	Occupant->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	Elevator->Tick(0.2f);
	TestTrue(TEXT("Enabled button starts travel only after full depression"), Elevator->IsMoving());
	TestTrue(TEXT("Completed press activates the manual Receiver"), Elevator->PuzzleReceiver->IsReceiverActive());
	Elevator->PuzzleReceiver->SetControllerRequest(Controller, false);
	TestTrue(TEXT("Removing enablement during travel does not stop the elevator"), Elevator->IsMoving());
	TestTrue(TEXT("Removing enablement during travel keeps the button down"), Elevator->IsButtonPressed());
	Elevator->Tick(1.0f);
	TestFalse(TEXT("Receiver is inactive after arrival"), Elevator->PuzzleReceiver->IsReceiverActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxElevatorCycleTest,
	"Paradox.Elevator.ButtonCycleAndTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxElevatorCycleTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Elevator::Tests;
	FScopedWorld Scope(TEXT("ParadoxElevatorCycleWorld"));
	if (!TestNotNull(TEXT("Elevator cycle world exists"), Scope.World))
	{
		return false;
	}
	AParadoxElevatorTestActor* Elevator = SpawnElevator(Scope, TEXT("CycleElevator"));
	AParadoxElevatorTestOccupant* Occupant = Spawn<AParadoxElevatorTestOccupant>(
		*Scope.World, TEXT("CycleOccupant"), FVector(10000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Cycle elevator exists"), Elevator)
		|| !TestNotNull(TEXT("Cycle occupant exists"), Occupant))
	{
		return false;
	}
	Elevator->RequiredButtonActorTags = { TEXT("Heavy"), TEXT("Authorized") };
	Scope.StartPlay();
	TestTrue(TEXT("External Controller enables the button"), Elevator->IsButtonEnabled());
	TestFalse(TEXT("Controller prerequisite alone cannot start the elevator"), Elevator->IsMoving());
	TestTrue(TEXT("Start endpoint is navigable"), Elevator->IsPassageOpen());
	TestFalse(TEXT("Start endpoint does not block passage navigation"), Elevator->IsPassageBlockingNavigation());
	TestFalse(TEXT("External movement request is rejected"), Elevator->RequestEndWithoutButtonForTest());
	TestTrue(TEXT("Empty button starts armed"), Elevator->IsButtonArmed());

	const FVector StartButtonLocation = Elevator->ButtonOccupancyVolume->GetComponentLocation();
	Occupant->SetActorLocation(StartButtonLocation);
	Occupant->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("Untagged object does not start a trip"), Elevator->IsMoving());
	Occupant->Tags.Add(TEXT("Heavy"));
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("Object missing one required tag does not start a trip"), Elevator->IsMoving());
	Occupant->Tags.Add(TEXT("Authorized"));
	Elevator->RefreshButtonOccupancy();
	TestTrue(TEXT("All tags start travel toward End"), Elevator->GetMoverState() == EPuzzleTransformMoverState::MovingTowardEnd);
	TestTrue(TEXT("Button stays logically pressed while traveling"), Elevator->IsButtonPressed());
	TestEqual(TEXT("Zero-duration test button is visually down"), Elevator->GetButtonMovementAlpha(), 1.0f);
	TestTrue(TEXT("Passage is blocked during downward travel"), Elevator->IsPassageBlockingNavigation());
	Elevator->RefreshButtonOccupancy();
	TestTrue(TEXT("Multiple overlaps do not reverse or retrigger"),
		Elevator->GetMoverState() == EPuzzleTransformMoverState::MovingTowardEnd);

	Occupant->SetActorLocation(FVector(10000.0f, 0.0f, 0.0f));
	Occupant->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestTrue(TEXT("Leaving during travel leaves the button down"), Elevator->IsButtonPressed());
	Elevator->Tick(1.0f);
	TestTrue(TEXT("First trip reaches End"), Elevator->IsAtEnd());
	TestFalse(TEXT("End endpoint is navigable"), Elevator->IsPassageBlockingNavigation());
	TestFalse(TEXT("Button releases only after arrival"), Elevator->IsButtonPressed());
	TestEqual(TEXT("Button is raised after arrival"), Elevator->GetButtonMovementAlpha(), 0.0f);
	TestTrue(TEXT("Empty button rearms after arrival"), Elevator->IsButtonArmed());

	Occupant->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation());
	Occupant->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestTrue(TEXT("Next entry starts the opposite trip"),
		Elevator->GetMoverState() == EPuzzleTransformMoverState::MovingTowardStart);
	Elevator->Tick(1.0f);
	TestTrue(TEXT("Second trip returns to Start"), Elevator->IsAtStart());
	TestTrue(TEXT("Start remains navigable after return"), Elevator->IsPassageOpen());
	TestFalse(TEXT("Occupant remaining on the button does not rearm"), Elevator->IsButtonArmed());
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("Persistent occupancy cannot start a third trip"), Elevator->IsMoving());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxElevatorImmediatePrerequisiteTest,
	"Paradox.Elevator.ImmediatePressAfterPrerequisite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxElevatorImmediatePrerequisiteTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Elevator::Tests;
	FScopedWorld Scope(TEXT("ParadoxElevatorImmediatePrerequisiteWorld"));
	if (!TestNotNull(TEXT("Immediate prerequisite world exists"), Scope.World))
	{
		return false;
	}
	AParadoxElevatorTestActor* Elevator = SpawnElevator(Scope, TEXT("ImmediatePrerequisiteElevator"), false);
	AParadoxElevatorTestOccupant* Occupant = Spawn<AParadoxElevatorTestOccupant>(
		*Scope.World, TEXT("ImmediatePrerequisiteOccupant"), FVector(10000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Elevator exists"), Elevator) || !TestNotNull(TEXT("Occupant exists"), Occupant))
	{
		return false;
	}
	Scope.StartPlay();
	Occupant->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation());
	Occupant->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("Occupancy without prerequisite cannot press"), Elevator->IsButtonPressed());
	Elevator->PuzzleReceiver->SetControllerRequest(Scope.Controller.Get(), true);
	Scope.World->Tick(LEVELTICK_All, 0.01f);
	TestTrue(TEXT("Already occupied zero-duration button starts after prerequisite notification"), Elevator->IsMoving());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxElevatorButtonAnimationTest,
	"Paradox.Elevator.ButtonAnimationAndPhysicalRearm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxElevatorButtonAnimationTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Elevator::Tests;
	FScopedWorld Scope(TEXT("ParadoxElevatorButtonAnimationWorld"));
	if (!TestNotNull(TEXT("Elevator animation world exists"), Scope.World))
	{
		return false;
	}
	AParadoxElevatorTestActor* Elevator = SpawnElevator(Scope, TEXT("AnimationElevator"));
	AParadoxElevatorTestOccupant* ValidActor = Spawn<AParadoxElevatorTestOccupant>(
		*Scope.World, TEXT("AnimationValidActor"), FVector(10000.0f, 0.0f, 0.0f));
	AParadoxElevatorTestOccupant* InvalidActor = Spawn<AParadoxElevatorTestOccupant>(
		*Scope.World, TEXT("AnimationInvalidActor"), FVector(10000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Animation elevator exists"), Elevator)
		|| !TestNotNull(TEXT("Valid Actor exists"), ValidActor)
		|| !TestNotNull(TEXT("Invalid Actor exists"), InvalidActor))
	{
		return false;
	}
	Elevator->PressDuration = 0.2f;
	Elevator->ReleaseDuration = 0.2f;
	Elevator->ButtonMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 20.0f));
	Elevator->RequiredButtonActorTags.Add(TEXT("Heavy"));
	ValidActor->Tags.Add(TEXT("Heavy"));
	Scope.StartPlay();
	TestEqual(TEXT("Initializing an empty elevator emits no button events"),
		Elevator->GetButtonPresentationEventCountForTest(), 0);
	InvalidActor->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation());
	InvalidActor->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestTrue(TEXT("Ineligible Actor still physically occupies the button"), Elevator->IsButtonOccupied());
	TestTrue(TEXT("Ineligible Actor does not consume the initial arm"), Elevator->IsButtonArmed());
	ValidActor->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation());
	ValidActor->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("Eligible entry does not move the platform before the press finishes"), Elevator->IsMoving());
	TestEqual(TEXT("Press animation begins at the raised position"), Elevator->GetButtonMovementAlpha(), 0.0f);
	TestEqual(TEXT("Press hook fires as the descent starts"), Elevator->ButtonPressedCount, 1);
	Elevator->RefreshButtonOccupancy();
	TestEqual(TEXT("Repeated occupancy refresh does not duplicate the press hook"), Elevator->ButtonPressedCount, 1);
	const FVector RaisedButtonLocation = Elevator->ButtonMesh->GetRelativeLocation();
	const FVector InitialPlatformLocation = Elevator->BarrierMesh->GetComponentLocation();
	Elevator->Tick(0.1f);
	TestTrue(TEXT("Button animates downward before travel"),
		Elevator->GetButtonMovementAlpha() > 0.0f && Elevator->GetButtonMovementAlpha() < 1.0f);
	TestTrue(TEXT("The idle mover keeps Actor Tick enabled while the button descends"),
		Elevator->IsActorTickEnabled());
	TestFalse(TEXT("Partial press leaves the platform stationary"), Elevator->IsMoving());
	Elevator->Tick(0.1f);
	TestEqual(TEXT("Complete press reaches full depth"), Elevator->GetButtonMovementAlpha(), 1.0f);
	TestTrue(TEXT("Only a complete press starts travel"), Elevator->IsMoving());
	TestEqual(TEXT("Full depression emits one completion hook"), Elevator->ButtonMovementCompletedCount, 1);
	TestTrue(TEXT("Press completion reports the down endpoint"), Elevator->bLastButtonCompletionPressed);
	TestTrue(TEXT("Platform has not moved in the frame that completed the press"),
		Elevator->BarrierMesh->GetComponentLocation().Equals(InitialPlatformLocation, KINDA_SMALL_NUMBER));
	Elevator->Tick(1.0f);
	TestTrue(TEXT("Trip reaches End with an occupied button"), Elevator->IsAtEnd());
	TestFalse(TEXT("Button is logically released at arrival"), Elevator->IsButtonPressed());
	TestEqual(TEXT("Arrival starts one release hook even while occupied"), Elevator->ButtonReleasedCount, 1);
	TestFalse(TEXT("Physical occupancy prevents rearming"), Elevator->IsButtonArmed());
	TestTrue(TEXT("Release animation keeps Actor Tick enabled at End"), Elevator->IsActorTickEnabled());
	Elevator->Tick(0.25f);
	TestEqual(TEXT("Release animation raises the button"), Elevator->GetButtonMovementAlpha(), 0.0f);
	TestEqual(TEXT("Full release emits the second completion hook"), Elevator->ButtonMovementCompletedCount, 2);
	TestFalse(TEXT("Release completion reports the raised endpoint"), Elevator->bLastButtonCompletionPressed);
	TestTrue(TEXT("Button mesh returns to its authored raised transform"),
		Elevator->ButtonMesh->GetRelativeLocation().Equals(RaisedButtonLocation, KINDA_SMALL_NUMBER));
	TestFalse(TEXT("Idle raised elevator disables Actor Tick"), Elevator->IsActorTickEnabled());
	TestFalse(TEXT("Invalid occupant still prevents rearming"), Elevator->IsButtonArmed());
	ValidActor->SetActorLocation(FVector(10000.0f, 0.0f, 0.0f));
	ValidActor->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("Last invalid occupant keeps the button disarmed"), Elevator->IsButtonArmed());
	InvalidActor->SetActorLocation(FVector(10000.0f, 0.0f, 0.0f));
	InvalidActor->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestTrue(TEXT("A physically empty button rearms"), Elevator->IsButtonArmed());
	ValidActor->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation());
	ValidActor->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("New entry first presses the button at End"), Elevator->IsMoving());
	Elevator->Tick(0.2f);
	TestTrue(TEXT("Fresh valid entry starts the opposite trip"),
		Elevator->GetMoverState() == EPuzzleTransformMoverState::MovingTowardStart);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxElevatorPressCancellationTest,
	"Paradox.Elevator.PressCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxElevatorPressCancellationTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Elevator::Tests;
	FScopedWorld Scope(TEXT("ParadoxElevatorPressCancellationWorld"));
	if (!TestNotNull(TEXT("Press cancellation world exists"), Scope.World))
	{
		return false;
	}
	AParadoxElevatorTestActor* Elevator = SpawnElevator(Scope, TEXT("CancellationElevator"));
	AParadoxPlayerCharacter* Character = Spawn<AParadoxPlayerCharacter>(
		*Scope.World, TEXT("CancellationCharacter"), FVector(10000.0f, 0.0f, 120.0f));
	AParadoxElevatorTestOccupant* Object = Spawn<AParadoxElevatorTestOccupant>(
		*Scope.World, TEXT("CancellationObject"), FVector(10000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Cancellation elevator exists"), Elevator)
		|| !TestNotNull(TEXT("Cancellation Character exists"), Character)
		|| !TestNotNull(TEXT("Cancellation object exists"), Object))
	{
		return false;
	}
	Elevator->PressDuration = 0.4f;
	Elevator->ReleaseDuration = 0.2f;
	Elevator->RequiredButtonActorTags.Add(TEXT("Heavy"));
	Object->Tags.Add(TEXT("Heavy"));
	Scope.StartPlay();
	Character->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation() + FVector(0.0f, 0.0f, 60.0f));
	Character->GetCapsuleComponent()->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	Elevator->Tick(0.1f);
	TestTrue(TEXT("Character partially presses the button"), Elevator->GetButtonMovementAlpha() > 0.0f);
	Character->SetActorLocation(FVector(10000.0f, 0.0f, 120.0f));
	Character->GetCapsuleComponent()->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("Character exit cancels the pending trip"), Elevator->IsMoving());
	TestFalse(TEXT("Canceled press is no longer logically held"), Elevator->IsButtonPressed());
	TestEqual(TEXT("Canceling a partial press emits one release hook"), Elevator->ButtonReleasedCount, 1);
	Elevator->Tick(0.2f);
	TestTrue(TEXT("Canceled Character press returns to Start"), Elevator->IsAtStart());
	TestEqual(TEXT("Canceled Character press raises fully"), Elevator->GetButtonMovementAlpha(), 0.0f);
	TestTrue(TEXT("Empty button rearms after Character exit"), Elevator->IsButtonArmed());
	Object->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation());
	Object->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	Elevator->Tick(0.1f);
	TestFalse(TEXT("Tagged object cannot move the platform during partial press"), Elevator->IsMoving());
	Object->SetActorLocation(FVector(10000.0f, 0.0f, 0.0f));
	Object->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	Elevator->Tick(0.2f);
	TestTrue(TEXT("Removed object leaves the platform at Start"), Elevator->IsAtStart());
	TestEqual(TEXT("Removed object lets the button rise"), Elevator->GetButtonMovementAlpha(), 0.0f);
	Object->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation());
	Object->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("Object re-entry still waits for full depression"), Elevator->IsMoving());
	Elevator->Tick(0.4f);
	TestTrue(TEXT("Object re-entry starts travel after the full press"),
		Elevator->GetMoverState() == EPuzzleTransformMoverState::MovingTowardEnd);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxElevatorCharacterTest,
	"Paradox.Elevator.CharacterTransport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxElevatorCharacterTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Elevator::Tests;
	FScopedWorld Scope(TEXT("ParadoxElevatorCharacterWorld"));
	if (!TestNotNull(TEXT("Elevator character world exists"), Scope.World))
	{
		return false;
	}
	AParadoxElevatorTestActor* Elevator = SpawnElevator(Scope, TEXT("CharacterElevator"));
	AParadoxPlayerCharacter* Character = Spawn<AParadoxPlayerCharacter>(
		*Scope.World, TEXT("ElevatorPlayer"), FVector(10000.0f, 0.0f, 120.0f));
	if (!TestNotNull(TEXT("Character elevator exists"), Elevator)
		|| !TestNotNull(TEXT("Character exists"), Character))
	{
		return false;
	}
	Elevator->RequiredButtonActorTags.Add(TEXT("ObjectOnlyTag"));
	Scope.StartPlay();
	Character->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation() + FVector(0.0f, 0.0f, 60.0f));
	Character->GetCapsuleComponent()->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestTrue(TEXT("Character bypasses object tag filter"), Elevator->IsMoving());
	TestTrue(TEXT("Character is acquired as a passenger"), Elevator->IsActorBeingLifted(Character));
	UGameplayActionComponent* Actions = Character->GetGameplayActionComponent();
	if (TestNotNull(TEXT("Character has action component"), Actions))
	{
		TestTrue(TEXT("Travel owns the Character Movement lock"),
			Actions->IsExternalExecutionLockHeld(GameplayActionTags::Lock_Movement));
	}
	Elevator->Tick(1.0f);
	TestTrue(TEXT("Character trip reaches End"), Elevator->IsAtEnd());
	TestFalse(TEXT("Character passenger is released at End"), Elevator->IsActorBeingLifted(Character));
	TestEqual(TEXT("Button rises after carrying the Character"), Elevator->GetButtonMovementAlpha(), 0.0f);
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("Character remaining on the button cannot immediately retrigger"), Elevator->IsMoving());
	TestFalse(TEXT("Character remaining on the button keeps it disarmed"), Elevator->IsButtonArmed());
	if (Actions)
	{
		TestFalse(TEXT("Character Movement lock is released at End"),
			Actions->IsExternalExecutionLockHeld(GameplayActionTags::Lock_Movement));
	}
	Character->SetActorLocation(FVector(10000.0f, 0.0f, 120.0f));
	Character->GetCapsuleComponent()->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestTrue(TEXT("Character exit rearms the arrived elevator"), Elevator->IsButtonArmed());
	Character->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation() + FVector(0.0f, 0.0f, 60.0f));
	Character->GetCapsuleComponent()->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestTrue(TEXT("Character re-entry starts the opposite trip"),
		Elevator->GetMoverState() == EPuzzleTransformMoverState::MovingTowardStart);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxElevatorWorldStateTest,
	"Paradox.Elevator.WorldStateAndReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxElevatorWorldStateTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Elevator::Tests;
	FScopedWorld Scope(TEXT("ParadoxElevatorWorldStateWorld"));
	if (!TestNotNull(TEXT("Elevator WorldState world exists"), Scope.World))
	{
		return false;
	}
	AParadoxElevatorTestActor* Elevator = SpawnElevator(Scope, TEXT("WorldStateElevator"));
	AParadoxElevatorTestOccupant* Occupant = Spawn<AParadoxElevatorTestOccupant>(
		*Scope.World, TEXT("WorldStateOccupant"), FVector(10000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("WorldState elevator exists"), Elevator)
		|| !TestNotNull(TEXT("WorldState occupant exists"), Occupant))
	{
		return false;
	}
	Scope.StartPlay();
	UWorldStateSubsystem* WorldState = Scope.World->GetSubsystem<UWorldStateSubsystem>();
	if (!TestNotNull(TEXT("WorldState subsystem exists"), WorldState))
	{
		return false;
	}
	TestTrue(TEXT("WorldState registration finalizes"), WorldState->FinalizeWorldStateRegistration().IsSuccess());
	FWorldStateCaptureRequest CaptureRequest;
	TestTrue(TEXT("Elevator baseline captures"), WorldState->CaptureBaseline(CaptureRequest).IsSuccess());
	Occupant->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation());
	Occupant->Root->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	Elevator->Tick(0.5f);
	TestTrue(TEXT("WorldState capture starts from a moving, pressed elevator"),
		Elevator->IsMoving() && Elevator->IsButtonPressed());
	const FWorldStateCaptureResult MovingCapture = WorldState->CaptureRuntimeSnapshot(CaptureRequest);
	if (TestTrue(TEXT("Moving elevator snapshot captures"), MovingCapture.IsSuccess()))
	{
		Elevator->Tick(0.5f);
		TestTrue(TEXT("Elevator reaches End before moving-state restore"), Elevator->IsAtEnd());
		FWorldStateRestoreRequest MovingRestoreRequest;
		MovingRestoreRequest.SnapshotId = MovingCapture.SnapshotId;
		const int32 PresentationEventsBeforeRestore = Elevator->GetButtonPresentationEventCountForTest();
		TestTrue(TEXT("Moving elevator snapshot restores"), WorldState->RestoreSnapshot(MovingRestoreRequest).IsSuccess());
		TestEqual(TEXT("Moving-state restore does not replay button presentation"),
			Elevator->GetButtonPresentationEventCountForTest(), PresentationEventsBeforeRestore);
		TestTrue(TEXT("WorldState restores movement without a new button entry"), Elevator->IsMoving());
		TestTrue(TEXT("Moving WorldState restores a pressed button"), Elevator->IsButtonPressed());
		TestEqual(TEXT("Moving WorldState snaps button to pressed alpha"), Elevator->GetButtonMovementAlpha(), 1.0f);
	}
	FWorldStateRestoreRequest RestoreRequest;
	const int32 PresentationEventsBeforeBaselineRestore = Elevator->GetButtonPresentationEventCountForTest();
	TestTrue(TEXT("WorldState baseline restores"), WorldState->RestoreBaseline(RestoreRequest).IsSuccess());
	TestEqual(TEXT("Baseline restore does not replay button presentation"),
		Elevator->GetButtonPresentationEventCountForTest(), PresentationEventsBeforeBaselineRestore);
	TestTrue(TEXT("WorldState restores Start"), Elevator->IsAtStart());
	TestFalse(TEXT("WorldState restores a raised button"), Elevator->IsButtonPressed());
	TestEqual(TEXT("WorldState restores exact raised alpha"), Elevator->GetButtonMovementAlpha(), 0.0f);
	TestFalse(TEXT("WorldState restores endpoint navigation"), Elevator->IsPassageBlockingNavigation());
	TestFalse(TEXT("WorldState does not start another trip"), Elevator->IsMoving());

	Occupant->SetActorLocation(Elevator->ButtonOccupancyVolume->GetComponentLocation());
	Occupant->Root->UpdateOverlaps(nullptr, true);
	const int32 PresentationEventsBeforeReset = Elevator->GetButtonPresentationEventCountForTest();
	Elevator->ResetMover();
	TestEqual(TEXT("Reset does not replay button presentation"),
		Elevator->GetButtonPresentationEventCountForTest(), PresentationEventsBeforeReset);
	TestFalse(TEXT("Reset while occupied requires an exit before rearming"), Elevator->IsButtonArmed());
	Elevator->RefreshButtonOccupancy();
	TestFalse(TEXT("Reset does not retrigger from persistent occupancy"), Elevator->IsMoving());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxElevatorPickupableDropTest,
	"Paradox.Elevator.PickupableDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxElevatorPickupableDropTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Elevator::Tests;
	FScopedWorld Scope(TEXT("ParadoxElevatorDropWorld"));
	if (!TestNotNull(TEXT("Elevator Drop world exists"), Scope.World))
	{
		return false;
	}
	AParadoxElevatorTestActor* Elevator = SpawnElevator(Scope, TEXT("DropElevator"));
	APressurePlateTestPickupable* Pickupable = Spawn<APressurePlateTestPickupable>(
		*Scope.World, TEXT("DropPickupable"), FVector(500.0f, 0.0f, 40.0f));
	AParadoxPlayerCharacter* Holder = Spawn<AParadoxPlayerCharacter>(
		*Scope.World, TEXT("DropHolder"), FVector(1000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Drop elevator exists"), Elevator)
		|| !TestNotNull(TEXT("Drop pickupable exists"), Pickupable)
		|| !TestNotNull(TEXT("Drop holder exists"), Holder))
	{
		return false;
	}
	Elevator->RequiredButtonActorTags.Add(TEXT("Heavy"));
	Pickupable->Tags.Add(TEXT("Heavy"));
	Scope.StartPlay();
	UParadoxInventoryComponent* Inventory = Holder->GetInventoryComponent();
	if (!TestNotNull(TEXT("Drop holder has an inventory"), Inventory))
	{
		return false;
	}
	TestTrue(TEXT("Pickupable equips before Drop"), Inventory->TryEquip(Pickupable).IsSuccess());
	TestTrue(TEXT("Pickupable drops onto button"),
		Inventory->TryDropAtTransform(FTransform(Elevator->ButtonOccupancyVolume->GetComponentLocation())).IsSuccess());
	TestTrue(TEXT("Drop reconciliation starts elevator without a manual refresh"), Elevator->IsMoving());
	TestTrue(TEXT("Drop leaves button pressed during travel"), Elevator->IsButtonPressed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxElevatorPIETransportTest,
	"Paradox.Elevator.PIEMeshCollisionAndTransport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxElevatorPIETransportTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Elevator::Tests;
	FScopedWorld Scope(TEXT("ParadoxElevatorPIEWorld"), EWorldType::PIE);
	if (!TestNotNull(TEXT("Elevator PIE world exists"), Scope.World))
	{
		return false;
	}
	AParadoxElevatorTestActor* Elevator = SpawnElevator(Scope, TEXT("PIEElevator"));
	AParadoxPlayerCharacter* Character = Spawn<AParadoxPlayerCharacter>(
		*Scope.World, TEXT("PIECharacter"), FVector(10000.0f, 0.0f, 144.0f));
	UStaticMesh* TestCube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("PIE elevator exists"), Elevator)
		|| !TestNotNull(TEXT("PIE Character exists"), Character)
		|| !TestNotNull(TEXT("PIE collision cube is available"), TestCube))
	{
		return false;
	}
	Elevator->BarrierMesh->SetStaticMesh(TestCube);
	Elevator->BarrierMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Elevator->BarrierMesh->SetCollisionResponseToAllChannels(ECR_Block);
	Elevator->ButtonMesh->SetStaticMesh(TestCube);
	Elevator->ButtonMesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.2f));
	Elevator->ButtonMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	Scope.StartPlay();
	TestEqual(TEXT("PIE platform mesh blocks Character collision"),
		Elevator->BarrierMesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
	TestEqual(TEXT("PIE visual button has no blocking collision"),
		Elevator->ButtonMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestEqual(TEXT("PIE button volume overlaps Character collision"),
		Elevator->ButtonOccupancyVolume->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
	const float PlatformTop = Elevator->BarrierMesh->Bounds.Origin.Z + Elevator->BarrierMesh->Bounds.BoxExtent.Z;
	const float CharacterHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(FVector(0.0f, 0.0f, PlatformTop + CharacterHalfHeight + 2.0f));
	Character->GetCapsuleComponent()->UpdateOverlaps(nullptr, true);
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	FMovementBaseInterfaceData MovementBase(Elevator->BarrierMesh);
	Character->SetBase(&MovementBase);
	Elevator->RefreshButtonOccupancy();
	TestTrue(TEXT("PIE Character button entry starts travel"), Elevator->IsMoving());
	TestTrue(TEXT("PIE Character is carried by the platform"), Elevator->IsActorBeingLifted(Character));
	TestTrue(TEXT("PIE Character uses the platform mesh as its movement base"),
		Character->GetMovementBaseObject() == Elevator->BarrierMesh);
	const FVector InitialCharacterLocation = Character->GetActorLocation();
	const FVector InitialPlatformLocation = Elevator->BarrierMesh->GetComponentLocation();
	constexpr float FrameDelta = 1.0f / 60.0f;
	for (int32 Frame = 0; Frame < 15; ++Frame)
	{
		Elevator->Tick(FrameDelta);
		Character->GetCharacterMovement()->TickComponent(FrameDelta, ELevelTick::LEVELTICK_All, nullptr);
	}
	const FVector CharacterDelta = Character->GetActorLocation() - InitialCharacterLocation;
	const FVector PlatformDelta = Elevator->BarrierMesh->GetComponentLocation() - InitialPlatformLocation;
	TestTrue(
		*FString::Printf(TEXT("PIE passenger follows the platform (Character=%s Platform=%s)"),
			*CharacterDelta.ToCompactString(), *PlatformDelta.ToCompactString()),
		CharacterDelta.Equals(PlatformDelta, 1.0f));
	TestTrue(TEXT("PIE movement lock remains held during travel"),
		Character->GetGameplayActionComponent()->IsExternalExecutionLockHeld(GameplayActionTags::Lock_Movement));
	for (int32 Frame = 0; Frame < 46; ++Frame)
	{
		Elevator->Tick(FrameDelta);
		Character->GetCharacterMovement()->TickComponent(FrameDelta, ELevelTick::LEVELTICK_All, nullptr);
	}
	TestTrue(TEXT("PIE elevator reaches End"), Elevator->IsAtEnd());
	TestFalse(TEXT("PIE passenger lock is released at End"),
		Character->GetGameplayActionComponent()->IsExternalExecutionLockHeld(GameplayActionTags::Lock_Movement));
	TestFalse(TEXT("PIE endpoint passage is navigable"), Elevator->IsPassageBlockingNavigation());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxElevatorAuthoredBlueprintPIETest,
	"Paradox.Elevator.AuthoredBlueprintPIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxElevatorAuthoredBlueprintPIETest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Elevator::Tests;
	FScopedWorld Scope(TEXT("ParadoxElevatorAuthoredBlueprintPIEWorld"), EWorldType::PIE);
	UClass* ElevatorClass = LoadClass<AParadoxElevator>(nullptr,
		TEXT("/Game/Environment/SpaceShip/Blueprints/Movable/BP_Elevator.BP_Elevator_C"));
	if (!TestNotNull(TEXT("Authored BP_Elevator class loads"), ElevatorClass)
		|| !TestNotNull(TEXT("Authored Blueprint PIE world exists"), Scope.World))
	{
		return false;
	}
	AParadoxElevator* Elevator = Scope.World->SpawnActor<AParadoxElevator>(ElevatorClass);
	AParadoxPlayerCharacter* Character = Spawn<AParadoxPlayerCharacter>(
		*Scope.World, TEXT("AuthoredElevatorPassenger"), FVector(10000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Authored elevator spawns"), Elevator)
		|| !TestNotNull(TEXT("Passenger spawns"), Character))
	{
		return false;
	}
	Scope.Controller.Reset(NewObject<APuzzleController>(GetTransientPackage()));
	Elevator->PuzzleReceiver->SetControllerRequest(Scope.Controller.Get(), true);
	Scope.StartPlay();
	TestEqual(TEXT("Placed Blueprint Receiver uses Manual mode"),
		Elevator->PuzzleReceiver->GetActivationMode(), EPuzzleReceiverActivationMode::Manual);
	TestTrue(TEXT("Placed Blueprint shows incoming puzzle links when selected"),
		Elevator->SelectableComponent->bShowPuzzleConnectionsWhenSelected);
	TestNotNull(TEXT("Authored platform mesh is assigned"), Elevator->BarrierMesh->GetStaticMesh().Get());
	TestNotNull(TEXT("Authored button mesh is assigned"), Elevator->ButtonMesh->GetStaticMesh().Get());
	TestEqual(TEXT("Authored button overlaps Pawn"),
		Elevator->ButtonOccupancyVolume->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
	const float PlatformTop = Elevator->BarrierMesh->Bounds.Origin.Z + Elevator->BarrierMesh->Bounds.BoxExtent.Z;
	const float CharacterHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Character->SetActorLocation(FVector(0.0f, 0.0f, PlatformTop + CharacterHalfHeight + 2.0f));
	Character->GetCapsuleComponent()->UpdateOverlaps(nullptr, true);
	Elevator->RefreshButtonOccupancy();
	TestTrue(TEXT("Authored button begins pressing under the Character"), Elevator->IsButtonPressed());
	TestFalse(TEXT("Authored platform waits for the button press"), Elevator->IsMoving());
	Elevator->Tick(Elevator->PressDuration + 0.01f);
	TestTrue(TEXT("Authored platform starts after the full press"), Elevator->IsMoving());
	Elevator->Tick(Elevator->ForwardMovementTime + 0.01f);
	Elevator->Tick(Elevator->ReleaseDuration + 0.01f);
	TestTrue(TEXT("Authored platform reaches End"), Elevator->IsAtEnd());
	TestEqual(TEXT("Authored button rises on arrival"), Elevator->GetButtonMovementAlpha(), 0.0f);
	return true;
}

#endif
