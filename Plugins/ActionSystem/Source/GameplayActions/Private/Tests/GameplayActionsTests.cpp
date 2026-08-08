#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actions/GameplayActionDefinition.h"
#include "Actions/GameplayWaitAction.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayActionTags.h"
#include "Misc/DataValidation.h"
#include "NativeGameplayTags.h"
#include "StructUtils/PropertyBag.h"
#include "Tests/GameplayActionTestTypes.h"
#include "UObject/UnrealType.h"

#include <limits>

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GameplayAction_Test_Action, "GameplayAction.Test.Core.Action");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GameplayAction_Test_LockA, "GameplayAction.Lock.Test.Conflict.Primary");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GameplayAction_Test_LockAChild, "GameplayAction.Lock.Test.Conflict.Primary.Child");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GameplayAction_Test_LockB, "GameplayAction.Lock.Test.Conflict.Secondary");

namespace GameplayActionsTests
{
	UGameplayActionDefinition* MakeDefinition(
		const TSubclassOf<UGameplayActionInstance> InstanceClass = UGameplayActionTestInstance::StaticClass(),
		const int32 Priority = 0,
		const EGameplayActionBlockedPolicy BlockedPolicy = EGameplayActionBlockedPolicy::Queue,
		const bool bInterruptible = true)
	{
		UGameplayActionDefinition* Definition = NewObject<UGameplayActionDefinition>();
		Definition->InstanceClass = InstanceClass;
		Definition->ActionTag = TAG_GameplayAction_Test_Action;
		Definition->DefaultPriority = Priority;
		Definition->BlockedPolicy = BlockedPolicy;
		Definition->bInterruptible = bInterruptible;
		return Definition;
	}

	void SetSchema(UGameplayActionDefinition& Definition, const TArray<FPropertyBagPropertyDesc>& Descs)
	{
		Definition.DefaultParameters.InitializeFromBagStruct(UPropertyBag::GetOrCreateFromDescs(Descs));
	}

	FGameplayActionRequest MakeRequest(UGameplayActionDefinition& Definition)
	{
		return UGameplayActionBlueprintLibrary::CreateActionRequest(&Definition).Request;
	}

	UGameplayActionComponent* MakeComponent()
	{
		return NewObject<UGameplayActionComponent>();
	}

	void AddLock(UGameplayActionDefinition& Definition, const FGameplayTag Lock)
	{
		Definition.ExecutionLocks.AddTag(Lock);
	}

	EGameplayActionParameterAccessResult SetFromHolder(
		FGameplayActionRequest& Request,
		const FName Name,
		UGameplayActionTestPropertyHolder& Holder)
	{
		const FProperty* Property = Holder.GetClass()->FindPropertyByName(Name);
		return UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
			Request, Name, Property, Property ? Property->ContainerPtrToValuePtr<void>(&Holder) : nullptr);
	}

	EGameplayActionParameterAccessResult GetToHolder(
		const FInstancedPropertyBag& Bag,
		const FName Name,
		UGameplayActionTestPropertyHolder& Holder)
	{
		const FProperty* Property = Holder.GetClass()->FindPropertyByName(Name);
		return UGameplayActionBlueprintLibrary::GetBagValueToProperty(
			Bag, Name, Property, Property ? Property->ContainerPtrToValuePtr<void>(&Holder) : nullptr);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsPropertyBagTest,
	"GameplayActions.Parameters.DeepCopyAndWildcardTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsPropertyBagTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UGameplayActionDefinition* Definition = MakeDefinition();
	TArray<FPropertyBagPropertyDesc> Descs = {
		{ TEXT("bBoolValue"), EPropertyBagPropertyType::Bool },
		{ TEXT("IntValue"), EPropertyBagPropertyType::Int32 },
		{ TEXT("EnumValue"), EPropertyBagPropertyType::Enum, StaticEnum<EGameplayActionTestEnum>() },
		{ TEXT("StructValue"), EPropertyBagPropertyType::Struct, FGameplayActionTestPayload::StaticStruct() },
		{ TEXT("TagValue"), EPropertyBagPropertyType::Struct, FGameplayTag::StaticStruct() },
		{ TEXT("VectorValue"), EPropertyBagPropertyType::Struct, TBaseStructure<FVector>::Get() },
		{ TEXT("RotatorValue"), EPropertyBagPropertyType::Struct, TBaseStructure<FRotator>::Get() },
		{ TEXT("TransformValue"), EPropertyBagPropertyType::Struct, TBaseStructure<FTransform>::Get() },
		{ TEXT("ObjectValue"), EPropertyBagPropertyType::Object, UObject::StaticClass() },
		{ TEXT("SoftObjectValue"), EPropertyBagPropertyType::SoftObject, UObject::StaticClass() },
		{ TEXT("ClassValue"), EPropertyBagPropertyType::Class, UObject::StaticClass() },
		{ TEXT("SoftClassValue"), EPropertyBagPropertyType::SoftClass, UObject::StaticClass() }
	};
	SetSchema(*Definition, Descs);
	Definition->DefaultParameters.SetValueInt32(TEXT("IntValue"), 7);
	Definition->OptionalTimeout.bEnabled = true;
	Definition->OptionalTimeout.DurationSeconds = 12.5;
	Definition->MaxQueueTimeSeconds = 4.5;

	const FGameplayActionRequestCreationResult CreationA = UGameplayActionBlueprintLibrary::CreateActionRequest(Definition);
	const FGameplayActionRequestCreationResult CreationB = UGameplayActionBlueprintLibrary::CreateActionRequest(Definition);
	TestTrue(TEXT("Factory creates request A"), CreationA.WasCreated());
	TestTrue(TEXT("Factory creates request B"), CreationB.WasCreated());
	FGameplayActionRequest RequestA = CreationA.Request;
	FGameplayActionRequest RequestB = CreationB.Request;
	FGameplayActionCorrelationData Correlation;
	Correlation.Type = GameplayActionTags::Result_Success;
	Correlation.Id = FGuid::NewGuid();
	UGameplayActionBlueprintLibrary::SetRequestContext(
		RequestA, GameplayActionTags::Result_Success, Definition, Correlation);

	UGameplayActionTestPropertyHolder* Source = NewObject<UGameplayActionTestPropertyHolder>();
	Source->bBoolValue = true;
	Source->IntValue = 42;
	Source->FloatValue = 99.0f;
	Source->EnumValue = EGameplayActionTestEnum::Second;
	Source->StructValue.Number = 23;
	Source->StructValue.Name = TEXT("Payload");
	Source->TagValue = GameplayActionTags::Result_Success;
	Source->VectorValue = FVector(1.0, 2.0, 3.0);
	Source->RotatorValue = FRotator(10.0, 20.0, 30.0);
	Source->TransformValue = FTransform(Source->RotatorValue, Source->VectorValue, FVector(2.0));
	Source->ObjectValue = Definition;
	Source->SoftObjectValue = Definition;
	Source->ClassValue = UGameplayActionTestInstance::StaticClass();
	Source->SoftClassValue = UGameplayActionTestInstance::StaticClass();

	for (const FPropertyBagPropertyDesc& Desc : Descs)
	{
		TestEqual(FString::Printf(TEXT("Set %s"), *Desc.Name.ToString()),
			SetFromHolder(RequestA, Desc.Name, *Source), EGameplayActionParameterAccessResult::Success);
	}

	UGameplayActionTestPropertyHolder* Output = NewObject<UGameplayActionTestPropertyHolder>();
	for (const FPropertyBagPropertyDesc& Desc : Descs)
	{
		TestEqual(FString::Printf(TEXT("Get %s"), *Desc.Name.ToString()),
			GetToHolder(RequestA.GetParameters(), Desc.Name, *Output), EGameplayActionParameterAccessResult::Success);
	}
	TestTrue(TEXT("Bool round-trips"), Output->bBoolValue);
	TestEqual(TEXT("Int round-trips"), Output->IntValue, 42);
	TestEqual(TEXT("Enum round-trips"), Output->EnumValue, EGameplayActionTestEnum::Second);
	TestTrue(TEXT("Struct round-trips"), Output->StructValue == Source->StructValue);
	TestEqual(TEXT("Tag round-trips"), Output->TagValue, Source->TagValue);
	TestEqual(TEXT("Vector round-trips"), Output->VectorValue, Source->VectorValue);
	TestEqual(TEXT("Rotator round-trips"), Output->RotatorValue, Source->RotatorValue);
	TestTrue(TEXT("Transform round-trips"), Output->TransformValue.Equals(Source->TransformValue));
	TestEqual(TEXT("Object round-trips"), Output->ObjectValue.Get(), Source->ObjectValue.Get());
	TestEqual(TEXT("Soft object round-trips"), Output->SoftObjectValue.ToSoftObjectPath(), Source->SoftObjectValue.ToSoftObjectPath());
	TestEqual(TEXT("Class round-trips"), Output->ClassValue.Get(), Source->ClassValue.Get());
	TestEqual(TEXT("Soft class round-trips"), Output->SoftClassValue.ToSoftObjectPath(), Source->SoftClassValue.ToSoftObjectPath());

	const int32 PropertyCount = RequestA.GetParameters().GetNumPropertiesInBag();
	TestEqual(TEXT("Missing parameter is rejected"),
		SetFromHolder(RequestA, TEXT("Missing"), *Source), EGameplayActionParameterAccessResult::ParameterNotFound);
	TestEqual(TEXT("Missing parameter does not mutate schema"), RequestA.GetParameters().GetNumPropertiesInBag(), PropertyCount);

	const FProperty* FloatProperty = Source->GetClass()->FindPropertyByName(TEXT("FloatValue"));
	TestEqual(TEXT("Wrong type is rejected"),
		UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
			RequestA, TEXT("IntValue"), FloatProperty, FloatProperty->ContainerPtrToValuePtr<void>(Source)),
		EGameplayActionParameterAccessResult::TypeMismatch);
	UGameplayActionTestPropertyHolder* IntCheck = NewObject<UGameplayActionTestPropertyHolder>();
	GetToHolder(RequestA.GetParameters(), TEXT("IntValue"), *IntCheck);
	TestEqual(TEXT("Wrong type leaves value unchanged"), IntCheck->IntValue, 42);

	UGameplayActionTestPropertyHolder* IsolatedCheck = NewObject<UGameplayActionTestPropertyHolder>();
	GetToHolder(RequestB.GetParameters(), TEXT("IntValue"), *IsolatedCheck);
	TestEqual(TEXT("Requests have isolated values"), IsolatedCheck->IntValue, 7);
	Definition->DefaultParameters.SetValueInt32(TEXT("IntValue"), 100);
	GetToHolder(RequestB.GetParameters(), TEXT("IntValue"), *IsolatedCheck);
	TestEqual(TEXT("Definition changes do not mutate existing request"), IsolatedCheck->IntValue, 7);

	UGameplayActionComponent* Component = MakeComponent();
	UGameplayActionTestObserver* Observer = NewObject<UGameplayActionTestObserver>();
	Component->OnActionEvent.AddDynamic(Observer, &UGameplayActionTestObserver::HandleActionEvent);
	const FGameplayActionSubmissionResult Submission = Component->SubmitAction(RequestA);
	TestTrue(TEXT("Parameterized action starts"), Submission.IsAccepted());
	UGameplayActionInstance* Instance = Component->GetActionInstance(Submission.Handle);
	TestNotNull(TEXT("Instance exists"), Instance);
	TestEqual(TEXT("Timeout is copied into instance snapshot"), Instance->GetOptionalTimeout().DurationSeconds, 12.5);
	TestEqual(TEXT("Queue timeout is copied into instance snapshot"), Instance->GetMaxQueueTimeSeconds(), 4.5);
	TestTrue(TEXT("Origin is copied into instance snapshot"), Instance->GetOriginTag() == GameplayActionTags::Result_Success);
	TestEqual(TEXT("Correlation is copied into instance snapshot"), Instance->GetCorrelation().Id, Correlation.Id);

	Source->IntValue = 77;
	SetFromHolder(RequestA, TEXT("IntValue"), *Source);
	UGameplayActionTestPropertyHolder* SnapshotCheck = NewObject<UGameplayActionTestPropertyHolder>();
	GetToHolder(Instance->GetParameters(), TEXT("IntValue"), *SnapshotCheck);
	TestEqual(TEXT("Instance snapshot is immutable from request changes"), SnapshotCheck->IntValue, 42);
	TestTrue(TEXT("Accepted event was observed"), Observer->ObservedEvents.Num() >= 1);
	GetToHolder(Observer->ObservedEvents[0].GetParameters(), TEXT("IntValue"), *SnapshotCheck);
	TestEqual(TEXT("Event snapshot is immutable from request changes"), SnapshotCheck->IntValue, 42);
	TestEqual(TEXT("Correlation is copied into event snapshot"), Observer->ObservedEvents[0].Correlation.Id, Correlation.Id);
	TestEqual(TEXT("Queue timeout is copied into event snapshot"), Observer->ObservedEvents[0].MaxQueueTimeSeconds, 4.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsLifecycleTest,
	"GameplayActions.Lifecycle.SinglePauseCancelShutdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UGameplayActionTestInstance::ResetCounters();
	UGameplayActionDefinition* Definition = MakeDefinition();
	UGameplayActionTestComponent* Component = NewObject<UGameplayActionTestComponent>();
	const FGameplayActionSubmissionResult First = Component->SubmitAction(MakeRequest(*Definition));
	TestEqual(TEXT("Single action starts"), First.Status, EGameplayActionSubmissionStatus::AcceptedStarted);
	UGameplayActionTestInstance* FirstInstance = Cast<UGameplayActionTestInstance>(Component->GetActionInstance(First.Handle));
	TestNotNull(TEXT("Single instance exists"), FirstInstance);
	TestEqual(TEXT("Init hook runs once"), UGameplayActionTestInstance::InitCount, 1);
	TestEqual(TEXT("Immediate Init observes Starting"), UGameplayActionTestInstance::LastInitState, EGameplayActionState::Starting);
	TestEqual(TEXT("Start hook runs once"), UGameplayActionTestInstance::StartedCount, 1);

	TestEqual(TEXT("Pause succeeds"), Component->PauseActions(), EGameplayActionOperationResult::Succeeded);
	EGameplayActionState State;
	TestTrue(TEXT("Paused state query succeeds"), Component->GetActionState(First.Handle, State));
	TestEqual(TEXT("State is paused"), State, EGameplayActionState::Paused);
	TestEqual(TEXT("Pause hook runs"), UGameplayActionTestInstance::PausedCount, 1);
	TestEqual(TEXT("Resume succeeds"), Component->ResumeActions(), EGameplayActionOperationResult::Succeeded);
	TestEqual(TEXT("Resume hook runs"), UGameplayActionTestInstance::ResumedCount, 1);

	FirstInstance->CompleteForTest();
	FGameplayActionResult Result;
	TestTrue(TEXT("Success result remains queryable"), Component->GetActionResult(First.Handle, Result));
	TestEqual(TEXT("Action succeeded"), Result.TerminalState, EGameplayActionState::Succeeded);
	TestNull(TEXT("Heavy instance is released after Ended dispatch"), Component->GetActionInstance(First.Handle));
	FirstInstance->CompleteForTest();
	Component->GetActionResult(First.Handle, Result);
	TestEqual(TEXT("Late callback cannot change terminal result"), Result.TerminalState, EGameplayActionState::Succeeded);

	const FGameplayActionSubmissionResult Cancelled = Component->SubmitAction(MakeRequest(*Definition));
	TestEqual(TEXT("Queued/running cancel succeeds"),
		Component->CancelAction(Cancelled.Handle, FGameplayTag()), EGameplayActionOperationResult::Succeeded);
	Component->GetActionResult(Cancelled.Handle, Result);
	TestEqual(TEXT("Cancel terminal state"), Result.TerminalState, EGameplayActionState::Cancelled);
	const FGameplayActionSubmissionResult Interrupted = Component->SubmitAction(MakeRequest(*Definition));
	TestEqual(
		TEXT("Explicit interruption succeeds"),
		Component->InterruptAction(Interrupted.Handle, FGameplayTag()),
		EGameplayActionOperationResult::Succeeded);
	Component->GetActionResult(Interrupted.Handle, Result);
	TestEqual(TEXT("Interruption terminal state"), Result.TerminalState, EGameplayActionState::Interrupted);
	TestEqual(
		TEXT("Invalid interruption reason uses the generic fallback"),
		Result.ReasonTag,
		GameplayActionTags::Result_Interrupted_External.GetTag());
	TestEqual(
		TEXT("A terminal action cannot be interrupted again"),
		Component->InterruptAction(
			Interrupted.Handle,
			GameplayActionTags::Result_Interrupted_HigherPriority),
		EGameplayActionOperationResult::HandleNotFound);
	const FGameplayActionSubmissionResult Failed = Component->SubmitAction(MakeRequest(*Definition));
	CastChecked<UGameplayActionTestInstance>(Component->GetActionInstance(Failed.Handle))->FailForTest();
	Component->GetActionResult(Failed.Handle, Result);
	TestEqual(TEXT("Failure terminal state"), Result.TerminalState, EGameplayActionState::Failed);

	const FGameplayActionSubmissionResult ToDeactivate = Component->SubmitAction(MakeRequest(*Definition));
	Component->Deactivate();
	Component->GetActionResult(ToDeactivate.Handle, Result);
	TestEqual(TEXT("Deactivate aborts action"), Result.TerminalState, EGameplayActionState::Aborted);
	TestFalse(TEXT("Deactivate stops submissions"), Component->IsAcceptingSubmissions());
	TestEqual(TEXT("Submission after deactivate rejected"),
		Component->SubmitAction(MakeRequest(*Definition)).Status, EGameplayActionSubmissionStatus::RejectedNotAccepting);
	Component->Activate();
	const FGameplayActionSubmissionResult Reactivated = Component->SubmitAction(MakeRequest(*Definition));
	TestTrue(TEXT("Activate reopens submissions"), Reactivated.IsAccepted());
	TestTrue(TEXT("Handles are monotonic and never reused"), Reactivated.Handle.GetValue() > ToDeactivate.Handle.GetValue());
	Component->AbortAllActions(GameplayActionTags::Result_Aborted_SystemReset);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GameplayActionsLifecycleTestWorld"));
	AActor* EndPlayOwner = World->SpawnActor<AActor>();
	UGameplayActionTestComponent* EndPlayComponent = NewObject<UGameplayActionTestComponent>(EndPlayOwner);
	EndPlayOwner->AddInstanceComponent(EndPlayComponent);
	EndPlayComponent->RegisterComponent();
	EndPlayComponent->InvokeBeginPlayForTest();
	const FGameplayActionSubmissionResult ToEndPlay = EndPlayComponent->SubmitAction(MakeRequest(*Definition));
	EndPlayComponent->InvokeEndPlayForTest();
	EndPlayComponent->GetActionResult(ToEndPlay.Handle, Result);
	TestEqual(TEXT("EndPlay aborts action"), Result.TerminalState, EGameplayActionState::Aborted);
	TestTrue(TEXT("Cleanup runs for every accepted terminal action"), UGameplayActionTestInstance::CleanupCount >= 6);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsInitStartLifecycleTest,
	"GameplayActions.Lifecycle.InitStartQueueAndReentrancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsInitStartLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UGameplayActionTestInstance::ResetCounters();

	UGameplayActionDefinition* HolderDefinition = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 10);
	UGameplayActionDefinition* QueuedDefinition = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 1);
	AddLock(*HolderDefinition, TAG_GameplayAction_Test_LockA);
	AddLock(*QueuedDefinition, TAG_GameplayAction_Test_LockA);
	UGameplayActionComponent* Component = MakeComponent();

	const FGameplayActionSubmissionResult Holder = Component->SubmitAction(MakeRequest(*HolderDefinition));
	TestEqual(TEXT("Immediate action is initialized once"), UGameplayActionTestInstance::InitCount, 1);
	TestEqual(TEXT("Immediate action starts once"), UGameplayActionTestInstance::StartedCount, 1);
	TestEqual(TEXT("Immediate Init runs in Starting state"), UGameplayActionTestInstance::LastInitState, EGameplayActionState::Starting);

	const FGameplayActionSubmissionResult Queued = Component->SubmitAction(MakeRequest(*QueuedDefinition));
	TestEqual(TEXT("Blocked action is accepted into the queue"), Queued.Status, EGameplayActionSubmissionStatus::AcceptedQueued);
	TestEqual(TEXT("Queued action receives Init"), UGameplayActionTestInstance::InitCount, 2);
	TestEqual(TEXT("Queued action does not receive Start"), UGameplayActionTestInstance::StartedCount, 1);
	TestEqual(TEXT("Queued Init observes Queued state"), UGameplayActionTestInstance::LastInitState, EGameplayActionState::Queued);
	Component->CancelAction(Queued.Handle, FGameplayTag());
	TestEqual(TEXT("Cancelling queued action never calls Start"), UGameplayActionTestInstance::StartedCount, 1);

	const FGameplayActionSubmissionResult AutoStart = Component->SubmitAction(MakeRequest(*QueuedDefinition));
	TestEqual(TEXT("Replacement action queues"), AutoStart.Status, EGameplayActionSubmissionStatus::AcceptedQueued);
	CastChecked<UGameplayActionTestInstance>(Component->GetActionInstance(Holder.Handle))->CompleteForTest();
	EGameplayActionState State = EGameplayActionState::Created;
	TestTrue(TEXT("Automatically started action remains queryable"), Component->GetActionState(AutoStart.Handle, State));
	TestEqual(TEXT("Queued action starts automatically after lock release"), State, EGameplayActionState::Running);
	TestEqual(TEXT("Automatically started action receives Start exactly once"), UGameplayActionTestInstance::StartedCount, 2);

	const int32 InitBeforePreflight = UGameplayActionTestInstance::InitCount;
	UGameplayActionDefinition* PreflightDefinition = MakeDefinition();
	TestEqual(TEXT("Preflight reports an immediate start"),
		Component->PreflightAction(MakeRequest(*PreflightDefinition)).Status,
		EGameplayActionSubmissionStatus::AcceptedStarted);
	TestEqual(TEXT("Preflight never calls Init"), UGameplayActionTestInstance::InitCount, InitBeforePreflight);

	UGameplayActionDefinition* RejectedDefinition = MakeDefinition();
	SetSchema(*RejectedDefinition, { { TEXT("RejectStart"), EPropertyBagPropertyType::Bool } });
	RejectedDefinition->DefaultParameters.SetValueBool(TEXT("RejectStart"), true);
	TestEqual(TEXT("Validation rejects the action"),
		Component->SubmitAction(MakeRequest(*RejectedDefinition)).Status,
		EGameplayActionSubmissionStatus::RejectedValidation);
	TestEqual(TEXT("Rejected action never calls Init"), UGameplayActionTestInstance::InitCount, InitBeforePreflight);

	UGameplayActionComponent* ReentrantComponent = MakeComponent();
	UGameplayActionDefinition* ReentrantDefinition = MakeDefinition();
	UGameplayActionTestInstance::InitCallbackRequest = MakeRequest(*ReentrantDefinition);
	UGameplayActionTestInstance::bAttemptOperationsDuringInit = true;
	const FGameplayActionSubmissionResult ReentrantOuter =
		ReentrantComponent->SubmitAction(MakeRequest(*ReentrantDefinition));
	TestEqual(TEXT("Outer action survives rejected Init operations"),
		ReentrantOuter.Status, EGameplayActionSubmissionStatus::AcceptedStarted);
	TestEqual(TEXT("Submit from Init is rejected"),
		UGameplayActionTestInstance::InitReentrantSubmissionStatus,
		EGameplayActionSubmissionStatus::RejectedReentrant);
	TestEqual(TEXT("Cancel from Init is rejected"),
		UGameplayActionTestInstance::InitReentrantCancelResult,
		EGameplayActionOperationResult::RejectedReentrant);
	TestEqual(TEXT("Pause from Init is rejected"),
		UGameplayActionTestInstance::InitReentrantPauseResult,
		EGameplayActionOperationResult::RejectedReentrant);
	FGameplayActionResult ReentrantTerminalResult;
	TestFalse(TEXT("Completion from Init does not create a terminal result"),
		ReentrantComponent->GetActionResult(ReentrantOuter.Handle, ReentrantTerminalResult));
	TestTrue(TEXT("Completion from Init does not prevent the real Start"),
		ReentrantComponent->GetActionState(ReentrantOuter.Handle, State));
	TestEqual(TEXT("Outer action reaches Running after Init"), State, EGameplayActionState::Running);

	Component->AbortAllActions(GameplayActionTags::Result_Aborted_SystemReset);
	ReentrantComponent->AbortAllActions(GameplayActionTags::Result_Aborted_SystemReset);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsSchedulerTest,
	"GameplayActions.Scheduler.LocksPreemptionAtomicityAndFifo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsSchedulerTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UGameplayActionDefinition* A = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 1);
	UGameplayActionDefinition* B = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 2);
	UGameplayActionDefinition* ABHigh = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 10);
	AddLock(*A, TAG_GameplayAction_Test_LockA);
	AddLock(*B, TAG_GameplayAction_Test_LockB);
	AddLock(*ABHigh, TAG_GameplayAction_Test_LockA);
	AddLock(*ABHigh, TAG_GameplayAction_Test_LockB);

	UGameplayActionComponent* Component = MakeComponent();
	UGameplayActionTestObserver* Observer = NewObject<UGameplayActionTestObserver>();
	Component->OnActionEvent.AddDynamic(Observer, &UGameplayActionTestObserver::HandleActionEvent);
	const FGameplayActionSubmissionResult AResult = Component->SubmitAction(MakeRequest(*A));
	const FGameplayActionSubmissionResult BResult = Component->SubmitAction(MakeRequest(*B));
	TestEqual(TEXT("Independent lock A starts"), AResult.Status, EGameplayActionSubmissionStatus::AcceptedStarted);
	TestEqual(TEXT("Independent lock B starts concurrently"), BResult.Status, EGameplayActionSubmissionStatus::AcceptedStarted);
	const int32 EventBase = Observer->ObservedEvents.Num();
	const FGameplayActionSubmissionResult HighResult = Component->SubmitAction(MakeRequest(*ABHigh));
	TestEqual(TEXT("Higher multi-lock action starts"), HighResult.Status, EGameplayActionSubmissionStatus::AcceptedStarted);
	FGameplayActionResult Terminal;
	TestTrue(TEXT("A was interrupted"), Component->GetActionResult(AResult.Handle, Terminal));
	TestEqual(TEXT("A interruption state"), Terminal.TerminalState, EGameplayActionState::Interrupted);
	TestTrue(
		TEXT("A interruption identifies the preempting action"),
		Terminal.CausingActionHandle == HighResult.Handle);
	TestTrue(TEXT("B was interrupted"), Component->GetActionResult(BResult.Handle, Terminal));
	TestEqual(TEXT("B interruption state"), Terminal.TerminalState, EGameplayActionState::Interrupted);
	TestTrue(
		TEXT("B interruption identifies the preempting action"),
		Terminal.CausingActionHandle == HighResult.Handle);
	TestTrue(TEXT("Preemption emitted four events"), Observer->ObservedEvents.Num() >= EventBase + 4);
	TestEqual(TEXT("Incoming Accepted is first"), Observer->ObservedEvents[EventBase].EventType, EGameplayActionEventType::Accepted);
	TestEqual(TEXT("Higher-priority conflict ends first"), Observer->ObservedEvents[EventBase + 1].Handle, BResult.Handle);
	TestEqual(TEXT("Second conflict ends next"), Observer->ObservedEvents[EventBase + 2].Handle, AResult.Handle);
	TestEqual(TEXT("Incoming Started is last"), Observer->ObservedEvents[EventBase + 3].EventType, EGameplayActionEventType::Started);

	UGameplayActionComponent* ExactComponent = MakeComponent();
	UGameplayActionDefinition* ParentLock = MakeDefinition();
	UGameplayActionDefinition* ChildLock = MakeDefinition();
	AddLock(*ParentLock, TAG_GameplayAction_Test_LockA);
	AddLock(*ChildLock, TAG_GameplayAction_Test_LockAChild);
	TestEqual(TEXT("Parent exact lock starts"),
		ExactComponent->SubmitAction(MakeRequest(*ParentLock)).Status, EGameplayActionSubmissionStatus::AcceptedStarted);
	TestEqual(TEXT("Child tag does not conflict with parent under exact matching"),
		ExactComponent->SubmitAction(MakeRequest(*ChildLock)).Status, EGameplayActionSubmissionStatus::AcceptedStarted);

	UGameplayActionComponent* AtomicComponent = MakeComponent();
	UGameplayActionDefinition* InterruptibleA = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 1);
	UGameplayActionDefinition* NonInterruptibleB = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 1, EGameplayActionBlockedPolicy::Queue, false);
	UGameplayActionDefinition* IncomingAB = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 20);
	AddLock(*InterruptibleA, TAG_GameplayAction_Test_LockA);
	AddLock(*NonInterruptibleB, TAG_GameplayAction_Test_LockB);
	AddLock(*IncomingAB, TAG_GameplayAction_Test_LockA);
	AddLock(*IncomingAB, TAG_GameplayAction_Test_LockB);
	const FGameplayActionSubmissionResult AtomicA = AtomicComponent->SubmitAction(MakeRequest(*InterruptibleA));
	const FGameplayActionSubmissionResult AtomicB = AtomicComponent->SubmitAction(MakeRequest(*NonInterruptibleB));
	const FGameplayActionSubmissionResult AtomicIncoming = AtomicComponent->SubmitAction(MakeRequest(*IncomingAB));
	TestEqual(TEXT("All-or-none conflict queues incoming"), AtomicIncoming.Status, EGameplayActionSubmissionStatus::AcceptedQueued);
	EGameplayActionState State;
	AtomicComponent->GetActionState(AtomicA.Handle, State);
	TestEqual(TEXT("Interruptible conflict was not partially preempted"), State, EGameplayActionState::Running);
	AtomicComponent->CancelAction(AtomicA.Handle, FGameplayTag());
	AtomicComponent->GetActionState(AtomicIncoming.Handle, State);
	TestEqual(TEXT("Incoming still waits on second lock"), State, EGameplayActionState::Queued);
	AtomicComponent->CancelAction(AtomicB.Handle, FGameplayTag());
	AtomicComponent->GetActionState(AtomicIncoming.Handle, State);
	TestEqual(TEXT("Incoming starts only after full lock set is available"), State, EGameplayActionState::Running);

	UGameplayActionComponent* FifoComponent = MakeComponent();
	UGameplayActionDefinition* Holder = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 100);
	UGameplayActionDefinition* QueueDefinition = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 5);
	AddLock(*Holder, TAG_GameplayAction_Test_LockA);
	AddLock(*QueueDefinition, TAG_GameplayAction_Test_LockA);
	const FGameplayActionSubmissionResult HolderResult = FifoComponent->SubmitAction(MakeRequest(*Holder));
	const FGameplayActionSubmissionResult QueueOne = FifoComponent->SubmitAction(MakeRequest(*QueueDefinition));
	const FGameplayActionSubmissionResult QueueTwo = FifoComponent->SubmitAction(MakeRequest(*QueueDefinition));
	TestEqual(TEXT("First equal-priority action queues"), QueueOne.Status, EGameplayActionSubmissionStatus::AcceptedQueued);
	TestEqual(TEXT("Second equal-priority action queues"), QueueTwo.Status, EGameplayActionSubmissionStatus::AcceptedQueued);
	UGameplayActionDefinition* EqualReject = MakeDefinition(
		UGameplayActionTestInstance::StaticClass(), 100, EGameplayActionBlockedPolicy::Reject);
	AddLock(*EqualReject, TAG_GameplayAction_Test_LockA);
	TestEqual(TEXT("Equal priority cannot preempt and Reject policy rejects"),
		FifoComponent->SubmitAction(MakeRequest(*EqualReject)).Status, EGameplayActionSubmissionStatus::RejectedBlocked);
	const FGameplayActionDebugSnapshot QueuedSnapshot = FifoComponent->GetDebugSnapshot();
	TestEqual(TEXT("Debug snapshot exposes queued actions"), QueuedSnapshot.QueuedActions.Num(), 2);
	FifoComponent->CancelAction(QueueTwo.Handle, FGameplayTag());
	FGameplayActionResult QueueTwoTerminal;
	TestTrue(TEXT("Queued cancellation retains result"), FifoComponent->GetActionResult(QueueTwo.Handle, QueueTwoTerminal));
	TestEqual(TEXT("Queued action cancels without starting"), QueueTwoTerminal.TerminalState, EGameplayActionState::Cancelled);
	const FGameplayActionSubmissionResult QueueThree = FifoComponent->SubmitAction(MakeRequest(*QueueDefinition));
	FifoComponent->CancelAction(HolderResult.Handle, FGameplayTag());
	FifoComponent->GetActionState(QueueOne.Handle, State);
	TestEqual(TEXT("FIFO first starts"), State, EGameplayActionState::Running);
	FifoComponent->GetActionState(QueueThree.Handle, State);
	TestEqual(TEXT("FIFO second stays queued"), State, EGameplayActionState::Queued);
	FifoComponent->CancelAction(QueueOne.Handle, FGameplayTag());
	FifoComponent->GetActionState(QueueThree.Handle, State);
	TestEqual(TEXT("FIFO second starts after first releases"), State, EGameplayActionState::Running);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsPausePolicyTest,
	"GameplayActions.Scheduler.PauseOverridesBlockedReject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsPausePolicyTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UGameplayActionComponent* Component = MakeComponent();
	UGameplayActionDefinition* Low = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 1);
	UGameplayActionDefinition* HighReject = MakeDefinition(
		UGameplayActionTestInstance::StaticClass(), 10, EGameplayActionBlockedPolicy::Reject);
	AddLock(*Low, TAG_GameplayAction_Test_LockA);
	AddLock(*HighReject, TAG_GameplayAction_Test_LockA);
	const FGameplayActionSubmissionResult LowResult = Component->SubmitAction(MakeRequest(*Low));
	Component->PauseActions();
	const FGameplayActionSubmissionResult HighResult = Component->SubmitAction(MakeRequest(*HighReject));
	TestEqual(TEXT("Submission during pause queues despite Reject policy"),
		HighResult.Status, EGameplayActionSubmissionStatus::AcceptedQueued);
	Component->ResumeActions();
	FGameplayActionResult LowTerminal;
	TestTrue(TEXT("Queued high-priority action preempts after resume"), Component->GetActionResult(LowResult.Handle, LowTerminal));
	TestEqual(TEXT("Low action interrupted after resume"), LowTerminal.TerminalState, EGameplayActionState::Interrupted);
	EGameplayActionState HighState;
	Component->GetActionState(HighResult.Handle, HighState);
	TestEqual(TEXT("High action is running"), HighState, EGameplayActionState::Running);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsQueueStartsAfterSuccessTest,
	"GameplayActions.Scheduler.QueueStartsAutomaticallyAfterSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsQueueStartsAfterSuccessTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UGameplayActionComponent* Component = MakeComponent();
	UGameplayActionDefinition* HolderDefinition = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 10);
	UGameplayActionDefinition* QueuedDefinition = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 1);
	AddLock(*HolderDefinition, TAG_GameplayAction_Test_LockA);
	AddLock(*QueuedDefinition, TAG_GameplayAction_Test_LockA);

	const FGameplayActionSubmissionResult Holder = Component->SubmitAction(MakeRequest(*HolderDefinition));
	const FGameplayActionSubmissionResult Queued = Component->SubmitAction(MakeRequest(*QueuedDefinition));
	TestEqual(TEXT("Conflicting action is initially queued"), Queued.Status, EGameplayActionSubmissionStatus::AcceptedQueued);

	UGameplayActionTestInstance* HolderInstance = Cast<UGameplayActionTestInstance>(Component->GetActionInstance(Holder.Handle));
	TestNotNull(TEXT("Running holder instance exists"), HolderInstance);
	if (!HolderInstance)
	{
		return false;
	}
	HolderInstance->CompleteForTest();

	FGameplayActionResult HolderResult;
	TestTrue(TEXT("Succeed Action creates the holder terminal result"), Component->GetActionResult(Holder.Handle, HolderResult));
	TestEqual(TEXT("Holder succeeded"), HolderResult.TerminalState, EGameplayActionState::Succeeded);
	EGameplayActionState QueuedState = EGameplayActionState::Created;
	TestTrue(TEXT("Queued handle remains queryable"), Component->GetActionState(Queued.Handle, QueuedState));
	TestEqual(TEXT("Queued action starts automatically after lock release"), QueuedState, EGameplayActionState::Running);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsTickTest,
	"GameplayActions.Tick.OptInPauseAndCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsTickTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UGameplayActionTestInstance::ResetCounters();
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GameplayActionsTickTestWorld"));
	TestNotNull(TEXT("Tick test world created"), World);
	if (!World)
	{
		return false;
	}

	AActor* Owner = World->SpawnActor<AActor>();
	UGameplayActionTestComponent* Component = NewObject<UGameplayActionTestComponent>(Owner);
	Owner->AddInstanceComponent(Component);
	Component->RegisterComponent();
	UGameplayActionDefinition* Definition = MakeDefinition();
	const FGameplayActionSubmissionResult Submission = Component->SubmitAction(MakeRequest(*Definition));
	UGameplayActionTestInstance* Instance = Cast<UGameplayActionTestInstance>(Component->GetActionInstance(Submission.Handle));
	TestNotNull(TEXT("Tick test instance exists"), Instance);
	if (!Instance)
	{
		World->DestroyWorld(false);
		return false;
	}

	TestFalse(TEXT("Component remains tickless by default"), Component->IsComponentTickEnabled());
	Instance->EnableTickForTest(true);
	TestTrue(TEXT("Enabling action tick enables component tick"), Component->IsComponentTickEnabled());
	Component->InvokeTickForTest(0.25f);
	TestEqual(TEXT("Running opted-in action receives tick"), UGameplayActionTestInstance::TickCount, 1);
	TestEqual(TEXT("Action receives component Delta Seconds"), UGameplayActionTestInstance::LastTickDeltaSeconds, 0.25f);

	Component->PauseActions();
	TestFalse(TEXT("Pausing disables component tick"), Component->IsComponentTickEnabled());
	Component->InvokeTickForTest(0.5f);
	TestEqual(TEXT("Paused action does not tick even if invoked manually"), UGameplayActionTestInstance::TickCount, 1);
	Component->ResumeActions();
	TestTrue(TEXT("Resume restores tick for opted-in running action"), Component->IsComponentTickEnabled());

	Instance->CompleteOnNextTickForTest();
	Component->InvokeTickForTest(0.1f);
	TestEqual(TEXT("Action can complete safely from its tick"), UGameplayActionTestInstance::TickCount, 2);
	FGameplayActionResult Result;
	TestTrue(TEXT("Tick completion produces a result"), Component->GetActionResult(Submission.Handle, Result));
	TestEqual(TEXT("Tick completion succeeds"), Result.TerminalState, EGameplayActionState::Succeeded);
	TestFalse(TEXT("Component disables tick after the last ticking action ends"), Component->IsComponentTickEnabled());
	Component->InvokeTickForTest(0.1f);
	TestEqual(TEXT("Terminal action receives no late tick"), UGameplayActionTestInstance::TickCount, 2);

	Component->Deactivate();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsQueueTimeoutTest,
	"GameplayActions.Scheduler.QueueTimeoutPauseEventsAndTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsQueueTimeoutTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UGameplayActionTestInstance::ResetCounters();

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GameplayActionsQueueTimeoutTestWorld"));
	TestNotNull(TEXT("Queue timeout world created"), World);
	if (!World)
	{
		return false;
	}

	AActor* Owner = World->SpawnActor<AActor>();
	UGameplayActionTestComponent* Component = NewObject<UGameplayActionTestComponent>(Owner);
	Owner->AddInstanceComponent(Component);
	Component->RegisterComponent();

	UGameplayActionTestObserver* Observer = NewObject<UGameplayActionTestObserver>();
	Observer->Component = Component;
	Component->OnActionEvent.AddDynamic(Observer, &UGameplayActionTestObserver::HandleActionEvent);
	TestTrue(TEXT("Timeout test journal sink registers"), Component->RegisterJournalSink(Observer));

	UGameplayActionDefinition* HolderDefinition = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 100);
	UGameplayActionDefinition* TimedDefinition = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 1);
	AddLock(*HolderDefinition, TAG_GameplayAction_Test_LockA);
	AddLock(*TimedDefinition, TAG_GameplayAction_Test_LockA);
	TimedDefinition->MaxQueueTimeSeconds = 1.0;
	TimedDefinition->JournalRequirement = EGameplayActionJournalRequirement::Optional;

	const FGameplayActionSubmissionResult Holder = Component->SubmitAction(MakeRequest(*HolderDefinition));
	const FGameplayActionSubmissionResult Timed = Component->SubmitAction(MakeRequest(*TimedDefinition));
	TestEqual(TEXT("Timed action starts queued"), Timed.Status, EGameplayActionSubmissionStatus::AcceptedQueued);
	TestTrue(TEXT("Timed queued action enables the component tick"), Component->IsComponentTickEnabled());

	Component->InvokeTickForTest(0.4f);
	EGameplayActionState State = EGameplayActionState::Created;
	Component->GetActionState(Timed.Handle, State);
	TestEqual(TEXT("Timed action remains queued before its deadline"), State, EGameplayActionState::Queued);
	FGameplayActionDebugSnapshot Snapshot = Component->GetDebugSnapshot();
	TestEqual(TEXT("Debug snapshot contains the timed queue entry"), Snapshot.QueuedActions.Num(), 1);
	if (Snapshot.QueuedActions.Num() == 1)
	{
		const FGameplayActionDebugEntry& Entry = Snapshot.QueuedActions[0];
		TestTrue(TEXT("Debug marks queue timeout active"), Entry.bHasQueueTimeout);
		TestFalse(TEXT("Debug marks timed queue as finite"), Entry.bQueueTimeUnlimited);
		TestTrue(TEXT("Debug records queue elapsed time"), FMath::IsNearlyEqual(Entry.QueueElapsedSeconds, 0.4, 0.001));
		TestTrue(TEXT("Debug records queue remaining time"), FMath::IsNearlyEqual(Entry.QueueRemainingSeconds, 0.6, 0.001));
	}

	Component->PauseActions();
	TestFalse(TEXT("Local pause disables timeout ticking"), Component->IsComponentTickEnabled());
	Component->InvokeTickForTest(5.0f);
	Snapshot = Component->GetDebugSnapshot();
	TestTrue(TEXT("Local pause freezes queue elapsed time"),
		Snapshot.QueuedActions.Num() == 1
		&& FMath::IsNearlyEqual(Snapshot.QueuedActions[0].QueueElapsedSeconds, 0.4, 0.001));
	Component->ResumeActions();

	APlayerState* Pauser = World->SpawnActor<APlayerState>();
	World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	TestTrue(TEXT("Test world reports paused"), World->IsPaused());
	Component->InvokeTickForTest(0.5f);
	Snapshot = Component->GetDebugSnapshot();
	TestTrue(TEXT("World pause freezes queue elapsed time"),
		Snapshot.QueuedActions.Num() == 1
		&& FMath::IsNearlyEqual(Snapshot.QueuedActions[0].QueueElapsedSeconds, 0.4, 0.001));
	World->GetWorldSettings()->SetPauserPlayerState(nullptr);

	Component->InvokeTickForTest(0.6f);
	FGameplayActionResult TimedResult;
	TestTrue(TEXT("Expired queue action retains a result"), Component->GetActionResult(Timed.Handle, TimedResult));
	TestEqual(TEXT("Queue timeout ends as Failed"), TimedResult.TerminalState, EGameplayActionState::Failed);
	TestTrue(TEXT("Queue timeout has the authoritative reason"),
		TimedResult.ReasonTag == GameplayActionTags::Result_Failure_QueueTimeout);
	TestEqual(TEXT("Queued timeout never calls Action Start"), UGameplayActionTestInstance::StartedCount, 1);
	TestFalse(TEXT("Component tick disables after last timed queue entry expires"), Component->IsComponentTickEnabled());

	const FGameplayActionEvent* EndedEvent = Observer->ObservedEvents.FindByPredicate(
		[Timed](const FGameplayActionEvent& Event)
		{
			return Event.Handle == Timed.Handle && Event.EventType == EGameplayActionEventType::Ended;
		});
	TestNotNull(TEXT("Timeout emits an Ended event"), EndedEvent);
	if (EndedEvent)
	{
		TestEqual(TEXT("Ended event preserves max queue time"), EndedEvent->MaxQueueTimeSeconds, 1.0);
		TestTrue(TEXT("Ended event preserves elapsed queue time"), EndedEvent->QueueElapsedSeconds >= 1.0);
		TestTrue(TEXT("Ended event preserves queue timeout reason"),
			EndedEvent->Result.ReasonTag == GameplayActionTags::Result_Failure_QueueTimeout);
	}
	const FGameplayActionEvent* JournalEndedEvent = Observer->JournalEvents.FindByPredicate(
		[Timed](const FGameplayActionEvent& Event)
		{
			return Event.Handle == Timed.Handle && Event.EventType == EGameplayActionEventType::Ended;
		});
	TestNotNull(TEXT("Timeout Ended event reaches the journal"), JournalEndedEvent);

	UGameplayActionDefinition* UnlimitedDefinition = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 1);
	AddLock(*UnlimitedDefinition, TAG_GameplayAction_Test_LockA);
	UnlimitedDefinition->MaxQueueTimeSeconds = 0.0;
	const FGameplayActionSubmissionResult Unlimited = Component->SubmitAction(MakeRequest(*UnlimitedDefinition));
	TestEqual(TEXT("Zero queue timeout still queues"), Unlimited.Status, EGameplayActionSubmissionStatus::AcceptedQueued);
	TestFalse(TEXT("Unlimited queued action alone does not require component tick"), Component->IsComponentTickEnabled());
	Component->InvokeTickForTest(20.0f);
	Component->GetActionState(Unlimited.Handle, State);
	TestEqual(TEXT("Zero queue timeout never expires"), State, EGameplayActionState::Queued);
	Component->CancelAction(Unlimited.Handle, FGameplayTag());

	UGameplayActionDefinition* StartsBeforeDeadlineDefinition = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 1);
	AddLock(*StartsBeforeDeadlineDefinition, TAG_GameplayAction_Test_LockA);
	StartsBeforeDeadlineDefinition->MaxQueueTimeSeconds = 2.0;
	const FGameplayActionSubmissionResult StartsBeforeDeadline =
		Component->SubmitAction(MakeRequest(*StartsBeforeDeadlineDefinition));
	Component->InvokeTickForTest(0.5f);
	Component->CancelAction(Holder.Handle, FGameplayTag());
	Component->GetActionState(StartsBeforeDeadline.Handle, State);
	TestEqual(TEXT("Action starts when locks release before its deadline"), State, EGameplayActionState::Running);
	UGameplayActionInstance* StartedInstance = Component->GetActionInstance(StartsBeforeDeadline.Handle);
	TestNotNull(TEXT("Started-before-deadline instance exists"), StartedInstance);
	if (StartedInstance)
	{
		TestTrue(TEXT("Queue elapsed snapshot stops at start"),
			FMath::IsNearlyEqual(StartedInstance->GetQueueElapsedSeconds(), 0.5, 0.001));
	}
	Component->InvokeTickForTest(5.0f);
	Component->GetActionState(StartsBeforeDeadline.Handle, State);
	TestEqual(TEXT("Queue timeout no longer advances after Start"), State, EGameplayActionState::Running);
	Component->CancelAction(StartsBeforeDeadline.Handle, FGameplayTag());

	UGameplayActionTestComponent* OrderedComponent = NewObject<UGameplayActionTestComponent>(Owner);
	Owner->AddInstanceComponent(OrderedComponent);
	OrderedComponent->RegisterComponent();
	UGameplayActionTestObserver* OrderedObserver = NewObject<UGameplayActionTestObserver>();
	OrderedComponent->OnActionEvent.AddDynamic(OrderedObserver, &UGameplayActionTestObserver::HandleActionEvent);
	const FGameplayActionSubmissionResult OrderedHolder =
		OrderedComponent->SubmitAction(MakeRequest(*HolderDefinition));
	const FGameplayActionSubmissionResult FirstExpired =
		OrderedComponent->SubmitAction(MakeRequest(*TimedDefinition));
	const FGameplayActionSubmissionResult SecondExpired =
		OrderedComponent->SubmitAction(MakeRequest(*TimedDefinition));
	const int32 OrderedEventBase = OrderedObserver->ObservedEvents.Num();
	OrderedComponent->InvokeTickForTest(1.0f);
	TArray<FGameplayActionHandle> ExpiredOrder;
	for (int32 Index = OrderedEventBase; Index < OrderedObserver->ObservedEvents.Num(); ++Index)
	{
		const FGameplayActionEvent& Event = OrderedObserver->ObservedEvents[Index];
		if (Event.EventType == EGameplayActionEventType::Ended
			&& (Event.Handle == FirstExpired.Handle || Event.Handle == SecondExpired.Handle))
		{
			ExpiredOrder.Add(Event.Handle);
		}
	}
	TestEqual(TEXT("Both same-frame timed actions expire"), ExpiredOrder.Num(), 2);
	if (ExpiredOrder.Num() == 2)
	{
		TestEqual(TEXT("Same-priority expirations use FIFO submission order"), ExpiredOrder[0], FirstExpired.Handle);
		TestEqual(TEXT("Second expiration follows deterministically"), ExpiredOrder[1], SecondExpired.Handle);
	}
	OrderedComponent->CancelAction(OrderedHolder.Handle, FGameplayTag());

	Component->Deactivate();
	OrderedComponent->Deactivate();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsJournalTest,
	"GameplayActions.Journal.TransactionalAndReentrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsJournalTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UGameplayActionDefinition* Required = MakeDefinition();
	Required->JournalRequirement = EGameplayActionJournalRequirement::Required;
	UGameplayActionComponent* MissingSinkComponent = MakeComponent();
	TestEqual(TEXT("Required journal rejects without sink"),
		MissingSinkComponent->SubmitAction(MakeRequest(*Required)).Status,
		EGameplayActionSubmissionStatus::RejectedJournal);

	UGameplayActionComponent* Component = MakeComponent();
	UGameplayActionTestObserver* Sink = NewObject<UGameplayActionTestObserver>();
	Sink->Component = Component;
	Sink->bAcceptJournal = false;
	TestTrue(TEXT("Journal sink registers"), Component->RegisterJournalSink(Sink));
	UGameplayActionDefinition* Low = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 1);
	AddLock(*Low, TAG_GameplayAction_Test_LockA);
	const FGameplayActionSubmissionResult LowResult = Component->SubmitAction(MakeRequest(*Low));
	UGameplayActionDefinition* RequiredHigh = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 50);
	RequiredHigh->JournalRequirement = EGameplayActionJournalRequirement::Required;
	AddLock(*RequiredHigh, TAG_GameplayAction_Test_LockA);
	const FGameplayActionSubmissionResult RejectedHigh = Component->SubmitAction(MakeRequest(*RequiredHigh));
	TestEqual(TEXT("Required sink rejection is atomic"), RejectedHigh.Status, EGameplayActionSubmissionStatus::RejectedJournal);
	EGameplayActionState LowState;
	Component->GetActionState(LowResult.Handle, LowState);
	TestEqual(TEXT("Journal rejection performs no preemption"), LowState, EGameplayActionState::Running);

	UGameplayActionDefinition* Optional = MakeDefinition();
	Optional->JournalRequirement = EGameplayActionJournalRequirement::Optional;
	const FGameplayActionSubmissionResult OptionalResult = Component->SubmitAction(MakeRequest(*Optional));
	TestTrue(TEXT("Optional sink rejection does not block action"), OptionalResult.IsAccepted());

	UGameplayActionComponent* AcceptedComponent = MakeComponent();
	UGameplayActionTestObserver* AcceptedSink = NewObject<UGameplayActionTestObserver>();
	AcceptedSink->Component = AcceptedComponent;
	AcceptedComponent->RegisterJournalSink(AcceptedSink);
	AcceptedComponent->OnActionEvent.AddDynamic(AcceptedSink, &UGameplayActionTestObserver::HandleActionEvent);
	FGameplayActionRequest RequiredRequest = MakeRequest(*Required);
	AcceptedSink->CallbackRequest = RequiredRequest;
	AcceptedSink->bSubmitDuringJournal = true;
	const FGameplayActionSubmissionResult Accepted = AcceptedComponent->SubmitAction(RequiredRequest);
	TestTrue(TEXT("Required journal accepts action"), Accepted.IsAccepted());
	TestEqual(TEXT("Submission during initial journal is observably rejected"),
		AcceptedSink->ReentrantJournalSubmissionStatus, EGameplayActionSubmissionStatus::RejectedReentrant);
	AcceptedComponent->CancelAction(Accepted.Handle, FGameplayTag());
	TestTrue(TEXT("Journal received Accepted, Started, and Ended"), AcceptedSink->JournalEvents.Num() >= 3);
	TestEqual(TEXT("First journal event is Accepted"), AcceptedSink->JournalEvents[0].EventType, EGameplayActionEventType::Accepted);
	TestEqual(TEXT("Lifecycle ends with Ended"), AcceptedSink->ObservedEvents.Last().EventType, EGameplayActionEventType::Ended);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsEventReentrancyTest,
	"GameplayActions.Events.FifoAndLifecycleReentrancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsEventReentrancyTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UGameplayActionComponent* Component = MakeComponent();
	UGameplayActionDefinition* Definition = MakeDefinition();
	UGameplayActionTestObserver* Observer = NewObject<UGameplayActionTestObserver>();
	Observer->Component = Component;
	Observer->CallbackRequest = MakeRequest(*Definition);
	Observer->bSubmitOnEnded = true;
	Component->OnActionEvent.AddDynamic(Observer, &UGameplayActionTestObserver::HandleActionEvent);
	const FGameplayActionSubmissionResult First = Component->SubmitAction(MakeRequest(*Definition));
	Component->CancelAction(First.Handle, FGameplayTag());
	TestTrue(TEXT("Submit from OnActionEnded is accepted"), Observer->CallbackSubmissionResult.IsAccepted());
	TestTrue(TEXT("Reentrant accepted handle differs"), Observer->CallbackSubmissionResult.Handle != First.Handle);

	UGameplayActionDefinition* OtherDefinition = MakeDefinition();
	const FGameplayActionSubmissionResult Other = Component->SubmitAction(MakeRequest(*OtherDefinition));
	Observer->HandleToCancel = Other.Handle;
	Observer->bCancelOtherOnStarted = true;
	const FGameplayActionSubmissionResult Trigger = Component->SubmitAction(MakeRequest(*Definition));
	TestTrue(TEXT("Trigger starts"), Trigger.IsAccepted());
	TestEqual(TEXT("Cancelling another action from event callback succeeds"),
		Observer->CallbackCancelResult, EGameplayActionOperationResult::Succeeded);
	FGameplayActionResult OtherResult;
	TestTrue(TEXT("Cancelled action result is retained"), Component->GetActionResult(Other.Handle, OtherResult));
	TestEqual(TEXT("Other action was cancelled"), OtherResult.TerminalState, EGameplayActionState::Cancelled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsWaitActionTest,
	"GameplayActions.WaitAction.TimerPauseCancelAndLateCallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsWaitActionTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GameplayActionsWaitTestWorld"));
	TestNotNull(TEXT("Test world created"), World);
	if (!World)
	{
		return false;
	}
	AActor* Owner = World->SpawnActor<AActor>();
	UGameplayActionComponent* Component = NewObject<UGameplayActionComponent>(Owner);
	UGameplayActionDefinition* WaitDefinition = MakeDefinition(UGameplayWaitAction::StaticClass());
	SetSchema(*WaitDefinition, { { TEXT("Duration"), EPropertyBagPropertyType::Double } });
	WaitDefinition->DefaultParameters.SetValueDouble(TEXT("Duration"), 0.1);
	auto TickTimerManager = [World](const float DeltaSeconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(DeltaSeconds);
	};

	const FGameplayActionSubmissionResult Wait = Component->SubmitAction(MakeRequest(*WaitDefinition));
	TestEqual(TEXT("Wait starts"), Wait.Status, EGameplayActionSubmissionStatus::AcceptedStarted);
	TickTimerManager(0.0f);
	TickTimerManager(0.04f);
	Component->PauseActions();
	TickTimerManager(0.2f);
	EGameplayActionState State;
	Component->GetActionState(Wait.Handle, State);
	TestEqual(TEXT("Paused timer does not complete"), State, EGameplayActionState::Paused);
	Component->ResumeActions();
	TickTimerManager(0.07f);
	FGameplayActionResult WaitResult;
	TestTrue(TEXT("Resumed wait completes"), Component->GetActionResult(Wait.Handle, WaitResult));
	TestEqual(TEXT("Wait succeeds"), WaitResult.TerminalState, EGameplayActionState::Succeeded);

	WaitDefinition->DefaultParameters.SetValueDouble(TEXT("Duration"), 0.2);
	const FGameplayActionSubmissionResult Cancelled = Component->SubmitAction(MakeRequest(*WaitDefinition));
	Component->CancelAction(Cancelled.Handle, FGameplayTag());
	TickTimerManager(0.3f);
	Component->GetActionResult(Cancelled.Handle, WaitResult);
	TestEqual(TEXT("Cancelled wait stays cancelled after old deadline"), WaitResult.TerminalState, EGameplayActionState::Cancelled);

	const FGameplayActionSubmissionResult Aborted = Component->SubmitAction(MakeRequest(*WaitDefinition));
	Component->AbortAllActions(GameplayActionTags::Result_Aborted_SystemReset);
	TickTimerManager(0.3f);
	Component->GetActionResult(Aborted.Handle, WaitResult);
	TestEqual(TEXT("Aborted wait stays aborted after old deadline"), WaitResult.TerminalState, EGameplayActionState::Aborted);

	AddLock(*WaitDefinition, TAG_GameplayAction_Test_LockA);
	WaitDefinition->DefaultPriority = 1;
	UGameplayActionDefinition* QueueHolder = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 100);
	AddLock(*QueueHolder, TAG_GameplayAction_Test_LockA);
	const FGameplayActionSubmissionResult QueueHolderResult = Component->SubmitAction(MakeRequest(*QueueHolder));
	const FGameplayActionSubmissionResult QueuedWait = Component->SubmitAction(MakeRequest(*WaitDefinition));
	TestEqual(TEXT("Wait action can initialize while queued"), QueuedWait.Status, EGameplayActionSubmissionStatus::AcceptedQueued);
	TickTimerManager(0.3f);
	EGameplayActionState QueuedWaitState = EGameplayActionState::Created;
	Component->GetActionState(QueuedWait.Handle, QueuedWaitState);
	TestEqual(TEXT("Queued wait has no timer before Action Start"), QueuedWaitState, EGameplayActionState::Queued);
	Component->CancelAction(QueueHolderResult.Handle, FGameplayTag());
	Component->GetActionState(QueuedWait.Handle, QueuedWaitState);
	TestEqual(TEXT("Queued wait starts after lock release"), QueuedWaitState, EGameplayActionState::Running);
	TickTimerManager(0.21f);
	TestTrue(TEXT("Queued wait completes only after its real Start"), Component->GetActionResult(QueuedWait.Handle, WaitResult));
	TestEqual(TEXT("Queued wait succeeds"), WaitResult.TerminalState, EGameplayActionState::Succeeded);

	const FGameplayActionSubmissionResult InterruptedWait = Component->SubmitAction(MakeRequest(*WaitDefinition));
	UGameplayActionDefinition* Interruptor = MakeDefinition(UGameplayActionTestInstance::StaticClass(), 10);
	AddLock(*Interruptor, TAG_GameplayAction_Test_LockA);
	TestTrue(TEXT("Higher priority action starts over wait"), Component->SubmitAction(MakeRequest(*Interruptor)).IsAccepted());
	TickTimerManager(0.3f);
	Component->GetActionResult(InterruptedWait.Handle, WaitResult);
	TestEqual(TEXT("Interrupted wait stays interrupted after old deadline"), WaitResult.TerminalState, EGameplayActionState::Interrupted);

	WaitDefinition->ExecutionLocks.Reset();
	WaitDefinition->DefaultParameters.SetValueDouble(TEXT("Duration"), -1.0);
	TestEqual(TEXT("Negative wait duration fails instance validation"),
		Component->SubmitAction(MakeRequest(*WaitDefinition)).Status, EGameplayActionSubmissionStatus::RejectedValidation);

	Component->Deactivate();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsValidationTest,
	"GameplayActions.Validation.DefinitionSchemaAndLocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsValidationTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;
	UGameplayActionDefinition* MissingTag = MakeDefinition();
	MissingTag->ActionTag = FGameplayTag();
	TestFalse(TEXT("Factory rejects missing Action Tag"),
		UGameplayActionBlueprintLibrary::CreateActionRequest(MissingTag).WasCreated());

	UGameplayActionDefinition* InvalidLock = MakeDefinition();
	InvalidLock->ExecutionLocks.AddTag(GameplayActionTags::Result_Success);
	FGameplayActionRequest InvalidRequest = MakeRequest(*InvalidLock);
	TestEqual(TEXT("Submit rejects lock outside root hierarchy"),
		MakeComponent()->SubmitAction(InvalidRequest).Status, EGameplayActionSubmissionStatus::RejectedInvalidRequest);

#if WITH_EDITOR
	FDataValidationContext Context;
	TestEqual(TEXT("Definition data validation rejects invalid lock"),
		InvalidLock->IsDataValid(Context), EDataValidationResult::Invalid);
	UGameplayActionDefinition* Valid = MakeDefinition();
	AddLock(*Valid, TAG_GameplayAction_Test_LockA);
	FDataValidationContext ValidContext;
	TestEqual(TEXT("Definition data validation accepts valid definition"),
		Valid->IsDataValid(ValidContext), EDataValidationResult::Valid);
#endif

	UGameplayActionDefinition* SchemaDefinition = MakeDefinition();
	SetSchema(*SchemaDefinition, { { TEXT("IntValue"), EPropertyBagPropertyType::Int32 } });
	FGameplayActionRequest SchemaRequest = MakeRequest(*SchemaDefinition);
	SetSchema(*SchemaDefinition, { { TEXT("FloatValue"), EPropertyBagPropertyType::Float } });
	TestEqual(TEXT("Submit detects Definition/request schema drift"),
		MakeComponent()->SubmitAction(SchemaRequest).Status, EGameplayActionSubmissionStatus::RejectedInvalidRequest);

	UGameplayActionDefinition* NegativeQueueTimeout = MakeDefinition();
	NegativeQueueTimeout->MaxQueueTimeSeconds = -1.0;
	TestFalse(TEXT("Factory rejects a negative queue timeout"),
		UGameplayActionBlueprintLibrary::CreateActionRequest(NegativeQueueTimeout).WasCreated());

	UGameplayActionDefinition* NonFiniteQueueTimeout = MakeDefinition();
	NonFiniteQueueTimeout->MaxQueueTimeSeconds = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("Factory rejects a non-finite queue timeout"),
		UGameplayActionBlueprintLibrary::CreateActionRequest(NonFiniteQueueTimeout).WasCreated());

	UGameplayActionDefinition* ValidationDefinition = MakeDefinition();
	SetSchema(*ValidationDefinition, { { TEXT("RejectStart"), EPropertyBagPropertyType::Bool } });
	ValidationDefinition->DefaultParameters.SetValueBool(TEXT("RejectStart"), true);
	TestEqual(TEXT("Instance validation can reject a structurally valid request"),
		MakeComponent()->SubmitAction(MakeRequest(*ValidationDefinition)).Status,
		EGameplayActionSubmissionStatus::RejectedValidation);

	UGameplayActionDefinition* ReentrantDefinition = MakeDefinition();
	FGameplayActionRequest ReentrantRequest = MakeRequest(*ReentrantDefinition);
	UGameplayActionTestInstance::ValidationCallbackRequest = ReentrantRequest;
	UGameplayActionTestInstance::bSubmitDuringValidation = true;
	UGameplayActionComponent* ReentrantComponent = MakeComponent();
	TestTrue(TEXT("Outer submission survives validation callback"), ReentrantComponent->SubmitAction(ReentrantRequest).IsAccepted());
	TestEqual(TEXT("Submission during validation is observably rejected"),
		UGameplayActionTestInstance::ValidationReentrantSubmissionStatus,
		EGameplayActionSubmissionStatus::RejectedReentrant);

	UGameplayActionDefinition* PreflightDefinition = MakeDefinition();
	UGameplayActionComponent* PreflightComponent = MakeComponent();
	const FGameplayActionSubmissionResult Preflight = PreflightComponent->PreflightAction(MakeRequest(*PreflightDefinition));
	TestEqual(TEXT("Preflight reports immediate start"), Preflight.Status, EGameplayActionSubmissionStatus::AcceptedStarted);
	TestFalse(TEXT("Preflight does not allocate a public handle"), Preflight.Handle.IsValid());
	TestEqual(TEXT("Preflight does not create runtime instances"), PreflightComponent->GetActiveActionHandles().Num(), 0);

	UGameplayActionDefinition* AbstractDefinition = MakeDefinition(UGameplayActionInstance::StaticClass());
	TestFalse(TEXT("Factory rejects abstract instance classes"),
		UGameplayActionBlueprintLibrary::CreateActionRequest(AbstractDefinition).WasCreated());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsNativeEndedObserverTest,
	"GameplayActions.Events.NativeEndedObserverLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsNativeEndedObserverTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;

	UGameplayActionDefinition* Definition = MakeDefinition();
	UGameplayActionComponent* Component = MakeComponent();
	bool bNativeObserverCalled = false;
	bool bInstanceWasStillOwned = false;
	FGameplayActionHandle ObservedHandle;

	const FDelegateHandle DelegateHandle = Component->OnActionEndedNative().AddLambda(
		[&](const FGameplayActionEvent& Event)
		{
			bNativeObserverCalled = true;
			ObservedHandle = Event.Handle;
			bInstanceWasStillOwned = Component->GetActionInstance(Event.Handle) != nullptr;
		});

	const FGameplayActionSubmissionResult Submission =
		Component->SubmitAction(MakeRequest(*Definition));
	UGameplayActionTestInstance* Instance =
		Cast<UGameplayActionTestInstance>(Component->GetActionInstance(Submission.Handle));
	TestNotNull(TEXT("Accepted action exposes its transient instance"), Instance);
	if (Instance)
	{
		Instance->CompleteForTest();
	}

	TestTrue(TEXT("Native Ended observer is called"), bNativeObserverCalled);
	TestEqual(TEXT("Native observer receives the authoritative handle"), ObservedHandle, Submission.Handle);
	TestTrue(TEXT("Instance remains owned during native Ended dispatch"), bInstanceWasStillOwned);
	TestNull(TEXT("Instance is released immediately after Ended dispatch"),
		Component->GetActionInstance(Submission.Handle));
	TestTrue(TEXT("Movement lock belongs to the authored lock hierarchy"),
		GameplayActionTags::Lock_Movement.GetTag().MatchesTag(
			GameplayActionTags::Lock_Root.GetTag()));

	Component->OnActionEndedNative().Remove(DelegateHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsExternalExecutionLocksTest,
	"GameplayActions.Locks.SourceOwnedExternalAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsExternalExecutionLocksTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsTests;

	UGameplayActionDefinition* Definition = MakeDefinition();
	AddLock(*Definition, GameplayActionTags::Lock_Movement);
	UGameplayActionComponent* Component = MakeComponent();
	UGameplayActionComponent* BarrierA = NewObject<UGameplayActionComponent>();
	UGameplayActionComponent* BarrierB = NewObject<UGameplayActionComponent>();

	const FGameplayActionSubmissionResult Running = Component->SubmitAction(MakeRequest(*Definition));
	TestEqual(TEXT("Movement action starts before external locking"), Running.Status, EGameplayActionSubmissionStatus::AcceptedStarted);

	FGameplayTagContainer MovementLocks;
	MovementLocks.AddTag(GameplayActionTags::Lock_Movement);
	TestEqual(
		TEXT("First source acquires Movement"),
		Component->AcquireExternalExecutionLocks(BarrierA, MovementLocks, GameplayActionTags::Result_Interrupted_External),
		EGameplayActionOperationResult::Succeeded);
	FGameplayActionResult InterruptedResult;
	TestTrue(TEXT("Conflicting running action has a terminal result"), Component->GetActionResult(Running.Handle, InterruptedResult));
	TestEqual(TEXT("External lock interrupts the conflicting action"), InterruptedResult.TerminalState, EGameplayActionState::Interrupted);
	TestTrue(TEXT("Interruption reason is preserved"), InterruptedResult.ReasonTag.MatchesTagExact(GameplayActionTags::Result_Interrupted_External));
	TestTrue(TEXT("Movement lock is externally held"), Component->IsExternalExecutionLockHeld(GameplayActionTags::Lock_Movement));
	TestEqual(
		TEXT("New conflicting submission is rejected while held"),
		Component->SubmitAction(MakeRequest(*Definition)).Status,
		EGameplayActionSubmissionStatus::RejectedBlocked);

	TestEqual(
		TEXT("Second source independently acquires Movement"),
		Component->AcquireExternalExecutionLocks(BarrierB, MovementLocks, GameplayActionTags::Result_Interrupted_External),
		EGameplayActionOperationResult::Succeeded);
	TestEqual(
		TEXT("First source releases only its own lock"),
		Component->ReleaseExternalExecutionLocks(BarrierA),
		EGameplayActionOperationResult::Succeeded);
	TestTrue(TEXT("Second source still retains Movement"), Component->IsExternalExecutionLockHeld(GameplayActionTags::Lock_Movement));
	TestEqual(
		TEXT("Second source releases its lock"),
		Component->ReleaseExternalExecutionLocks(BarrierB),
		EGameplayActionOperationResult::Succeeded);
	TestFalse(TEXT("Movement is unlocked after all owners release"), Component->IsExternalExecutionLockHeld(GameplayActionTags::Lock_Movement));
	TestEqual(
		TEXT("Movement action can start after release"),
		Component->SubmitAction(MakeRequest(*Definition)).Status,
		EGameplayActionSubmissionStatus::AcceptedStarted);
	return true;
}

#endif
