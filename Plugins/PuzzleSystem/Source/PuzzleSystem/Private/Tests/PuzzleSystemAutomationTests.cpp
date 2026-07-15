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
#include "Engine/Texture2D.h"
#include "NativeGameplayTags.h"
#include "PuzzleSystemTestTypes.h"
#include "Receivers/PuzzleReceiverComponent.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PuzzleSystemTestPressed, "PuzzleSystem.Test.Pressed");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PuzzleSystemTestPowered, "PuzzleSystem.Test.Powered");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PuzzleSystemTestCompleted, "PuzzleSystem.Test.Completed");

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
	 * @param ComponentName Optional emitter component name.
	 * @param SignalTag Gameplay tag channel observed on the emitter.
	 */
	static void BindInput(APuzzleController* Controller, FName InputId, AActor* EmitterActor, FName ComponentName, FGameplayTag SignalTag)
	{
		FPuzzleInputBinding& Binding = Controller->InputBindings.AddDefaulted_GetRef();
		Binding.InputId = InputId;
		Binding.EmitterActor = EmitterActor;
		Binding.EmitterComponentName = ComponentName;
		Binding.SignalTag = SignalTag;
	}

	/**
	 * Appends one receiver binding to a controller.
	 *
	 * @param Controller Controller receiving the binding.
	 * @param ReceiverActor Actor owning the receiver component.
	 * @param ComponentName Optional receiver component name.
	 */
	static void BindReceiver(APuzzleController* Controller, AActor* ReceiverActor, FName ComponentName = NAME_None)
	{
		FPuzzleReceiverBinding& Binding = Controller->ReceiverBindings.AddDefaulted_GetRef();
		Binding.ReceiverActor = ReceiverActor;
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
		BindInput(Controller, InputId, EmitterActor, NAME_None, TAG_PuzzleSystemTestPressed);
		BindReceiver(Controller, ReceiverActor);
		Controller->RootCondition = NewInputCondition(Controller, InputId);
		return Controller;
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

	Emitter->SetSignalState(TAG_PuzzleSystemTestPressed, true, nullptr);
	TestTrue(TEXT("Receiver activates when input becomes active"), Receiver->IsReceiverActive());

	Emitter->SetSignalState(TAG_PuzzleSystemTestPressed, false, nullptr);
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
	PuzzleSystemTest::BindInput(AllController, TEXT("Left"), LeftEmitterActor, NAME_None, TAG_PuzzleSystemTestPressed);
	PuzzleSystemTest::BindInput(AllController, TEXT("Right"), RightEmitterActor, NAME_None, TAG_PuzzleSystemTestPressed);
	PuzzleSystemTest::BindReceiver(AllController, ReceiverActor);
	UPuzzleAllCondition* AllCondition = NewObject<UPuzzleAllCondition>(AllController);
	AllCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(AllCondition, TEXT("Left")));
	AllCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(AllCondition, TEXT("Right")));
	AllController->RootCondition = AllCondition;

	TestTrue(TEXT("ALL controller initializes"), AllController->InitializePuzzleController());
	LeftEmitter->SetSignalState(TAG_PuzzleSystemTestPressed, true, nullptr);
	TestFalse(TEXT("ALL remains inactive with only one active input"), Receiver->IsReceiverActive());
	RightEmitter->SetSignalState(TAG_PuzzleSystemTestPressed, true, nullptr);
	TestTrue(TEXT("ALL activates when both inputs are active"), Receiver->IsReceiverActive());
	AllController->ShutdownPuzzleController();

	APuzzleController* AnyController = PuzzleSystemTest::NewController(TEXT("AnyController"));
	PuzzleSystemTest::BindInput(AnyController, TEXT("Left"), LeftEmitterActor, NAME_None, TAG_PuzzleSystemTestPressed);
	PuzzleSystemTest::BindInput(AnyController, TEXT("Right"), RightEmitterActor, NAME_None, TAG_PuzzleSystemTestPressed);
	PuzzleSystemTest::BindReceiver(AnyController, ReceiverActor);
	UPuzzleAnyCondition* AnyCondition = NewObject<UPuzzleAnyCondition>(AnyController);
	AnyCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(AnyCondition, TEXT("Left")));
	AnyCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(AnyCondition, TEXT("Right")));
	AnyController->RootCondition = AnyCondition;

	LeftEmitter->SetSignalState(TAG_PuzzleSystemTestPressed, false, nullptr);
	RightEmitter->SetSignalState(TAG_PuzzleSystemTestPressed, false, nullptr);
	TestTrue(TEXT("ANY controller initializes"), AnyController->InitializePuzzleController());
	TestFalse(TEXT("ANY starts inactive"), Receiver->IsReceiverActive());
	RightEmitter->SetSignalState(TAG_PuzzleSystemTestPressed, true, nullptr);
	TestTrue(TEXT("ANY activates with one active input"), Receiver->IsReceiverActive());
	AnyController->ShutdownPuzzleController();

	APuzzleController* NotController = PuzzleSystemTest::NewController(TEXT("NotController"));
	PuzzleSystemTest::BindInput(NotController, TEXT("Blocked"), RightEmitterActor, NAME_None, TAG_PuzzleSystemTestPressed);
	PuzzleSystemTest::BindReceiver(NotController, ReceiverActor);
	UPuzzleNotCondition* NotCondition = NewObject<UPuzzleNotCondition>(NotController);
	NotCondition->Condition = PuzzleSystemTest::NewInputCondition(NotCondition, TEXT("Blocked"));
	NotController->RootCondition = NotCondition;

	RightEmitter->SetSignalState(TAG_PuzzleSystemTestPressed, false, nullptr);
	TestTrue(TEXT("NOT controller initializes"), NotController->InitializePuzzleController());
	TestTrue(TEXT("NOT activates when child is false and input is valid"), Receiver->IsReceiverActive());
	RightEmitter->SetSignalState(TAG_PuzzleSystemTestPressed, true, nullptr);
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

	EmitterA->SetSignalState(TAG_PuzzleSystemTestPressed, true, nullptr);
	EmitterB->SetSignalState(TAG_PuzzleSystemTestPressed, true, nullptr);
	EmitterC->SetSignalState(TAG_PuzzleSystemTestPressed, false, nullptr);

	APuzzleController* Controller = PuzzleSystemTest::NewController(TEXT("ThresholdController"));
	PuzzleSystemTest::BindInput(Controller, TEXT("A"), EmitterActorA, NAME_None, TAG_PuzzleSystemTestPressed);
	PuzzleSystemTest::BindInput(Controller, TEXT("B"), EmitterActorB, NAME_None, TAG_PuzzleSystemTestPressed);
	PuzzleSystemTest::BindInput(Controller, TEXT("C"), EmitterActorC, NAME_None, TAG_PuzzleSystemTestPressed);
	PuzzleSystemTest::BindReceiver(Controller, ReceiverActor);
	UPuzzleThresholdCondition* ThresholdCondition = NewObject<UPuzzleThresholdCondition>(Controller);
	ThresholdCondition->RequiredCount = 2;
	ThresholdCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(ThresholdCondition, TEXT("A")));
	ThresholdCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(ThresholdCondition, TEXT("B")));
	ThresholdCondition->Conditions.Add(PuzzleSystemTest::NewInputCondition(ThresholdCondition, TEXT("C")));
	Controller->RootCondition = ThresholdCondition;

	TestTrue(TEXT("Threshold controller initializes from prepublished emitter state"), Controller->InitializePuzzleController());
	TestTrue(TEXT("Receiver activates from initial emitter state"), Receiver->IsReceiverActive());
	EmitterB->SetSignalState(TAG_PuzzleSystemTestPressed, false, nullptr);
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

	Emitter->SetSignalState(TAG_PuzzleSystemTestPressed, true, Payload);
	TestTrue(TEXT("Controller initializes"), Controller->InitializePuzzleController());

	const int64 FirstRevision = Controller->GetInputRevision(TEXT("Pressed"));
	TestTrue(TEXT("Controller can read payload object"), Controller->GetInputPayload(TEXT("Pressed")) == Payload);
	TestTrue(TEXT("Republish succeeds for existing signal"), Emitter->RepublishSignal(TAG_PuzzleSystemTestPressed));
	TestTrue(TEXT("Republish increments input revision"), Controller->GetInputRevision(TEXT("Pressed")) > FirstRevision);

	Emitter->OnComponentDestroyed(false);
	TestFalse(TEXT("Emitter invalidation deactivates receiver"), Receiver->IsReceiverActive());
	TestFalse(TEXT("Input becomes invalid after emitter invalidation"), Controller->IsInputValid(TEXT("Pressed")));

	Emitter->RepublishSignal(TAG_PuzzleSystemTestPressed);
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

	Emitter->SetSignalState(TAG_PuzzleSystemTestPressed, true, nullptr);
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
	SecondEmitter->SetSignalState(TAG_PuzzleSystemTestPowered, true, nullptr);

	APuzzleController* AmbiguousController = PuzzleSystemTest::NewController(TEXT("AmbiguousController"));
	PuzzleSystemTest::BindInput(AmbiguousController, TEXT("Power"), MultiEmitterActor, NAME_None, TAG_PuzzleSystemTestPowered);
	PuzzleSystemTest::BindReceiver(AmbiguousController, ReceiverActor);
	AmbiguousController->RootCondition = PuzzleSystemTest::NewInputCondition(AmbiguousController, TEXT("Power"));
	AddExpectedError(TEXT("expected exactly one Emitter"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("failed configuration validation"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Actor with multiple emitters and no component name fails closed"), AmbiguousController->InitializePuzzleController());
	TestFalse(TEXT("Ambiguous binding does not activate receiver"), Receiver->IsReceiverActive());

	APuzzleController* NamedController = PuzzleSystemTest::NewController(TEXT("NamedController"));
	PuzzleSystemTest::BindInput(NamedController, TEXT("Power"), MultiEmitterActor, TEXT("SecondEmitter"), TAG_PuzzleSystemTestPowered);
	PuzzleSystemTest::BindReceiver(NamedController, ReceiverActor);
	NamedController->RootCondition = PuzzleSystemTest::NewInputCondition(NamedController, TEXT("Power"));
	TestTrue(TEXT("Named component binding initializes"), NamedController->InitializePuzzleController());
	TestTrue(TEXT("Named component binding activates receiver"), Receiver->IsReceiverActive());

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
	ReentrantReceiver->SignalTagToPublish = TAG_PuzzleSystemTestCompleted;

	APuzzleController* FirstController = PuzzleSystemTest::BuildSingleInputController(FirstEmitterActor, ChainActor);
	APuzzleController* SecondController = PuzzleSystemTest::NewController(TEXT("SecondController"));
	PuzzleSystemTest::BindInput(SecondController, TEXT("Completed"), ChainActor, TEXT("Emitter"), TAG_PuzzleSystemTestCompleted);
	PuzzleSystemTest::BindReceiver(SecondController, FinalReceiverActor);
	SecondController->RootCondition = PuzzleSystemTest::NewInputCondition(SecondController, TEXT("Completed"));

	TestTrue(TEXT("First controller initializes"), FirstController->InitializePuzzleController());
	TestTrue(TEXT("Second controller initializes"), SecondController->InitializePuzzleController());

	FirstEmitter->SetSignalState(TAG_PuzzleSystemTestPressed, true, nullptr);
	TestTrue(TEXT("Reentrant receiver activated"), ReentrantReceiver->IsReceiverActive());
	FPuzzleSignalState ChainedSignalState;
	TestTrue(TEXT("Reentrant receiver published chained signal"), ChainEmitter->TryGetSignalState(TAG_PuzzleSystemTestCompleted, ChainedSignalState));
	TestTrue(TEXT("Final receiver activated by chained signal"), FinalReceiver->IsReceiverActive());
	TestEqual(TEXT("Receiver state delegate fired once"), Observer->StateChangedCount, 1);
	TestTrue(TEXT("Receiver state delegate reported active"), Observer->bLastActive);
	TestEqual(TEXT("Reentrant publish happened once for stable state"), ReentrantReceiver->PublishCount, 1);

	return true;
}

#endif
