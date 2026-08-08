#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Controllers/PuzzleController.h"
#include "GameFramework/Actor.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Tests/PuzzleSystemTestTypes.h"
#include "UObject/UnrealType.h"

namespace UE::PuzzleSystem::Receiver::Tests
{
	template <typename ComponentType = UPuzzleReceiverComponent>
	ComponentType* AddReceiver(AActor& Owner, const FName Name)
	{
		ComponentType* Receiver = NewObject<ComponentType>(&Owner, ComponentType::StaticClass(), Name);
		Owner.AddInstanceComponent(Receiver);
		return Receiver;
	}

	AActor* NewActor(const FName Name)
	{
		return NewObject<AActor>(
			GetTransientPackage(),
			AActor::StaticClass(),
			MakeUniqueObjectName(GetTransientPackage(), AActor::StaticClass(), Name));
	}

	APuzzleController* NewController(const FName Name)
	{
		return NewObject<APuzzleController>(
			GetTransientPackage(),
			APuzzleController::StaticClass(),
			MakeUniqueObjectName(GetTransientPackage(), APuzzleController::StaticClass(), Name));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleReceiverAutomaticCompatibilityTest,
	"PuzzleSystem.Receiver.ActivationMode.AutomaticCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleReceiverAutomaticCompatibilityTest::RunTest(const FString& Parameters)
{
	using namespace UE::PuzzleSystem::Receiver::Tests;
	AActor* Owner = NewActor(TEXT("AutomaticReceiverOwner"));
	UPuzzleReceiverComponent* Receiver = AddReceiver(*Owner, TEXT("Receiver"));
	APuzzleController* Controller = NewController(TEXT("AutomaticController"));

	TestEqual(
		TEXT("Receivers preserve Automatic as their native default"),
		Receiver->GetActivationMode(),
		EPuzzleReceiverActivationMode::Automatic);
	TestTrue(TEXT("Automatic Receiver accepts an active Controller request"),
		Receiver->SetControllerRequest(Controller, true));
	TestTrue(TEXT("Automatic Receiver exposes satisfied prerequisites"),
		Receiver->AreActivationPrerequisitesSatisfied());
	TestTrue(TEXT("Automatic Receiver preserves historical immediate activation"),
		Receiver->IsReceiverActive());
	TestFalse(TEXT("Automatic Receiver never owns a manual latch"),
		Receiver->IsManualActivationRequested());
	TestEqual(
		TEXT("Manual commands reject Automatic Receivers"),
		Receiver->RequestManualActivation().Status,
		EPuzzleReceiverActivationCommandStatus::NotInManualMode);
	TestTrue(TEXT("Removing the final request changes Automatic state"),
		Receiver->SetControllerRequest(Controller, false));
	TestFalse(TEXT("Automatic Receiver deactivates with its final prerequisite"),
		Receiver->IsReceiverActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleReceiverManualLifecycleTest,
	"PuzzleSystem.Receiver.ActivationMode.ManualLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleReceiverManualLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace UE::PuzzleSystem::Receiver::Tests;
	AActor* Owner = NewActor(TEXT("ManualReceiverOwner"));
	UPuzzleReceiverComponent* Receiver = AddReceiver(*Owner, TEXT("Receiver"));
	Receiver->ActivationMode = EPuzzleReceiverActivationMode::Manual;
	APuzzleController* Controller = NewController(TEXT("ManualController"));
	UPuzzleReceiverTestObserver* Observer = NewObject<UPuzzleReceiverTestObserver>();
	Receiver->OnReceiverStateChanged.AddDynamic(
		Observer,
		&UPuzzleReceiverTestObserver::HandleReceiverStateChanged);
	Receiver->OnReceiverActivationPrerequisitesChanged.AddDynamic(
		Observer,
		&UPuzzleReceiverTestObserver::HandleReceiverPrerequisitesChanged);

	const FPuzzleReceiverActivationCommandResult RejectedOpen =
		Receiver->RequestManualActivation();
	TestEqual(TEXT("Open rejects missing prerequisites"), RejectedOpen.Status,
		EPuzzleReceiverActivationCommandStatus::PrerequisitesNotSatisfied);
	TestFalse(TEXT("Rejected Open does not latch manual intent"),
		RejectedOpen.bManualActivationRequested);

	TestTrue(TEXT("First Controller request changes prerequisite state"),
		Receiver->SetControllerRequest(Controller, true));
	TestTrue(TEXT("Manual Receiver exposes current prerequisites"),
		Receiver->AreActivationPrerequisitesSatisfied());
	TestTrue(TEXT("Open is available after prerequisites become valid"),
		Receiver->CanRequestManualActivation());
	TestFalse(TEXT("Prerequisites alone do not activate a Manual Receiver"),
		Receiver->IsReceiverActive());
	TestEqual(TEXT("Prerequisite delegate fires once"), Observer->PrerequisitesChangedCount, 1);

	const FPuzzleReceiverActivationCommandResult OpenResult = Receiver->RequestManualActivation();
	TestEqual(TEXT("Open applies in Manual mode"), OpenResult.Status,
		EPuzzleReceiverActivationCommandStatus::Applied);
	TestTrue(TEXT("Open latches manual intent"), OpenResult.bManualActivationRequested);
	TestTrue(TEXT("Open activates the Receiver"), OpenResult.bReceiverActive);
	TestEqual(TEXT("Repeated Open is idempotent"),
		Receiver->RequestManualActivation().Status,
		EPuzzleReceiverActivationCommandStatus::AlreadyInRequestedState);

	const FPuzzleReceiverActivationCommandResult CloseResult = Receiver->RequestManualDeactivation();
	TestEqual(TEXT("Close applies without changing prerequisites"), CloseResult.Status,
		EPuzzleReceiverActivationCommandStatus::Applied);
	TestFalse(TEXT("Close clears manual intent"), CloseResult.bManualActivationRequested);
	TestFalse(TEXT("Close deactivates the Receiver"), CloseResult.bReceiverActive);
	TestEqual(TEXT("Repeated Close is idempotent"),
		Receiver->RequestManualDeactivation().Status,
		EPuzzleReceiverActivationCommandStatus::AlreadyInRequestedState);

	Receiver->RequestManualActivation();
	TestTrue(TEXT("Receiver reopens while prerequisites remain valid"), Receiver->IsReceiverActive());
	Receiver->SetControllerRequest(Controller, false);
	TestFalse(TEXT("Lost prerequisites close a Manual Receiver"), Receiver->IsReceiverActive());
	TestFalse(TEXT("Lost prerequisites clear the manual latch"),
		Receiver->IsManualActivationRequested());
	TestFalse(TEXT("Lost prerequisites are observable"),
		Receiver->AreActivationPrerequisitesSatisfied());
	Receiver->SetControllerRequest(Controller, true);
	TestFalse(TEXT("Restored prerequisites do not reopen without a new command"),
		Receiver->IsReceiverActive());
	TestTrue(TEXT("A fresh Open is required after prerequisite restoration"),
		Receiver->RequestManualActivation().WasAccepted());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleReceiverManualAggregationTest,
	"PuzzleSystem.Receiver.ActivationMode.AggregationAndIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleReceiverManualAggregationTest::RunTest(const FString& Parameters)
{
	using namespace UE::PuzzleSystem::Receiver::Tests;
	AActor* Owner = NewActor(TEXT("MultiReceiverOwner"));
	UPuzzleReceiverComponent* ManualReceiver = AddReceiver(*Owner, TEXT("ManualReceiver"));
	UPuzzleReceiverComponent* AutomaticReceiver = AddReceiver(*Owner, TEXT("AutomaticReceiver"));
	ManualReceiver->ActivationMode = EPuzzleReceiverActivationMode::Manual;
	APuzzleController* FirstController = NewController(TEXT("FirstController"));
	APuzzleController* SecondController = NewController(TEXT("SecondController"));

	ManualReceiver->SetControllerRequest(FirstController, true);
	ManualReceiver->SetControllerRequest(SecondController, true);
	AutomaticReceiver->SetControllerRequest(FirstController, true);
	TestTrue(TEXT("Any active Controller authorizes Manual Open"),
		ManualReceiver->RequestManualActivation().WasAccepted());
	TestTrue(TEXT("Manual Receiver activates independently"), ManualReceiver->IsReceiverActive());
	TestTrue(TEXT("Automatic sibling keeps its own policy"), AutomaticReceiver->IsReceiverActive());

	TestFalse(TEXT("Removing one of two requests does not change effective state"),
		ManualReceiver->RemoveControllerRequest(FirstController));
	TestTrue(TEXT("Second Controller preserves prerequisites"),
		ManualReceiver->AreActivationPrerequisitesSatisfied());
	TestTrue(TEXT("Second Controller preserves manual activation"),
		ManualReceiver->IsReceiverActive());
	TestTrue(TEXT("Removing the final request changes manual state"),
		ManualReceiver->RemoveControllerRequest(SecondController));
	TestFalse(TEXT("Final request loss clears prerequisites"),
		ManualReceiver->AreActivationPrerequisitesSatisfied());
	TestFalse(TEXT("Final request loss closes only the Manual Receiver"),
		ManualReceiver->IsReceiverActive());
	TestTrue(TEXT("Independent Automatic Receiver remains active"),
		AutomaticReceiver->IsReceiverActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleReceiverManualReentrancyTest,
	"PuzzleSystem.Receiver.ActivationMode.Reentrancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleReceiverManualReentrancyTest::RunTest(const FString& Parameters)
{
	using namespace UE::PuzzleSystem::Receiver::Tests;
	AActor* Owner = NewActor(TEXT("ReentrantManualOwner"));
	UPuzzleManualReentrantReceiverComponent* Receiver =
		AddReceiver<UPuzzleManualReentrantReceiverComponent>(*Owner, TEXT("Receiver"));
	Receiver->ActivationMode = EPuzzleReceiverActivationMode::Manual;
	APuzzleController* Controller = NewController(TEXT("ReentrantController"));
	Receiver->ControllerToDeactivate = Controller;
	Receiver->SetControllerRequest(Controller, true);

	const FPuzzleReceiverActivationCommandResult Result = Receiver->RequestManualActivation();
	TestEqual(TEXT("Synchronous prerequisite loss is reported"), Result.Status,
		EPuzzleReceiverActivationCommandStatus::PrerequisitesNotSatisfied);
	TestEqual(TEXT("Reentrant activation hook runs once"), Receiver->ReentrantUpdateCount, 1);
	TestFalse(TEXT("Reentrant chain settles without prerequisites"),
		Receiver->AreActivationPrerequisitesSatisfied());
	TestFalse(TEXT("Reentrant chain clears manual intent"),
		Receiver->IsManualActivationRequested());
	TestFalse(TEXT("Reentrant chain settles inactive"), Receiver->IsReceiverActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleReceiverActivationReflectionTest,
	"PuzzleSystem.Receiver.ActivationMode.BlueprintReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleReceiverActivationReflectionTest::RunTest(const FString& Parameters)
{
	const UClass* ReceiverClass = UPuzzleReceiverComponent::StaticClass();
	const FName BlueprintFunctions[] =
	{
		GET_FUNCTION_NAME_CHECKED(UPuzzleReceiverComponent, RequestManualActivation),
		GET_FUNCTION_NAME_CHECKED(UPuzzleReceiverComponent, RequestManualDeactivation),
		GET_FUNCTION_NAME_CHECKED(UPuzzleReceiverComponent, CanRequestManualActivation),
		GET_FUNCTION_NAME_CHECKED(UPuzzleReceiverComponent, AreActivationPrerequisitesSatisfied),
		GET_FUNCTION_NAME_CHECKED(UPuzzleReceiverComponent, IsManualActivationRequested),
		GET_FUNCTION_NAME_CHECKED(UPuzzleReceiverComponent, GetActivationMode)
	};
	for (const FName FunctionName : BlueprintFunctions)
	{
		const UFunction* Function = ReceiverClass->FindFunctionByName(FunctionName);
		TestTrue(
			*FString::Printf(TEXT("Receiver API '%s' is Blueprint-callable"), *FunctionName.ToString()),
			Function && Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	}

	const FMulticastDelegateProperty* PrerequisiteDelegate =
		FindFProperty<FMulticastDelegateProperty>(
			ReceiverClass,
			GET_MEMBER_NAME_CHECKED(
				UPuzzleReceiverComponent,
				OnReceiverActivationPrerequisitesChanged));
	TestTrue(TEXT("Prerequisite event is Blueprint-assignable"),
		PrerequisiteDelegate
			&& PrerequisiteDelegate->HasAnyPropertyFlags(CPF_BlueprintAssignable));
	TestNotNull(TEXT("Activation mode enum is reflected"),
		StaticEnum<EPuzzleReceiverActivationMode>());
	TestNotNull(TEXT("Activation command status enum is reflected"),
		StaticEnum<EPuzzleReceiverActivationCommandStatus>());
	TestTrue(TEXT("Activation command result is a Blueprint type"),
		FPuzzleReceiverActivationCommandResult::StaticStruct()->HasMetaData(TEXT("BlueprintType")));
	return true;
}

#endif
