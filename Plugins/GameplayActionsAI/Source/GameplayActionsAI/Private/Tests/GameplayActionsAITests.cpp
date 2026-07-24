#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Actions/GameplayActionDefinition.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayActionTags.h"
#include "NativeGameplayTags.h"
#include "StateTree/GameplayActionStateTreeObserver.h"
#include "StructUtils/PropertyBag.h"
#include "Tests/GameplayActionsAITestTypes.h"
#include "Types/GameplayActionExecutionSpec.h"
#include "UObject/Package.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(
	TAG_GameplayActionsAI_Test_Action,
	"GameplayAction.Test.AI");

namespace GameplayActionsAITests
{
	UGameplayActionDefinition* MakeDefinition()
	{
		UGameplayActionDefinition* Definition = NewObject<UGameplayActionDefinition>();
		Definition->InstanceClass = UGameplayActionsAITestAction::StaticClass();
		Definition->ActionTag = TAG_GameplayActionsAI_Test_Action;
		return Definition;
	}

	template <typename TKeyType>
	TKeyType* AddKey(UBlackboardData& Data, const FName Name)
	{
		FBlackboardEntry& Entry = Data.Keys.AddDefaulted_GetRef();
		Entry.EntryName = Name;
		TKeyType* Key = NewObject<TKeyType>(&Data);
		Entry.KeyType = Key;
		return Key;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsAIExecutionSpecTest,
	"GameplayActionsAI.ExecutionSpec.SchemaDeepCopyAndFactory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsAIExecutionSpecTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsAITests;

	UGameplayActionDefinition* Definition = MakeDefinition();
	Definition->DefaultParameters.InitializeFromBagStruct(
		UPropertyBag::GetOrCreateFromDescs({
			{ TEXT("Count"), EPropertyBagPropertyType::Int32 },
			{ TEXT("Target"), EPropertyBagPropertyType::Object, UObject::StaticClass() },
			{ TEXT("Location"), EPropertyBagPropertyType::Struct, TBaseStructure<FVector>::Get() },
			{ TEXT("Transform"), EPropertyBagPropertyType::Struct, TBaseStructure<FTransform>::Get() },
			{ TEXT("Mode"), EPropertyBagPropertyType::Enum, StaticEnum<EGameplayActionsAITestEnum>() },
			{ TEXT("Payload"), EPropertyBagPropertyType::Struct, FGameplayActionsAITestStruct::StaticStruct() },
			{ TEXT("SoftTarget"), EPropertyBagPropertyType::SoftObject, UObject::StaticClass() }
		}));
	Definition->DefaultParameters.SetValueInt32(TEXT("Count"), 7);

	FGameplayActionExecutionSpec Spec;
	Spec.Definition = Definition;
	TestTrue(TEXT("Spec synchronizes to Definition"), Spec.SynchronizeParameters());
	TestTrue(TEXT("Spec reports exact schema"), Spec.IsSchemaSynchronized());
	Spec.Parameters.SetValueInt32(TEXT("Count"), 19);
	Spec.bOverridePriority = true;
	Spec.Priority = 42;
	Spec.bOverrideBlockedPolicy = true;
	Spec.BlockedPolicy = EGameplayActionBlockedPolicy::Reject;

	TestEqual(TEXT("Definition defaults remain isolated"),
		Definition->DefaultParameters.GetValueInt32(TEXT("Count")).GetValue(),
		7);

	const FGameplayActionRequestBuildResult Build =
		GameplayActionsAI::BuildRequest(Spec, nullptr, {});
	TestTrue(TEXT("Authoritative factory builds request"), Build.bSucceeded);
	if (!Build.bSucceeded)
	{
		AddError(FString::Printf(TEXT("Request build failed: %s"), *Build.DiagnosticMessage));
		return false;
	}
	TestTrue(TEXT("Built request is initialized"), Build.Request.IsInitialized());
	TestEqual(TEXT("Spec value reaches request"),
		Build.Request.GetParameters().GetValueInt32(TEXT("Count")).GetValue(),
		19);
	TestTrue(TEXT("Priority override is preserved"), Build.Request.HasPriorityOverride());
	TestEqual(TEXT("Priority override value"), Build.Request.GetPriorityOverride(), 42);
	TestEqual(TEXT("Blocked policy override"),
		Build.Request.GetBlockedPolicyOverride(),
		EGameplayActionBlockedPolicy::Reject);

	Definition->DefaultParameters.InitializeFromBagStruct(
		UPropertyBag::GetOrCreateFromDescs({
			{ TEXT("Count"), EPropertyBagPropertyType::Int32 },
			{ TEXT("NewField"), EPropertyBagPropertyType::Bool }
		}));
	TestFalse(TEXT("Definition schema drift is detected"), Spec.IsSchemaSynchronized());
	TestTrue(TEXT("Resynchronization migrates compatible values"), Spec.SynchronizeParameters());
	TestEqual(TEXT("Compatible value survives migration"),
		Spec.Parameters.GetValueInt32(TEXT("Count")).GetValue(),
		19);
	TestTrue(TEXT("New field is present"),
		Spec.Parameters.GetValueBool(TEXT("NewField")).HasValue());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsAIBlackboardBindingTest,
	"GameplayActionsAI.Blackboard.TypeSafeBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsAIBlackboardBindingTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsAITests;

	UGameplayActionDefinition* Definition = MakeDefinition();
	Definition->DefaultParameters.InitializeFromBagStruct(
		UPropertyBag::GetOrCreateFromDescs({
			{ TEXT("Enabled"), EPropertyBagPropertyType::Bool },
			{ TEXT("Count"), EPropertyBagPropertyType::Int32 },
			{ TEXT("Weight"), EPropertyBagPropertyType::Float },
			{ TEXT("Mode"), EPropertyBagPropertyType::Enum, StaticEnum<EGameplayActionsAITestEnum>() },
			{ TEXT("Label"), EPropertyBagPropertyType::Name },
			{ TEXT("Message"), EPropertyBagPropertyType::String },
			{ TEXT("Target"), EPropertyBagPropertyType::Object, UObject::StaticClass() },
			{ TEXT("TargetClass"), EPropertyBagPropertyType::Class, UObject::StaticClass() },
			{ TEXT("Location"), EPropertyBagPropertyType::Struct, TBaseStructure<FVector>::Get() },
			{ TEXT("Rotation"), EPropertyBagPropertyType::Struct, TBaseStructure<FRotator>::Get() },
			{ TEXT("Transform"), EPropertyBagPropertyType::Struct, TBaseStructure<FTransform>::Get() },
			{ TEXT("Payload"), EPropertyBagPropertyType::Struct, FGameplayActionsAITestStruct::StaticStruct() },
			{ TEXT("SoftTarget"), EPropertyBagPropertyType::SoftObject, UObject::StaticClass() },
			{ TEXT("SoftTargetClass"), EPropertyBagPropertyType::SoftClass, UObject::StaticClass() }
		}));

	UBlackboardData* Data = NewObject<UBlackboardData>();
	AddKey<UBlackboardKeyType_Bool>(*Data, TEXT("EnabledKey"));
	AddKey<UBlackboardKeyType_Int>(*Data, TEXT("CountKey"));
	AddKey<UBlackboardKeyType_Float>(*Data, TEXT("WeightKey"));
	UBlackboardKeyType_Enum* ModeKey =
		AddKey<UBlackboardKeyType_Enum>(*Data, TEXT("ModeKey"));
	ModeKey->EnumType = StaticEnum<EGameplayActionsAITestEnum>();
	AddKey<UBlackboardKeyType_Name>(*Data, TEXT("LabelKey"));
	AddKey<UBlackboardKeyType_String>(*Data, TEXT("MessageKey"));
	UBlackboardKeyType_Object* TargetKey =
		AddKey<UBlackboardKeyType_Object>(*Data, TEXT("TargetKey"));
	TargetKey->BaseClass = UObject::StaticClass();
	UBlackboardKeyType_Class* ClassKey =
		AddKey<UBlackboardKeyType_Class>(*Data, TEXT("ClassKey"));
	ClassKey->BaseClass = UObject::StaticClass();
	AddKey<UBlackboardKeyType_Vector>(*Data, TEXT("LocationKey"));
	AddKey<UBlackboardKeyType_Rotator>(*Data, TEXT("RotationKey"));
	UBlackboardKeyType_Struct* TransformKey =
		AddKey<UBlackboardKeyType_Struct>(*Data, TEXT("TransformKey"));
	TransformKey->DefaultValue.InitializeAs(TBaseStructure<FTransform>::Get());
	// Runtime-created Struct keys do not receive the editor property-change callback that
	// normally derives their memory size, so finish the same initialization through PostLoad.
	TransformKey->PostLoad();
	UBlackboardKeyType_Struct* SoftKey =
		AddKey<UBlackboardKeyType_Struct>(*Data, TEXT("SoftKey"));
	SoftKey->DefaultValue.InitializeAs(FSoftObjectPath::StaticStruct());
	SoftKey->PostLoad();
	UBlackboardKeyType_Struct* PayloadKey =
		AddKey<UBlackboardKeyType_Struct>(*Data, TEXT("PayloadKey"));
	PayloadKey->DefaultValue.InitializeAs(FGameplayActionsAITestStruct::StaticStruct());
	PayloadKey->PostLoad();

	const UWorld::InitializationValues WorldInitialization =
		UWorld::InitializationValues()
			.CreatePhysicsScene(false)
			.CreateNavigation(false)
			.CreateAISystem(true)
			.ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("GameplayActionsAIBlackboardTest")),
		nullptr,
		true,
		ERHIFeatureLevel::Num,
		&WorldInitialization);
	if (!TestNotNull(TEXT("Transient Blackboard test world"), World))
	{
		return false;
	}
	AActor* BlackboardOwner = World->SpawnActor<AActor>();
	UBlackboardComponent* Blackboard = NewObject<UBlackboardComponent>(BlackboardOwner);
	BlackboardOwner->AddInstanceComponent(Blackboard);
	Blackboard->RegisterComponent();
	TestTrue(TEXT("Blackboard initializes"), Blackboard->InitializeBlackboard(*Data));
	if (!Blackboard->GetBlackboardAsset())
	{
		AddError(TEXT("The test Blackboard did not initialize."));
		World->DestroyWorld(false);
		return false;
	}
	Blackboard->SetValueAsBool(TEXT("EnabledKey"), true);
	Blackboard->SetValueAsInt(TEXT("CountKey"), 37);
	Blackboard->SetValueAsFloat(TEXT("WeightKey"), 2.5f);
	Blackboard->SetValueAsEnum(TEXT("ModeKey"), static_cast<uint8>(EGameplayActionsAITestEnum::Second));
	Blackboard->SetValueAsName(TEXT("LabelKey"), TEXT("BoundLabel"));
	Blackboard->SetValueAsString(TEXT("MessageKey"), TEXT("Bound message"));
	UObject* Target = Definition;
	Blackboard->SetValueAsObject(TEXT("TargetKey"), Target);
	Blackboard->SetValueAsClass(TEXT("ClassKey"), UGameplayActionsAITestAction::StaticClass());
	Blackboard->SetValueAsVector(TEXT("LocationKey"), FVector(10.0, 20.0, 30.0));
	const FRotator Rotation(10.0, 20.0, 30.0);
	Blackboard->SetValueAsRotator(TEXT("RotationKey"), Rotation);
	const FTransform Transform(FRotator(0.0, 45.0, 0.0), FVector(1.0, 2.0, 3.0));
	Blackboard->SetValue<UBlackboardKeyType_Struct>(
		TEXT("TransformKey"),
		FConstStructView::Make(Transform));
	const FSoftObjectPath SoftPath(TEXT("/Game/Tests/SoftTarget.SoftTarget"));
	Blackboard->SetValue<UBlackboardKeyType_Struct>(
		TEXT("SoftKey"),
		FConstStructView::Make(SoftPath));
	FGameplayActionsAITestStruct Payload;
	Payload.Value = 73;
	Blackboard->SetValue<UBlackboardKeyType_Struct>(
		TEXT("PayloadKey"),
		FConstStructView::Make(Payload));

	FGameplayActionExecutionSpec Spec;
	Spec.Definition = Definition;
	Spec.SynchronizeParameters();

	auto Binding = [](const FName ParameterName, const FName KeyName)
	{
		FGameplayActionBlackboardParameterBinding Result;
		Result.ParameterName = ParameterName;
		Result.BlackboardKey.SelectedKeyName = KeyName;
		return Result;
	};
	const TArray<FGameplayActionBlackboardParameterBinding> Bindings = {
		Binding(TEXT("Enabled"), TEXT("EnabledKey")),
		Binding(TEXT("Count"), TEXT("CountKey")),
		Binding(TEXT("Weight"), TEXT("WeightKey")),
		Binding(TEXT("Mode"), TEXT("ModeKey")),
		Binding(TEXT("Label"), TEXT("LabelKey")),
		Binding(TEXT("Message"), TEXT("MessageKey")),
		Binding(TEXT("Target"), TEXT("TargetKey")),
		Binding(TEXT("TargetClass"), TEXT("ClassKey")),
		Binding(TEXT("Location"), TEXT("LocationKey")),
		Binding(TEXT("Rotation"), TEXT("RotationKey")),
		Binding(TEXT("Transform"), TEXT("TransformKey")),
		Binding(TEXT("Payload"), TEXT("PayloadKey")),
		Binding(TEXT("SoftTarget"), TEXT("SoftKey")),
		Binding(TEXT("SoftTargetClass"), TEXT("ClassKey"))
	};

	const FGameplayActionRequestBuildResult Build =
		GameplayActionsAI::BuildRequest(Spec, Blackboard, Bindings);
	TestTrue(TEXT("All supported Blackboard bindings build"), Build.bSucceeded);
	if (!Build.bSucceeded)
	{
		AddError(FString::Printf(TEXT("Blackboard request build failed: %s"), *Build.DiagnosticMessage));
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(TEXT("Bool binding"),
		Build.Request.GetParameters().GetValueBool(TEXT("Enabled")).GetValue());
	TestEqual(TEXT("Int binding"),
		Build.Request.GetParameters().GetValueInt32(TEXT("Count")).GetValue(),
		37);
	TestEqual(TEXT("Float binding"),
		Build.Request.GetParameters().GetValueFloat(TEXT("Weight")).GetValue(),
		2.5f);
	TestEqual(TEXT("Enum binding"),
		Build.Request.GetParameters().GetValueEnum<EGameplayActionsAITestEnum>(TEXT("Mode")).GetValue(),
		EGameplayActionsAITestEnum::Second);
	TestEqual(TEXT("Name binding"),
		Build.Request.GetParameters().GetValueName(TEXT("Label")).GetValue(),
		FName(TEXT("BoundLabel")));
	TestEqual(TEXT("String binding"),
		Build.Request.GetParameters().GetValueString(TEXT("Message")).GetValue(),
		FString(TEXT("Bound message")));
	TestEqual(TEXT("Object binding"),
		Build.Request.GetParameters().GetValueObject(TEXT("Target")).GetValue(),
		Target);
	TestEqual(TEXT("Class binding"),
		Build.Request.GetParameters().GetValueClass(TEXT("TargetClass")).GetValue(),
		UGameplayActionsAITestAction::StaticClass());
	TestEqual(TEXT("Vector binding"),
		*Build.Request.GetParameters().GetValueStruct<FVector>(TEXT("Location")).GetValue(),
		FVector(10.0, 20.0, 30.0));
	TestEqual(TEXT("Rotator binding"),
		*Build.Request.GetParameters().GetValueStruct<FRotator>(TEXT("Rotation")).GetValue(),
		Rotation);
	TestEqual(TEXT("Transform binding"),
		*Build.Request.GetParameters().GetValueStruct<FTransform>(TEXT("Transform")).GetValue(),
		Transform);
	TestEqual(TEXT("Struct binding"),
		Build.Request.GetParameters()
			.GetValueStruct<FGameplayActionsAITestStruct>(TEXT("Payload"))
			.GetValue()
			->Value,
		73);
	TestEqual(TEXT("Soft object binding"),
		Build.Request.GetParameters().GetValueSoftPath(TEXT("SoftTarget")).GetValue(),
		SoftPath);
	TestEqual(TEXT("Soft class binding"),
		Build.Request.GetParameters().GetValueSoftPath(TEXT("SoftTargetClass")).GetValue(),
		FSoftObjectPath(UGameplayActionsAITestAction::StaticClass()));

	FGameplayActionBlackboardParameterBinding Missing =
		Binding(TEXT("MissingField"), TEXT("LocationKey"));
	TestFalse(TEXT("Bindings cannot create missing fields"),
		GameplayActionsAI::BuildRequest(Spec, Blackboard, { Missing }).bSucceeded);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsAIResolverAndSubmissionTest,
	"GameplayActionsAI.Runtime.ComponentResolutionAndSynchronousEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsAIResolverAndSubmissionTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsAITests;

	AActor* Owner = NewObject<AActor>();
	UGameplayActionComponent* Component = NewObject<UGameplayActionComponent>(Owner);
	Owner->AddInstanceComponent(Component);
	FString Diagnostic;
	TestEqual(TEXT("One component resolves"),
		GameplayActionsAI::ResolveActionComponent(Owner, nullptr, Diagnostic),
		Component);

	UGameplayActionComponent* Ambiguous = NewObject<UGameplayActionComponent>(Owner);
	Owner->AddInstanceComponent(Ambiguous);
	TestNull(TEXT("Multiple components are rejected"),
		GameplayActionsAI::ResolveActionComponent(Owner, nullptr, Diagnostic));
	TestTrue(TEXT("Ambiguity is diagnosed"), Diagnostic.Contains(TEXT("ambiguous")));
	Owner->RemoveInstanceComponent(Ambiguous);

	FGameplayActionExecutionSpec Spec;
	Spec.Definition = MakeDefinition();
	Spec.SynchronizeParameters();
	const FGameplayActionRequestBuildResult Build =
		GameplayActionsAI::BuildRequest(Spec, nullptr, {});
	if (!Build.bSucceeded)
	{
		AddError(FString::Printf(TEXT("Synchronous test request build failed: %s"), *Build.DiagnosticMessage));
		return false;
	}
	UGameplayActionsAITestAction::bCompleteSynchronously = true;
	const FGameplayActionSubmissionResult Submission = Component->SubmitAction(Build.Request);
	UGameplayActionsAITestAction::bCompleteSynchronously = false;
	FGameplayActionResult Result;
	TestTrue(TEXT("Synchronous action submission is accepted"), Submission.IsAccepted());
	TestTrue(TEXT("Synchronous terminal result remains queryable"),
		Component->GetActionResult(Submission.Handle, Result));
	TestEqual(TEXT("Synchronous result maps to success"),
		Result.TerminalState,
		EGameplayActionState::Succeeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsAIBehaviorTreeLifecycleTest,
	"GameplayActionsAI.BehaviorTree.ExecuteQueueRejectAbort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsAIBehaviorTreeLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsAITests;

	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game,
		false,
		MakeUniqueObjectName(
			GetTransientPackage(),
			UWorld::StaticClass(),
			TEXT("GameplayActionsAIBehaviorTreeTest")));
	if (!TestNotNull(TEXT("Transient Behavior Tree test world"), World))
	{
		return false;
	}

	AAIController* Controller = World->SpawnActor<AAIController>();
	UGameplayActionComponent* Component =
		NewObject<UGameplayActionComponent>(Controller);
	UBehaviorTreeComponent* BehaviorTree =
		NewObject<UBehaviorTreeComponent>(Controller);
	Controller->AddInstanceComponent(Component);
	Controller->AddInstanceComponent(BehaviorTree);
	Component->RegisterComponent();
	BehaviorTree->RegisterComponent();

	UGameplayActionDefinition* ImmediateDefinition = MakeDefinition();
	UGameplayActionsAITestBTTask* ImmediateTask =
		NewObject<UGameplayActionsAITestBTTask>();
	ImmediateTask->ExecutionSpec.Definition = ImmediateDefinition;
	ImmediateTask->ExecutionSpec.SynchronizeParameters();
	UGameplayActionsAITestAction::bCompleteSynchronously = true;
	TestEqual(TEXT("Synchronous success returns BT success"),
		ImmediateTask->ExecuteForTest(*BehaviorTree),
		EBTNodeResult::Succeeded);
	UGameplayActionsAITestAction::bCompleteSynchronously = false;

	Component->Deactivate();
	UGameplayActionsAITestBTTask* RejectedTask =
		NewObject<UGameplayActionsAITestBTTask>();
	RejectedTask->ExecutionSpec.Definition = MakeDefinition();
	RejectedTask->ExecutionSpec.SynchronizeParameters();
	TestEqual(TEXT("Rejected submission returns BT failure"),
		RejectedTask->ExecuteForTest(*BehaviorTree),
		EBTNodeResult::Failed);
	Component->Activate();

	UGameplayActionDefinition* HolderDefinition = MakeDefinition();
	HolderDefinition->ExecutionLocks.AddTag(GameplayActionTags::Lock_Movement);
	UGameplayActionDefinition* QueuedDefinition = MakeDefinition();
	QueuedDefinition->ExecutionLocks.AddTag(GameplayActionTags::Lock_Movement);
	FGameplayActionExecutionSpec HolderSpec;
	HolderSpec.Definition = HolderDefinition;
	HolderSpec.SynchronizeParameters();
	const FGameplayActionRequestBuildResult HolderBuild =
		GameplayActionsAI::BuildRequest(HolderSpec, nullptr, {});
	if (!HolderBuild.bSucceeded)
	{
		AddError(FString::Printf(TEXT("BT holder request failed: %s"), *HolderBuild.DiagnosticMessage));
		World->DestroyWorld(false);
		return false;
	}
	const FGameplayActionSubmissionResult Holder =
		Component->SubmitAction(HolderBuild.Request);

	UGameplayActionsAITestBTTask* QueuedTask =
		NewObject<UGameplayActionsAITestBTTask>();
	QueuedTask->ExecutionSpec.Definition = QueuedDefinition;
	QueuedTask->ExecutionSpec.SynchronizeParameters();
	TestEqual(TEXT("Queued submission keeps BT task in progress"),
		QueuedTask->ExecuteForTest(*BehaviorTree),
		EBTNodeResult::InProgress);
	const TArray<FGameplayActionHandle> QueuedHandles =
		Component->GetQueuedActionHandles();
	if (!TestEqual(TEXT("BT task created exactly one queued action"), QueuedHandles.Num(), 1))
	{
		World->DestroyWorld(false);
		return false;
	}

	TestEqual(TEXT("BT abort reports Aborted"),
		QueuedTask->AbortForTest(*BehaviorTree),
		EBTNodeResult::Aborted);
	FGameplayActionResult AbortedTaskResult;
	TestTrue(TEXT("Aborted BT task leaves a terminal result"),
		Component->GetActionResult(QueuedHandles[0], AbortedTaskResult));
	TestEqual(TEXT("BT abort cancels only its own action"),
		AbortedTaskResult.TerminalState,
		EGameplayActionState::Cancelled);

	if (UGameplayActionsAITestAction* HolderInstance =
		Cast<UGameplayActionsAITestAction>(Component->GetActionInstance(Holder.Handle)))
	{
		HolderInstance->CompleteForTest(true);
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGameplayActionsAIStateTreeObserverTest,
	"GameplayActionsAI.StateTree.ObserverLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayActionsAIStateTreeObserverTest::RunTest(const FString& Parameters)
{
	using namespace GameplayActionsAITests;

	UGameplayActionComponent* Component = NewObject<UGameplayActionComponent>();
	FGameplayActionExecutionSpec Spec;
	Spec.Definition = MakeDefinition();
	Spec.SynchronizeParameters();
	const FGameplayActionRequestBuildResult Build =
		GameplayActionsAI::BuildRequest(Spec, nullptr, {});
	if (!Build.bSucceeded)
	{
		AddError(FString::Printf(TEXT("StateTree observer request failed: %s"), *Build.DiagnosticMessage));
		return false;
	}

	UGameplayActionStateTreeObserver* Observer =
		NewObject<UGameplayActionStateTreeObserver>();
	Observer->Bind(*Component);
	TestEqual(TEXT("Observer retains the exact accepting component"),
		Observer->GetActionComponent(),
		Component);
	Observer->BeginSubmission();
	UGameplayActionsAITestAction::bCompleteSynchronously = true;
	const FGameplayActionSubmissionResult Synchronous =
		Component->SubmitAction(Build.Request);
	UGameplayActionsAITestAction::bCompleteSynchronously = false;
	Observer->CompleteSubmission(Synchronous.Handle);
	TestTrue(TEXT("Observer captures synchronous Ended during submission"),
		Observer->HasTerminalResult());
	TestEqual(TEXT("Synchronous observer result is successful"),
		Observer->GetTerminalResult().TerminalState,
		EGameplayActionState::Succeeded);

	Observer->BeginSubmission();
	const FGameplayActionSubmissionResult Running =
		Component->SubmitAction(Build.Request);
	Observer->CompleteSubmission(Running.Handle);
	TestFalse(TEXT("Running action has no terminal observer result"),
		Observer->HasTerminalResult());
	Component->CancelAction(
		Running.Handle,
		GameplayActionTags::Result_Cancelled_ByRequester);
	TestTrue(TEXT("Observer captures external cancellation"),
		Observer->HasTerminalResult());
	TestEqual(TEXT("External cancellation remains observable"),
		Observer->GetTerminalResult().TerminalState,
		EGameplayActionState::Cancelled);

	Observer->Unbind();
	const FGameplayActionResult ResultBeforeLateEvent =
		Observer->GetTerminalResult();
	UGameplayActionsAITestAction::bCompleteSynchronously = true;
	Component->SubmitAction(Build.Request);
	UGameplayActionsAITestAction::bCompleteSynchronously = false;
	TestEqual(TEXT("Unbound observer ignores later Ended events"),
		Observer->GetTerminalResult().TerminalState,
		ResultBeforeLateEvent.TerminalState);
	return true;
}

#endif
