#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Conditions/PuzzleAllCondition.h"
#include "Conditions/PuzzleInputStateCondition.h"
#include "Controllers/PuzzleController.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Graph/PuzzleGraphSubsystem.h"
#include "NativeGameplayTags.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Tests/PuzzleSystemTestTypes.h"
#include "UObject/UnrealType.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PuzzleGraphPrimary, "Puzzle.Test.Graph.Primary");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PuzzleGraphGate, "Puzzle.Test.Graph.Gate");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_PuzzleGraphSecondary, "Puzzle.Test.Graph.Secondary");

namespace UE::PuzzleSystem::Graph::Tests
{
	struct FScopedGraphWorld
	{
		explicit FScopedGraphWorld(const TCHAR* Name)
		{
			Context = GEngine ? &GEngine->CreateNewWorldContext(EWorldType::Game) : nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			if (World)
			{
				World->AddToRoot();
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		~FScopedGraphWorld()
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

		void StartPlay() const
		{
			if (!World || World->HasBegunPlay())
			{
				return;
			}

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
	};

	template <typename ActorType = AActor>
	ActorType* Spawn(UWorld& World, const FName Name)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = Name;
		Parameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<ActorType>(ActorType::StaticClass(), FTransform::Identity, Parameters);
	}

	template <typename ComponentType>
	ComponentType* AddComponent(AActor& Owner, const FName Name)
	{
		ComponentType* Component = NewObject<ComponentType>(&Owner, ComponentType::StaticClass(), Name);
		Owner.AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}

	UPuzzleInputStateCondition* NewInputCondition(
		APuzzleController& Controller,
		const FName InputId,
		const bool bExpectedActive = true)
	{
		UPuzzleInputStateCondition* Condition = NewObject<UPuzzleInputStateCondition>(&Controller);
		Condition->InputId = InputId;
		Condition->bExpectedActive = bExpectedActive;
		return Condition;
	}

	FPuzzleInputBinding& AddPrimaryInput(
		APuzzleController& Controller,
		const FName InputId,
		UPuzzleEmitterComponent& Emitter,
		const FGameplayTag SignalTag)
	{
		FPuzzleInputBinding& Binding = Controller.InputBindings.AddDefaulted_GetRef();
		Binding.InputId = InputId;
		Binding.EmitterActor = Emitter.GetOwner();
		Binding.bSpecifyEmitterComponent = true;
		Binding.EmitterComponentName = Emitter.GetFName();
		Binding.SignalTag = SignalTag;
		return Binding;
	}

	FPuzzleEmitterGateBinding& AddGateInput(
		FPuzzleInputBinding& PrimaryBinding,
		const FName InputId,
		UPuzzleEmitterComponent& Emitter,
		const FGameplayTag SignalTag)
	{
		FPuzzleEmitterGateBinding& Binding = PrimaryBinding.EmitterGates.AddDefaulted_GetRef();
		Binding.InputId = InputId;
		Binding.EmitterActor = Emitter.GetOwner();
		Binding.bSpecifyEmitterComponent = true;
		Binding.EmitterComponentName = Emitter.GetFName();
		Binding.SignalTag = SignalTag;
		return Binding;
	}

	void AddReceiver(APuzzleController& Controller, UPuzzleReceiverComponent& Receiver)
	{
		FPuzzleReceiverBinding& Binding = Controller.ReceiverBindings.AddDefaulted_GetRef();
		Binding.ReceiverActor = Receiver.GetOwner();
		Binding.bSpecifyReceiverComponent = true;
		Binding.ReceiverComponentName = Receiver.GetFName();
	}

	APuzzleController* ConfigureSingleController(
		UWorld& World,
		const FName ControllerName,
		UPuzzleEmitterComponent& PrimaryEmitter,
		UPuzzleReceiverComponent& Receiver,
		UPuzzleEmitterComponent* GateEmitter = nullptr)
	{
		APuzzleController* Controller = Spawn<APuzzleController>(World, ControllerName);
		if (!Controller)
		{
			return nullptr;
		}

		FPuzzleInputBinding& PrimaryBinding = AddPrimaryInput(
			*Controller,
			TEXT("Main"),
			PrimaryEmitter,
			TAG_PuzzleGraphPrimary);
		if (GateEmitter)
		{
			AddGateInput(PrimaryBinding, TEXT("Enabled"), *GateEmitter, TAG_PuzzleGraphGate);
			PrimaryBinding.GateConditions.Add(NewInputCondition(*Controller, TEXT("Enabled")));
		}
		Controller->RootCondition = NewInputCondition(*Controller, TEXT("Main"));
		AddReceiver(*Controller, Receiver);
		return Controller;
	}

	const FPuzzleGraphLink* FindLink(
		const TArray<FPuzzleGraphLink>& Links,
		const EPuzzleGraphLinkKind Kind)
	{
		return Links.FindByPredicate([Kind](const FPuzzleGraphLink& Link)
		{
			return Link.LinkKind == Kind;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleGraphTopologyAndQueryTest,
	"PuzzleSystem.Graph.TopologyAndQueries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleGraphTopologyAndQueryTest::RunTest(const FString& Parameters)
{
	using namespace UE::PuzzleSystem::Graph::Tests;
	FScopedGraphWorld TestWorld(TEXT("PuzzleGraphTopologyWorld"));
	if (!TestNotNull(TEXT("Graph test world exists"), TestWorld.World))
	{
		return false;
	}

	AActor* SourceActor = Spawn(*TestWorld.World, TEXT("SourceActor"));
	AActor* GateActor = Spawn(*TestWorld.World, TEXT("GateActor"));
	AActor* ReceiverActor = Spawn(*TestWorld.World, TEXT("ReceiverActor"));
	UPuzzleEmitterComponent* UnusedEmitter = AddComponent<UPuzzleEmitterComponent>(*SourceActor, TEXT("UnusedEmitter"));
	UPuzzleEmitterComponent* PrimaryEmitter = AddComponent<UPuzzleEmitterComponent>(*SourceActor, TEXT("PowerEmitter"));
	UPuzzleEmitterComponent* GateEmitter = AddComponent<UPuzzleEmitterComponent>(*GateActor, TEXT("GateEmitter"));
	UPuzzleReceiverComponent* FirstReceiver = AddComponent<UPuzzleReceiverComponent>(*ReceiverActor, TEXT("FirstReceiver"));
	UPuzzleReceiverComponent* SecondReceiver = AddComponent<UPuzzleReceiverComponent>(*ReceiverActor, TEXT("SecondReceiver"));
	APuzzleController* Controller = Spawn<APuzzleController>(*TestWorld.World, TEXT("GraphController"));
	if (!TestNotNull(TEXT("Source Actor exists"), SourceActor)
		|| !TestNotNull(TEXT("Gate Actor exists"), GateActor)
		|| !TestNotNull(TEXT("Receiver Actor exists"), ReceiverActor)
		|| !TestNotNull(TEXT("Controller exists"), Controller))
	{
		return false;
	}

	PrimaryEmitter->SetSignalState(TAG_PuzzleGraphPrimary, true, nullptr);
	GateEmitter->SetSignalState(TAG_PuzzleGraphGate, true, nullptr);
	FPuzzleInputBinding& PrimaryBinding = AddPrimaryInput(
		*Controller,
		TEXT("MainPower"),
		*PrimaryEmitter,
		TAG_PuzzleGraphPrimary);
	AddGateInput(PrimaryBinding, TEXT("Permission"), *GateEmitter, TAG_PuzzleGraphGate);
	PrimaryBinding.GateConditions.Add(NewInputCondition(*Controller, TEXT("Permission")));
	Controller->RootCondition = NewInputCondition(*Controller, TEXT("MainPower"));
	AddReceiver(*Controller, *FirstReceiver);
	AddReceiver(*Controller, *SecondReceiver);

	UPuzzleGraphSubsystem* Graph = TestWorld.World->GetSubsystem<UPuzzleGraphSubsystem>();
	if (!TestNotNull(TEXT("Graph subsystem exists"), Graph))
	{
		return false;
	}
	TestWorld.StartPlay();

	TestEqual(TEXT("One Controller registration increments topology once"), Graph->GetGraphTopologyRevision(), int64(1));
	const FPuzzleActorGraphView SourceView = Graph->QueryActorGraph(SourceActor);
	TestEqual(TEXT("Primary Actor has one incoming gate relationship"), SourceView.IncomingGateLinks.Num(), 1);
	TestEqual(TEXT("One primary input produces one link per Receiver"), SourceView.OutgoingPrimaryLinks.Num(), 2);
	TestTrue(TEXT("Primary Actor has no outgoing gate relationship"), SourceView.OutgoingGateLinks.IsEmpty());
	TestEqual(TEXT("Actor view carries the current topology revision"), SourceView.GraphTopologyRevision, int64(1));

	const FPuzzleActorGraphView GateView = Graph->QueryActorGraph(GateActor);
	TestEqual(TEXT("Gate-only Actor exposes its outgoing gate relationship"), GateView.OutgoingGateLinks.Num(), 1);
	const FPuzzleActorGraphView ReceiverView = Graph->QueryActorGraph(ReceiverActor);
	TestEqual(TEXT("Receiver Actor exposes both component endpoints"), ReceiverView.IncomingPrimaryLinks.Num(), 2);

	const TArray<FPuzzleGraphLink> PrimaryComponentLinks = Graph->QueryLinksForEmitterComponent(PrimaryEmitter);
	TestEqual(TEXT("Exact primary component owns two signals plus one incoming gate context"), PrimaryComponentLinks.Num(), 3);
	TestTrue(TEXT("Unused component is not substituted for an explicit binding"), Graph->QueryLinksForEmitterComponent(UnusedEmitter).IsEmpty());
	TestEqual(TEXT("Exact first Receiver component has one link"), Graph->QueryLinksForReceiverComponent(FirstReceiver).Num(), 1);
	TestEqual(TEXT("Exact second Receiver component has one link"), Graph->QueryLinksForReceiverComponent(SecondReceiver).Num(), 1);

	const FPuzzleGraphLink* GateLink = FindLink(SourceView.IncomingGateLinks, EPuzzleGraphLinkKind::GateInfluence);
	if (!TestNotNull(TEXT("Gate descriptor exists"), GateLink))
	{
		return false;
	}
	TestTrue(TEXT("Gate handle is opaque but valid"), GateLink->LinkHandle.IsValid());
	TestEqual(TEXT("Gate descriptor preserves Controller"), GateLink->Controller.Get(), Controller);
	TestEqual(TEXT("Gate descriptor preserves primary InputId"), GateLink->PrimaryInputId, FName(TEXT("MainPower")));
	TestTrue(TEXT("Gate descriptor preserves primary signal tag"), GateLink->PrimarySignalTag == TAG_PuzzleGraphPrimary);
	TestEqual(TEXT("Gate descriptor preserves primary component"), GateLink->PrimaryEmitterComponent.Get(), PrimaryEmitter);
	TestEqual(TEXT("Gate descriptor preserves gate InputId"), GateLink->GateInputId, FName(TEXT("Permission")));
	TestTrue(TEXT("Gate descriptor preserves gate signal tag"), GateLink->GateSignalTag == TAG_PuzzleGraphGate);
	TestEqual(TEXT("Gate descriptor preserves gate component"), GateLink->GateEmitterComponent.Get(), GateEmitter);
	TestNull(TEXT("GateInfluence is not duplicated per Receiver endpoint"), GateLink->TargetReceiverComponent.Get());

	TestEqual(TEXT("First authored Receiver remains first"), SourceView.OutgoingPrimaryLinks[0].TargetReceiverComponent.Get(), FirstReceiver);
	TestEqual(TEXT("Second authored Receiver remains second"), SourceView.OutgoingPrimaryLinks[1].TargetReceiverComponent.Get(), SecondReceiver);
	TestTrue(TEXT("Receiver endpoint handles are distinct"),
		SourceView.OutgoingPrimaryLinks[0].LinkHandle != SourceView.OutgoingPrimaryLinks[1].LinkHandle);
	const FPuzzleActorGraphView RepeatedSourceView = Graph->QueryActorGraph(SourceActor);
	TestTrue(TEXT("Repeated query preserves deterministic first handle"),
		RepeatedSourceView.OutgoingPrimaryLinks[0].LinkHandle == SourceView.OutgoingPrimaryLinks[0].LinkHandle);
	TestEqual(TEXT("Incoming query combines the gate group"), Graph->QueryIncomingLinksForActor(SourceActor).Num(), 1);
	TestEqual(TEXT("Outgoing query combines primary links"), Graph->QueryOutgoingLinksForActor(SourceActor).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleGraphStateAndEndpointLifecycleTest,
	"PuzzleSystem.Graph.StateEventsAndEndpointLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleGraphStateAndEndpointLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace UE::PuzzleSystem::Graph::Tests;
	FScopedGraphWorld TestWorld(TEXT("PuzzleGraphStateWorld"));
	if (!TestNotNull(TEXT("State test world exists"), TestWorld.World))
	{
		return false;
	}

	AActor* PrimaryActor = Spawn(*TestWorld.World, TEXT("PrimaryActor"));
	AActor* GateActor = Spawn(*TestWorld.World, TEXT("GateActor"));
	AActor* ReceiverActor = Spawn(*TestWorld.World, TEXT("ReceiverActor"));
	UPuzzleEmitterComponent* PrimaryEmitter = AddComponent<UPuzzleEmitterComponent>(*PrimaryActor, TEXT("PrimaryEmitter"));
	UPuzzleEmitterComponent* GateEmitter = AddComponent<UPuzzleEmitterComponent>(*GateActor, TEXT("GateEmitter"));
	UPuzzleReceiverComponent* Receiver = AddComponent<UPuzzleReceiverComponent>(*ReceiverActor, TEXT("Receiver"));
	UPuzzleTestSignalPayload* Payload = NewObject<UPuzzleTestSignalPayload>(PrimaryEmitter);
	UPuzzleTestSignalPayload* GatePayload = NewObject<UPuzzleTestSignalPayload>(GateEmitter);
	Payload->Value = 37;
	GatePayload->Value = 73;
	PrimaryEmitter->SetSignalState(TAG_PuzzleGraphPrimary, true, Payload);
	GateEmitter->SetSignalState(TAG_PuzzleGraphGate, true, GatePayload);
	APuzzleController* Controller = ConfigureSingleController(
		*TestWorld.World,
		TEXT("StateController"),
		*PrimaryEmitter,
		*Receiver,
		GateEmitter);
	if (!TestNotNull(TEXT("State Controller exists"), Controller))
	{
		return false;
	}

	UPuzzleGraphSubsystem* Graph = TestWorld.World->GetSubsystem<UPuzzleGraphSubsystem>();
	int32 TopologyEventCount = 0;
	int32 StateEventCount = 0;
	const FDelegateHandle TopologyDelegateHandle = Graph->OnPuzzleGraphTopologyChangedNative.AddLambda(
		[&TopologyEventCount](int64, APuzzleController*, EPuzzleGraphTopologyChangeKind)
		{
			++TopologyEventCount;
		});
	const FDelegateHandle StateDelegateHandle = Graph->OnPuzzleGraphLinkStateChangedNative.AddLambda(
		[&StateEventCount](const FPuzzleGraphLinkHandle&, const FPuzzleGraphLinkState&, const FPuzzleGraphLinkState&)
		{
			++StateEventCount;
		});
	TestWorld.StartPlay();

	const FPuzzleActorGraphView InitialView = Graph->QueryActorGraph(PrimaryActor);
	const FPuzzleGraphLink& PrimaryLink = InitialView.OutgoingPrimaryLinks[0];
	const FPuzzleGraphLink& GateLink = InitialView.IncomingGateLinks[0];
	FPuzzleGraphLinkState InitialPrimaryState;
	FPuzzleGraphLinkState InitialGateState;
	TestTrue(TEXT("Initial PrimarySignal state is queryable"), Graph->TryGetLinkState(PrimaryLink.LinkHandle, InitialPrimaryState));
	TestTrue(TEXT("Initial GateInfluence state is queryable"), Graph->TryGetLinkState(GateLink.LinkHandle, InitialGateState));
	TestTrue(TEXT("Raw primary is valid and active"), InitialPrimaryState.bRawPrimaryValid && InitialPrimaryState.bRawPrimaryActive);
	TestTrue(TEXT("Raw primary revision is preserved"), InitialPrimaryState.RawPrimaryRevision > 0);
	TestEqual(TEXT("Raw payload is referenced without duplication"), InitialPrimaryState.RawPrimaryPayload.Get(), static_cast<UPuzzleSignalPayload*>(Payload));
	TestEqual(TEXT("Open gate mode is explicit"), InitialPrimaryState.GateMode, EPuzzleGraphGateMode::Open);
	TestTrue(TEXT("Effective primary starts active"), InitialPrimaryState.bEffectivePrimaryActive);
	TestTrue(TEXT("Effective primary revision is preserved"), InitialPrimaryState.EffectivePrimaryRevision > 0);
	TestEqual(TEXT("Effective payload preserves the admitted primary payload"), InitialPrimaryState.EffectivePrimaryPayload.Get(), static_cast<UPuzzleSignalPayload*>(Payload));
	TestTrue(TEXT("Controller result is independently valid and active"), InitialPrimaryState.bControllerResultValid && InitialPrimaryState.bControllerResultActive);
	TestTrue(TEXT("Receiver snapshot starts valid and active"), InitialPrimaryState.bTargetReceiverValid && InitialPrimaryState.bTargetReceiverEffectiveActive);
	TestTrue(TEXT("Gate link exposes its individual gate state"), InitialGateState.bGateInputValid && InitialGateState.bGateInputActive);
	TestTrue(TEXT("Gate input revision is preserved"), InitialGateState.GateInputRevision > 0);
	TestEqual(TEXT("Gate input payload is referenced without duplication"), InitialGateState.GateInputPayload.Get(), static_cast<UPuzzleSignalPayload*>(GatePayload));
	TestEqual(TEXT("Registration emits one topology event"), TopologyEventCount, 1);
	TestEqual(TEXT("Registration emits no synthetic state event"), StateEventCount, 0);

	const int64 StableTopologyRevision = Graph->GetGraphTopologyRevision();
	GateEmitter->SetSignalState(TAG_PuzzleGraphGate, false, nullptr);
	TestEqual(TEXT("Gate close updates PrimarySignal and GateInfluence once each"), StateEventCount, 2);
	TestEqual(TEXT("Gate close does not change topology revision"), Graph->GetGraphTopologyRevision(), StableTopologyRevision);
	FPuzzleGraphLinkState ClosedPrimaryState;
	FPuzzleGraphLinkState ClosedGateState;
	Graph->TryGetLinkState(PrimaryLink.LinkHandle, ClosedPrimaryState);
	Graph->TryGetLinkState(GateLink.LinkHandle, ClosedGateState);
	TestTrue(TEXT("Closed gate preserves active raw primary"), ClosedPrimaryState.bRawPrimaryActive);
	TestEqual(TEXT("Closed gate mode is distinct from invalid"), ClosedPrimaryState.GateMode, EPuzzleGraphGateMode::Closed);
	TestTrue(TEXT("Closed gate remains valid"), ClosedPrimaryState.bGateValid);
	TestFalse(TEXT("Closed gate blocks effective primary"), ClosedPrimaryState.bEffectivePrimaryActive);
	TestEqual(TEXT("Closed gate retains the raw primary payload"), ClosedPrimaryState.RawPrimaryPayload.Get(), static_cast<UPuzzleSignalPayload*>(Payload));
	TestNull(TEXT("Closed gate suppresses the effective primary payload"), ClosedPrimaryState.EffectivePrimaryPayload.Get());
	TestFalse(TEXT("Gate input reports its own inactive state"), ClosedGateState.bGateInputActive);
	TestFalse(TEXT("Controller result settles inactive"), ClosedPrimaryState.bControllerResultActive);
	TestFalse(TEXT("Receiver effective snapshot settles inactive"), ClosedPrimaryState.bTargetReceiverEffectiveActive);

	TestFalse(TEXT("Duplicate gate publication is deduplicated by the Emitter"),
		GateEmitter->SetSignalState(TAG_PuzzleGraphGate, false, nullptr));
	TestEqual(TEXT("Duplicate publication emits no graph state event"), StateEventCount, 2);

	GateEmitter->DestroyComponent();
	FPuzzleGraphLinkState InvalidGatePrimaryState;
	FPuzzleGraphLinkState InvalidGateLinkState;
	TestTrue(TEXT("Gate-invalidated PrimarySignal handle remains valid"),
		Graph->TryGetLinkState(PrimaryLink.LinkHandle, InvalidGatePrimaryState));
	TestTrue(TEXT("Gate-invalidated GateInfluence handle remains valid"),
		Graph->TryGetLinkState(GateLink.LinkHandle, InvalidGateLinkState));
	TestEqual(TEXT("Destroyed gate produces Invalid mode"), InvalidGatePrimaryState.GateMode, EPuzzleGraphGateMode::Invalid);
	TestFalse(TEXT("Destroyed gate makes effective primary invalid"), InvalidGatePrimaryState.bEffectivePrimaryValid);
	TestFalse(TEXT("Destroyed gate input is invalid, not inactive"), InvalidGateLinkState.bGateInputValid);
	TestEqual(TEXT("Endpoint invalidation leaves topology unchanged"), Graph->GetGraphTopologyRevision(), StableTopologyRevision);

	PrimaryEmitter->DestroyComponent();
	FPuzzleGraphLinkState InvalidPrimaryState;
	TestTrue(TEXT("Destroyed primary retains contextual handle"),
		Graph->TryGetLinkState(PrimaryLink.LinkHandle, InvalidPrimaryState));
	TestFalse(TEXT("Destroyed primary is explicitly raw-invalid"), InvalidPrimaryState.bRawPrimaryValid);

	Receiver->DestroyComponent();
	FPuzzleGraphLinkState InvalidReceiverState;
	TestTrue(TEXT("Destroyed Receiver retains contextual handle"),
		Graph->TryGetLinkState(PrimaryLink.LinkHandle, InvalidReceiverState));
	TestFalse(TEXT("Destroyed Receiver endpoint is explicitly invalid"), InvalidReceiverState.bTargetReceiverValid);
	TestEqual(TEXT("All endpoint destruction remains state-only"), Graph->GetGraphTopologyRevision(), StableTopologyRevision);

	Graph->OnPuzzleGraphTopologyChangedNative.Remove(TopologyDelegateHandle);
	Graph->OnPuzzleGraphLinkStateChangedNative.Remove(StateDelegateHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleGraphContextAndOrderingTest,
	"PuzzleSystem.Graph.ControllerContextsAndOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleGraphContextAndOrderingTest::RunTest(const FString& Parameters)
{
	using namespace UE::PuzzleSystem::Graph::Tests;
	FScopedGraphWorld TestWorld(TEXT("PuzzleGraphContextWorld"));
	if (!TestNotNull(TEXT("Context test world exists"), TestWorld.World))
	{
		return false;
	}

	AActor* SourceActor = Spawn(*TestWorld.World, TEXT("SharedSource"));
	AActor* OpenGateActor = Spawn(*TestWorld.World, TEXT("OpenGate"));
	AActor* ClosedGateActor = Spawn(*TestWorld.World, TEXT("ClosedGate"));
	AActor* FirstTarget = Spawn(*TestWorld.World, TEXT("FirstTarget"));
	UPuzzleEmitterComponent* SharedEmitter = AddComponent<UPuzzleEmitterComponent>(*SourceActor, TEXT("SharedEmitter"));
	UPuzzleEmitterComponent* OpenGate = AddComponent<UPuzzleEmitterComponent>(*OpenGateActor, TEXT("GateEmitter"));
	UPuzzleEmitterComponent* ClosedGate = AddComponent<UPuzzleEmitterComponent>(*ClosedGateActor, TEXT("GateEmitter"));
	UPuzzleReceiverComponent* FirstReceiver = AddComponent<UPuzzleReceiverComponent>(*FirstTarget, TEXT("Receiver"));
	SharedEmitter->SetSignalState(TAG_PuzzleGraphPrimary, true, nullptr);
	OpenGate->SetSignalState(TAG_PuzzleGraphGate, true, nullptr);
	ClosedGate->SetSignalState(TAG_PuzzleGraphGate, false, nullptr);

	APuzzleController* ZController = ConfigureSingleController(
		*TestWorld.World,
		TEXT("Z_Controller"),
		*SharedEmitter,
		*FirstReceiver,
		ClosedGate);
	APuzzleController* AController = ConfigureSingleController(
		*TestWorld.World,
		TEXT("A_Controller"),
		*SharedEmitter,
		*FirstReceiver,
		OpenGate);
	TestNotNull(TEXT("Z Controller exists"), ZController);
	TestNotNull(TEXT("A Controller exists"), AController);

	AActor* MultiAActor = Spawn(*TestWorld.World, TEXT("MultiA"));
	AActor* MultiBActor = Spawn(*TestWorld.World, TEXT("MultiB"));
	AActor* MultiTargetActor = Spawn(*TestWorld.World, TEXT("MultiTarget"));
	UPuzzleEmitterComponent* MultiA = AddComponent<UPuzzleEmitterComponent>(*MultiAActor, TEXT("Emitter"));
	UPuzzleEmitterComponent* MultiB = AddComponent<UPuzzleEmitterComponent>(*MultiBActor, TEXT("Emitter"));
	UPuzzleReceiverComponent* MultiReceiver = AddComponent<UPuzzleReceiverComponent>(*MultiTargetActor, TEXT("Receiver"));
	MultiA->SetSignalState(TAG_PuzzleGraphPrimary, true, nullptr);
	MultiB->SetSignalState(TAG_PuzzleGraphSecondary, false, nullptr);
	APuzzleController* MultiController = Spawn<APuzzleController>(*TestWorld.World, TEXT("M_MultiController"));
	AddPrimaryInput(*MultiController, TEXT("A"), *MultiA, TAG_PuzzleGraphPrimary);
	AddPrimaryInput(*MultiController, TEXT("B"), *MultiB, TAG_PuzzleGraphSecondary);
	UPuzzleAllCondition* AllCondition = NewObject<UPuzzleAllCondition>(MultiController);
	AllCondition->Conditions.Add(NewInputCondition(*MultiController, TEXT("A")));
	AllCondition->Conditions.Add(NewInputCondition(*MultiController, TEXT("B")));
	MultiController->RootCondition = AllCondition;
	AddReceiver(*MultiController, *MultiReceiver);
	TestWorld.StartPlay();

	UPuzzleGraphSubsystem* Graph = TestWorld.World->GetSubsystem<UPuzzleGraphSubsystem>();
	const TArray<FPuzzleGraphLink> OutgoingLinks = Graph->QueryActorGraph(SourceActor).OutgoingPrimaryLinks;
	TestEqual(TEXT("Same Emitter retains two Controller contexts"), OutgoingLinks.Num(), 2);
	TestEqual(TEXT("Controller path provides deterministic ordering independent of spawn order"), OutgoingLinks[0].Controller.Get(), AController);
	TestEqual(TEXT("Second deterministic entry is the Z Controller"), OutgoingLinks[1].Controller.Get(), ZController);

	FPuzzleGraphLinkState FirstState;
	FPuzzleGraphLinkState SecondState;
	Graph->TryGetLinkState(OutgoingLinks[0].LinkHandle, FirstState);
	Graph->TryGetLinkState(OutgoingLinks[1].LinkHandle, SecondState);
	TestTrue(TEXT("Open Controller context admits the shared Emitter"), FirstState.bEffectivePrimaryActive);
	TestFalse(TEXT("Closed Controller context blocks the same shared Emitter"), SecondState.bEffectivePrimaryActive);
	TestTrue(TEXT("Both contexts retain the shared raw state"), FirstState.bRawPrimaryActive && SecondState.bRawPrimaryActive);
	TestTrue(TEXT("Closed Controller link reports Receiver active through the other Controller"),
		!SecondState.bControllerResultActive && SecondState.bTargetReceiverEffectiveActive);

	const TArray<FPuzzleGraphLink> MultiIncoming = Graph->QueryActorGraph(MultiTargetActor).IncomingPrimaryLinks;
	TestEqual(TEXT("Multiple primary inputs remain distinct links"), MultiIncoming.Num(), 2);
	FPuzzleGraphLinkState MultiFirstState;
	FPuzzleGraphLinkState MultiSecondState;
	Graph->TryGetLinkState(MultiIncoming[0].LinkHandle, MultiFirstState);
	Graph->TryGetLinkState(MultiIncoming[1].LinkHandle, MultiSecondState);
	TestTrue(TEXT("First effective input is independently active"), MultiFirstState.bEffectivePrimaryActive);
	TestFalse(TEXT("Second effective input is independently inactive"), MultiSecondState.bEffectivePrimaryActive);
	TestFalse(TEXT("Both links expose the false aggregate Controller result"),
		MultiFirstState.bControllerResultActive || MultiSecondState.bControllerResultActive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleGraphCompositionBypassAndHandleTest,
	"PuzzleSystem.Graph.CompositionBypassAndHandles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleGraphCompositionBypassAndHandleTest::RunTest(const FString& Parameters)
{
	using namespace UE::PuzzleSystem::Graph::Tests;
	FScopedGraphWorld TestWorld(TEXT("PuzzleGraphCompositionWorld"));
	if (!TestNotNull(TEXT("Composition test world exists"), TestWorld.World))
	{
		return false;
	}

	AActor* SourceActor = Spawn(*TestWorld.World, TEXT("Source"));
	AActor* HybridActor = Spawn(*TestWorld.World, TEXT("Hybrid"));
	AActor* TargetActor = Spawn(*TestWorld.World, TEXT("Target"));
	AActor* IgnoredGateActor = Spawn(*TestWorld.World, TEXT("IgnoredGate"));
	AActor* ConditionOnlySourceActor = Spawn(*TestWorld.World, TEXT("ConditionOnlySource"));
	AActor* ConditionOnlyTargetActor = Spawn(*TestWorld.World, TEXT("ConditionOnlyTarget"));
	AActor* SharedRoleActor = Spawn(*TestWorld.World, TEXT("SharedRoleSource"));
	AActor* SharedRoleTargetActor = Spawn(*TestWorld.World, TEXT("SharedRoleTarget"));
	UPuzzleEmitterComponent* SourceEmitter = AddComponent<UPuzzleEmitterComponent>(*SourceActor, TEXT("Emitter"));
	UPuzzleReceiverComponent* HybridReceiver = AddComponent<UPuzzleReceiverComponent>(*HybridActor, TEXT("Receiver"));
	UPuzzleEmitterComponent* HybridEmitter = AddComponent<UPuzzleEmitterComponent>(*HybridActor, TEXT("Emitter"));
	UPuzzleReceiverComponent* TargetReceiver = AddComponent<UPuzzleReceiverComponent>(*TargetActor, TEXT("Receiver"));
	UPuzzleEmitterComponent* IgnoredGate = AddComponent<UPuzzleEmitterComponent>(*IgnoredGateActor, TEXT("Emitter"));
	UPuzzleEmitterComponent* ConditionOnlyEmitter = AddComponent<UPuzzleEmitterComponent>(*ConditionOnlySourceActor, TEXT("Emitter"));
	UPuzzleReceiverComponent* ConditionOnlyReceiver = AddComponent<UPuzzleReceiverComponent>(*ConditionOnlyTargetActor, TEXT("Receiver"));
	UPuzzleEmitterComponent* SharedRoleEmitter = AddComponent<UPuzzleEmitterComponent>(*SharedRoleActor, TEXT("Emitter"));
	UPuzzleReceiverComponent* SharedRoleReceiver = AddComponent<UPuzzleReceiverComponent>(*SharedRoleTargetActor, TEXT("Receiver"));
	SourceEmitter->SetSignalState(TAG_PuzzleGraphPrimary, true, nullptr);
	HybridEmitter->SetSignalState(TAG_PuzzleGraphPrimary, true, nullptr);
	IgnoredGate->SetSignalState(TAG_PuzzleGraphGate, false, nullptr);
	ConditionOnlyEmitter->SetSignalState(TAG_PuzzleGraphPrimary, true, nullptr);
	SharedRoleEmitter->SetSignalState(TAG_PuzzleGraphPrimary, true, nullptr);
	SharedRoleEmitter->SetSignalState(TAG_PuzzleGraphGate, true, nullptr);

	APuzzleController* IntoHybrid = ConfigureSingleController(
		*TestWorld.World,
		TEXT("IntoHybrid"),
		*SourceEmitter,
		*HybridReceiver);
	APuzzleController* OutOfHybrid = ConfigureSingleController(
		*TestWorld.World,
		TEXT("OutOfHybrid"),
		*HybridEmitter,
		*TargetReceiver);
	FPuzzleEmitterGateBinding& IgnoredGateBinding = OutOfHybrid->InputBindings[0].EmitterGates.AddDefaulted_GetRef();
	IgnoredGateBinding.InputId = TEXT("Ignored");
	IgnoredGateBinding.EmitterActor = IgnoredGateActor;
	IgnoredGateBinding.SignalTag = TAG_PuzzleGraphGate;
	APuzzleController* ConditionOnlyController = ConfigureSingleController(
		*TestWorld.World,
		TEXT("ConditionOnlyController"),
		*ConditionOnlyEmitter,
		*ConditionOnlyReceiver);
	ConditionOnlyController->InputBindings[0].GateConditions.Add(nullptr);
	APuzzleController* SharedRoleController = ConfigureSingleController(
		*TestWorld.World,
		TEXT("SharedRoleController"),
		*SharedRoleEmitter,
		*SharedRoleReceiver,
		SharedRoleEmitter);
	TestNotNull(TEXT("Into-Hybrid Controller exists"), IntoHybrid);
	TestNotNull(TEXT("Out-of-Hybrid Controller exists"), OutOfHybrid);
	TestWorld.StartPlay();

	UPuzzleGraphSubsystem* Graph = TestWorld.World->GetSubsystem<UPuzzleGraphSubsystem>();
	const FPuzzleActorGraphView HybridView = Graph->QueryActorGraph(HybridActor);
	TestEqual(TEXT("Emitter+Receiver Actor has incoming primary links"), HybridView.IncomingPrimaryLinks.Num(), 1);
	TestEqual(TEXT("Emitter+Receiver Actor has outgoing primary links"), HybridView.OutgoingPrimaryLinks.Num(), 1);
	TestTrue(TEXT("Unpaired gate array produces no GateInfluence topology"), HybridView.IncomingGateLinks.IsEmpty());
	TestTrue(TEXT("Ignored gate-only Actor has no runtime graph link"), Graph->QueryActorGraph(IgnoredGateActor).OutgoingGateLinks.IsEmpty());
	const FPuzzleActorGraphView ConditionOnlyView = Graph->QueryActorGraph(ConditionOnlySourceActor);
	TestTrue(TEXT("Unpaired condition array produces no GateInfluence topology"), ConditionOnlyView.IncomingGateLinks.IsEmpty());
	FPuzzleGraphLinkState ConditionOnlyState;
	Graph->TryGetLinkState(ConditionOnlyView.OutgoingPrimaryLinks[0].LinkHandle, ConditionOnlyState);
	TestEqual(TEXT("Condition-only gate configuration is Bypassed"), ConditionOnlyState.GateMode, EPuzzleGraphGateMode::Bypassed);

	const FPuzzleActorGraphView SharedRoleView = Graph->QueryActorGraph(SharedRoleActor);
	TestEqual(TEXT("Same source used as primary exposes one PrimarySignal"), SharedRoleView.OutgoingPrimaryLinks.Num(), 1);
	TestEqual(TEXT("Same source used as gate exposes one incoming GateInfluence"), SharedRoleView.IncomingGateLinks.Num(), 1);
	TestEqual(TEXT("Same source used as gate exposes one outgoing GateInfluence"), SharedRoleView.OutgoingGateLinks.Num(), 1);
	TestEqual(TEXT("Component query does not deduplicate different semantic roles"),
		Graph->QueryLinksForEmitterComponent(SharedRoleEmitter).Num(), 2);

	FPuzzleGraphLinkState BypassedState;
	TestTrue(TEXT("Bypassed primary state is queryable"),
		Graph->TryGetLinkState(HybridView.OutgoingPrimaryLinks[0].LinkHandle, BypassedState));
	TestEqual(TEXT("Unpaired gate reports Bypassed"), BypassedState.GateMode, EPuzzleGraphGateMode::Bypassed);
	TestTrue(TEXT("Bypassed gate remains valid and admits primary"),
		BypassedState.bGateValid && BypassedState.bGateAllowsSignal && BypassedState.bEffectivePrimaryActive);

	const FPuzzleGraphLinkHandle OldHandle = HybridView.IncomingPrimaryLinks[0].LinkHandle;
	const int64 RevisionBeforeShutdown = Graph->GetGraphTopologyRevision();
	IntoHybrid->ShutdownPuzzleController();
	FPuzzleGraphLink RemovedLink;
	FPuzzleGraphLinkState RemovedState;
	TestFalse(TEXT("Controller shutdown makes its old handle stale"), Graph->TryGetLink(OldHandle, RemovedLink));
	TestFalse(TEXT("Stale handle state query fails predictably"), Graph->TryGetLinkState(OldHandle, RemovedState));
	TestEqual(TEXT("Controller shutdown increments topology once"),
		Graph->GetGraphTopologyRevision(), RevisionBeforeShutdown + 1);
	TestTrue(TEXT("Shutdown removes only the affected incoming relationship"),
		Graph->QueryActorGraph(HybridActor).IncomingPrimaryLinks.IsEmpty());

	TestTrue(TEXT("Controlled reinitialization registers fresh topology"), IntoHybrid->InitializePuzzleController());
	const FPuzzleGraphLinkHandle NewHandle = Graph->QueryActorGraph(HybridActor).IncomingPrimaryLinks[0].LinkHandle;
	TestTrue(TEXT("Reinitialization creates a non-reused handle"), NewHandle != OldHandle);
	TestFalse(TEXT("Old handle remains stale after reinitialization"), Graph->TryGetLink(OldHandle, RemovedLink));
	const int64 RevisionBeforeDestroy = Graph->GetGraphTopologyRevision();
	IntoHybrid->Destroy();
	TestFalse(TEXT("Controller destruction invalidates its current handle"), Graph->TryGetLink(NewHandle, RemovedLink));
	TestEqual(TEXT("Controller destruction increments topology once"),
		Graph->GetGraphTopologyRevision(), RevisionBeforeDestroy + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleGraphBlueprintReentrancyAndRegressionTest,
	"PuzzleSystem.Graph.BlueprintReentrancyAndNoConsumerRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleGraphBlueprintReentrancyAndRegressionTest::RunTest(const FString& Parameters)
{
	using namespace UE::PuzzleSystem::Graph::Tests;
	const UClass* GraphClass = UPuzzleGraphSubsystem::StaticClass();
	const FName BlueprintQueryFunctions[] =
	{
		GET_FUNCTION_NAME_CHECKED(UPuzzleGraphSubsystem, QueryActorGraph),
		GET_FUNCTION_NAME_CHECKED(UPuzzleGraphSubsystem, QueryIncomingLinksForActor),
		GET_FUNCTION_NAME_CHECKED(UPuzzleGraphSubsystem, QueryOutgoingLinksForActor),
		GET_FUNCTION_NAME_CHECKED(UPuzzleGraphSubsystem, QueryLinksForEmitterComponent),
		GET_FUNCTION_NAME_CHECKED(UPuzzleGraphSubsystem, QueryLinksForReceiverComponent),
		GET_FUNCTION_NAME_CHECKED(UPuzzleGraphSubsystem, TryGetLink),
		GET_FUNCTION_NAME_CHECKED(UPuzzleGraphSubsystem, TryGetLinkState),
		GET_FUNCTION_NAME_CHECKED(UPuzzleGraphSubsystem, GetGraphTopologyRevision)
	};
	for (const FName FunctionName : BlueprintQueryFunctions)
	{
		const UFunction* Function = GraphClass->FindFunctionByName(FunctionName);
		TestTrue(
			*FString::Printf(TEXT("Graph API '%s' is Blueprint-callable"), *FunctionName.ToString()),
			Function && Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	}
	const FMulticastDelegateProperty* TopologyDelegateProperty = FindFProperty<FMulticastDelegateProperty>(
		GraphClass,
		GET_MEMBER_NAME_CHECKED(UPuzzleGraphSubsystem, OnPuzzleGraphTopologyChanged));
	const FMulticastDelegateProperty* StateDelegateProperty = FindFProperty<FMulticastDelegateProperty>(
		GraphClass,
		GET_MEMBER_NAME_CHECKED(UPuzzleGraphSubsystem, OnPuzzleGraphLinkStateChanged));
	TestTrue(TEXT("Topology event is Blueprint-assignable"),
		TopologyDelegateProperty && TopologyDelegateProperty->HasAnyPropertyFlags(CPF_BlueprintAssignable));
	TestTrue(TEXT("State event is Blueprint-assignable"),
		StateDelegateProperty && StateDelegateProperty->HasAnyPropertyFlags(CPF_BlueprintAssignable));
	TestNotNull(TEXT("Link kind enum is reflected"), StaticEnum<EPuzzleGraphLinkKind>());
	TestNotNull(TEXT("Gate mode enum is reflected"), StaticEnum<EPuzzleGraphGateMode>());
	TestNotNull(TEXT("Topology change kind enum is reflected"), StaticEnum<EPuzzleGraphTopologyChangeKind>());
	TestTrue(TEXT("Link handle is a Blueprint type"),
		FPuzzleGraphLinkHandle::StaticStruct()->HasMetaData(TEXT("BlueprintType")));
	TestTrue(TEXT("Link descriptor is a Blueprint type"),
		FPuzzleGraphLink::StaticStruct()->HasMetaData(TEXT("BlueprintType")));
	TestTrue(TEXT("Link state is a Blueprint type"),
		FPuzzleGraphLinkState::StaticStruct()->HasMetaData(TEXT("BlueprintType")));
	TestTrue(TEXT("Actor graph view is a Blueprint type"),
		FPuzzleActorGraphView::StaticStruct()->HasMetaData(TEXT("BlueprintType")));

	FScopedGraphWorld TestWorld(TEXT("PuzzleGraphReentrantWorld"));
	if (!TestNotNull(TEXT("Reentrant graph world exists"), TestWorld.World))
	{
		return false;
	}

	AActor* PrimaryActor = Spawn(*TestWorld.World, TEXT("Primary"));
	AActor* GateActor = Spawn(*TestWorld.World, TEXT("Gate"));
	AActor* ReceiverActor = Spawn(*TestWorld.World, TEXT("ReentrantReceiverActor"));
	UPuzzleEmitterComponent* PrimaryEmitter = AddComponent<UPuzzleEmitterComponent>(*PrimaryActor, TEXT("Emitter"));
	UPuzzleEmitterComponent* GateEmitter = AddComponent<UPuzzleEmitterComponent>(*GateActor, TEXT("Emitter"));
	UPuzzleReentrantReceiverComponent* Receiver = AddComponent<UPuzzleReentrantReceiverComponent>(*ReceiverActor, TEXT("Receiver"));
	Receiver->EmitterToPublish = GateEmitter;
	Receiver->SignalTagToPublish = TAG_PuzzleGraphGate;
	Receiver->bPublishedState = false;
	PrimaryEmitter->SetSignalState(TAG_PuzzleGraphPrimary, true, nullptr);
	GateEmitter->SetSignalState(TAG_PuzzleGraphGate, true, nullptr);
	APuzzleController* Controller = ConfigureSingleController(
		*TestWorld.World,
		TEXT("ReentrantController"),
		*PrimaryEmitter,
		*Receiver,
		GateEmitter);
	if (!TestNotNull(TEXT("Reentrant Controller exists"), Controller))
	{
		return false;
	}

	UPuzzleGraphSubsystem* Graph = TestWorld.World->GetSubsystem<UPuzzleGraphSubsystem>();
	int32 StateEventCount = 0;
	const FDelegateHandle StateDelegateHandle = Graph->OnPuzzleGraphLinkStateChangedNative.AddLambda(
		[&StateEventCount](const FPuzzleGraphLinkHandle&, const FPuzzleGraphLinkState&, const FPuzzleGraphLinkState&)
		{
			++StateEventCount;
		});
	TestWorld.StartPlay();
	TestEqual(TEXT("Reentrant Receiver published its chained update once"), Receiver->PublishCount, 1);
	TestFalse(TEXT("Normal puzzle gameplay settled closed without any graph consumer action"), Receiver->IsReceiverActive());
	TestEqual(TEXT("Initial registration exposes no intermediate reentrant state events"), StateEventCount, 0);

	const FPuzzleActorGraphView LateView = Graph->QueryActorGraph(PrimaryActor);
	FPuzzleGraphLinkState LateState;
	TestTrue(TEXT("Late consumer immediately resolves current topology"), !LateView.OutgoingPrimaryLinks.IsEmpty());
	TestTrue(TEXT("Late consumer immediately resolves current state"),
		Graph->TryGetLinkState(LateView.OutgoingPrimaryLinks[0].LinkHandle, LateState));
	TestEqual(TEXT("Late state contains the final closed gate"), LateState.GateMode, EPuzzleGraphGateMode::Closed);
	TestFalse(TEXT("Late state never exposes the intermediate active result"), LateState.bEffectivePrimaryActive);

	const int64 RevisionBeforeReentrantUpdate = LateState.EffectivePrimaryRevision;
	TestTrue(TEXT("Runtime gate update starts the reentrant chain"),
		GateEmitter->SetSignalState(TAG_PuzzleGraphGate, true, nullptr));
	TestEqual(TEXT("Reentrant Receiver published its chained update twice in total"), Receiver->PublishCount, 2);
	TestFalse(TEXT("Runtime reentrant puzzle update settles Receiver inactive"), Receiver->IsReceiverActive());
	TestEqual(TEXT("Only final PrimarySignal and GateInfluence snapshots are broadcast"), StateEventCount, 2);
	FPuzzleGraphLinkState SettledState;
	TestTrue(TEXT("State remains queryable after runtime reentrancy"),
		Graph->TryGetLinkState(LateView.OutgoingPrimaryLinks[0].LinkHandle, SettledState));
	TestEqual(TEXT("Runtime reentrancy exposes only the final closed gate"),
		SettledState.GateMode, EPuzzleGraphGateMode::Closed);
	TestFalse(TEXT("Runtime reentrancy exposes only the final inactive effective state"),
		SettledState.bEffectivePrimaryActive);
	TestTrue(TEXT("Collapsed reentrant changes still advance the public effective revision"),
		SettledState.EffectivePrimaryRevision > RevisionBeforeReentrantUpdate);
	Graph->OnPuzzleGraphLinkStateChangedNative.Remove(StateDelegateHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPuzzleGraphManualReceiverStateTest,
	"PuzzleSystem.Graph.ManualReceiverState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPuzzleGraphManualReceiverStateTest::RunTest(const FString& Parameters)
{
	using namespace UE::PuzzleSystem::Graph::Tests;
	FScopedGraphWorld TestWorld(TEXT("PuzzleGraphManualReceiverWorld"));
	if (!TestNotNull(TEXT("Manual Receiver graph world exists"), TestWorld.World))
	{
		return false;
	}

	AActor* PrimaryActor = Spawn(*TestWorld.World, TEXT("Primary"));
	AActor* ReceiverActor = Spawn(*TestWorld.World, TEXT("ReceiverActor"));
	UPuzzleEmitterComponent* Emitter =
		AddComponent<UPuzzleEmitterComponent>(*PrimaryActor, TEXT("Emitter"));
	UPuzzleReceiverComponent* Receiver =
		AddComponent<UPuzzleReceiverComponent>(*ReceiverActor, TEXT("Receiver"));
	Receiver->ActivationMode = EPuzzleReceiverActivationMode::Manual;
	Emitter->SetSignalState(TAG_PuzzleGraphPrimary, true, nullptr);
	APuzzleController* Controller = ConfigureSingleController(
		*TestWorld.World,
		TEXT("ManualReceiverController"),
		*Emitter,
		*Receiver);
	if (!TestNotNull(TEXT("Manual Receiver Controller exists"), Controller))
	{
		return false;
	}

	UPuzzleGraphSubsystem* Graph = TestWorld.World->GetSubsystem<UPuzzleGraphSubsystem>();
	TestWorld.StartPlay();
	const FPuzzleActorGraphView View = Graph->QueryActorGraph(PrimaryActor);
	if (!TestEqual(TEXT("Manual Receiver creates one primary link"), View.OutgoingPrimaryLinks.Num(), 1))
	{
		return false;
	}

	const FPuzzleGraphLinkHandle Handle = View.OutgoingPrimaryLinks[0].LinkHandle;
	FPuzzleGraphLinkState InitialState;
	TestTrue(TEXT("Manual Receiver state is queryable"), Graph->TryGetLinkState(Handle, InitialState));
	TestEqual(TEXT("Graph exposes Manual Receiver policy"),
		InitialState.TargetReceiverActivationMode,
		EPuzzleReceiverActivationMode::Manual);
	TestTrue(TEXT("Graph exposes satisfied Receiver prerequisites"),
		InitialState.bTargetReceiverPrerequisitesSatisfied);
	TestFalse(TEXT("Graph exposes an initially clear manual latch"),
		InitialState.bTargetReceiverManualActivationRequested);
	TestFalse(TEXT("Manual Receiver remains effectively inactive before Open"),
		InitialState.bTargetReceiverEffectiveActive);

	int32 StateEventCount = 0;
	const FDelegateHandle StateDelegateHandle = Graph->OnPuzzleGraphLinkStateChangedNative.AddLambda(
		[&StateEventCount](const FPuzzleGraphLinkHandle&, const FPuzzleGraphLinkState&, const FPuzzleGraphLinkState&)
		{
			++StateEventCount;
		});
	const int64 StableTopologyRevision = Graph->GetGraphTopologyRevision();
	TestTrue(TEXT("Graph fixture accepts manual Open"),
		Receiver->RequestManualActivation().WasAccepted());
	TestEqual(TEXT("Manual Open broadcasts one link-state change"), StateEventCount, 1);
	TestEqual(TEXT("Manual Open leaves topology unchanged"),
		Graph->GetGraphTopologyRevision(), StableTopologyRevision);

	FPuzzleGraphLinkState OpenState;
	Graph->TryGetLinkState(Handle, OpenState);
	TestTrue(TEXT("Graph exposes the manual latch after Open"),
		OpenState.bTargetReceiverManualActivationRequested);
	TestTrue(TEXT("Graph exposes effective activation after Open"),
		OpenState.bTargetReceiverEffectiveActive);
	TestTrue(TEXT("Graph fixture accepts manual Close"),
		Receiver->RequestManualDeactivation().WasAccepted());
	TestEqual(TEXT("Manual Close broadcasts one additional state change"), StateEventCount, 2);
	TestEqual(TEXT("Manual Close also leaves topology unchanged"),
		Graph->GetGraphTopologyRevision(), StableTopologyRevision);

	Graph->OnPuzzleGraphLinkStateChangedNative.Remove(StateDelegateHandle);
	Receiver->DestroyComponent();
	TestEqual(TEXT("Manual commands fail predictably after Receiver teardown"),
		Receiver->RequestManualActivation().Status,
		EPuzzleReceiverActivationCommandStatus::ReceiverUnavailable);
	return true;
}

#endif
