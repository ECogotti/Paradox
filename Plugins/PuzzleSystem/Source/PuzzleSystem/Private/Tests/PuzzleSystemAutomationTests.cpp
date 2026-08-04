#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Conditions/PuzzleAllCondition.h"
#include "Conditions/PuzzleAnyCondition.h"
#include "Conditions/PuzzleInputStateCondition.h"
#include "Conditions/PuzzleNotCondition.h"
#include "Conditions/PuzzleThresholdCondition.h"
#include "Components/BillboardComponent.h"
#include "Controllers/PuzzleController.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Emitters/PuzzleSwitch.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "NativeGameplayTags.h"
#include "PuzzleSystemTestTypes.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "TimerManager.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PuzzleTestPressed, "Puzzle.Test.Pressed");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PuzzleTestPowered, "Puzzle.Test.Powered");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PuzzleTestCompleted, "Puzzle.Test.Completed");

namespace PuzzleSystemTest
{
	/**
	 * Creates a transient actor for automation tests.
	 *
	 * @param Name Base name used to create a unique transient object name.
	 * @return Newly allocated transient actor.
	 */
	static AActor* NewActor(FName Name)
	{
		return NewObject<AActor>(GetTransientPackage(), AActor::StaticClass(), MakeUniqueObjectName(GetTransientPackage(), AActor::StaticClass(), Name));
	}

	/**
	 * Adds an unregistered component instance to a transient test actor.
	 *
	 * @param Owner Actor that owns the new component.
	 * @param Name Object name assigned to the component.
	 * @return Newly allocated component.
	 */
	template <typename ComponentType>
	ComponentType* AddComponent(AActor* Owner, FName Name)
	{
		ComponentType* Component = NewObject<ComponentType>(Owner, ComponentType::StaticClass(), Name);
		Owner->AddInstanceComponent(Component);
		return Component;
	}

	/**
	 * Creates a transient puzzle controller for automation tests.
	 *
	 * @param Name Base name used to create a unique transient object name.
	 * @return Newly allocated controller.
	 */
	static APuzzleController* NewController(FName Name)
	{
		return NewObject<APuzzleController>(GetTransientPackage(), APuzzleController::StaticClass(), MakeUniqueObjectName(GetTransientPackage(), APuzzleController::StaticClass(), Name));
	}

	/**
	 * Creates an input-state condition owned by the supplied outer.
	 *
	 * @param Outer UObject that owns the instanced condition.
	 * @param InputId Local controller input ID queried by the condition.
	 * @param bExpectedActive Required active state.
	 * @return Newly allocated input-state condition.
	 */
	static UPuzzleInputStateCondition* NewInputCondition(UObject* Outer, FName InputId, bool bExpectedActive = true)
	{
		UPuzzleInputStateCondition* Condition = NewObject<UPuzzleInputStateCondition>(Outer);
		Condition->InputId = InputId;
		Condition->bExpectedActive = bExpectedActive;
		return Condition;
	}

	/**
	 * Appends one input binding to a controller.
	 *
	 * @param Controller Controller receiving the binding.
	 * @param InputId Local input ID for conditions.
	 * @param EmitterActor Actor owning the emitter component.
	 * @param ComponentName Optional emitter component name; a non-empty name enables explicit selection.
	 * @param SignalTag Gameplay tag channel observed on the emitter.
	 */
	static void BindInput(APuzzleController* Controller, FName InputId, AActor* EmitterActor, FName ComponentName, FGameplayTag SignalTag)
	{
		FPuzzleInputBinding& Binding = Controller->InputBindings.AddDefaulted_GetRef();
		Binding.InputId = InputId;
		Binding.EmitterActor = EmitterActor;
		Binding.bSpecifyEmitterComponent = !ComponentName.IsNone();
		Binding.EmitterComponentName = ComponentName;
		Binding.SignalTag = SignalTag;
	}

	/**
	 * Appends one receiver binding to a controller.
	 *
	 * @param Controller Controller receiving the binding.
	 * @param ReceiverActor Actor owning the receiver component.
	 * @param ComponentName Optional receiver component name; a non-empty name enables explicit selection.
	 */
	static void BindReceiver(APuzzleController* Controller, AActor* ReceiverActor, FName ComponentName = NAME_None)
	{
		FPuzzleReceiverBinding& Binding = Controller->ReceiverBindings.AddDefaulted_GetRef();
		Binding.ReceiverActor = ReceiverActor;
		Binding.bSpecifyReceiverComponent = !ComponentName.IsNone();
		Binding.ReceiverComponentName = ComponentName;
	}

	/**
	 * Builds the common one-input, one-receiver controller fixture.
	 *
	 * @param EmitterActor Actor owning the emitter component.
	 * @param ReceiverActor Actor owning the receiver component.
	 * @param InputId Local input ID used by the generated condition.
	 * @return Newly configured transient controller.
	 */
	static APuzzleController* BuildSingleInputController(AActor* EmitterActor, AActor* ReceiverActor, FName InputId = FName(TEXT("Pressed")))
	{
		APuzzleController* Controller = NewController(TEXT("Controller"));
		BindInput(Controller, InputId, EmitterActor, NAME_None, TAG_PuzzleTestPressed);
		BindReceiver(Controller, ReceiverActor);
		Controller->RootCondition = NewInputCondition(Controller, InputId);
		return Controller;
	}

	/** Owns a transient world whose timer manager can exercise switch delays deterministically. */
	struct FScopedSwitchWorld
	{
		FScopedSwitchWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("PuzzleSwitchTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName);
			if (World)
			{
				World->AddToRoot();
				++GFrameCounter;
				World->GetTimerManager().Tick(0.0f);
			}
		}

		~FScopedSwitchWorld()
		{
			if (World)
			{
				World->RemoveFromRoot();
				World->DestroyWorld(false);
			}
		}

		/** Advances only timer-owned behavior; puzzle state itself remains event-driven. */
		void Advance(float DeltaSeconds) const
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(DeltaSeconds);
		}

		UWorld* World = nullptr;
	};

	/** Creates and initializes one concrete test child of the abstract switch template. */
	static APuzzleSwitchTestActor* NewSwitch(
		FScopedSwitchWorld& TestWorld,
		EPuzzleSwitchMode Mode,
		bool bStartActive = false,
		float PressDelay = 0.0f,
		float ReleaseDelay = 0.0f,
		float PulseDuration = 1.0f,
		EPuzzlePulseRetriggerMode RetriggerMode = EPuzzlePulseRetriggerMode::Ignore,
		EPuzzleSwitchInitialInputState InitialInputState = EPuzzleSwitchInitialInputState::Released)
	{
		APuzzleSwitchTestActor* PuzzleSwitch = TestWorld.World
			? TestWorld.World->SpawnActor<APuzzleSwitchTestActor>()
			: nullptr;
		if (!PuzzleSwitch)
		{
			return nullptr;
		}

		PuzzleSwitch->SwitchMode = Mode;
		PuzzleSwitch->InitialInputState = InitialInputState;
		PuzzleSwitch->OutputSignalTag = TAG_PuzzleTestPressed;
		PuzzleSwitch->bStartActive = bStartActive;
		PuzzleSwitch->PressDelay = PressDelay;
		PuzzleSwitch->ReleaseDelay = ReleaseDelay;
		PuzzleSwitch->PulseDuration = PulseDuration;
		PuzzleSwitch->PulseRetriggerMode = RetriggerMode;
		PuzzleSwitch->InitializeForTest();
		return PuzzleSwitch;
	}

	/** Binds every presentation delegate to a counter-based test observer. */
	static void BindSwitchObserver(APuzzleSwitch* PuzzleSwitch, UPuzzleSwitchTestObserver* Observer)
	{
		PuzzleSwitch->OnInputPressed.AddDynamic(Observer, &UPuzzleSwitchTestObserver::HandlePressed);
		PuzzleSwitch->OnInputReleased.AddDynamic(Observer, &UPuzzleSwitchTestObserver::HandleReleased);
		PuzzleSwitch->OnPressDelayStarted.AddDynamic(Observer, &UPuzzleSwitchTestObserver::HandlePressDelayStarted);
		PuzzleSwitch->OnPressDelayCancelled.AddDynamic(Observer, &UPuzzleSwitchTestObserver::HandlePressDelayCancelled);
		PuzzleSwitch->OnPressDelayCompleted.AddDynamic(Observer, &UPuzzleSwitchTestObserver::HandlePressDelayCompleted);
		PuzzleSwitch->OnReleaseDelayStarted.AddDynamic(Observer, &UPuzzleSwitchTestObserver::HandleReleaseDelayStarted);
		PuzzleSwitch->OnReleaseDelayCancelled.AddDynamic(Observer, &UPuzzleSwitchTestObserver::HandleReleaseDelayCancelled);
		PuzzleSwitch->OnReleaseDelayCompleted.AddDynamic(Observer, &UPuzzleSwitchTestObserver::HandleReleaseDelayCompleted);
		PuzzleSwitch->OnSwitchActivated.AddDynamic(Observer, &UPuzzleSwitchTestObserver::HandleActivated);
		PuzzleSwitch->OnSwitchDeactivated.AddDynamic(Observer, &UPuzzleSwitchTestObserver::HandleDeactivated);
		PuzzleSwitch->OnSwitchReset.AddDynamic(Observer, &UPuzzleSwitchTestObserver::HandleReset);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuzzleSystemSingleInputTest, "PuzzleSystem.Core.SingleInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleSystemSingleInputTest::RunTest(const FString& Parameters)
{
	AActor* EmitterActor = PuzzleSystemTest::NewActor(TEXT("EmitterActor"));
	UPuzzleEmitterComponent* Emitter = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(EmitterActor, TEXT("Emitter"));
	AActor* ReceiverActor = PuzzleSystemTest::NewActor(TEXT("ReceiverActor"));
	UPuzzleReceiverComponent* Receiver = PuzzleSystemTest::AddComponent<UPuzzleReceiverComponent>(ReceiverActor, TEXT("Receiver"));

	APuzzleController* Controller = PuzzleSystemTest::BuildSingleInputController(EmitterActor, ReceiverActor);

	TestTrue(TEXT("Controller uses a billboard root component"), Controller->GetRootComponent()->IsA<UBillboardComponent>());
	UTexture2D* ExpectedControllerIcon = LoadObject<UTexture2D>(nullptr, TEXT("/PuzzleSystem/Textures/T_PuzzleControllerIcon.T_PuzzleControllerIcon"));
	UBillboardComponent* ControllerBillboard = Cast<UBillboardComponent>(Controller->GetRootComponent());
	TestNotNull(TEXT("Controller icon asset loads"), ExpectedControllerIcon);
	TestNotNull(TEXT("Controller billboard is available"), ControllerBillboard);
	if (ExpectedControllerIcon && ControllerBillboard)
	{
		TestEqual(TEXT("Controller billboard uses the plugin icon"), ControllerBillboard->Sprite.Get(), ExpectedControllerIcon);
	}

	TestTrue(TEXT("Controller initializes"), Controller->InitializePuzzleController());
	TestFalse(TEXT("Receiver starts inactive because missing input state is invalid"), Receiver->IsReceiverActive());

	Emitter->SetSignalState(TAG_PuzzleTestPressed, true, nullptr);
	TestTrue(TEXT("Receiver activates when input becomes active"), Receiver->IsReceiverActive());

	Emitter->SetSignalState(TAG_PuzzleTestPressed, false, nullptr);
	TestFalse(TEXT("Receiver deactivates when input becomes inactive"), Receiver->IsReceiverActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuzzleSystemBooleanConditionsTest, "PuzzleSystem.Core.BooleanConditions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleSystemBooleanConditionsTest::RunTest(const FString& Parameters)
{
	AActor* LeftEmitterActor = PuzzleSystemTest::NewActor(TEXT("LeftEmitterActor"));
	UPuzzleEmitterComponent* LeftEmitter = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(LeftEmitterActor, TEXT("Emitter"));
	AActor* RightEmitterActor = PuzzleSystemTest::NewActor(TEXT("RightEmitterActor"));
	UPuzzleEmitterComponent* RightEmitter = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(RightEmitterActor, TEXT("Emitter"));
	AActor* ReceiverActor = PuzzleSystemTest::NewActor(TEXT("ReceiverActor"));
	UPuzzleReceiverComponent* Receiver = PuzzleSystemTest::AddComponent<UPuzzleReceiverComponent>(ReceiverActor, TEXT("Receiver"));

	APuzzleController* AllController = PuzzleSystemTest::NewController(TEXT("AllController"));
	PuzzleSystemTest::BindInput(AllController, TEXT("Left"), LeftEmitterActor, NAME_None, TAG_PuzzleTestPressed);
	PuzzleSystemTest::BindInput(AllController, TEXT("Right"), RightEmitterActor, NAME_None, TAG_PuzzleTestPressed);
	PuzzleSystemTest::BindReceiver(AllController, ReceiverActor);
	UPuzzleAllCondition* AllCondition = NewObject<UPuzzleAllCondition>(AllController);
	AllCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(AllCondition, TEXT("Left")));
	AllCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(AllCondition, TEXT("Right")));
	AllController->RootCondition = AllCondition;

	TestTrue(TEXT("ALL controller initializes"), AllController->InitializePuzzleController());
	LeftEmitter->SetSignalState(TAG_PuzzleTestPressed, true, nullptr);
	TestFalse(TEXT("ALL remains inactive with only one active input"), Receiver->IsReceiverActive());
	RightEmitter->SetSignalState(TAG_PuzzleTestPressed, true, nullptr);
	TestTrue(TEXT("ALL activates when both inputs are active"), Receiver->IsReceiverActive());
	AllController->ShutdownPuzzleController();

	APuzzleController* AnyController = PuzzleSystemTest::NewController(TEXT("AnyController"));
	PuzzleSystemTest::BindInput(AnyController, TEXT("Left"), LeftEmitterActor, NAME_None, TAG_PuzzleTestPressed);
	PuzzleSystemTest::BindInput(AnyController, TEXT("Right"), RightEmitterActor, NAME_None, TAG_PuzzleTestPressed);
	PuzzleSystemTest::BindReceiver(AnyController, ReceiverActor);
	UPuzzleAnyCondition* AnyCondition = NewObject<UPuzzleAnyCondition>(AnyController);
	AnyCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(AnyCondition, TEXT("Left")));
	AnyCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(AnyCondition, TEXT("Right")));
	AnyController->RootCondition = AnyCondition;

	LeftEmitter->SetSignalState(TAG_PuzzleTestPressed, false, nullptr);
	RightEmitter->SetSignalState(TAG_PuzzleTestPressed, false, nullptr);
	TestTrue(TEXT("ANY controller initializes"), AnyController->InitializePuzzleController());
	TestFalse(TEXT("ANY starts inactive"), Receiver->IsReceiverActive());
	RightEmitter->SetSignalState(TAG_PuzzleTestPressed, true, nullptr);
	TestTrue(TEXT("ANY activates with one active input"), Receiver->IsReceiverActive());
	AnyController->ShutdownPuzzleController();

	APuzzleController* NotController = PuzzleSystemTest::NewController(TEXT("NotController"));
	PuzzleSystemTest::BindInput(NotController, TEXT("Blocked"), RightEmitterActor, NAME_None, TAG_PuzzleTestPressed);
	PuzzleSystemTest::BindReceiver(NotController, ReceiverActor);
	UPuzzleNotCondition* NotCondition = NewObject<UPuzzleNotCondition>(NotController);
	NotCondition->Condition = PuzzleSystemTest::NewInputCondition(NotCondition, TEXT("Blocked"));
	NotController->RootCondition = NotCondition;

	RightEmitter->SetSignalState(TAG_PuzzleTestPressed, false, nullptr);
	TestTrue(TEXT("NOT controller initializes"), NotController->InitializePuzzleController());
	TestTrue(TEXT("NOT activates when child is false and input is valid"), Receiver->IsReceiverActive());
	RightEmitter->SetSignalState(TAG_PuzzleTestPressed, true, nullptr);
	TestFalse(TEXT("NOT deactivates when child is true"), Receiver->IsReceiverActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuzzleSystemThresholdAndInitialStateTest, "PuzzleSystem.Core.ThresholdAndInitialState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleSystemThresholdAndInitialStateTest::RunTest(const FString& Parameters)
{
	AActor* EmitterActorA = PuzzleSystemTest::NewActor(TEXT("EmitterActorA"));
	UPuzzleEmitterComponent* EmitterA = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(EmitterActorA, TEXT("Emitter"));
	AActor* EmitterActorB = PuzzleSystemTest::NewActor(TEXT("EmitterActorB"));
	UPuzzleEmitterComponent* EmitterB = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(EmitterActorB, TEXT("Emitter"));
	AActor* EmitterActorC = PuzzleSystemTest::NewActor(TEXT("EmitterActorC"));
	UPuzzleEmitterComponent* EmitterC = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(EmitterActorC, TEXT("Emitter"));
	AActor* ReceiverActor = PuzzleSystemTest::NewActor(TEXT("ReceiverActor"));
	UPuzzleReceiverComponent* Receiver = PuzzleSystemTest::AddComponent<UPuzzleReceiverComponent>(ReceiverActor, TEXT("Receiver"));

	EmitterA->SetSignalState(TAG_PuzzleTestPressed, true, nullptr);
	EmitterB->SetSignalState(TAG_PuzzleTestPressed, true, nullptr);
	EmitterC->SetSignalState(TAG_PuzzleTestPressed, false, nullptr);

	APuzzleController* Controller = PuzzleSystemTest::NewController(TEXT("ThresholdController"));
	PuzzleSystemTest::BindInput(Controller, TEXT("A"), EmitterActorA, NAME_None, TAG_PuzzleTestPressed);
	PuzzleSystemTest::BindInput(Controller, TEXT("B"), EmitterActorB, NAME_None, TAG_PuzzleTestPressed);
	PuzzleSystemTest::BindInput(Controller, TEXT("C"), EmitterActorC, NAME_None, TAG_PuzzleTestPressed);
	PuzzleSystemTest::BindReceiver(Controller, ReceiverActor);
	UPuzzleThresholdCondition* ThresholdCondition = NewObject<UPuzzleThresholdCondition>(Controller);
	ThresholdCondition->RequiredCount = 2;
	ThresholdCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(ThresholdCondition, TEXT("A")));
	ThresholdCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(ThresholdCondition, TEXT("B")));
	ThresholdCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(ThresholdCondition, TEXT("C")));
	Controller->RootCondition = ThresholdCondition;

	TestTrue(TEXT("Threshold controller initializes from prepublished emitter state"), Controller->InitializePuzzleController());
	TestTrue(TEXT("Receiver activates from initial emitter state"), Receiver->IsReceiverActive());
	EmitterB->SetSignalState(TAG_PuzzleTestPressed, false, nullptr);
	TestFalse(TEXT("Receiver deactivates when threshold is no longer met"), Receiver->IsReceiverActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuzzleSystemPayloadAndLifecycleTest, "PuzzleSystem.Core.PayloadAndLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleSystemPayloadAndLifecycleTest::RunTest(const FString& Parameters)
{
	AActor* EmitterActor = PuzzleSystemTest::NewActor(TEXT("EmitterActor"));
	UPuzzleEmitterComponent* Emitter = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(EmitterActor, TEXT("Emitter"));
	AActor* ReceiverActor = PuzzleSystemTest::NewActor(TEXT("ReceiverActor"));
	UPuzzleReceiverComponent* Receiver = PuzzleSystemTest::AddComponent<UPuzzleReceiverComponent>(ReceiverActor, TEXT("Receiver"));
	APuzzleController* Controller = PuzzleSystemTest::BuildSingleInputController(EmitterActor, ReceiverActor);

	UPuzzleTestSignalPayload* Payload = NewObject<UPuzzleTestSignalPayload>(Emitter);
	Payload->Value = 7;

	Emitter->SetSignalState(TAG_PuzzleTestPressed, true, Payload);
	TestTrue(TEXT("Controller initializes"), Controller->InitializePuzzleController());

	const int64 FirstRevision = Controller->GetInputRevision(TEXT("Pressed"));
	TestTrue(TEXT("Controller can read payload object"), Controller->GetInputPayload(TEXT("Pressed")) == Payload);
	TestTrue(TEXT("Republish succeeds for existing signal"), Emitter->RepublishSignal(TAG_PuzzleTestPressed));
	TestTrue(TEXT("Republish increments input revision"), Controller->GetInputRevision(TEXT("Pressed")) > FirstRevision);

	Emitter->OnComponentDestroyed(false);
	TestFalse(TEXT("Emitter invalidation deactivates receiver"), Receiver->IsReceiverActive());
	TestFalse(TEXT("Input becomes invalid after emitter invalidation"), Controller->IsInputValid(TEXT("Pressed")));

	Emitter->RepublishSignal(TAG_PuzzleTestPressed);
	TestTrue(TEXT("Receiver reactivates after emitter republishes valid state"), Receiver->IsReceiverActive());
	Controller->ShutdownPuzzleController();
	TestFalse(TEXT("Controller shutdown removes active receiver request"), Receiver->IsReceiverActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuzzleSystemMultipleControllersAndBindingsTest, "PuzzleSystem.Core.MultipleControllersAndBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleSystemMultipleControllersAndBindingsTest::RunTest(const FString& Parameters)
{
	AActor* EmitterActor = PuzzleSystemTest::NewActor(TEXT("EmitterActor"));
	UPuzzleEmitterComponent* Emitter = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(EmitterActor, TEXT("Emitter"));
	AActor* ReceiverActor = PuzzleSystemTest::NewActor(TEXT("ReceiverActor"));
	UPuzzleReceiverComponent* Receiver = PuzzleSystemTest::AddComponent<UPuzzleReceiverComponent>(ReceiverActor, TEXT("Receiver"));

	APuzzleController* ControllerA = PuzzleSystemTest::BuildSingleInputController(EmitterActor, ReceiverActor, TEXT("PressedA"));
	APuzzleController* ControllerB = PuzzleSystemTest::BuildSingleInputController(EmitterActor, ReceiverActor, TEXT("PressedB"));

	Emitter->SetSignalState(TAG_PuzzleTestPressed, true, nullptr);
	TestTrue(TEXT("Controller A initializes"), ControllerA->InitializePuzzleController());
	TestTrue(TEXT("Controller B initializes"), ControllerB->InitializePuzzleController());
	TestTrue(TEXT("Receiver active with both controllers requesting active"), Receiver->IsReceiverActive());

	ControllerA->ShutdownPuzzleController();
	TestTrue(TEXT("Receiver stays active while Controller B still requests active"), Receiver->IsReceiverActive());

	ControllerB->ShutdownPuzzleController();
	TestFalse(TEXT("Receiver deactivates after all controller requests are removed"), Receiver->IsReceiverActive());

	AActor* MultiEmitterActor = PuzzleSystemTest::NewActor(TEXT("MultiEmitterActor"));
	UPuzzleEmitterComponent* FirstEmitter = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(MultiEmitterActor, TEXT("FirstEmitter"));
	UPuzzleEmitterComponent* SecondEmitter = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(MultiEmitterActor, TEXT("SecondEmitter"));
	FirstEmitter->SetSignalState(TAG_PuzzleTestPowered, false, nullptr);
	SecondEmitter->SetSignalState(TAG_PuzzleTestPowered, true, nullptr);

	APuzzleController* DefaultEmitterController = PuzzleSystemTest::NewController(TEXT("DefaultEmitterController"));
	PuzzleSystemTest::BindInput(DefaultEmitterController, TEXT("Power"), MultiEmitterActor, NAME_None, TAG_PuzzleTestPowered);
	DefaultEmitterController->InputBindings[0].EmitterComponentName = TEXT("SecondEmitter");
	PuzzleSystemTest::BindReceiver(DefaultEmitterController, ReceiverActor);
	DefaultEmitterController->RootCondition = PuzzleSystemTest::NewInputCondition(DefaultEmitterController, TEXT("Power"));
	TestFalse(TEXT("Explicit emitter selection defaults to disabled"), DefaultEmitterController->InputBindings[0].bSpecifyEmitterComponent);
	TestTrue(TEXT("Actor with multiple emitters resolves its first emitter by default"), DefaultEmitterController->InitializePuzzleController());
	TestFalse(TEXT("Disabled emitter selection ignores a stored component name"), Receiver->IsReceiverActive());
	DefaultEmitterController->ShutdownPuzzleController();

	APuzzleController* NamedController = PuzzleSystemTest::NewController(TEXT("NamedController"));
	PuzzleSystemTest::BindInput(NamedController, TEXT("Power"), MultiEmitterActor, TEXT("SecondEmitter"), TAG_PuzzleTestPowered);
	PuzzleSystemTest::BindReceiver(NamedController, ReceiverActor);
	NamedController->RootCondition = PuzzleSystemTest::NewInputCondition(NamedController, TEXT("Power"));
	TestTrue(TEXT("Named emitter binding enables explicit selection"), NamedController->InputBindings[0].bSpecifyEmitterComponent);
	TestTrue(TEXT("Named component binding initializes"), NamedController->InitializePuzzleController());
	TestTrue(TEXT("Named component binding activates receiver"), Receiver->IsReceiverActive());
	NamedController->ShutdownPuzzleController();

	AActor* MultiReceiverActor = PuzzleSystemTest::NewActor(TEXT("MultiReceiverActor"));
	UPuzzleReceiverComponent* FirstReceiver = PuzzleSystemTest::AddComponent<UPuzzleReceiverComponent>(MultiReceiverActor, TEXT("FirstReceiver"));
	UPuzzleReceiverComponent* SecondReceiver = PuzzleSystemTest::AddComponent<UPuzzleReceiverComponent>(MultiReceiverActor, TEXT("SecondReceiver"));

	APuzzleController* DefaultReceiverController = PuzzleSystemTest::NewController(TEXT("DefaultReceiverController"));
	PuzzleSystemTest::BindInput(DefaultReceiverController, TEXT("Power"), MultiEmitterActor, TEXT("SecondEmitter"), TAG_PuzzleTestPowered);
	PuzzleSystemTest::BindReceiver(DefaultReceiverController, MultiReceiverActor);
	DefaultReceiverController->ReceiverBindings[0].ReceiverComponentName = TEXT("SecondReceiver");
	DefaultReceiverController->RootCondition = PuzzleSystemTest::NewInputCondition(DefaultReceiverController, TEXT("Power"));
	TestFalse(TEXT("Explicit receiver selection defaults to disabled"), DefaultReceiverController->ReceiverBindings[0].bSpecifyReceiverComponent);
	TestTrue(TEXT("Actor with multiple receivers resolves its first receiver by default"), DefaultReceiverController->InitializePuzzleController());
	TestTrue(TEXT("Default receiver binding activates the first receiver"), FirstReceiver->IsReceiverActive());
	TestFalse(TEXT("Disabled receiver selection ignores a stored component name"), SecondReceiver->IsReceiverActive());
	DefaultReceiverController->ShutdownPuzzleController();

	APuzzleController* NamedReceiverController = PuzzleSystemTest::NewController(TEXT("NamedReceiverController"));
	PuzzleSystemTest::BindInput(NamedReceiverController, TEXT("Power"), MultiEmitterActor, TEXT("SecondEmitter"), TAG_PuzzleTestPowered);
	PuzzleSystemTest::BindReceiver(NamedReceiverController, MultiReceiverActor, TEXT("SecondReceiver"));
	NamedReceiverController->RootCondition = PuzzleSystemTest::NewInputCondition(NamedReceiverController, TEXT("Power"));
	TestTrue(TEXT("Named receiver binding enables explicit selection"), NamedReceiverController->ReceiverBindings[0].bSpecifyReceiverComponent);
	TestTrue(TEXT("Named receiver component binding initializes"), NamedReceiverController->InitializePuzzleController());
	TestFalse(TEXT("Named receiver binding leaves the first receiver inactive"), FirstReceiver->IsReceiverActive());
	TestTrue(TEXT("Named receiver binding activates the selected receiver"), SecondReceiver->IsReceiverActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuzzleSystemReentrantAndDelegateTest, "PuzzleSystem.Core.ReentrantAndDelegate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleSystemReentrantAndDelegateTest::RunTest(const FString& Parameters)
{
	AActor* FirstEmitterActor = PuzzleSystemTest::NewActor(TEXT("FirstEmitterActor"));
	UPuzzleEmitterComponent* FirstEmitter = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(FirstEmitterActor, TEXT("Emitter"));
	AActor* ChainActor = PuzzleSystemTest::NewActor(TEXT("ChainActor"));
	UPuzzleReentrantReceiverComponent* ReentrantReceiver = PuzzleSystemTest::AddComponent<UPuzzleReentrantReceiverComponent>(ChainActor, TEXT("Receiver"));
	UPuzzleEmitterComponent* ChainEmitter = PuzzleSystemTest::AddComponent<UPuzzleEmitterComponent>(ChainActor, TEXT("Emitter"));
	AActor* FinalReceiverActor = PuzzleSystemTest::NewActor(TEXT("FinalReceiverActor"));
	UPuzzleReceiverComponent* FinalReceiver = PuzzleSystemTest::AddComponent<UPuzzleReceiverComponent>(FinalReceiverActor, TEXT("Receiver"));

	UPuzzleReceiverTestObserver* Observer = NewObject<UPuzzleReceiverTestObserver>();
	FinalReceiver->OnReceiverStateChanged.AddDynamic(Observer, &UPuzzleReceiverTestObserver::HandleReceiverStateChanged);

	ReentrantReceiver->EmitterToPublish = ChainEmitter;
	ReentrantReceiver->SignalTagToPublish = TAG_PuzzleTestCompleted;

	APuzzleController* FirstController = PuzzleSystemTest::BuildSingleInputController(FirstEmitterActor, ChainActor);
	APuzzleController* SecondController = PuzzleSystemTest::NewController(TEXT("SecondController"));
	PuzzleSystemTest::BindInput(SecondController, TEXT("Completed"), ChainActor, TEXT("Emitter"), TAG_PuzzleTestCompleted);
	PuzzleSystemTest::BindReceiver(SecondController, FinalReceiverActor);
	SecondController->RootCondition = PuzzleSystemTest::NewInputCondition(SecondController, TEXT("Completed"));

	TestTrue(TEXT("First controller initializes"), FirstController->InitializePuzzleController());
	TestTrue(TEXT("Second controller initializes"), SecondController->InitializePuzzleController());

	FirstEmitter->SetSignalState(TAG_PuzzleTestPressed, true, nullptr);
	TestTrue(TEXT("Reentrant receiver activated"), ReentrantReceiver->IsReceiverActive());
	FPuzzleSignalState ChainedSignalState;
	TestTrue(TEXT("Reentrant receiver published chained signal"), ChainEmitter->TryGetSignalState(TAG_PuzzleTestCompleted, ChainedSignalState));
	TestTrue(TEXT("Final receiver activated by chained signal"), FinalReceiver->IsReceiverActive());
	TestEqual(TEXT("Receiver state delegate fired once"), Observer->StateChangedCount, 1);
	TestTrue(TEXT("Receiver state delegate reported active"), Observer->bLastActive);
	TestEqual(TEXT("Reentrant publish happened once for stable state"), ReentrantReceiver->PublishCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuzzleSwitchModesTest, "PuzzleSystem.Switch.ModesAndEdges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleSwitchModesTest::RunTest(const FString& Parameters)
{
	PuzzleSystemTest::FScopedSwitchWorld TestWorld;
	if (!TestNotNull(TEXT("Switch test world exists"), TestWorld.World))
	{
		return false;
	}

	TestTrue(TEXT("Native Puzzle Switch base is abstract"), APuzzleSwitch::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
	TestFalse(TEXT("Concrete test child is not abstract"), APuzzleSwitchTestActor::StaticClass()->HasAnyClassFlags(CLASS_Abstract));

	APuzzleSwitchTestActor* HoldSwitch = PuzzleSystemTest::NewSwitch(TestWorld, EPuzzleSwitchMode::Hold);
	if (!TestNotNull(TEXT("Hold switch exists"), HoldSwitch))
	{
		return false;
	}
	TestNotNull(TEXT("Switch owns its minimal scene root"), HoldSwitch->SceneRootComponent.Get());
	TestNotNull(TEXT("Switch owns an emitter component"), HoldSwitch->PuzzleEmitterComponent.Get());
	TestTrue(TEXT("Switch scene component is the Actor root"), HoldSwitch->GetRootComponent() == HoldSwitch->SceneRootComponent);

	UPuzzleSwitchTestObserver* HoldObserver = NewObject<UPuzzleSwitchTestObserver>();
	PuzzleSystemTest::BindSwitchObserver(HoldSwitch, HoldObserver);
	FPuzzleSignalState OutputState;
	TestTrue(TEXT("Hold switch publishes initial output"), HoldSwitch->PuzzleEmitterComponent->TryGetSignalState(TAG_PuzzleTestPressed, OutputState));
	TestFalse(TEXT("Hold switch starts inactive"), OutputState.bIsActive);
	TestTrue(TEXT("First Hold press is accepted"), HoldSwitch->Press());
	TestEqual(TEXT("Hold input confirms pressed"), HoldSwitch->GetInputState(), EPuzzleSwitchInputState::Pressed);
	TestTrue(TEXT("Hold raw input is pressed"), HoldSwitch->IsInputPressed());
	TestTrue(TEXT("Hold logical input is pressed"), HoldSwitch->IsPressed());
	TestTrue(TEXT("Hold output activates"), HoldSwitch->IsSwitchActive());
	TestFalse(TEXT("Duplicate Hold press is deduplicated"), HoldSwitch->Press());
	TestEqual(TEXT("One confirmed Hold press event is emitted"), HoldObserver->PressedCount, 1);
	TestEqual(TEXT("One Hold activation event is emitted"), HoldObserver->ActivatedCount, 1);
	TestTrue(TEXT("Hold release is accepted"), HoldSwitch->Release());
	TestEqual(TEXT("Hold input returns released"), HoldSwitch->GetInputState(), EPuzzleSwitchInputState::Released);
	TestFalse(TEXT("Hold output deactivates"), HoldSwitch->IsSwitchActive());
	TestFalse(TEXT("Duplicate Hold release is deduplicated"), HoldSwitch->Release());
	TestEqual(TEXT("One confirmed Hold release event is emitted"), HoldObserver->ReleasedCount, 1);
	TestEqual(TEXT("One Hold deactivation event is emitted"), HoldObserver->DeactivatedCount, 1);

	APuzzleSwitchTestActor* InitiallyPressedSwitch = PuzzleSystemTest::NewSwitch(
		TestWorld,
		EPuzzleSwitchMode::Hold,
		true,
		0.0f,
		0.0f,
		1.0f,
		EPuzzlePulseRetriggerMode::Ignore,
		EPuzzleSwitchInitialInputState::Pressed);
	if (TestNotNull(TEXT("Initially pressed switch exists"), InitiallyPressedSwitch))
	{
		TestEqual(
			TEXT("Initial Pressed setting establishes confirmed input without a synthetic edge"),
			InitiallyPressedSwitch->GetInputState(),
			EPuzzleSwitchInputState::Pressed);
		TestTrue(TEXT("Independent configured start output remains active"), InitiallyPressedSwitch->IsSwitchActive());
		TestTrue(TEXT("Initially pressed switch accepts a normal Release"), InitiallyPressedSwitch->Release());
		TestEqual(TEXT("Normal Release transitions to Released"), InitiallyPressedSwitch->GetInputState(), EPuzzleSwitchInputState::Released);
		TestFalse(TEXT("Hold Release deactivates output"), InitiallyPressedSwitch->IsSwitchActive());
		InitiallyPressedSwitch->ResetSwitch();
		TestEqual(TEXT("Reset restores configured Pressed input"), InitiallyPressedSwitch->GetInputState(), EPuzzleSwitchInputState::Pressed);
		TestTrue(TEXT("Reset restores configured active output"), InitiallyPressedSwitch->IsSwitchActive());
	}

	APuzzleSwitchTestActor* ToggleSwitch = PuzzleSystemTest::NewSwitch(TestWorld, EPuzzleSwitchMode::Toggle);
	TestNotNull(TEXT("Toggle switch exists"), ToggleSwitch);
	if (ToggleSwitch)
	{
		TestTrue(TEXT("Toggle first press activates"), ToggleSwitch->Press());
		TestTrue(TEXT("Toggle output becomes active"), ToggleSwitch->IsSwitchActive());
		TestFalse(TEXT("Held Toggle press cannot toggle again"), ToggleSwitch->Press());
		TestTrue(TEXT("Toggle release rearms input"), ToggleSwitch->Release());
		TestTrue(TEXT("Toggle release preserves output"), ToggleSwitch->IsSwitchActive());
		TestTrue(TEXT("Toggle second press is accepted"), ToggleSwitch->Press());
		TestFalse(TEXT("Toggle second press deactivates output"), ToggleSwitch->IsSwitchActive());
	}

	APuzzleSwitchTestActor* LatchSwitch = PuzzleSystemTest::NewSwitch(TestWorld, EPuzzleSwitchMode::Latch);
	TestNotNull(TEXT("Latch switch exists"), LatchSwitch);
	if (LatchSwitch)
	{
		UPuzzleSwitchTestObserver* LatchObserver = NewObject<UPuzzleSwitchTestObserver>();
		PuzzleSystemTest::BindSwitchObserver(LatchSwitch, LatchObserver);
		LatchSwitch->Press();
		LatchSwitch->Release();
		LatchSwitch->Press();
		TestTrue(TEXT("Latch remains active across release and later press"), LatchSwitch->IsSwitchActive());
		TestEqual(TEXT("Latch publishes only one activation transition"), LatchObserver->ActivatedCount, 1);
		LatchSwitch->ResetSwitch();
		TestFalse(TEXT("Latch reset restores configured inactive state"), LatchSwitch->IsSwitchActive());
		TestEqual(TEXT("Latch reset restores Released input"), LatchSwitch->GetInputState(), EPuzzleSwitchInputState::Released);
		TestEqual(TEXT("Latch emits one reset event"), LatchObserver->ResetCount, 1);
		TestTrue(TEXT("Latch is reusable after reset"), LatchSwitch->Press());
		TestTrue(TEXT("Latch reactivates after reset"), LatchSwitch->IsSwitchActive());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuzzleSwitchInputDelayTest, "PuzzleSystem.Switch.InputDelays", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleSwitchInputDelayTest::RunTest(const FString& Parameters)
{
	PuzzleSystemTest::FScopedSwitchWorld TestWorld;
	if (!TestNotNull(TEXT("Switch delay test world exists"), TestWorld.World))
	{
		return false;
	}

	APuzzleSwitchTestActor* PuzzleSwitch = PuzzleSystemTest::NewSwitch(
		TestWorld,
		EPuzzleSwitchMode::Hold,
		false,
		0.2f,
		0.2f);
	if (!TestNotNull(TEXT("Delayed switch exists"), PuzzleSwitch))
	{
		return false;
	}

	UPuzzleSwitchTestObserver* Observer = NewObject<UPuzzleSwitchTestObserver>();
	PuzzleSystemTest::BindSwitchObserver(PuzzleSwitch, Observer);

	TestTrue(TEXT("Press starts PressPending"), PuzzleSwitch->Press());
	TestEqual(TEXT("Press delay owns PressPending state"), PuzzleSwitch->GetInputState(), EPuzzleSwitchInputState::PressPending);
	TestTrue(TEXT("PressPending represents raw pressed input"), PuzzleSwitch->IsInputPressed());
	TestFalse(TEXT("PressPending is not logically pressed"), PuzzleSwitch->IsPressed());
	TestFalse(TEXT("Press delay does not activate early"), PuzzleSwitch->IsSwitchActive());
	TestEqual(TEXT("Press delay Started emits once"), Observer->PressDelayStartedCount, 1);
	TestWorld.Advance(0.1f);
	TestTrue(TEXT("RestartPressDelay succeeds while pending"), PuzzleSwitch->RestartPressDelay());
	TestEqual(TEXT("Restart does not emit another press Started event"), Observer->PressDelayStartedCount, 1);
	TestWorld.Advance(0.11f);
	TestTrue(TEXT("Restarted press delay remains pending before full duration"), PuzzleSwitch->IsPressDelayPending());
	TestTrue(TEXT("Release cancels pending press"), PuzzleSwitch->Release());
	TestEqual(TEXT("Cancelled press returns to Released"), PuzzleSwitch->GetInputState(), EPuzzleSwitchInputState::Released);
	TestEqual(TEXT("Press cancellation emits once"), Observer->PressDelayCancelledCount, 1);
	TestEqual(TEXT("Cancelled press emits no confirmed edge"), Observer->PressedCount, 0);

	PuzzleSwitch->Press();
	TestWorld.Advance(0.21f);
	TestEqual(TEXT("Completed press delay confirms Pressed"), PuzzleSwitch->GetInputState(), EPuzzleSwitchInputState::Pressed);
	TestTrue(TEXT("Completed press delay activates Hold output"), PuzzleSwitch->IsSwitchActive());
	TestEqual(TEXT("Press completion emits after one confirmed edge"), Observer->PressDelayCompletedCount, 1);
	TestEqual(TEXT("Exactly one confirmed press was emitted"), Observer->PressedCount, 1);

	TestTrue(TEXT("Release starts ReleasePending"), PuzzleSwitch->Release());
	TestEqual(TEXT("Release delay owns ReleasePending state"), PuzzleSwitch->GetInputState(), EPuzzleSwitchInputState::ReleasePending);
	TestFalse(TEXT("ReleasePending represents raw released input"), PuzzleSwitch->IsInputPressed());
	TestTrue(TEXT("ReleasePending remains logically pressed"), PuzzleSwitch->IsPressed());
	TestTrue(TEXT("ReleasePending preserves Hold output"), PuzzleSwitch->IsSwitchActive());
	TestWorld.Advance(0.1f);
	TestTrue(TEXT("RestartReleaseDelay succeeds while pending"), PuzzleSwitch->RestartReleaseDelay());
	TestEqual(TEXT("Restart does not emit another release Started event"), Observer->ReleaseDelayStartedCount, 1);
	TestTrue(TEXT("Press cancels ReleasePending"), PuzzleSwitch->Press());
	TestEqual(TEXT("Cancelled release returns to Pressed"), PuzzleSwitch->GetInputState(), EPuzzleSwitchInputState::Pressed);
	TestEqual(TEXT("Release cancellation emits once"), Observer->ReleaseDelayCancelledCount, 1);
	TestEqual(TEXT("Cancelling release does not manufacture another Press edge"), Observer->PressedCount, 1);

	PuzzleSwitch->Release();
	TestTrue(TEXT("Explicit release cancellation succeeds"), PuzzleSwitch->CancelPendingRelease());
	TestEqual(TEXT("Explicit release cancellation emits once more"), Observer->ReleaseDelayCancelledCount, 2);
	PuzzleSwitch->Release();
	TestWorld.Advance(0.21f);
	TestEqual(TEXT("Completed release delay restores Released"), PuzzleSwitch->GetInputState(), EPuzzleSwitchInputState::Released);
	TestFalse(TEXT("Completed release delay deactivates Hold output"), PuzzleSwitch->IsSwitchActive());
	TestEqual(TEXT("Exactly one confirmed Release edge was emitted"), Observer->ReleasedCount, 1);
	TestEqual(TEXT("Release completion event emits once"), Observer->ReleaseDelayCompletedCount, 1);

	PuzzleSwitch->Press();
	TestTrue(TEXT("Explicit pending press cancellation succeeds"), PuzzleSwitch->CancelPendingPress());
	TestEqual(TEXT("Explicit pending press cancellation emits once more"), Observer->PressDelayCancelledCount, 2);
	TestFalse(TEXT("RestartPressDelay fails outside PressPending"), PuzzleSwitch->RestartPressDelay());
	TestFalse(TEXT("CancelPendingRelease fails outside ReleasePending"), PuzzleSwitch->CancelPendingRelease());

	PuzzleSwitch->Press();
	PuzzleSwitch->ResetSwitch();
	TestEqual(TEXT("Reset during PressPending restores Released"), PuzzleSwitch->GetInputState(), EPuzzleSwitchInputState::Released);
	const int32 PressedCountAfterReset = Observer->PressedCount;
	TestWorld.Advance(0.3f);
	TestEqual(TEXT("Reset invalidates stale press callbacks"), Observer->PressedCount, PressedCountAfterReset);
	TestTrue(TEXT("Every switch BlueprintNativeEvent hook runs before its matching delegate"), Observer->bHooksPrecededDelegates);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPuzzleSwitchPulseTest, "PuzzleSystem.Switch.PulseAndReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPuzzleSwitchPulseTest::RunTest(const FString& Parameters)
{
	PuzzleSystemTest::FScopedSwitchWorld TestWorld;
	if (!TestNotNull(TEXT("Switch pulse test world exists"), TestWorld.World))
	{
		return false;
	}

	APuzzleSwitchTestActor* IgnoreSwitch = PuzzleSystemTest::NewSwitch(
		TestWorld,
		EPuzzleSwitchMode::Pulse,
		false,
		0.0f,
		0.0f,
		1.0f,
		EPuzzlePulseRetriggerMode::Ignore);
	if (!TestNotNull(TEXT("Ignore pulse switch exists"), IgnoreSwitch))
	{
		return false;
	}
	IgnoreSwitch->Press();
	IgnoreSwitch->Release();
	TestWorld.Advance(0.5f);
	IgnoreSwitch->Press();
	TestWorld.Advance(0.51f);
	TestFalse(TEXT("Ignore retrigger preserves original pulse end"), IgnoreSwitch->IsSwitchActive());

	APuzzleSwitchTestActor* RestartSwitch = PuzzleSystemTest::NewSwitch(
		TestWorld,
		EPuzzleSwitchMode::Pulse,
		false,
		0.0f,
		0.0f,
		1.0f,
		EPuzzlePulseRetriggerMode::Restart);
	TestNotNull(TEXT("Restart pulse switch exists"), RestartSwitch);
	if (RestartSwitch)
	{
		UPuzzleSwitchTestObserver* RestartObserver = NewObject<UPuzzleSwitchTestObserver>();
		PuzzleSystemTest::BindSwitchObserver(RestartSwitch, RestartObserver);
		RestartSwitch->Press();
		RestartSwitch->Release();
		TestWorld.Advance(0.5f);
		RestartSwitch->Press();
		TestEqual(TEXT("Pulse retrigger does not republish Active"), RestartObserver->ActivatedCount, 1);
		TestWorld.Advance(0.51f);
		TestTrue(TEXT("Restart retrigger extends pulse past original end"), RestartSwitch->IsSwitchActive());
		TestWorld.Advance(0.5f);
		TestFalse(TEXT("Restarted pulse eventually completes"), RestartSwitch->IsSwitchActive());
	}

	APuzzleSwitchTestActor* ZeroSwitch = PuzzleSystemTest::NewSwitch(
		TestWorld,
		EPuzzleSwitchMode::Pulse,
		false,
		0.0f,
		0.0f,
		0.0f);
	TestNotNull(TEXT("Zero-duration pulse switch exists"), ZeroSwitch);
	if (ZeroSwitch)
	{
		UPuzzleSwitchTestObserver* ZeroObserver = NewObject<UPuzzleSwitchTestObserver>();
		PuzzleSystemTest::BindSwitchObserver(ZeroSwitch, ZeroObserver);
		ZeroSwitch->Press();
		TestTrue(TEXT("Zero-duration pulse is observably active synchronously"), ZeroSwitch->IsSwitchActive());
		TestEqual(TEXT("Zero-duration pulse emits Active once"), ZeroObserver->ActivatedCount, 1);
		TestEqual(TEXT("Zero-duration pulse does not deactivate synchronously"), ZeroObserver->DeactivatedCount, 0);
		TestWorld.Advance(1.0f / 60.0f);
		TestFalse(TEXT("Zero-duration pulse deactivates on a later timer tick"), ZeroSwitch->IsSwitchActive());
		TestEqual(TEXT("Zero-duration pulse emits Inactive once"), ZeroObserver->DeactivatedCount, 1);
	}

	APuzzleSwitchTestActor* StartActiveSwitch = PuzzleSystemTest::NewSwitch(
		TestWorld,
		EPuzzleSwitchMode::Pulse,
		true,
		0.0f,
		0.0f,
		0.5f);
	TestNotNull(TEXT("Start-active pulse switch exists"), StartActiveSwitch);
	if (StartActiveSwitch)
	{
		FPuzzleSignalState InitialState;
		TestTrue(TEXT("Start-active signal is queryable by late controllers"), StartActiveSwitch->PuzzleEmitterComponent->TryGetSignalState(TAG_PuzzleTestPressed, InitialState));
		TestTrue(TEXT("Start-active signal publishes active"), InitialState.bIsActive);
		StartActiveSwitch->Press();
		StartActiveSwitch->ResetSwitch();
		TestWorld.Advance(1.0f);
		TestTrue(TEXT("Reset invalidates stale pulse completion and restores start-active output"), StartActiveSwitch->IsSwitchActive());
		TestEqual(TEXT("Reset restores Released even when output starts active"), StartActiveSwitch->GetInputState(), EPuzzleSwitchInputState::Released);
	}

	return true;
}

#endif
