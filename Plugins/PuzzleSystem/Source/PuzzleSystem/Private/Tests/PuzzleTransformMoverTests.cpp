#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Activators/PuzzleTransformMover.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Controllers/PuzzleController.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "PuzzleTransformMoverTestTypes.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "UObject/UnrealType.h"

namespace PuzzleTransformMoverTest
{
	/** Owns a minimal transient world with registered Actor components. */
	struct FScopedWorld
	{
		FScopedWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("PuzzleTransformMoverTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName);
			if (World)
			{
				World->AddToRoot();
			}
		}

		~FScopedWorld()
		{
			if (World)
			{
				World->RemoveFromRoot();
				World->DestroyWorld(false);
			}
		}

		UWorld* World = nullptr;
	};

	static APuzzleTransformMoverTestActor* SpawnMover(FScopedWorld& ScopedWorld)
	{
		return ScopedWorld.World ? ScopedWorld.World->SpawnActor<APuzzleTransformMoverTestActor>() : nullptr;
	}

	static APuzzleController* SpawnRequestSource(FScopedWorld& ScopedWorld)
	{
		return ScopedWorld.World ? ScopedWorld.World->SpawnActor<APuzzleController>() : nullptr;
	}

	static void BindObserver(APuzzleTransformMover* Mover, UPuzzleTransformMoverTestObserver* Observer)
	{
		Mover->OnMovementStarted.AddDynamic(Observer, &UPuzzleTransformMoverTestObserver::HandleMovementStarted);
		Mover->OnMovementResumed.AddDynamic(Observer, &UPuzzleTransformMoverTestObserver::HandleMovementResumed);
		Mover->OnMovementReversed.AddDynamic(Observer, &UPuzzleTransformMoverTestObserver::HandleMovementReversed);
		Mover->OnMovementPaused.AddDynamic(Observer, &UPuzzleTransformMoverTestObserver::HandleMovementPaused);
		Mover->OnReachedStart.AddDynamic(Observer, &UPuzzleTransformMoverTestObserver::HandleReachedStart);
		Mover->OnReachedEnd.AddDynamic(Observer, &UPuzzleTransformMoverTestObserver::HandleReachedEnd);
		Mover->OnMovedComponentChanged.AddDynamic(Observer, &UPuzzleTransformMoverTestObserver::HandleMovedComponentChanged);
		Mover->OnMoverReset.AddDynamic(Observer, &UPuzzleTransformMoverTestObserver::HandleMoverReset);
	}

	static bool SetReceiverActive(APuzzleTransformMover* Mover, APuzzleController* Source, bool bActive)
	{
		return Mover && Mover->PuzzleReceiver
			? Mover->PuzzleReceiver->SetControllerRequest(Source, bActive)
			: false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleTransformMoverCompositionAndStopTest,
	"PuzzleSystem.TransformMover.CompositionAndStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleTransformMoverCompositionAndStopTest::RunTest(const FString& Parameters)
{
	PuzzleTransformMoverTest::FScopedWorld ScopedWorld;
	if (!TestNotNull(TEXT("Transform-mover test world exists"), ScopedWorld.World))
	{
		return false;
	}

	TestTrue(TEXT("Native Puzzle Transform Mover is abstract"), APuzzleTransformMover::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
	TestFalse(TEXT("Concrete test child is usable"), APuzzleTransformMoverTestActor::StaticClass()->HasAnyClassFlags(CLASS_Abstract));

	APuzzleTransformMoverTestActor* Mover = PuzzleTransformMoverTest::SpawnMover(ScopedWorld);
	APuzzleController* Source = PuzzleTransformMoverTest::SpawnRequestSource(ScopedWorld);
	if (!TestNotNull(TEXT("Transform mover exists"), Mover) || !TestNotNull(TEXT("Receiver request source exists"), Source))
	{
		return false;
	}

	TestTrue(TEXT("Billboard is the Actor root"), Mover->GetRootComponent() == Mover->BillboardRoot);
	TestTrue(TEXT("Start marker is attached to the billboard"), Mover->StartArrow->GetAttachParent() == Mover->BillboardRoot);
	TestTrue(TEXT("End marker is attached to the billboard"), Mover->EndArrow->GetAttachParent() == Mover->BillboardRoot);
	TestTrue(TEXT("Start marker is green"), Mover->StartArrow->ArrowColor == FColor::Green);
	TestTrue(TEXT("End marker is red"), Mover->EndArrow->ArrowColor == FColor::Red);
	TestFalse(TEXT("Start marker collision is disabled"), Mover->StartArrow->IsCollisionEnabled());
	TestFalse(TEXT("End marker collision is disabled"), Mover->EndArrow->IsCollisionEnabled());

	const FName ComponentPropertyNames[] = {
		GET_MEMBER_NAME_CHECKED(APuzzleTransformMover, BillboardRoot),
		GET_MEMBER_NAME_CHECKED(APuzzleTransformMover, StartArrow),
		GET_MEMBER_NAME_CHECKED(APuzzleTransformMover, EndArrow),
		GET_MEMBER_NAME_CHECKED(APuzzleTransformMover, PuzzleReceiver)
	};
	for (const FName PropertyName : ComponentPropertyNames)
	{
		const FProperty* Property = FindFProperty<FProperty>(APuzzleTransformMover::StaticClass(), PropertyName);
		TestNotNull(*FString::Printf(TEXT("%s component property exists"), *PropertyName.ToString()), Property);
		if (Property)
		{
			TestTrue(
				*FString::Printf(TEXT("%s is disabled on placed instances (VisibleDefaultsOnly)"), *PropertyName.ToString()),
				Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
		}
	}

	Mover->MovementMode = EPuzzleTransformMoverMode::PingPong;
	Mover->DeactivationBehavior = EPuzzleTransformMoverDeactivationBehavior::Stop;
	Mover->TimingMode = EPuzzleTransformMoverTimingMode::MovementTime;
	Mover->ForwardMovementTime = 1.0f;
	TestTrue(TEXT("Mover initializes with its configured child component"), Mover->InitializeForTest());
	TestTrue(TEXT("Mover starts at Start"), Mover->IsAtStart());
	TestEqual(TEXT("Initial movement alpha is zero"), Mover->GetMovementAlpha(), 0.0f);
	TestTrue(TEXT("Moved component resolves from FComponentReference"), Mover->GetMovedComponent() == Mover->TestMovedComponent);
	TestFalse(TEXT("Idle mover Tick is disabled"), Mover->IsActorTickEnabled());

	UPuzzleTransformMoverTestObserver* Observer = NewObject<UPuzzleTransformMoverTestObserver>();
	PuzzleTransformMoverTest::BindObserver(Mover, Observer);

	TestTrue(TEXT("Receiver activation changes effective state"), PuzzleTransformMoverTest::SetReceiverActive(Mover, Source, true));
	TestEqual(TEXT("PingPong activation requests End"), Mover->GetMoverState(), EPuzzleTransformMoverState::MovingTowardEnd);
	TestTrue(TEXT("Active interpolation enables Tick"), Mover->IsActorTickEnabled());
	Mover->Tick(0.25f);
	TestEqual(TEXT("MovementTime advances linear alpha proportionally"), Mover->GetMovementAlpha(), 0.25f);

	TestTrue(TEXT("Receiver deactivation changes effective state"), PuzzleTransformMoverTest::SetReceiverActive(Mover, Source, false));
	TestTrue(TEXT("Stop pauses the traversal"), Mover->IsMovementPaused());
	TestEqual(TEXT("Stop preserves direction"), Mover->GetMoverState(), EPuzzleTransformMoverState::MovingTowardEnd);
	TestFalse(TEXT("Paused traversal disables Tick"), Mover->IsActorTickEnabled());
	Mover->Tick(0.25f);
	TestEqual(TEXT("Paused traversal preserves alpha"), Mover->GetMovementAlpha(), 0.25f);

	PuzzleTransformMoverTest::SetReceiverActive(Mover, Source, true);
	TestFalse(TEXT("Activation resumes Stop-paused movement"), Mover->IsMovementPaused());
	TestEqual(TEXT("Resume keeps the preserved direction"), Mover->GetMoverState(), EPuzzleTransformMoverState::MovingTowardEnd);
	Mover->Tick(0.75f);
	TestTrue(TEXT("Traversal reaches End exactly"), Mover->IsAtEnd());
	TestEqual(TEXT("End alpha is exact"), Mover->GetMovementAlpha(), 1.0f);
	TestFalse(TEXT("Endpoint disables Tick"), Mover->IsActorTickEnabled());

	PuzzleTransformMoverTest::SetReceiverActive(Mover, Source, false);
	TestEqual(TEXT("Stable PingPong deactivation requests Start"), Mover->GetMoverState(), EPuzzleTransformMoverState::MovingTowardStart);
	Mover->Tick(1.0f);
	TestTrue(TEXT("Return traversal reaches Start"), Mover->IsAtStart());
	TestEqual(TEXT("Movement started once per stable-endpoint traversal"), Observer->MovementStartedCount, 2);
	TestEqual(TEXT("Stop emits one pause"), Observer->MovementPausedCount, 1);
	TestEqual(TEXT("Stop reactivation emits one resume"), Observer->MovementResumedCount, 1);
	TestEqual(TEXT("End arrival emits once"), Observer->ReachedEndCount, 1);
	TestEqual(TEXT("Start arrival emits once"), Observer->ReachedStartCount, 1);
	TestTrue(TEXT("BlueprintNativeEvent hooks precede matching delegates"), Observer->bHooksPrecededDelegates);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleTransformMoverPoliciesTest,
	"PuzzleSystem.TransformMover.Policies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleTransformMoverPoliciesTest::RunTest(const FString& Parameters)
{
	PuzzleTransformMoverTest::FScopedWorld ScopedWorld;
	if (!TestNotNull(TEXT("Policy test world exists"), ScopedWorld.World))
	{
		return false;
	}

	APuzzleController* LatchSource = PuzzleTransformMoverTest::SpawnRequestSource(ScopedWorld);
	APuzzleTransformMoverTestActor* LatchMover = PuzzleTransformMoverTest::SpawnMover(ScopedWorld);
	if (!TestNotNull(TEXT("Latch mover exists"), LatchMover) || !TestNotNull(TEXT("Latch source exists"), LatchSource))
	{
		return false;
	}
	LatchMover->MovementMode = EPuzzleTransformMoverMode::Latch;
	LatchMover->DeactivationBehavior = EPuzzleTransformMoverDeactivationBehavior::Return;
	LatchMover->ForwardMovementTime = 1.0f;
	TestTrue(TEXT("Latch mover initializes"), LatchMover->InitializeForTest());

	PuzzleTransformMoverTest::SetReceiverActive(LatchMover, LatchSource, true);
	LatchMover->Tick(0.4f);
	PuzzleTransformMoverTest::SetReceiverActive(LatchMover, LatchSource, false);
	TestEqual(TEXT("Return reverses an incomplete Latch traversal"), LatchMover->GetMoverState(), EPuzzleTransformMoverState::MovingTowardStart);
	TestEqual(TEXT("Return preserves progress"), LatchMover->GetMovementAlpha(), 0.4f);
	TestFalse(TEXT("Latch is not consumed before End"), LatchMover->IsLatchCompleted());
	LatchMover->Tick(0.4f);
	TestTrue(TEXT("Interrupted Latch returns to Start"), LatchMover->IsAtStart());

	PuzzleTransformMoverTest::SetReceiverActive(LatchMover, LatchSource, true);
	LatchMover->Tick(1.0f);
	TestTrue(TEXT("Latch completes only at End"), LatchMover->IsLatchCompleted());
	PuzzleTransformMoverTest::SetReceiverActive(LatchMover, LatchSource, false);
	PuzzleTransformMoverTest::SetReceiverActive(LatchMover, LatchSource, true);
	TestTrue(TEXT("Completed Latch ignores later activation"), LatchMover->IsAtEnd());
	LatchMover->ResetMover();
	TestTrue(TEXT("Latch reset restores Start"), LatchMover->IsAtStart());
	TestFalse(TEXT("Latch reset clears completion"), LatchMover->IsLatchCompleted());

	APuzzleController* ContinueSource = PuzzleTransformMoverTest::SpawnRequestSource(ScopedWorld);
	APuzzleTransformMoverTestActor* ContinueMover = PuzzleTransformMoverTest::SpawnMover(ScopedWorld);
	ContinueMover->MovementMode = EPuzzleTransformMoverMode::PingPong;
	ContinueMover->DeactivationBehavior = EPuzzleTransformMoverDeactivationBehavior::Continue;
	ContinueMover->ForwardMovementTime = 1.0f;
	TestTrue(TEXT("Continue mover initializes"), ContinueMover->InitializeForTest());
	PuzzleTransformMoverTest::SetReceiverActive(ContinueMover, ContinueSource, true);
	ContinueMover->Tick(0.25f);
	PuzzleTransformMoverTest::SetReceiverActive(ContinueMover, ContinueSource, false);
	TestEqual(TEXT("Continue preserves current direction"), ContinueMover->GetMoverState(), EPuzzleTransformMoverState::MovingTowardEnd);
	ContinueMover->Tick(0.75f);
	TestTrue(TEXT("Continue finishes at End"), ContinueMover->IsAtEnd());
	TestFalse(TEXT("Continue does not queue automatic PingPong return"), ContinueMover->IsActorTickEnabled());

	APuzzleController* FlipSource = PuzzleTransformMoverTest::SpawnRequestSource(ScopedWorld);
	APuzzleTransformMoverTestActor* FlipMover = PuzzleTransformMoverTest::SpawnMover(ScopedWorld);
	FlipMover->MovementMode = EPuzzleTransformMoverMode::FlipFlop;
	FlipMover->DeactivationBehavior = EPuzzleTransformMoverDeactivationBehavior::Return;
	FlipMover->ForwardMovementTime = 1.0f;
	TestTrue(TEXT("FlipFlop mover initializes"), FlipMover->InitializeForTest());
	PuzzleTransformMoverTest::SetReceiverActive(FlipMover, FlipSource, true);
	FlipMover->Tick(0.4f);
	PuzzleTransformMoverTest::SetReceiverActive(FlipMover, FlipSource, false);
	TestEqual(TEXT("FlipFlop Return reverses toward Start"), FlipMover->GetMoverState(), EPuzzleTransformMoverState::MovingTowardStart);
	PuzzleTransformMoverTest::SetReceiverActive(FlipMover, FlipSource, true);
	TestEqual(TEXT("FlipFlop activation while returning selects End"), FlipMover->GetMoverState(), EPuzzleTransformMoverState::MovingTowardEnd);
	TestEqual(TEXT("FlipFlop reversal does not snap progress"), FlipMover->GetMovementAlpha(), 0.4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleTransformMoverTimingAndReplacementTest,
	"PuzzleSystem.TransformMover.TimingEasingReplacementAndInitialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleTransformMoverTimingAndReplacementTest::RunTest(const FString& Parameters)
{
	PuzzleTransformMoverTest::FScopedWorld ScopedWorld;
	if (!TestNotNull(TEXT("Timing test world exists"), ScopedWorld.World))
	{
		return false;
	}

	APuzzleController* Source = PuzzleTransformMoverTest::SpawnRequestSource(ScopedWorld);
	APuzzleTransformMoverTestActor* Mover = PuzzleTransformMoverTest::SpawnMover(ScopedWorld);
	Mover->TimingMode = EPuzzleTransformMoverTimingMode::MovementTime;
	Mover->ForwardMovementTime = 2.0f;
	Mover->InterpolationSource = EPuzzleTransformMoverInterpolationSource::BuiltInEasing;
	Mover->BuiltInEasingType = EEasingFunc::EaseIn;
	Mover->EasingExponent = 2.0f;
	TestTrue(TEXT("Eased mover initializes"), Mover->InitializeForTest());
	PuzzleTransformMoverTest::SetReceiverActive(Mover, Source, true);
	Mover->Tick(1.0f);
	TestEqual(TEXT("MovementTime keeps linear progress authoritative"), Mover->GetMovementAlpha(), 0.5f);
	TestEqual(TEXT("Unreal EaseIn derives visible alpha"), Mover->GetEasedAlpha(), 0.25f);
	TestTrue(TEXT("Visible component follows eased path"), FMath::IsNearlyEqual(Mover->TestMovedComponent->GetComponentLocation().X, 50.0f, 0.01f));

	UPuzzleTransformMoverTestObserver* Observer = NewObject<UPuzzleTransformMoverTestObserver>();
	PuzzleTransformMoverTest::BindObserver(Mover, Observer);
	TestTrue(TEXT("Runtime replacement succeeds"), Mover->SetMovedComponent(Mover->ReplacementComponent));
	TestTrue(TEXT("Replacement becomes authoritative"), Mover->GetMovedComponent() == Mover->ReplacementComponent);
	TestTrue(TEXT("Replacement synchronizes to eased progress"), FMath::IsNearlyEqual(Mover->ReplacementComponent->GetComponentLocation().X, 50.0f, 0.01f));
	TestEqual(TEXT("Replacement emits one change event"), Observer->MovedComponentChangedCount, 1);

	Mover->bUseSeparateReturnTiming = true;
	Mover->ReturnMovementTime = 4.0f;
	TestTrue(TEXT("Protected target request can reverse movement"), Mover->RequestStartForTest());
	float RemainingTime = 0.0f;
	TestTrue(TEXT("Remaining time query succeeds"), Mover->GetRemainingMovementTime(RemainingTime));
	TestEqual(TEXT("Partial return time uses current alpha and ReturnMovementTime"), RemainingTime, 2.0f);
	Mover->ResetMover();
	TestTrue(TEXT("Reset returns to configured initial endpoint"), Mover->IsAtStart());
	TestEqual(TEXT("Reset synchronizes the replacement component"), Mover->ReplacementComponent->GetComponentLocation(), Mover->GetStartTransform().GetLocation());
	TestEqual(TEXT("Reset emits once"), Observer->MoverResetCount, 1);
	TestTrue(TEXT("Replacement/reset hooks precede delegates"), Observer->bHooksPrecededDelegates);

	APuzzleController* SpeedSource = PuzzleTransformMoverTest::SpawnRequestSource(ScopedWorld);
	APuzzleTransformMoverTestActor* SpeedMover = PuzzleTransformMoverTest::SpawnMover(ScopedWorld);
	SpeedMover->TimingMode = EPuzzleTransformMoverTimingMode::Speed;
	SpeedMover->ForwardSpeed = 100.0f;
	TestTrue(TEXT("Speed mover initializes"), SpeedMover->InitializeForTest());
	PuzzleTransformMoverTest::SetReceiverActive(SpeedMover, SpeedSource, true);
	SpeedMover->Tick(1.0f);
	TestEqual(TEXT("100 units/second covers half a 200-unit path in one second"), SpeedMover->GetMovementAlpha(), 0.5f);

	APuzzleController* CurveSource = PuzzleTransformMoverTest::SpawnRequestSource(ScopedWorld);
	APuzzleTransformMoverTestActor* CurveMover = PuzzleTransformMoverTest::SpawnMover(ScopedWorld);
	CurveMover->InterpolationSource = EPuzzleTransformMoverInterpolationSource::CustomCurve;
	CurveMover->MovementCurve = NewObject<UCurveFloat>(CurveMover);
	const FKeyHandle StartKey = CurveMover->MovementCurve->FloatCurve.AddKey(0.0f, 0.0f);
	const FKeyHandle MidKey = CurveMover->MovementCurve->FloatCurve.AddKey(0.5f, 0.1f);
	const FKeyHandle EndKey = CurveMover->MovementCurve->FloatCurve.AddKey(1.0f, 1.0f);
	CurveMover->MovementCurve->FloatCurve.SetKeyInterpMode(StartKey, RCIM_Linear);
	CurveMover->MovementCurve->FloatCurve.SetKeyInterpMode(MidKey, RCIM_Linear);
	CurveMover->MovementCurve->FloatCurve.SetKeyInterpMode(EndKey, RCIM_Linear);
	TestTrue(TEXT("Custom-curve mover initializes"), CurveMover->InitializeForTest());
	PuzzleTransformMoverTest::SetReceiverActive(CurveMover, CurveSource, true);
	CurveMover->Tick(0.5f);
	TestEqual(TEXT("Custom curve does not replace linear MovementAlpha"), CurveMover->GetMovementAlpha(), 0.5f);
	TestEqual(TEXT("Custom curve controls visible interpolation"), CurveMover->GetEasedAlpha(), 0.1f);

	APuzzleController* InitialSource = PuzzleTransformMoverTest::SpawnRequestSource(ScopedWorld);
	APuzzleTransformMoverTestActor* InitiallyActiveMover = PuzzleTransformMoverTest::SpawnMover(ScopedWorld);
	InitiallyActiveMover->MovementMode = EPuzzleTransformMoverMode::PingPong;
	InitiallyActiveMover->bAnimateInitialReceiverState = false;
	PuzzleTransformMoverTest::SetReceiverActive(InitiallyActiveMover, InitialSource, true);
	TestTrue(TEXT("Already-active Receiver initializes mover"), InitiallyActiveMover->InitializeForTest());
	TestTrue(TEXT("Non-animated initial synchronization snaps to End"), InitiallyActiveMover->IsAtEnd());
	TestEqual(TEXT("Initial snap emits no movement-start hook"), InitiallyActiveMover->MovementStartedHookCount, 0);
	TestEqual(TEXT("Initial snap emits no endpoint-arrival hook"), InitiallyActiveMover->ReachedEndHookCount, 0);
	return true;
}

#endif
