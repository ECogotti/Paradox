#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/ArrowComponent.h"
#include "Controllers/PuzzleController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ParadoxPairedTransferEndpointTestTypes.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "TimerManager.h"
#include "Types/WorldStateTypes.h"

namespace UE::Paradox::PairedTransfer::Tests
{
	struct FScopedWorld
	{
		explicit FScopedWorld(const TCHAR* Name)
		{
			Context = GEngine ? &GEngine->CreateNewWorldContext(EWorldType::Game) : nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
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
		return World.SpawnActor<T>(T::StaticClass(), FTransform(Location), Parameters);
	}

	struct FPairFixture
	{
		explicit FPairFixture(const TCHAR* WorldName)
			: Scope(WorldName)
		{
			Source = Spawn<AParadoxPairedTransferTestEndpoint>(*Scope.World, TEXT("SourceEndpoint"));
			Destination = Spawn<AParadoxPairedTransferTestEndpoint>(
				*Scope.World,
				TEXT("DestinationEndpoint"),
				FVector(500.0f, 0.0f, 0.0f));
			Subject = Spawn<AParadoxPairedTransferTestSubject>(
				*Scope.World,
				TEXT("TransferSubject"),
				FVector(25.0f, 25.0f, 0.0f));
			Requester = Spawn<AParadoxPairedTransferTestSubject>(
				*Scope.World,
				TEXT("Requester"),
				FVector::ZeroVector);
			Controller = NewObject<APuzzleController>(
				GetTransientPackage(),
				APuzzleController::StaticClass(),
				MakeUniqueObjectName(
					GetTransientPackage(),
					APuzzleController::StaticClass(),
					TEXT("ReceiverController")));

			if (Source && Destination)
			{
				Source->LinkedEndpoint = Destination;
				Destination->LinkedEndpoint = Source;
				Source->TransferOutDuration = 60.0f;
				Destination->TransferInDuration = 60.0f;
			}
		}

		bool IsValid() const
		{
			return Scope.World && Source && Destination && Subject && Requester && Controller;
		}

		void StartPlayAndActivate(const bool bActivateSource = true, const bool bActivateDestination = true)
		{
			Scope.StartPlay();
			Source->PuzzleReceiver->SetControllerRequest(Controller, bActivateSource);
			Destination->PuzzleReceiver->SetControllerRequest(Controller, bActivateDestination);
		}

		FScopedWorld Scope;
		AParadoxPairedTransferTestEndpoint* Source = nullptr;
		AParadoxPairedTransferTestEndpoint* Destination = nullptr;
		AParadoxPairedTransferTestSubject* Subject = nullptr;
		AParadoxPairedTransferTestSubject* Requester = nullptr;
		APuzzleController* Controller = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPairedTransferArchitectureTest,
	"Paradox.PairedTransferEndpoint.Architecture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPairedTransferArchitectureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const AParadoxPairedTransferEndpoint* Defaults = GetDefault<AParadoxPairedTransferEndpoint>();
	if (!TestNotNull(TEXT("Base endpoint CDO exists"), Defaults))
	{
		return false;
	}
	TestTrue(TEXT("Base endpoint remains abstract"), AParadoxPairedTransferEndpoint::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
	TestFalse(TEXT("Base endpoint does not tick"), Defaults->PrimaryActorTick.bCanEverTick);
	TestNotNull(TEXT("Scene root exists"), Defaults->SceneRoot.Get());
	TestNotNull(TEXT("Transfer anchor exists"), Defaults->TransferAnchor.Get());
	TestNotNull(TEXT("Puzzle Receiver exists"), Defaults->PuzzleReceiver.Get());
	TestTrue(TEXT("Paired endpoints opt into active-when-uncontrolled Receiver fallback"),
		Defaults->PuzzleReceiver && Defaults->PuzzleReceiver->bActivateWhenUncontrolled);
	TestEqual(TEXT("State starts Idle"), Defaults->GetTransferState(), EParadoxTransferEndpointState::Idle);
	TestEqual(TEXT("Phase starts None"), Defaults->GetTransferPhase(), EParadoxTransferPhase::None);
	TestEqual(TEXT("Transfer-Out remains Timed by default"), Defaults->TransferOutCompletionMode, EParadoxTransferPhaseCompletionMode::Timed);
	TestEqual(TEXT("Transfer-In remains Timed by default"), Defaults->TransferInCompletionMode, EParadoxTransferPhaseCompletionMode::Timed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPairedTransferCompletionModesTest,
	"Paradox.PairedTransferEndpoint.CompletionModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPairedTransferCompletionModesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UE::Paradox::PairedTransfer::Tests;

	{
		FPairFixture Fixture(TEXT("ParadoxPairedTransferTimedModeWorld"));
		if (!TestTrue(TEXT("Timed fixture is valid"), Fixture.IsValid()))
		{
			return false;
		}
		Fixture.Source->TransferOutDuration = 0.01f;
		Fixture.Destination->TransferInDuration = 0.01f;
		Fixture.StartPlayAndActivate();
		const FParadoxTransferOperationResult Request =
			Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester);
		TestTrue(TEXT("Timed transfer starts"), Request.IsSuccess());
		for (int32 TickIndex = 0; TickIndex < 4; ++TickIndex)
		{
			++GFrameCounter;
			Fixture.Scope.World->GetTimerManager().Tick(0.02f);
		}
		TestEqual(TEXT("Timed mode remains backward-compatible and auto-completes"),
			Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
	}

	{
		FPairFixture Fixture(TEXT("ParadoxPairedTransferExplicitModeWorld"));
		if (!TestTrue(TEXT("Explicit fixture is valid"), Fixture.IsValid()))
		{
			return false;
		}
		Fixture.Source->TransferOutCompletionMode = EParadoxTransferPhaseCompletionMode::Explicit;
		Fixture.Destination->TransferInCompletionMode = EParadoxTransferPhaseCompletionMode::Explicit;
		Fixture.Source->TransferOutDuration = 0.0f;
		Fixture.Destination->TransferInDuration = 0.0f;
		Fixture.StartPlayAndActivate();
		const FParadoxTransferOperationResult Request =
			Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester);
		TestTrue(TEXT("Explicit transfer starts"), Request.IsSuccess());
		Fixture.Scope.World->Tick(ELevelTick::LEVELTICK_All, 0.1f);
		Fixture.Scope.World->Tick(ELevelTick::LEVELTICK_All, 0.1f);
		TestEqual(TEXT("Explicit Transfer-Out never advances by timer"),
			Fixture.Source->GetTransferPhase(), EParadoxTransferPhase::TransferOut);
		TestFalse(TEXT("Wrong explicit Operation ID is ignored"), Fixture.Source->CompleteTransferOut(FGuid::NewGuid()));
		TestTrue(TEXT("Matching explicit Transfer-Out commits"), Fixture.Source->CompleteTransferOut(Request.OperationId));
		Fixture.Scope.World->Tick(ELevelTick::LEVELTICK_All, 0.1f);
		Fixture.Scope.World->Tick(ELevelTick::LEVELTICK_All, 0.1f);
		TestEqual(TEXT("Explicit Transfer-In never advances by timer"),
			Fixture.Destination->GetTransferPhase(), EParadoxTransferPhase::TransferIn);
		TestTrue(TEXT("Matching explicit Transfer-In finalizes"), Fixture.Destination->CompleteTransferIn(Request.OperationId));
		TestFalse(TEXT("Completed explicit callback is stale"), Fixture.Source->CompleteTransferIn(Request.OperationId));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPairedTransferUncontrolledReceiverTest,
	"Paradox.PairedTransferEndpoint.UncontrolledReceiverFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPairedTransferUncontrolledReceiverTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UE::Paradox::PairedTransfer::Tests;
	FPairFixture Fixture(TEXT("ParadoxPairedTransferUncontrolledWorld"));
	if (!TestTrue(TEXT("Fixture is valid"), Fixture.IsValid()))
	{
		return false;
	}
	Fixture.Scope.StartPlay();
	TestTrue(TEXT("Uncontrolled source Receiver starts active"),
		Fixture.Source->PuzzleReceiver->IsReceiverActive());
	TestTrue(TEXT("Uncontrolled destination Receiver starts active"),
		Fixture.Destination->PuzzleReceiver->IsReceiverActive());
	TestEqual(TEXT("Source has no registered Controller"),
		Fixture.Source->PuzzleReceiver->GetRegisteredControllerCount(), 0);
	TestEqual(TEXT("Destination has no registered Controller"),
		Fixture.Destination->PuzzleReceiver->GetRegisteredControllerCount(), 0);

	const FParadoxTransferOperationResult Request =
		Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester);
	TestEqual(TEXT("Uncontrolled pair can acquire a transfer"),
		Request.Status, EParadoxTransferOperationStatus::Succeeded);
	Fixture.Source->ResetTransferEndpoint();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPairedTransferSuccessfulLifecycleTest,
	"Paradox.PairedTransferEndpoint.SuccessfulLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPairedTransferSuccessfulLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UE::Paradox::PairedTransfer::Tests;
	FPairFixture Fixture(TEXT("ParadoxPairedTransferSuccessWorld"));
	if (!TestTrue(TEXT("Fixture is valid"), Fixture.IsValid()))
	{
		return false;
	}
	Fixture.Destination->TransferAnchor->SetWorldLocation(FVector(900.0f, 125.0f, 40.0f));
	Fixture.StartPlayAndActivate();

	const FParadoxTransferOperationResult Request = Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester);
	if (!TestEqual(TEXT("Request succeeds"), Request.Status, EParadoxTransferOperationStatus::Succeeded))
	{
		AddError(Request.DiagnosticMessage);
		return false;
	}
	TestTrue(TEXT("Operation ID is valid"), Request.OperationId.IsValid());
	TestEqual(TEXT("Source locks before async completion"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Sending);
	TestEqual(TEXT("Destination locks before async completion"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Receiving);
	TestEqual(TEXT("Pair shares operation ID"), Fixture.Destination->GetCurrentTransferOperationId(), Request.OperationId);
	TestEqual(TEXT("Pair shares subject"), Fixture.Destination->GetCurrentTransferSubject(), static_cast<AActor*>(Fixture.Subject));

	TestTrue(TEXT("Transfer-out completion commits"), Fixture.Source->CompleteTransferOutForTest(Request.OperationId));
	TestTrue(TEXT("Subject reaches destination anchor"), Fixture.Subject->GetActorTransform().Equals(Fixture.Destination->GetTransferAnchorTransform()));
	TestEqual(TEXT("Both endpoints enter TransferIn"), Fixture.Destination->GetTransferPhase(), EParadoxTransferPhase::TransferIn);
	TestTrue(TEXT("Destination may complete transfer-in"), Fixture.Destination->CompleteTransferInForTest(Request.OperationId));
	TestEqual(TEXT("Source returns Idle"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
	TestEqual(TEXT("Destination returns Idle"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Idle);
	TestFalse(TEXT("Operation identity is cleared"), Fixture.Source->GetCurrentTransferOperationId().IsValid());
	TestNull(TEXT("Subject reference is cleared"), Fixture.Destination->GetCurrentTransferSubject());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPairedTransferValidationAndBusyTest,
	"Paradox.PairedTransferEndpoint.ValidationAndAtomicBusy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPairedTransferValidationAndBusyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UE::Paradox::PairedTransfer::Tests;

	{
		FPairFixture Fixture(TEXT("ParadoxPairedTransferSourceInactiveWorld"));
		if (!TestTrue(TEXT("Source-inactive fixture is valid"), Fixture.IsValid()))
		{
			return false;
		}
		Fixture.StartPlayAndActivate(false, true);
		TestEqual(
			TEXT("Inactive source is rejected"),
			Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester).Status,
			EParadoxTransferOperationStatus::SourceInactive);
		TestEqual(TEXT("Destination remains Idle after rejection"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Idle);
	}

	{
		FPairFixture Fixture(TEXT("ParadoxPairedTransferDestinationInactiveWorld"));
		if (!TestTrue(TEXT("Destination-inactive fixture is valid"), Fixture.IsValid()))
		{
			return false;
		}
		Fixture.StartPlayAndActivate(true, false);
		TestEqual(
			TEXT("Inactive destination is rejected"),
			Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester).Status,
			EParadoxTransferOperationStatus::DestinationInactive);
		TestEqual(TEXT("Source remains Idle after rejection"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
	}

	{
		FPairFixture Fixture(TEXT("ParadoxPairedTransferBusyWorld"));
		if (!TestTrue(TEXT("Busy fixture is valid"), Fixture.IsValid()))
		{
			return false;
		}
		AParadoxPairedTransferTestEndpoint* OtherSource = Spawn<AParadoxPairedTransferTestEndpoint>(
			*Fixture.Scope.World,
			TEXT("OtherSource"),
			FVector(-500.0f, 0.0f, 0.0f));
		TestNotNull(TEXT("Other source exists"), OtherSource);
		Fixture.StartPlayAndActivate();
		OtherSource->PuzzleReceiver->SetControllerRequest(Fixture.Controller, true);

		const FParadoxTransferOperationResult First = Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester);
		TestEqual(TEXT("Initial request succeeds"), First.Status, EParadoxTransferOperationStatus::Succeeded);
		TestEqual(
			TEXT("Duplicate request is rejected while source is busy"),
			Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester).Status,
			EParadoxTransferOperationStatus::SourceBusy);
		TestEqual(TEXT("Duplicate request does not replace operation"), Fixture.Source->GetCurrentTransferOperationId(), First.OperationId);

		OtherSource->LinkedEndpoint = Fixture.Destination;
		Fixture.Destination->LinkedEndpoint = OtherSource;
		TestEqual(
			TEXT("A busy destination rejects another source"),
			OtherSource->RequestTransfer(Fixture.Subject, Fixture.Requester).Status,
			EParadoxTransferOperationStatus::DestinationBusy);
		Fixture.Destination->LinkedEndpoint = Fixture.Source;
		Fixture.Source->ResetTransferEndpoint();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPairedTransferCancellationSafetyTest,
	"Paradox.PairedTransferEndpoint.CancellationAndDestructionSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPairedTransferCancellationSafetyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UE::Paradox::PairedTransfer::Tests;

	{
		FPairFixture Fixture(TEXT("ParadoxPairedTransferCancellationWorld"));
		if (!TestTrue(TEXT("Cancellation fixture is valid"), Fixture.IsValid()))
		{
			return false;
		}
		Fixture.StartPlayAndActivate();
		const FParadoxTransferOperationResult Request = Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester);
		TestEqual(TEXT("Cancellation setup succeeds"), Request.Status, EParadoxTransferOperationStatus::Succeeded);
		TestTrue(TEXT("Reset releases the active transaction"), Fixture.Source->ResetTransferEndpoint());
		TestEqual(TEXT("Source is Idle after reset"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
		TestEqual(TEXT("Destination is Idle after reset"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Idle);
		TestFalse(TEXT("Stale transfer-out callback is ignored"), Fixture.Source->CompleteTransferOutForTest(Request.OperationId));
		TestFalse(TEXT("Stale callback does not move subject"), Fixture.Subject->GetActorTransform().Equals(Fixture.Destination->GetTransferAnchorTransform()));
	}

	{
		FPairFixture Fixture(TEXT("ParadoxPairedTransferDestructionWorld"));
		if (!TestTrue(TEXT("Destruction fixture is valid"), Fixture.IsValid()))
		{
			return false;
		}
		Fixture.StartPlayAndActivate();
		const FParadoxTransferOperationResult Request = Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester);
		TestEqual(TEXT("Destruction setup succeeds"), Request.Status, EParadoxTransferOperationStatus::Succeeded);
		TestTrue(TEXT("Destination destruction succeeds"), Fixture.Scope.World->DestroyActor(Fixture.Destination));
		TestEqual(TEXT("Surviving source is released"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
		TestFalse(TEXT("Destroyed-pair callback is ignored"), Fixture.Source->CompleteTransferOutForTest(Request.OperationId));
	}

	{
		FPairFixture Fixture(TEXT("ParadoxPairedTransferWorldStateWorld"));
		if (!TestTrue(TEXT("World-State fixture is valid"), Fixture.IsValid()))
		{
			return false;
		}
		Fixture.StartPlayAndActivate();
		const FParadoxTransferOperationResult Request = Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester);
		TestEqual(TEXT("World-State setup succeeds"), Request.Status, EParadoxTransferOperationStatus::Succeeded);
		UWorldStateSubsystem* WorldState = Fixture.Scope.World->GetSubsystem<UWorldStateSubsystem>();
		if (!TestNotNull(TEXT("World State subsystem exists"), WorldState))
		{
			return false;
		}
		FWorldStateRestoreLifecycleContext RestoreContext;
		RestoreContext.Stage = EWorldStateRestoreStage::Preflight;
		WorldState->OnRestoreStartedNative().Broadcast(RestoreContext);
		TestEqual(TEXT("Restore releases source"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
		TestEqual(TEXT("Restore releases destination"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Idle);
		TestEqual(
			TEXT("Restore blocks new requests"),
			Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester).Status,
			EParadoxTransferOperationStatus::ResetInProgress);
		const FWorldStateRestoreResult RestoreResult;
		WorldState->OnRestoreCompletedNative().Broadcast(RestoreResult);
		const FParadoxTransferOperationResult PostRestore = Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester);
		TestEqual(TEXT("Restore terminal event re-enables requests"), PostRestore.Status, EParadoxTransferOperationStatus::Succeeded);
		Fixture.Source->ResetTransferEndpoint();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPairedTransferReceiverDeactivationTest,
	"Paradox.PairedTransferEndpoint.ReceiverDeactivationPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPairedTransferReceiverDeactivationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UE::Paradox::PairedTransfer::Tests;
	FPairFixture Fixture(TEXT("ParadoxPairedTransferReceiverPolicyWorld"));
	if (!TestTrue(TEXT("Receiver-policy fixture is valid"), Fixture.IsValid()))
	{
		return false;
	}
	Fixture.StartPlayAndActivate();
	const FParadoxTransferOperationResult Request = Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester);
	TestEqual(TEXT("Receiver-policy setup succeeds"), Request.Status, EParadoxTransferOperationStatus::Succeeded);
	TestTrue(TEXT("Destination Receiver deactivates"), Fixture.Destination->PuzzleReceiver->SetControllerRequest(Fixture.Controller, false));
	TestTrue(TEXT("Current transfer-out still completes"), Fixture.Source->CompleteTransferOutForTest(Request.OperationId));
	TestTrue(TEXT("Current transfer-in still completes"), Fixture.Source->CompleteTransferInForTest(Request.OperationId));
	TestEqual(TEXT("Completed source is Idle"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
	TestEqual(
		TEXT("New operation observes inactive destination"),
		Fixture.Source->RequestTransfer(Fixture.Subject, Fixture.Requester).Status,
		EParadoxTransferOperationStatus::DestinationInactive);
	return true;
}

#endif
