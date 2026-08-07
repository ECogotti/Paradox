#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Conditions/PuzzleAllCondition.h"
#include "Conditions/PuzzleAnyCondition.h"
#include "Conditions/PuzzleInputStateCondition.h"
#include "Conditions/PuzzleNotCondition.h"
#include "Controllers/PuzzleController.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/Blueprint.h"
#include "PuzzleSystemTestTypes.h"
#include "Receivers/PuzzleReceiverComponent.h"

namespace PuzzleControllerGateTest
{
	static const FGameplayTag& PressedTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Puzzle.Test.Pressed"));
		return Tag;
	}

	static const FGameplayTag& PoweredTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Puzzle.Test.Powered"));
		return Tag;
	}

	static AActor* NewActor(FName Name)
	{
		return NewObject<AActor>(
			GetTransientPackage(),
			AActor::StaticClass(),
			MakeUniqueObjectName(GetTransientPackage(), AActor::StaticClass(), Name));
	}

	template <typename ComponentType>
	ComponentType* AddComponent(AActor* Owner, FName Name)
	{
		ComponentType* Component = NewObject<ComponentType>(Owner, ComponentType::StaticClass(), Name);
		Owner->AddInstanceComponent(Component);
		return Component;
	}

	static APuzzleController* NewController(FName Name)
	{
		return NewObject<APuzzleController>(
			GetTransientPackage(),
			APuzzleController::StaticClass(),
			MakeUniqueObjectName(GetTransientPackage(), APuzzleController::StaticClass(), Name));
	}

	static UPuzzleInputStateCondition* NewInputCondition(UObject* Outer, FName InputId, bool bExpectedActive = true)
	{
		UPuzzleInputStateCondition* Condition = NewObject<UPuzzleInputStateCondition>(Outer);
		Condition->InputId = InputId;
		Condition->bExpectedActive = bExpectedActive;
		return Condition;
	}

	static FPuzzleInputBinding& AddPrimaryInput(
		APuzzleController* Controller,
		FName InputId,
		AActor* EmitterActor,
		FGameplayTag SignalTag = PressedTag())
	{
		FPuzzleInputBinding& Binding = Controller->InputBindings.AddDefaulted_GetRef();
		Binding.InputId = InputId;
		Binding.EmitterActor = EmitterActor;
		Binding.SignalTag = SignalTag;
		return Binding;
	}

	static FPuzzleEmitterGateBinding& AddGateInput(
		FPuzzleInputBinding& PrimaryBinding,
		FName InputId,
		AActor* EmitterActor,
		FGameplayTag SignalTag = PoweredTag())
	{
		FPuzzleEmitterGateBinding& GateBinding = PrimaryBinding.EmitterGates.AddDefaulted_GetRef();
		GateBinding.InputId = InputId;
		GateBinding.EmitterActor = EmitterActor;
		GateBinding.SignalTag = SignalTag;
		return GateBinding;
	}

	static void BindReceiver(APuzzleController* Controller, AActor* ReceiverActor)
	{
		FPuzzleReceiverBinding& Binding = Controller->ReceiverBindings.AddDefaulted_GetRef();
		Binding.ReceiverActor = ReceiverActor;
	}

	static APuzzleController* BuildSingleInputController(
		AActor* PrimaryActor,
		AActor* ReceiverActor,
		FName PrimaryInputId = TEXT("Main"),
		bool bExpectedPrimaryActive = true)
	{
		APuzzleController* Controller = NewController(TEXT("GatedController"));
		AddPrimaryInput(Controller, PrimaryInputId, PrimaryActor);
		BindReceiver(Controller, ReceiverActor);
		Controller->RootCondition = NewInputCondition(Controller, PrimaryInputId, bExpectedPrimaryActive);
		return Controller;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleControllerGateBypassTest,
	"PuzzleSystem.Controller.InputGates.BypassAndDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleControllerGateBypassTest::RunTest(const FString& Parameters)
{
	using namespace PuzzleControllerGateTest;
	FPuzzleInputBinding DefaultBinding;
	TestTrue(TEXT("New primary bindings default to no gate Emitters"), DefaultBinding.EmitterGates.IsEmpty());
	TestTrue(TEXT("New primary bindings default to no gate Conditions"), DefaultBinding.GateConditions.IsEmpty());

	AActor* PrimaryActor = NewActor(TEXT("PrimaryActor"));
	UPuzzleEmitterComponent* PrimaryEmitter = AddComponent<UPuzzleEmitterComponent>(PrimaryActor, TEXT("Emitter"));
	AActor* ReceiverActor = NewActor(TEXT("ReceiverActor"));
	UPuzzleReceiverComponent* Receiver = AddComponent<UPuzzleReceiverComponent>(ReceiverActor, TEXT("Receiver"));
	PrimaryEmitter->SetSignalState(PressedTag(), true, nullptr);

	APuzzleController* EmptyGateController = BuildSingleInputController(PrimaryActor, ReceiverActor);
	TestTrue(TEXT("Controller without gate configuration initializes"), EmptyGateController->InitializePuzzleController());
	TestTrue(TEXT("Controller without gates admits its primary signal"), Receiver->IsReceiverActive());
	TestTrue(TEXT("Empty gate configuration reports bypass"), EmptyGateController->IsInputGateBypassed(TEXT("Main")));
	TestTrue(TEXT("A bypassed gate reports valid"), EmptyGateController->IsInputGateValid(TEXT("Main")));
	TestTrue(TEXT("A bypassed gate allows its signal"), EmptyGateController->DoesInputGateAllowSignal(TEXT("Main")));
	EmptyGateController->ShutdownPuzzleController();

	APuzzleController* GatesOnlyController = BuildSingleInputController(PrimaryActor, ReceiverActor);
	FPuzzleInputBinding& GatesOnlyBinding = GatesOnlyController->InputBindings[0];
	FPuzzleEmitterGateBinding& IgnoredInvalidGate = GatesOnlyBinding.EmitterGates.AddDefaulted_GetRef();
	IgnoredInvalidGate.InputId = NAME_None;
	TestTrue(TEXT("Gate-only configuration ignores invalid unpaired gate data"), GatesOnlyController->InitializePuzzleController());
	TestTrue(TEXT("Gate-only configuration remains admitted"), Receiver->IsReceiverActive());
	TestTrue(TEXT("Gate-only configuration reports bypass"), GatesOnlyController->IsInputGateBypassed(TEXT("Main")));
	FPuzzleSignalState IgnoredGateState;
	TestFalse(TEXT("Bypassed gate inputs are not subscribed or cached"), GatesOnlyController->TryGetGateInputState(TEXT("Main"), NAME_None, IgnoredGateState));
	GatesOnlyController->ShutdownPuzzleController();

	APuzzleController* ConditionsOnlyController = BuildSingleInputController(PrimaryActor, ReceiverActor);
	ConditionsOnlyController->InputBindings[0].GateConditions.Add(nullptr);
	TestTrue(TEXT("Condition-only configuration ignores invalid unpaired condition data"), ConditionsOnlyController->InitializePuzzleController());
	TestTrue(TEXT("Condition-only configuration remains admitted"), Receiver->IsReceiverActive());
	TestTrue(TEXT("Condition-only configuration reports bypass"), ConditionsOnlyController->IsInputGateBypassed(TEXT("Main")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleControllerGateEffectiveStateTest,
	"PuzzleSystem.Controller.InputGates.EffectiveStateAndRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleControllerGateEffectiveStateTest::RunTest(const FString& Parameters)
{
	using namespace PuzzleControllerGateTest;
	AActor* PrimaryActor = NewActor(TEXT("PrimaryActor"));
	UPuzzleEmitterComponent* PrimaryEmitter = AddComponent<UPuzzleEmitterComponent>(PrimaryActor, TEXT("Emitter"));
	AActor* GateActor = NewActor(TEXT("GateActor"));
	UPuzzleEmitterComponent* GateEmitter = AddComponent<UPuzzleEmitterComponent>(GateActor, TEXT("Emitter"));
	AActor* ReceiverActor = NewActor(TEXT("ReceiverActor"));
	UPuzzleReceiverComponent* Receiver = AddComponent<UPuzzleReceiverComponent>(ReceiverActor, TEXT("Receiver"));

	UPuzzleTestSignalPayload* PrimaryPayload = NewObject<UPuzzleTestSignalPayload>(PrimaryEmitter);
	PrimaryPayload->Value = 42;
	PrimaryEmitter->SetSignalState(PressedTag(), true, PrimaryPayload);
	GateEmitter->SetSignalState(PoweredTag(), true, nullptr);

	APuzzleController* Controller = BuildSingleInputController(PrimaryActor, ReceiverActor);
	FPuzzleInputBinding& Binding = Controller->InputBindings[0];
	AddGateInput(Binding, TEXT("Enabled"), GateActor);
	Binding.GateConditions.Add(NewInputCondition(Controller, TEXT("Enabled")));

	TestTrue(TEXT("Enabled gate configuration initializes"), Controller->InitializePuzzleController());
	TestFalse(TEXT("Enabled gate is not bypassed"), Controller->IsInputGateBypassed(TEXT("Main")));
	TestTrue(TEXT("Initially open gate is valid"), Controller->IsInputGateValid(TEXT("Main")));
	TestTrue(TEXT("Initially open gate allows the primary signal"), Controller->DoesInputGateAllowSignal(TEXT("Main")));
	TestTrue(TEXT("Open gate activates the Receiver"), Receiver->IsReceiverActive());
	TestTrue(TEXT("Open gate exposes admitted primary payload"), Controller->GetInputPayload(TEXT("Main")) == PrimaryPayload);

	FPuzzleSignalState RawState;
	TestTrue(TEXT("Raw primary state is available to C++ diagnostics"), Controller->TryGetRawInputState(TEXT("Main"), RawState));
	TestTrue(TEXT("Raw state retains its payload"), RawState.Payload == PrimaryPayload);

	GateEmitter->SetSignalState(PoweredTag(), false, nullptr);
	TestTrue(TEXT("Closed gate remains valid"), Controller->IsInputGateValid(TEXT("Main")));
	TestFalse(TEXT("Closed gate no longer allows the primary signal"), Controller->DoesInputGateAllowSignal(TEXT("Main")));
	TestFalse(TEXT("Closed gate deactivates the Receiver without a primary republish"), Receiver->IsReceiverActive());
	TestNull(TEXT("Closed gate suppresses primary payload access"), Controller->GetInputPayload(TEXT("Main")));
	FPuzzleSignalState ClosedEffectiveState;
	TestTrue(TEXT("A valid closed gate leaves the effective input valid"), Controller->TryGetEffectiveInputState(TEXT("Main"), ClosedEffectiveState));
	TestFalse(TEXT("A valid closed gate exposes the effective input as inactive"), ClosedEffectiveState.bIsActive);
	const int64 ClosedRevision = ClosedEffectiveState.Revision;

	PrimaryEmitter->RepublishSignal(PressedTag());
	FPuzzleSignalState ClosedAfterRawRepublish;
	Controller->TryGetEffectiveInputState(TEXT("Main"), ClosedAfterRawRepublish);
	TestEqual(TEXT("Raw republish remains suppressed while the gate is closed"), ClosedAfterRawRepublish.Revision, ClosedRevision);

	GateEmitter->SetSignalState(PoweredTag(), true, nullptr);
	TestTrue(TEXT("Reopening a gate reactivates without a primary republish"), Receiver->IsReceiverActive());
	TestTrue(TEXT("Reopening restores primary payload admission"), Controller->GetInputPayload(TEXT("Main")) == PrimaryPayload);
	TestTrue(TEXT("Reopening advances effective revision"), Controller->GetInputRevision(TEXT("Main")) > ClosedRevision);
	Controller->ShutdownPuzzleController();

	GateEmitter->SetSignalState(PoweredTag(), false, nullptr);
	APuzzleController* InactiveExpectedController = BuildSingleInputController(PrimaryActor, ReceiverActor, TEXT("Main"), false);
	FPuzzleInputBinding& InactiveBinding = InactiveExpectedController->InputBindings[0];
	AddGateInput(InactiveBinding, TEXT("Enabled"), GateActor);
	InactiveBinding.GateConditions.Add(NewInputCondition(InactiveExpectedController, TEXT("Enabled")));
	TestTrue(TEXT("Inactive-expected fixture initializes"), InactiveExpectedController->InitializePuzzleController());
	TestTrue(TEXT("A valid closed gate can satisfy a normal inactive condition"), Receiver->IsReceiverActive());

	GateEmitter->OnComponentDestroyed(false);
	TestFalse(TEXT("Destroyed gate source invalidates the effective input instead of satisfying inactive"), Receiver->IsReceiverActive());
	TestFalse(TEXT("Destroyed gate source reports invalid"), InactiveExpectedController->IsInputGateValid(TEXT("Main")));
	FPuzzleSignalState InvalidEffectiveState;
	TestFalse(TEXT("Destroyed gate source makes effective state invalid"), InactiveExpectedController->TryGetEffectiveInputState(TEXT("Main"), InvalidEffectiveState));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleControllerGateConditionsTest,
	"PuzzleSystem.Controller.InputGates.ConditionScopes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleControllerGateConditionsTest::RunTest(const FString& Parameters)
{
	using namespace PuzzleControllerGateTest;
	AActor* PrimaryActor = NewActor(TEXT("PrimaryActor"));
	UPuzzleEmitterComponent* PrimaryEmitter = AddComponent<UPuzzleEmitterComponent>(PrimaryActor, TEXT("Emitter"));
	PrimaryEmitter->SetSignalState(PressedTag(), true, nullptr);
	AActor* EnabledActor = NewActor(TEXT("EnabledActor"));
	UPuzzleEmitterComponent* EnabledEmitter = AddComponent<UPuzzleEmitterComponent>(EnabledActor, TEXT("Emitter"));
	EnabledEmitter->SetSignalState(PoweredTag(), true, nullptr);
	AActor* ReadyActor = NewActor(TEXT("ReadyActor"));
	UPuzzleEmitterComponent* ReadyEmitter = AddComponent<UPuzzleEmitterComponent>(ReadyActor, TEXT("Emitter"));
	ReadyEmitter->SetSignalState(PoweredTag(), false, nullptr);
	AActor* ReceiverActor = NewActor(TEXT("ReceiverActor"));
	UPuzzleReceiverComponent* Receiver = AddComponent<UPuzzleReceiverComponent>(ReceiverActor, TEXT("Receiver"));

	APuzzleController* AggregationController = BuildSingleInputController(PrimaryActor, ReceiverActor);
	FPuzzleInputBinding& AggregationBinding = AggregationController->InputBindings[0];
	AddGateInput(AggregationBinding, TEXT("Enabled"), EnabledActor);
	AddGateInput(AggregationBinding, TEXT("Ready"), ReadyActor);
	AggregationBinding.GateConditions.Add(NewInputCondition(AggregationController, TEXT("Enabled")));
	UPuzzleAnyCondition* CompositeCondition = NewObject<UPuzzleAnyCondition>(AggregationController);
	CompositeCondition->Conditions.Add(NewInputCondition(CompositeCondition, TEXT("Ready")));
	CompositeCondition->Conditions.Add(NewInputCondition(CompositeCondition, TEXT("Enabled"), false));
	AggregationBinding.GateConditions.Add(CompositeCondition);

	TestTrue(TEXT("Top-level AND and composite gate fixture initializes"), AggregationController->InitializePuzzleController());
	TestFalse(TEXT("One false top-level gate condition closes the gate"), Receiver->IsReceiverActive());
	ReadyEmitter->SetSignalState(PoweredTag(), true, nullptr);
	TestTrue(TEXT("All top-level gate conditions passing opens the gate"), Receiver->IsReceiverActive());
	AggregationController->ShutdownPuzzleController();

	AActor* PrimaryActorB = NewActor(TEXT("PrimaryActorB"));
	UPuzzleEmitterComponent* PrimaryEmitterB = AddComponent<UPuzzleEmitterComponent>(PrimaryActorB, TEXT("Emitter"));
	PrimaryEmitterB->SetSignalState(PressedTag(), true, nullptr);
	AActor* GateActorA = NewActor(TEXT("GateActorA"));
	UPuzzleEmitterComponent* GateEmitterA = AddComponent<UPuzzleEmitterComponent>(GateActorA, TEXT("Emitter"));
	GateEmitterA->SetSignalState(PoweredTag(), true, nullptr);
	AActor* GateActorB = NewActor(TEXT("GateActorB"));
	UPuzzleEmitterComponent* GateEmitterB = AddComponent<UPuzzleEmitterComponent>(GateActorB, TEXT("Emitter"));
	GateEmitterB->SetSignalState(PoweredTag(), false, nullptr);

	APuzzleController* LocalNamespaceController = NewController(TEXT("LocalNamespaceController"));
	FPuzzleInputBinding& BindingA = AddPrimaryInput(LocalNamespaceController, TEXT("MainA"), PrimaryActor);
	AddGateInput(BindingA, TEXT("Enabled"), GateActorA);
	BindingA.GateConditions.Add(NewInputCondition(LocalNamespaceController, TEXT("Enabled")));
	FPuzzleInputBinding& BindingB = AddPrimaryInput(LocalNamespaceController, TEXT("MainB"), PrimaryActorB);
	AddGateInput(BindingB, TEXT("Enabled"), GateActorB);
	BindingB.GateConditions.Add(NewInputCondition(LocalNamespaceController, TEXT("Enabled")));
	BindReceiver(LocalNamespaceController, ReceiverActor);
	UPuzzleAllCondition* RootAll = NewObject<UPuzzleAllCondition>(LocalNamespaceController);
	RootAll->Conditions.Add(NewInputCondition(RootAll, TEXT("MainA"), true));
	RootAll->Conditions.Add(NewInputCondition(RootAll, TEXT("MainB"), false));
	LocalNamespaceController->RootCondition = RootAll;

	TestTrue(TEXT("Identical gate-local IDs in different primary bindings initialize"), LocalNamespaceController->InitializePuzzleController());
	TestTrue(TEXT("Each primary binding resolves its own local Enabled gate"), Receiver->IsReceiverActive());
	GateEmitterB->SetSignalState(PoweredTag(), true, nullptr);
	TestFalse(TEXT("Changing the second local Enabled gate affects only its owning primary input"), Receiver->IsReceiverActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleControllerGatePayloadTest,
	"PuzzleSystem.Controller.InputGates.PayloadAndLateInitialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleControllerGatePayloadTest::RunTest(const FString& Parameters)
{
	using namespace PuzzleControllerGateTest;
	AActor* PrimaryActor = NewActor(TEXT("PrimaryActor"));
	UPuzzleEmitterComponent* PrimaryEmitter = AddComponent<UPuzzleEmitterComponent>(PrimaryActor, TEXT("Emitter"));
	PrimaryEmitter->SetSignalState(PressedTag(), true, nullptr);
	AActor* GateActor = NewActor(TEXT("GateActor"));
	UPuzzleEmitterComponent* GateEmitter = AddComponent<UPuzzleEmitterComponent>(GateActor, TEXT("Emitter"));
	UPuzzleTestSignalPayload* GatePayload = NewObject<UPuzzleTestSignalPayload>(GateEmitter);
	GatePayload->Value = 7;
	GateEmitter->SetSignalState(PoweredTag(), true, GatePayload);
	AActor* ReceiverActor = NewActor(TEXT("ReceiverActor"));
	UPuzzleReceiverComponent* Receiver = AddComponent<UPuzzleReceiverComponent>(ReceiverActor, TEXT("Receiver"));

	APuzzleController* Controller = BuildSingleInputController(PrimaryActor, ReceiverActor);
	FPuzzleInputBinding& Binding = Controller->InputBindings[0];
	AddGateInput(Binding, TEXT("Permission"), GateActor);
	UPuzzleTestPayloadCondition* PayloadCondition = NewObject<UPuzzleTestPayloadCondition>(Controller);
	PayloadCondition->InputId = TEXT("Permission");
	PayloadCondition->ExpectedValue = 7;
	Binding.GateConditions.Add(PayloadCondition);

	TestTrue(TEXT("Controller initializes from primary and gate states published before startup"), Controller->InitializePuzzleController());
	TestTrue(TEXT("Payload-aware gate condition reads its local typed payload"), Receiver->IsReceiverActive());
	GatePayload->Value = 9;
	TestTrue(TEXT("Gate payload republish succeeds"), GateEmitter->RepublishSignal(PoweredTag()));
	TestFalse(TEXT("Republished gate payload reevaluates admission"), Receiver->IsReceiverActive());
	GatePayload->Value = 7;
	GateEmitter->RepublishSignal(PoweredTag());
	TestTrue(TEXT("Restored gate payload reopens the primary input"), Receiver->IsReceiverActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleControllerGateRoutingAndReentrancyTest,
	"PuzzleSystem.Controller.InputGates.SharedRoutingAndReentrancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleControllerGateRoutingAndReentrancyTest::RunTest(const FString& Parameters)
{
	using namespace PuzzleControllerGateTest;
	AActor* SharedActor = NewActor(TEXT("SharedActor"));
	UPuzzleEmitterComponent* SharedEmitter = AddComponent<UPuzzleEmitterComponent>(SharedActor, TEXT("Emitter"));
	SharedEmitter->SetSignalState(PoweredTag(), false, nullptr);
	AActor* OtherPrimaryActor = NewActor(TEXT("OtherPrimaryActor"));
	UPuzzleEmitterComponent* OtherPrimaryEmitter = AddComponent<UPuzzleEmitterComponent>(OtherPrimaryActor, TEXT("Emitter"));
	OtherPrimaryEmitter->SetSignalState(PressedTag(), true, nullptr);
	AActor* ReceiverActor = NewActor(TEXT("ReceiverActor"));
	UPuzzleReceiverComponent* Receiver = AddComponent<UPuzzleReceiverComponent>(ReceiverActor, TEXT("Receiver"));

	APuzzleController* SharedController = NewController(TEXT("SharedController"));
	FPuzzleInputBinding& SharedPrimaryBinding = AddPrimaryInput(SharedController, TEXT("SharedPrimary"), SharedActor, PoweredTag());
	AddGateInput(SharedPrimaryBinding, TEXT("Enabled"), SharedActor, PoweredTag());
	SharedPrimaryBinding.GateConditions.Add(NewInputCondition(SharedController, TEXT("Enabled")));
	FPuzzleInputBinding& OtherPrimaryBinding = AddPrimaryInput(SharedController, TEXT("OtherPrimary"), OtherPrimaryActor);
	AddGateInput(OtherPrimaryBinding, TEXT("Enabled"), SharedActor, PoweredTag());
	OtherPrimaryBinding.GateConditions.Add(NewInputCondition(SharedController, TEXT("Enabled")));
	BindReceiver(SharedController, ReceiverActor);
	UPuzzleTestCountingCondition* CountingCondition = NewObject<UPuzzleTestCountingCondition>(SharedController);
	CountingCondition->FirstInputId = TEXT("SharedPrimary");
	CountingCondition->SecondInputId = TEXT("OtherPrimary");
	SharedController->RootCondition = CountingCondition;

	TestTrue(TEXT("Shared primary/gate source fixture initializes"), SharedController->InitializePuzzleController());
	TestEqual(TEXT("Initialization evaluates root once"), CountingCondition->EvaluationCount, 1);
	SharedEmitter->SetSignalState(PoweredTag(), true, nullptr);
	TestTrue(TEXT("One shared signal update refreshes every primary and gate destination"), Receiver->IsReceiverActive());
	TestEqual(TEXT("One emitter subscription produces one collapsed root evaluation"), CountingCondition->EvaluationCount, 2);
	SharedController->ShutdownPuzzleController();

	AActor* ReentrantPrimaryActor = NewActor(TEXT("ReentrantPrimaryActor"));
	UPuzzleEmitterComponent* ReentrantPrimaryEmitter = AddComponent<UPuzzleEmitterComponent>(ReentrantPrimaryActor, TEXT("Emitter"));
	ReentrantPrimaryEmitter->SetSignalState(PressedTag(), true, nullptr);
	AActor* ReentrantGateActor = NewActor(TEXT("ReentrantGateActor"));
	UPuzzleEmitterComponent* ReentrantGateEmitter = AddComponent<UPuzzleEmitterComponent>(ReentrantGateActor, TEXT("Emitter"));
	ReentrantGateEmitter->SetSignalState(PoweredTag(), true, nullptr);
	AActor* ReentrantReceiverActor = NewActor(TEXT("ReentrantReceiverActor"));
	UPuzzleReentrantReceiverComponent* ReentrantReceiver = AddComponent<UPuzzleReentrantReceiverComponent>(ReentrantReceiverActor, TEXT("Receiver"));
	ReentrantReceiver->EmitterToPublish = ReentrantGateEmitter;
	ReentrantReceiver->SignalTagToPublish = PoweredTag();
	ReentrantReceiver->bPublishedState = false;

	APuzzleController* ReentrantController = BuildSingleInputController(ReentrantPrimaryActor, ReentrantReceiverActor);
	FPuzzleInputBinding& ReentrantBinding = ReentrantController->InputBindings[0];
	AddGateInput(ReentrantBinding, TEXT("Enabled"), ReentrantGateActor);
	ReentrantBinding.GateConditions.Add(NewInputCondition(ReentrantController, TEXT("Enabled")));
	TestTrue(TEXT("Reentrant gate fixture initializes and settles"), ReentrantController->InitializePuzzleController());
	TestEqual(TEXT("Receiver activation published one reentrant gate update"), ReentrantReceiver->PublishCount, 1);
	TestFalse(TEXT("Collapsed reevaluation settles with the gate closed"), ReentrantReceiver->IsReceiverActive());
	TestFalse(TEXT("Reentrant gate update preserved the correct condition scope"), ReentrantController->DoesInputGateAllowSignal(TEXT("Main")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleControllerGateValidationAndDuplicationTest,
	"PuzzleSystem.Controller.InputGates.ValidationAndDuplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleControllerGateValidationAndDuplicationTest::RunTest(const FString& Parameters)
{
	using namespace PuzzleControllerGateTest;
	AActor* PrimaryActor = NewActor(TEXT("PrimaryActor"));
	UPuzzleEmitterComponent* PrimaryEmitter = AddComponent<UPuzzleEmitterComponent>(PrimaryActor, TEXT("Emitter"));
	PrimaryEmitter->SetSignalState(PressedTag(), true, nullptr);
	AActor* ReceiverActor = NewActor(TEXT("ReceiverActor"));
	UPuzzleReceiverComponent* Receiver = AddComponent<UPuzzleReceiverComponent>(ReceiverActor, TEXT("Receiver"));

	APuzzleController* MissingGateController = BuildSingleInputController(PrimaryActor, ReceiverActor, TEXT("Main"), false);
	FPuzzleInputBinding& MissingGateBinding = MissingGateController->InputBindings[0];
	AddGateInput(MissingGateBinding, TEXT("Missing"), nullptr);
	MissingGateBinding.GateConditions.Add(NewInputCondition(MissingGateController, TEXT("Missing")));
	AddExpectedError(TEXT("has no EmitterActor"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Enabled gate with a missing source fails initialization"), MissingGateController->InitializePuzzleController());
	TestFalse(TEXT("Invalid enabled gate cannot accidentally satisfy an inactive root condition"), Receiver->IsReceiverActive());

	AActor* GateActor = NewActor(TEXT("GateActor"));
	UPuzzleEmitterComponent* GateEmitter = AddComponent<UPuzzleEmitterComponent>(GateActor, TEXT("Emitter"));
	GateEmitter->SetSignalState(PoweredTag(), true, nullptr);
	APuzzleController* UnknownIdController = BuildSingleInputController(PrimaryActor, ReceiverActor);
	FPuzzleInputBinding& UnknownIdBinding = UnknownIdController->InputBindings[0];
	AddGateInput(UnknownIdBinding, TEXT("Known"), GateActor);
	UnknownIdBinding.GateConditions.Add(NewInputCondition(UnknownIdController, TEXT("Unknown")));
	AddExpectedError(TEXT("references unknown gate-local InputId 'Unknown'"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Gate condition referencing an unknown local ID fails initialization"), UnknownIdController->InitializePuzzleController());

	APuzzleController* ForeignOwnershipController = BuildSingleInputController(PrimaryActor, ReceiverActor);
	FPuzzleInputBinding& ForeignOwnershipBinding = ForeignOwnershipController->InputBindings[0];
	AddGateInput(ForeignOwnershipBinding, TEXT("Enabled"), GateActor);
	ForeignOwnershipBinding.GateConditions.Add(NewInputCondition(GetTransientPackage(), TEXT("Enabled")));
	AddExpectedError(TEXT("is not an instanced object owned by this Controller"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Gate condition not instanced under its Controller fails ownership validation"), ForeignOwnershipController->InitializePuzzleController());

	APuzzleController* SourceController = BuildSingleInputController(PrimaryActor, ReceiverActor);
	FPuzzleInputBinding& SourceBinding = SourceController->InputBindings[0];
	AddGateInput(SourceBinding, TEXT("Enabled"), GateActor);
	SourceBinding.GateConditions.Add(NewInputCondition(SourceController, TEXT("Enabled")));
	APuzzleController* DuplicateController = DuplicateObject<APuzzleController>(SourceController, GetTransientPackage());
	TestNotNull(TEXT("Controller with instanced gate conditions duplicates"), DuplicateController);
	if (DuplicateController)
	{
		TestEqual(TEXT("Duplicate preserves one gate condition"), DuplicateController->InputBindings[0].GateConditions.Num(), 1);
		TestTrue(
			TEXT("Duplicate owns a distinct gate condition instance"),
			DuplicateController->InputBindings[0].GateConditions[0] != SourceController->InputBindings[0].GateConditions[0]);
		TestTrue(
			TEXT("Duplicated gate condition is contained by the duplicated Controller"),
			DuplicateController->InputBindings[0].GateConditions[0]->IsIn(DuplicateController));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleControllerGateSerializedAssetCompatibilityTest,
	"PuzzleSystem.Controller.InputGates.SerializedAssetCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleControllerGateSerializedAssetCompatibilityTest::RunTest(const FString& Parameters)
{
	UBlueprint* ExistingControllerBlueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/PuzzleSystem/Blueprints/BP_PuzzleController_Test.BP_PuzzleController_Test"));
	if (!TestNotNull(TEXT("Existing PuzzleSystem Controller Blueprint loads"), ExistingControllerBlueprint))
	{
		return false;
	}

	TestTrue(
		TEXT("Existing Controller Blueprint still generates an APuzzleController class"),
		ExistingControllerBlueprint->GeneratedClass
			&& ExistingControllerBlueprint->GeneratedClass->IsChildOf(APuzzleController::StaticClass()));
	const APuzzleController* ControllerDefaults = ExistingControllerBlueprint->GeneratedClass
		? Cast<APuzzleController>(ExistingControllerBlueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (!TestNotNull(TEXT("Existing Controller Blueprint has valid class defaults"), ControllerDefaults))
	{
		return false;
	}

	for (int32 BindingIndex = 0; BindingIndex < ControllerDefaults->InputBindings.Num(); ++BindingIndex)
	{
		TestTrue(
			*FString::Printf(TEXT("Existing input binding %d defaults to no gate Emitters"), BindingIndex),
			ControllerDefaults->InputBindings[BindingIndex].EmitterGates.IsEmpty());
		TestTrue(
			*FString::Printf(TEXT("Existing input binding %d defaults to no gate Conditions"), BindingIndex),
			ControllerDefaults->InputBindings[BindingIndex].GateConditions.IsEmpty());
	}
	return true;
}

#endif
