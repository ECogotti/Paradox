#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationModifierComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayActionTags.h"
#include "Interfaces/MovementBaseInterface.h"
#include "NiagaraComponent.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "ParadoxVerticalBarrierTestTypes.h"
#include "SmartObjectComponent.h"
#include "Subsystems/WorldStateSubsystem.h"

namespace UE::Paradox::VerticalBarrier::Tests
{
	struct FScopedWorld
	{
		explicit FScopedWorld(const EWorldType::Type WorldType = EWorldType::Game)
		{
			Context = GEngine ? &GEngine->CreateNewWorldContext(WorldType) : nullptr;
			World = UWorld::CreateWorld(
				WorldType,
				false,
				WorldType == EWorldType::PIE
					? TEXT("ParadoxVerticalBarrierPIETestWorld")
					: TEXT("ParadoxVerticalBarrierTestWorld"));
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
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (!It->HasActorBegunPlay())
				{
					It->DispatchBeginPlay();
				}
			}
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	template <typename T>
	T* Spawn(UWorld& World, const FName Name, const FVector Location = FVector::ZeroVector)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = Name;
		Parameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<T>(T::StaticClass(), FTransform(FRotator::ZeroRotator, Location), Parameters);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxVerticalBarrierArchitectureTest,
	"Paradox.VerticalBarrier.Architecture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxVerticalBarrierArchitectureTest::RunTest(const FString& Parameters)
{
	const AParadoxVerticalBarrier* Defaults = GetDefault<AParadoxVerticalBarrier>();
	if (!TestNotNull(TEXT("Vertical Barrier CDO exists"), Defaults))
	{
		return false;
	}
	TestFalse(TEXT("Vertical Barrier is concrete"), AParadoxVerticalBarrier::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
	TestTrue(TEXT("Vertical Barrier derives from the generic mover"), AParadoxVerticalBarrier::StaticClass()->IsChildOf(APuzzleTransformMover::StaticClass()));
	TestTrue(TEXT("Barrier attaches to inherited root"), Defaults->BarrierMesh && Defaults->BarrierMesh->GetAttachParent() == Defaults->BillboardRoot.Get());
	TestTrue(TEXT("Audio follows BarrierMesh"), Defaults->MovementAudio && Defaults->MovementAudio->GetAttachParent() == Defaults->BarrierMesh.Get());
	TestTrue(TEXT("VFX follows BarrierMesh"), Defaults->MovementVFX && Defaults->MovementVFX->GetAttachParent() == Defaults->BarrierMesh.Get());
	TestTrue(TEXT("Occupancy volume mirrors modifier extent"),
		Defaults->PassageOccupancyVolume && Defaults->GridNavigationModifier
		&& Defaults->PassageOccupancyVolume->GetUnscaledBoxExtent().Equals(Defaults->GridNavigationModifier->BoxExtent));
	TestFalse(TEXT("Barrier mesh does not own navigation"), Defaults->BarrierMesh->CanEverAffectNavigation());
	TestFalse(TEXT("Stable endpoint navigation generation is opt-in"), Defaults->bGenerateNavigationAtStableEndpoints);
	TestFalse(TEXT("Occupancy volume does not own navigation"), Defaults->PassageOccupancyVolume->CanEverAffectNavigation());
	TestTrue(TEXT("Grid modifier auto activates"), Defaults->GridNavigationModifier->bAutoActivate);
	TestTrue(
		TEXT("End is lowered below the closed Start endpoint"),
		Defaults->EndArrow->GetRelativeLocation().Z < Defaults->StartArrow->GetRelativeLocation().Z);
	TestTrue(TEXT("Closed Start blocks navigation by default"), Defaults->GridNavigationModifier->bBlockCells);
	TestTrue(TEXT("Safe policy is the native default"), Defaults->bWaitForClearPassage);
	TestEqual(TEXT("Closed Start is the native initial endpoint"), Defaults->InitialPosition, EPuzzleTransformMoverInitialPosition::Start);
	TestNotNull(TEXT("World State participant exists"), Defaults->WorldStateParticipant.Get());
	TestNotNull(TEXT("Perception source exists"), Defaults->PerceptionSource.Get());
	TestNotNull(TEXT("Selectable Component exists"), Defaults->SelectableComponent.Get());
	TestTrue(TEXT("Vertical Barrier opts into selected interaction-cell presentation"),
		Defaults->SelectableComponent
			&& Defaults->SelectableComponent->bShowInteractionCellsWhenSelected);
	TestTrue(TEXT("Vertical Barrier opts into selected puzzle-circuit presentation"),
		Defaults->SelectableComponent
			&& Defaults->SelectableComponent->bShowPuzzleConnectionsWhenSelected);
	TestNotNull(TEXT("Smart Object Component exists"), Defaults->SmartObjectComponent.Get());
	TestTrue(TEXT("Smart Object Component attaches to inherited root"),
		Defaults->SmartObjectComponent
			&& Defaults->SmartObjectComponent->GetAttachParent() == Defaults->BillboardRoot.Get());
	TestNotNull(TEXT("Paradox Interaction Component exists"), Defaults->InteractionComponent.Get());
	TestNull(TEXT("Vertical Barrier permits an unassigned Smart Object Definition"),
		Defaults->SmartObjectComponent ? Defaults->SmartObjectComponent->GetDefinition() : nullptr);
	TestEqual(TEXT("Vertical Barrier permits an empty native interaction catalog"),
		Defaults->InteractionComponent ? Defaults->InteractionComponent->InteractionDefinitions.Num() : INDEX_NONE,
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxVerticalBarrierInitialPositionAndWorldResetTest,
	"Paradox.VerticalBarrier.InitialPositionAndWorldReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxVerticalBarrierInitialPositionAndWorldResetTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::VerticalBarrier::Tests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("Initial-position test world exists"), Scope.World))
	{
		return false;
	}

	AParadoxVerticalBarrierTestActor* Barrier = Spawn<AParadoxVerticalBarrierTestActor>(
		*Scope.World,
		TEXT("InitialEndBarrier"));
	if (!TestNotNull(TEXT("Initial-position barrier exists"), Barrier))
	{
		return false;
	}

	Barrier->InitialPosition = EPuzzleTransformMoverInitialPosition::End;
	Barrier->ForwardMovementTime = 1.0f;
	Barrier->bEmitNoiseOnRaiseStart = false;
	Barrier->bEmitNoiseOnLowerStart = false;
	Barrier->bEmitNoiseOnReachedEndpoint = false;
	Scope.StartPlay();

	TestTrue(TEXT("Inactive Receiver preserves open End at BeginPlay"), Barrier->IsAtEnd());
	TestEqual(TEXT("Open End begins at exact alpha"), Barrier->GetMovementAlpha(), 1.0f);
	TestTrue(TEXT("Open End is navigable"), Barrier->IsPassageOpen());
	TestTrue(
		TEXT("Barrier mesh begins at the authored End transform"),
		Barrier->BarrierMesh->GetComponentLocation().Equals(Barrier->GetEndTransform().GetLocation(), 0.01f));

	UWorldStateSubsystem* WorldState = Scope.World->GetSubsystem<UWorldStateSubsystem>();
	if (!TestNotNull(TEXT("World State subsystem exists"), WorldState))
	{
		return false;
	}
	TestTrue(
		TEXT("World State registration finalizes"),
		WorldState->FinalizeWorldStateRegistration().IsSuccess());
	FWorldStateCaptureRequest CaptureRequest;
	CaptureRequest.Label = TEXT("VerticalBarrierInitialEndBaseline");
	TestTrue(TEXT("Initial End baseline captures"), WorldState->CaptureBaseline(CaptureRequest).IsSuccess());

	TestTrue(TEXT("Barrier can close toward Start"), Barrier->RequestStartForTest());
	Barrier->Tick(1.0f);
	TestTrue(TEXT("Barrier reaches closed Start before local reset"), Barrier->IsAtStart());
	Barrier->ResetMover();
	TestTrue(TEXT("ResetMover restores configured End"), Barrier->IsAtEnd());
	TestTrue(TEXT("ResetMover restores open navigation"), Barrier->IsPassageOpen());

	TestTrue(TEXT("Barrier can close again before world reset"), Barrier->RequestStartForTest());
	Barrier->Tick(1.0f);
	TestTrue(TEXT("Barrier reaches closed Start before world reset"), Barrier->IsAtStart());
	FWorldStateRestoreRequest RestoreRequest;
	RestoreRequest.Reason = TEXT("VerticalBarrierInitialPositionTest");
	TestTrue(TEXT("World reset restores the baseline"), WorldState->RestoreBaseline(RestoreRequest).IsSuccess());
	TestTrue(TEXT("World reset restores configured End"), Barrier->IsAtEnd());
	TestEqual(TEXT("World reset restores exact End alpha"), Barrier->GetMovementAlpha(), 1.0f);
	TestTrue(TEXT("World reset restores open navigation"), Barrier->IsPassageOpen());
	TestTrue(
		TEXT("World reset rebuilds the BarrierMesh at End"),
		Barrier->BarrierMesh->GetComponentLocation().Equals(Barrier->GetEndTransform().GetLocation(), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxVerticalBarrierStableEndpointNavigationTest,
	"Paradox.VerticalBarrier.StableEndpointNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxVerticalBarrierStableEndpointNavigationTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::VerticalBarrier::Tests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("Test world exists"), Scope.World))
	{
		return false;
	}

	AParadoxVerticalBarrierTestActor* Barrier = Spawn<AParadoxVerticalBarrierTestActor>(
		*Scope.World,
		TEXT("StableEndpointNavigationBarrier"));
	UStaticMesh* TestCube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Barrier fixture exists"), Barrier)
		|| !TestNotNull(TEXT("Engine collision cube is available"), TestCube))
	{
		return false;
	}

	Barrier->BarrierMesh->SetStaticMesh(TestCube);
	Barrier->BarrierMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Barrier->bGenerateNavigationAtStableEndpoints = true;
	Barrier->ForwardMovementTime = 1.0f;
	Barrier->bEmitNoiseOnRaiseStart = false;
	Barrier->bEmitNoiseOnLowerStart = false;
	Barrier->bEmitNoiseOnReachedEndpoint = false;
	Scope.StartPlay();

	TestTrue(TEXT("Opt-in BarrierMesh affects navigation at stable Start"), Barrier->BarrierMesh->CanEverAffectNavigation());
	TestTrue(TEXT("Movement toward End starts"), Barrier->RequestEndForTest());
	TestFalse(TEXT("BarrierMesh stops affecting navigation before movement updates"), Barrier->BarrierMesh->CanEverAffectNavigation());
	TestTrue(TEXT("Active movement can pause"), Barrier->PauseForTest());
	TestFalse(TEXT("An intermediate pause does not publish a moving surface"), Barrier->BarrierMesh->CanEverAffectNavigation());
	TestTrue(TEXT("The paused End request resumes movement"), Barrier->RequestEndForTest());
	TestFalse(TEXT("Resumed movement remains navigation-irrelevant"), Barrier->BarrierMesh->CanEverAffectNavigation());

	Barrier->Tick(1.0f);
	TestTrue(TEXT("Barrier reaches stable End"), Barrier->IsAtEnd());
	TestTrue(TEXT("Opt-in BarrierMesh affects navigation at stable End"), Barrier->BarrierMesh->CanEverAffectNavigation());
	TestTrue(TEXT("Movement toward Start begins"), Barrier->RequestStartForTest());
	TestFalse(TEXT("Return movement removes the endpoint surface once"), Barrier->BarrierMesh->CanEverAffectNavigation());
	Barrier->Tick(1.0f);
	TestTrue(TEXT("Barrier returns to stable Start"), Barrier->IsAtStart());
	TestTrue(TEXT("Stable Start republishes the surface"), Barrier->BarrierMesh->CanEverAffectNavigation());

	Barrier->bGenerateNavigationAtStableEndpoints = false;
	Barrier->ResetMover();
	TestFalse(TEXT("Disabling the option restores navigation-irrelevant barrier behavior"), Barrier->BarrierMesh->CanEverAffectNavigation());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxVerticalBarrierSafePolicyTest,
	"Paradox.VerticalBarrier.SafePolicyOccupancyAndRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxVerticalBarrierSafePolicyTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::VerticalBarrier::Tests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("Test world exists"), Scope.World))
	{
		return false;
	}
	AParadoxVerticalBarrierTestActor* Barrier = Spawn<AParadoxVerticalBarrierTestActor>(*Scope.World, TEXT("SafeBarrier"));
	AParadoxVerticalBarrierTestOccupant* Occupant = Spawn<AParadoxVerticalBarrierTestOccupant>(
		*Scope.World, TEXT("Occupant"), FVector(0.0f, 0.0f, 120.0f));
	AParadoxVerticalBarrierTestOccupant* SecondOccupant = Spawn<AParadoxVerticalBarrierTestOccupant>(
		*Scope.World, TEXT("SecondOccupant"), FVector(30.0f, 0.0f, 120.0f));
	Occupant->EnablePhysicalOverlap();
	SecondOccupant->EnablePhysicalOverlap();
	Barrier->bEmitNoiseOnRaiseStart = false;
	Barrier->bEmitNoiseOnLowerStart = false;
	Barrier->bEmitNoiseOnReachedEndpoint = false;
	Barrier->MovementMode = EPuzzleTransformMoverMode::PingPong;
	Scope.StartPlay();
	TestTrue(TEXT("Safe barrier opens toward End before the close test"), Barrier->RequestEndForTest());
	Barrier->Tick(1.0f);
	TestTrue(TEXT("Safe barrier starts the close test at open End"), Barrier->IsAtEnd());

	TestEqual(TEXT("Two components per Actor count as two distinct Actors"), Barrier->GetPassageOccupantCount(), 2);
	TestTrue(TEXT("Occupancy query finds the first Actor"), Barrier->IsActorOccupyingPassage(Occupant));
	TestFalse(TEXT("Occupied safe raise is deferred"), Barrier->RequestStartForTest());
	TestTrue(TEXT("Deferred raise remains pending"), Barrier->IsRaiseRequestPending());
	TestTrue(TEXT("Deferred barrier remains at open End"), Barrier->IsAtEnd());
	TestFalse(TEXT("Deferred open End remains navigable"), Barrier->IsPassageBlockingNavigation());
	TestFalse(TEXT("Duplicate End does not move the barrier"), Barrier->RequestEndForTest());
	TestFalse(TEXT("End target cancels a pending raise request"), Barrier->IsRaiseRequestPending());
	TestFalse(TEXT("The still-occupied passage defers a new Start request"), Barrier->RequestStartForTest());

	Occupant->Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Barrier->SimulateEndOverlap(Occupant, Occupant->Root);
	TestEqual(TEXT("First component exit keeps both distinct Actors occupied"), Barrier->GetPassageOccupantCount(), 2);
	Occupant->SecondComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Barrier->SimulateEndOverlap(Occupant, Occupant->SecondComponent);
	TestEqual(TEXT("Final component exit removes only the first Actor"), Barrier->GetPassageOccupantCount(), 1);
	TestTrue(TEXT("Raise remains pending until every distinct Actor leaves"), Barrier->IsRaiseRequestPending());
	SecondOccupant->Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Barrier->SimulateEndOverlap(SecondOccupant, SecondOccupant->Root);
	SecondOccupant->SecondComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Barrier->SimulateEndOverlap(SecondOccupant, SecondOccupant->SecondComponent);
	TestEqual(TEXT("Final distinct Actor exit clears occupancy"), Barrier->GetPassageOccupantCount(), 0);
	TestEqual(TEXT("Final exit retries exactly into raising"), Barrier->GetMoverState(), EPuzzleTransformMoverState::MovingTowardStart);
	TestTrue(TEXT("Navigation blocks before raising"), Barrier->IsPassageBlockingNavigation());

	Occupant->Root->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Barrier->SimulateBeginOverlap(Occupant, Occupant->Root);
	TestEqual(TEXT("Safe entry during raise reverses toward open End"), Barrier->GetMoverState(), EPuzzleTransformMoverState::MovingTowardEnd);
	TestTrue(TEXT("Safety return retains pending raise"), Barrier->IsRaiseRequestPending());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxVerticalBarrierLiftPolicyTest,
	"Paradox.VerticalBarrier.LiftAttachmentLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxVerticalBarrierLiftPolicyTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::VerticalBarrier::Tests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("Test world exists"), Scope.World))
	{
		return false;
	}
	AParadoxVerticalBarrierTestActor* Barrier = Spawn<AParadoxVerticalBarrierTestActor>(*Scope.World, TEXT("LiftBarrier"));
	AParadoxVerticalBarrierTestOccupant* Occupant = Spawn<AParadoxVerticalBarrierTestOccupant>(
		*Scope.World, TEXT("LiftedActor"), FVector(0.0f, 0.0f, 120.0f));
	Occupant->EnablePhysicalOverlap();
	Occupant->Root->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Occupant->Root->SetEnableGravity(false);
	Occupant->Root->SetSimulatePhysics(true);
	Barrier->bWaitForClearPassage = false;
	Barrier->ForwardMovementTime = 1.0f;
	Barrier->bEmitNoiseOnRaiseStart = false;
	Barrier->bEmitNoiseOnLowerStart = false;
	Barrier->bEmitNoiseOnReachedEndpoint = false;
	Scope.StartPlay();
	TestTrue(TEXT("Lift barrier opens toward End before the raise test"), Barrier->RequestEndForTest());
	Barrier->Tick(1.0f);
	TestTrue(TEXT("Lift barrier starts the raise test at open End"), Barrier->IsAtEnd());

	TestTrue(TEXT("Lift mode accepts raise while occupied"), Barrier->RequestStartForTest());
	TestTrue(TEXT("Movable Actor becomes a passenger"), Barrier->IsActorBeingLifted(Occupant));
	TestTrue(TEXT("Passenger root attaches to moved component"), Occupant->GetRootComponent()->GetAttachParent() == Barrier->GetMovedComponent());
	TestFalse(TEXT("Physics simulation is disabled while attached"), Occupant->Root->IsSimulatingPhysics());
	Barrier->SimulateEndOverlap(Occupant, Occupant->Root);
	Barrier->SimulateEndOverlap(Occupant, Occupant->SecondComponent);
	TestTrue(TEXT("EndOverlap does not release a passenger"), Barrier->IsActorBeingLifted(Occupant));

	Barrier->Tick(1.0f);
	TestTrue(TEXT("Barrier reaches closed Start"), Barrier->IsAtStart());
	TestFalse(TEXT("Endpoint releases passenger"), Barrier->IsActorBeingLifted(Occupant));
	TestNull(TEXT("Previously unattached Actor is detached with world transform preserved"), Occupant->GetRootComponent()->GetAttachParent());
	TestTrue(TEXT("Endpoint restores physics simulation"), Occupant->Root->IsSimulatingPhysics());
	TestFalse(TEXT("Endpoint restores the previous gravity state"), Occupant->Root->IsGravityEnabled());
	TestTrue(TEXT("Closed Start remains navigation-blocking"), Barrier->IsPassageBlockingNavigation());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxVerticalBarrierCharacterTransportTest,
	"Paradox.VerticalBarrier.CharacterLockAndTransport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxVerticalBarrierCharacterTransportTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::VerticalBarrier::Tests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("Test world exists"), Scope.World))
	{
		return false;
	}
	AParadoxVerticalBarrierTestActor* Barrier = Spawn<AParadoxVerticalBarrierTestActor>(
		*Scope.World, TEXT("CharacterLiftBarrier"));
	AParadoxPlayerCharacter* Character = Spawn<AParadoxPlayerCharacter>(
		*Scope.World, TEXT("LiftedPlayerCharacter"), FVector(0.0f, 0.0f, 120.0f));
	Barrier->bWaitForClearPassage = false;
	Barrier->ForwardMovementTime = 1.0f;
	Barrier->bEmitNoiseOnRaiseStart = false;
	Barrier->bEmitNoiseOnLowerStart = false;
	Barrier->bEmitNoiseOnReachedEndpoint = false;
	Scope.StartPlay();
	TestTrue(TEXT("Character barrier opens toward End before the raise test"), Barrier->RequestEndForTest());
	Barrier->Tick(1.0f);
	TestTrue(TEXT("Character barrier starts the raise test at open End"), Barrier->IsAtEnd());

	UGameplayActionComponent* Actions = Character->GetGameplayActionComponent();
	if (!TestNotNull(TEXT("Paradox character owns its Gameplay Actions component"), Actions))
	{
		return false;
	}
	Barrier->SimulateBeginOverlap(Character, Character->GetCapsuleComponent());
	TestTrue(TEXT("Lift mode accepts an occupied raise"), Barrier->RequestStartForTest());
	TestTrue(TEXT("Character is tracked as a passenger"), Barrier->IsActorBeingLifted(Character));
	TestNull(TEXT("Character is transported without attachment"), Character->GetRootComponent()->GetAttachParent());
	TestTrue(
		TEXT("Barrier owns the character Movement lock while transporting"),
		Actions->IsExternalExecutionLockHeld(GameplayActionTags::Lock_Movement));

	const FVector InitialCharacterLocation = Character->GetActorLocation();
	const FVector InitialBarrierLocation = Barrier->GetMovedComponent()->GetComponentLocation();
	Barrier->Tick(0.5f);
	const FVector CharacterDelta = Character->GetActorLocation() - InitialCharacterLocation;
	const FVector BarrierDelta = Barrier->GetMovedComponent()->GetComponentLocation() - InitialBarrierLocation;
	TestTrue(TEXT("Character follows the barrier world-space delta"), CharacterDelta.Equals(BarrierDelta, 1.0f));

	Barrier->Tick(0.5f);
	TestTrue(TEXT("Barrier reaches closed Start"), Barrier->IsAtStart());
	TestFalse(TEXT("Endpoint releases the character passenger"), Barrier->IsActorBeingLifted(Character));
	TestFalse(
		TEXT("Endpoint releases the character Movement lock"),
		Actions->IsExternalExecutionLockHeld(GameplayActionTags::Lock_Movement));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxVerticalBarrierPIEMovingBaseTest,
	"Paradox.VerticalBarrier.PIEMovingBaseCharacterTransport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxVerticalBarrierPIEMovingBaseTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::VerticalBarrier::Tests;
	FScopedWorld Scope(EWorldType::PIE);
	if (!TestNotNull(TEXT("PIE test world exists"), Scope.World))
	{
		return false;
	}
	TestEqual(TEXT("Transport runs with PIE world semantics"), Scope.World->WorldType, EWorldType::PIE);
	AParadoxVerticalBarrierTestActor* Barrier = Spawn<AParadoxVerticalBarrierTestActor>(
		*Scope.World, TEXT("PIEMovingBaseBarrier"));
	AParadoxPlayerCharacter* Character = Spawn<AParadoxPlayerCharacter>(
		*Scope.World, TEXT("PIEMovingBaseCharacter"), FVector(0.0f, 0.0f, -94.0f));
	UStaticMesh* TestCube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine collision cube is available to the PIE test"), TestCube))
	{
		return false;
	}
	Barrier->BarrierMesh->SetStaticMesh(TestCube);
	Barrier->BarrierMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Barrier->BarrierMesh->SetCollisionResponseToAllChannels(ECR_Block);
	Barrier->bWaitForClearPassage = false;
	Barrier->ForwardMovementTime = 1.0f;
	Barrier->bEmitNoiseOnRaiseStart = false;
	Barrier->bEmitNoiseOnLowerStart = false;
	Barrier->bEmitNoiseOnReachedEndpoint = false;
	Scope.StartPlay();
	TestTrue(TEXT("PIE barrier opens toward End before the raise test"), Barrier->RequestEndForTest());
	Barrier->Tick(1.0f);
	TestTrue(TEXT("PIE barrier starts the raise test at open End"), Barrier->IsAtEnd());

	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	FMovementBaseInterfaceData MovementBase(Barrier->BarrierMesh);
	Character->SetBase(&MovementBase);
	TestTrue(
		TEXT("Character uses BarrierMesh as its engine movement base"),
		Character->GetMovementBaseObject() == Barrier->BarrierMesh);
	Barrier->SimulateBeginOverlap(Character, Character->GetCapsuleComponent());
	TestTrue(TEXT("PIE moving-base raise is accepted"), Barrier->RequestStartForTest());

	const FVector InitialCharacterLocation = Character->GetActorLocation();
	const FVector InitialBarrierLocation = Barrier->BarrierMesh->GetComponentLocation();
	constexpr float FrameDelta = 1.0f / 60.0f;
	for (int32 Frame = 0; Frame < 15; ++Frame)
	{
		Barrier->Tick(FrameDelta);
		Character->GetCharacterMovement()->TickComponent(FrameDelta, ELevelTick::LEVELTICK_All, nullptr);
	}
	const FVector CharacterDelta = Character->GetActorLocation() - InitialCharacterLocation;
	const FVector BarrierDelta = Barrier->BarrierMesh->GetComponentLocation() - InitialBarrierLocation;
	TestTrue(
		*FString::Printf(
			TEXT("Engine moving-base transport follows the rising barrier in PIE (Character=%s Barrier=%s)"),
			*CharacterDelta.ToCompactString(),
			*BarrierDelta.ToCompactString()),
		CharacterDelta.Equals(BarrierDelta, 1.0f));
	TestTrue(
		TEXT("Barrier Movement lock remains owned during PIE transport"),
		Character->GetGameplayActionComponent()->IsExternalExecutionLockHeld(GameplayActionTags::Lock_Movement));

	for (int32 Frame = 0; Frame < 46; ++Frame)
	{
		Barrier->Tick(FrameDelta);
		Character->GetCharacterMovement()->TickComponent(FrameDelta, ELevelTick::LEVELTICK_All, nullptr);
	}
	TestTrue(TEXT("PIE barrier reaches closed Start"), Barrier->IsAtStart());
	TestFalse(
		TEXT("PIE endpoint releases the barrier-owned Movement lock"),
		Character->GetGameplayActionComponent()->IsExternalExecutionLockHeld(GameplayActionTags::Lock_Movement));
	return true;
}

#endif
