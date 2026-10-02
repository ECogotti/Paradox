#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Puzzles/ParadoxHackingTerminal.h"
#include "Puzzles/ParadoxHackTerminalAction.h"
#include "Puzzles/ParadoxHackingTerminalWidget.h"
#include "Puzzles/ParadoxHackingLetterWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "CommonButtonBase.h"
#include "CommonTextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/NamedSlot.h"
#include "Components/VerticalBox.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetSwitcher.h"
#include "CommonGameViewportClient.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Conditions/PuzzleInputStateCondition.h"
#include "Controllers/PuzzleController.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Interaction/ParadoxSelectionComponent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Paradox.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "TimerManager.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "Actions/GameplayWaitAction.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Components/SceneComponent.h"
#include "GameplayActionTags.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Journal/IntentExecutionJournal.h"
#include "Navigation/GridNavigationData.h"
#include "Playback/ParadoxCloneReplayExecutionStrategy.h"
#include "Playback/IntentReplayPlaybackSession.h"
#include "Recording/IntentReplayTrack.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectSubsystem.h"
#include "Tests/ParadoxInteractionTestTypes.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

struct FParadoxHackingTestAccess
{
	static FGuid Begin(AParadoxHackingTerminal& Terminal, AActor* Actor, EParadoxHackingMode Mode = EParadoxHackingMode::Interactive,
		double Duration = 0, bool bSuccess = false, int32 Seed = 7)
	{
		FRandomStream Random(Seed); FGuid Id; FString Diagnostic;
		Terminal.BeginAttempt(Actor, Mode, Duration, bSuccess, Id, Diagnostic, &Random); return Id;
	}
	static void Finish(AParadoxHackingTerminal& Terminal, FGuid Id) { Terminal.FinishAttempt(Id, EParadoxHackingAttemptState::Success); }
	static void Reset(AParadoxHackingTerminal& Terminal) { Terminal.HandlePreRestore(FWorldStateParticipantId()); Terminal.HandlePropertiesRestored(FWorldStateParticipantId()); }
	static double SolveTime(AParadoxHackingTerminal& Terminal, FGuid Id) { return Terminal.Attempts[Id].CompletionAt - Terminal.Attempts[Id].StartedAt; }
	static bool WillSucceed(AParadoxHackingTerminal& Terminal, FGuid Id) { return Terminal.Attempts[Id].bSimulatedSuccess; }
	static void Bind(UParadoxHackingTerminalWidget& Widget, AParadoxHackingTerminal* Terminal, APlayerController* Controller,
		UParadoxSelectionComponent* Selection = nullptr)
	{
		Widget.AssignSelectionContext(Terminal, Terminal ? Terminal->Selectable.Get() : nullptr, Selection, Controller);
	}
	static void Unbind(UParadoxHackingTerminalWidget& Widget) { Widget.ClearSelectionContext(); }
};

namespace ParadoxHackingTests
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Origin_GoalDerivedTest, "GameplayAction.Origin.Test.Hacking.GoalDerived");
	struct FFixture
	{
		UWorld* World;
		AParadoxHackingTerminal* Terminal;
		APawn* Player;
		APawn* Other;
		APlayerController* Controller;
		TArray<TSharedRef<SWidget>> SlateViews;
		FFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("HackingTest")));
			World->AddToRoot(); GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Terminal = World->SpawnActor<AParadoxHackingTerminal>();
			Player = World->SpawnActor<APawn>(); Other = World->SpawnActor<APawn>();
			Controller = World->SpawnActor<APlayerController>(); Controller->Possess(Player);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay();
			for (TActorIterator<AActor> It(World); It; ++It) { if (!It->HasActorBegunPlay()) { It->DispatchBeginPlay(); } }
		}
		~FFixture()
		{
			SlateViews.Reset();
			// Purge destroyed actors while their physics scene/subsystems still exist (Water's manager
			// unregisters physics delegates in its destructor). Blueprint compilation performs GC.
			TArray<AActor*> Actors;
			for (TActorIterator<AActor> It(World); It; ++It) { Actors.Add(*It); }
			for (AActor* Actor : Actors) { Actor->Destroy(true); }
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			World->DestroyWorld(true); GEngine->DestroyWorldContext(World); World->RemoveFromRoot();
		}
		void Advance(double Seconds)
		{
			// World clamps large frame deltas. Advance actual game time in normal-sized frames.
			while (Seconds > 1.e-7)
			{
				const double Step = FMath::Min(Seconds, 0.05);
				++GFrameCounter; World->GetTimerManager().Tick(0.f);
				++GFrameCounter; World->Tick(LEVELTICK_All, float(Step));
				Seconds -= Step;
			}
		}
		FParadoxHackingAttemptSnapshot View(FGuid Id) { FParadoxHackingAttemptSnapshot Result; Terminal->GetAttemptSnapshot(Id, Result); return Result; }
		bool Signal() { FPuzzleSignalState State; return Terminal->Emitter->TryGetSignalState(Terminal->OutputSignalTag, State) && State.bIsActive; }
		void Solve(FGuid Id)
		{
			for (int32 Round = 0; Round < 1000 && View(Id).State == EParadoxHackingAttemptState::Active; ++Round)
			{
				auto Snapshot = View(Id);
				for (int32 Index = 0; Index < Snapshot.Letters.Num(); ++Index)
				{
					const auto& Letter = Snapshot.Letters[Index];
					if (!Letter.bFrozen && Letter.CurrentLetter == Letter.TargetLetter)
					{ Terminal->SubmitLetter(Player, Id, Snapshot.Generation, Index, Letter.CurrentLetter); }
				}
				if (View(Id).State == EParadoxHackingAttemptState::Active) { Advance(Terminal->LetterRefreshInterval + 0.001); }
			}
		}
	};

	UPuzzleEmitterComponent* AddTerminalGate(FFixture& Fixture, bool bInitiallyOpen)
	{
		AActor* GateActor = Fixture.World->SpawnActor<AActor>();
		auto* GateEmitter = NewObject<UPuzzleEmitterComponent>(GateActor);
		GateActor->AddInstanceComponent(GateEmitter); GateEmitter->RegisterComponent();
		GateEmitter->SetSignalState(ParadoxGameplayTags::Puzzle_Signal_Pressed, bInitiallyOpen, nullptr);
		AActor* ReceiverActor = Fixture.World->SpawnActor<AActor>();
		auto* Receiver = NewObject<UPuzzleReceiverComponent>(ReceiverActor);
		ReceiverActor->AddInstanceComponent(Receiver); Receiver->RegisterComponent();
		auto* PuzzleController = Fixture.World->SpawnActorDeferred<APuzzleController>(APuzzleController::StaticClass(), FTransform::Identity);
		auto& Binding = PuzzleController->InputBindings.AddDefaulted_GetRef();
		Binding.InputId = TEXT("Terminal"); Binding.EmitterActor = Fixture.Terminal; Binding.SignalTag = Fixture.Terminal->OutputSignalTag;
		auto& Gate = Binding.EmitterGates.AddDefaulted_GetRef();
		Gate.InputId = TEXT("Enabled"); Gate.EmitterActor = GateActor; Gate.SignalTag = ParadoxGameplayTags::Puzzle_Signal_Pressed;
		auto* Condition = NewObject<UPuzzleInputStateCondition>(PuzzleController); Condition->InputId = Gate.InputId;
		Binding.GateConditions.Add(Condition);
		auto* Root = NewObject<UPuzzleInputStateCondition>(PuzzleController); Root->InputId = Binding.InputId;
		PuzzleController->RootCondition = Root;
		PuzzleController->ReceiverBindings.AddDefaulted_GetRef().ReceiverActor = ReceiverActor;
		PuzzleController->FinishSpawning(FTransform::Identity);
		return GateEmitter;
	}

	UWidgetBlueprint* NewBlueprint(UClass* Parent, const TCHAR* Name)
	{
		FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("UMGEditor"));
		auto* Factory = NewObject<UWidgetBlueprintFactory>();
		Factory->ParentClass = Parent;
		const FName AssetName = MakeUniqueObjectName(GetTransientPackage(), UWidgetBlueprint::StaticClass(), Name);
		UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/HackingTests/%s"), *AssetName.ToString()));
		auto* Blueprint = CastChecked<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(), Package,
			AssetName, RF_Public | RF_Standalone, nullptr, GWarn));
		if (!Blueprint->WidgetTree) { Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, TEXT("WidgetTree"), RF_Transactional); }
		return Blueprint;
	}

	UParadoxHackingTerminalWidget* CreateView(FFixture& Fixture, bool bHorizontal)
	{
		auto* CommonButtonBlueprint = NewBlueprint(UCommonButtonBase::StaticClass(), TEXT("HackingCommonButtonTestBP"));
		auto* CommonRoot = CommonButtonBlueprint->WidgetTree->ConstructWidget<UVerticalBox>();
		CommonButtonBlueprint->WidgetTree->RootWidget = CommonRoot;
		auto* ContentSlot = CommonButtonBlueprint->WidgetTree->ConstructWidget<UNamedSlot>(UNamedSlot::StaticClass(), TEXT("ButtonContent"));
		ContentSlot->bIsVariable = true; ContentSlot->bExposeOnInstanceOnly = true; CommonRoot->AddChild(ContentSlot);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(CommonButtonBlueprint);
		FKismetEditorUtilities::CompileBlueprint(CommonButtonBlueprint);
		auto* LetterBlueprint = NewBlueprint(UParadoxHackingLetterWidget::StaticClass(), TEXT("HackingLetterTestBP"));
		// Designer templates must not run CreateWidget/Initialize before their named-slot content is authored.
		auto* Button = NewObject<UCommonButtonBase>(LetterBlueprint->WidgetTree, CommonButtonBlueprint->GeneratedClass.Get(), TEXT("LetterButton"), RF_Transactional);
		Button->SetDesignerFlags(EWidgetDesignFlags::Designing);
		auto* Text = LetterBlueprint->WidgetTree->ConstructWidget<UCommonTextBlock>(UCommonTextBlock::StaticClass(), TEXT("LetterText"));
		Button->bIsVariable = true; Text->bIsVariable = true;
		Button->SetContentForSlot(TEXT("ButtonContent"), Text); LetterBlueprint->WidgetTree->RootWidget = Button;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(LetterBlueprint);
		FKismetEditorUtilities::CompileBlueprint(LetterBlueprint);
		auto* Blueprint = NewBlueprint(UParadoxHackingTerminalWidget::StaticClass(), TEXT("HackingTerminalTestBP"));
		auto* Switcher = Blueprint->WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("HackingStateSwitcher"));
		Blueprint->WidgetTree->RootWidget = Switcher;
		Switcher->AddChild(NewObject<UCommonButtonBase>(Blueprint->WidgetTree, CommonButtonBlueprint->GeneratedClass.Get(), TEXT("StartHackingButton"), RF_Transactional));
		auto* Root = Blueprint->WidgetTree->ConstructWidget<UVerticalBox>();
		Switcher->AddChild(Root);
		Switcher->AddChild(Blueprint->WidgetTree->ConstructWidget<UCommonTextBlock>(UCommonTextBlock::StaticClass(), TEXT("HackedText")));
		Root->AddChild(Blueprint->WidgetTree->ConstructWidget<UCommonTextBlock>(UCommonTextBlock::StaticClass(), TEXT("PasswordText")));
		Root->AddChild(Blueprint->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("TimeRemainingProgress")));
		UPanelWidget* Container = bHorizontal
			? static_cast<UPanelWidget*>(Blueprint->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("LetterContainer")))
			: static_cast<UPanelWidget*>(Blueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LetterContainer")));
		Root->AddChild(Container);
		Blueprint->WidgetTree->ForEachWidget([](UWidget* Widget) { Widget->bIsVariable = true; });
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		auto* Defaults = CastChecked<UParadoxHackingTerminalWidget>(Blueprint->GeneratedClass->GetDefaultObject());
		Defaults->LetterWidgetClass = LetterBlueprint->GeneratedClass;
		auto* Widget = CreateWidget<UParadoxHackingTerminalWidget>(Fixture.World, Blueprint->GeneratedClass.Get());
		if (Widget) { Widget->LetterWidgetClass = LetterBlueprint->GeneratedClass.Get(); Fixture.SlateViews.Add(Widget->TakeWidget()); FParadoxHackingTestAccess::Bind(*Widget, Fixture.Terminal, Fixture.Controller); }
		return Widget;
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FParadoxHackingScenarios, "Paradox.Hacking.Scenarios", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FParadoxHackingScenarios::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	const TCHAR* Cases[] = { TEXT("PlayerSuccess"), TEXT("BlueprintButtonComposition"), TEXT("Pool3"), TEXT("Pool26"), TEXT("StablePools"),
		TEXT("CorrectClickFreezes"), TEXT("WrongClickResetsProgress"), TEXT("Timeout"), TEXT("Deselect"), TEXT("Reselect"), TEXT("MoveAway"), TEXT("ConcurrentInstigators"),
		TEXT("Synchronous"), TEXT("Asynchronous"), TEXT("ReplaySuccess"), TEXT("ReplayFailure"), TEXT("SimulationEasy"), TEXT("SimulationHard"),
		TEXT("SimulationSuccessDuration"), TEXT("SimulationTimeoutDuration"), TEXT("SimulationStable"), TEXT("RestorePlayerPending"), TEXT("RestoreReplayPending"),
		TEXT("RestoreSimulationPending"), TEXT("RestoreHackedSnapshot"), TEXT("RestoreBaseline"), TEXT("DuplicateCompletion"), TEXT("DestroyView"), TEXT("DestroyTerminal"), TEXT("BlueprintWithoutPresentationHooks") };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index) { Names.Add(FString::Printf(TEXT("%02d_%s"), Index + 1, Cases[Index])); Commands.Add(FString::FromInt(Index + 1)); }
}

bool FParadoxHackingScenarios::RunTest(const FString& Parameters)
{
	using namespace ParadoxHackingTests;
	FFixture F;
	const int32 Case = FCString::Atoi(*Parameters);
	if (Case == 4) { F.Terminal->LettersPerButton = 26; }
	if (Case == 14) { F.Terminal->bAsynchronousRefresh = true; }
	if (Case >= 17 && Case <= 21) { F.Terminal->Password = TEXT("H"); F.Terminal->LetterRefreshInterval = 0.1; }
	if (Case == 8 || Case == 20) { F.Terminal->TimeLimitSeconds = 0.001; }
	const auto Mode = Case == 15 || Case == 16 || Case == 23 ? EParadoxHackingMode::Replay
		: (Case >= 17 && Case <= 21) || Case == 24 ? EParadoxHackingMode::Simulated : EParadoxHackingMode::Interactive;
	FGuid Id = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player, Mode, 0.25, Case != 16);
	if (!TestTrue(TEXT("Attempt starts"), Id.IsValid())) { return false; }
	auto Before = F.View(Id);
	TestEqual(TEXT("One slot per password character"), Before.Letters.Num(), F.Terminal->Password.Len());
	TestFalse(TEXT("Terminal has no Receiver"), F.Terminal->FindComponentByClass<UPuzzleReceiverComponent>() != nullptr);
	switch (Case)
	{
	case 1: F.Solve(Id); TestEqual(TEXT("Solved terminal is Hacked"), F.Terminal->GetTerminalState(), EParadoxHackingTerminalState::Hacked); TestTrue(TEXT("Signal active"), F.Signal()); break;
	case 2: case 9: case 10: case 28: case 30:
	{
		for (bool bHorizontal : {true, false})
		{
			auto* Widget = CreateView(F, bHorizontal);
			if (!TestNotNull(TEXT("Blueprint view constructed"), Widget)) { return false; }
			auto* Container = CastChecked<UPanelWidget>(Widget->GetWidgetFromName(TEXT("LetterContainer")));
			TestEqual(TEXT("Dynamic Blueprint letters populated"), Container->GetChildrenCount(), Before.Letters.Num());
			auto* Letter = Cast<UParadoxHackingLetterWidget>(Container->GetChildAt(0));
			if (!TestNotNull(TEXT("Dynamic letter view exists"), Letter)) { return false; }
			TestTrue(TEXT("Letter bindings exist"), Letter->HasRequiredBindings());
			const TSharedRef<SWidget> LetterSlate = Letter->TakeWidget(); // Retain the live child while exercising click bindings.
			auto* Button = CastChecked<UCommonButtonBase>(Letter->GetWidgetFromName(TEXT("LetterButton")));
			TestTrue(TEXT("Common text composed inside Common Button named slot"),
				Button->GetContentForSlot(TEXT("ButtonContent")) == Letter->GetWidgetFromName(TEXT("LetterText")));
			TestNotNull(TEXT("Letter text uses Common styles"), Cast<UCommonTextBlock>(Letter->GetWidgetFromName(TEXT("LetterText"))));
			TestNotNull(TEXT("Password uses Common styles"), Cast<UCommonTextBlock>(Widget->GetWidgetFromName(TEXT("PasswordText"))));
			TestNotNull(TEXT("Start uses Common styles"), Cast<UCommonButtonBase>(Widget->GetWidgetFromName(TEXT("StartHackingButton"))));
			TestEqual(TEXT("Reconstructed attempt selects hacking page"),
				CastChecked<UWidgetSwitcher>(Widget->GetWidgetFromName(TEXT("HackingStateSwitcher")))->GetActiveWidgetIndex(), 1);
			TestTrue(TEXT("Same attempt is reconstructed"), Widget->GetViewedAttempt().AttemptId == Id);
			const auto BeforeClick = F.View(Id); Button->OnClicked().Broadcast();
			const auto AfterClick = F.View(Id);
			TestTrue(TEXT("Blueprint Button click reaches authoritative logic"), AfterClick.Letters[0].bFrozen
				|| AfterClick.ErrorCount > BeforeClick.ErrorCount);
			FParadoxHackingTestAccess::Unbind(*Widget); F.Advance(0.1);
			TestEqual(TEXT("View removal leaves attempt active"), F.View(Id).State, EParadoxHackingAttemptState::Active);
			FParadoxHackingTestAccess::Bind(*Widget, F.Terminal, F.Controller);
			TestTrue(TEXT("Reselection preserves attempt identity"), Widget->GetViewedAttempt().AttemptId == Id);
			TestTrue(TEXT("Countdown continues"), Widget->GetViewedAttempt().TimeRemaining < Before.TimeRemaining);
			if (Case == 28)
			{
				F.SlateViews.Reset(); F.Advance(0.1);
				TestEqual(TEXT("NativeDestruct of the view preserves hacking"), F.View(Id).State, EParadoxHackingAttemptState::Active);
				TestFalse(TEXT("NativeDestruct clears selection bindings"), Widget->GetSelectedActor() != nullptr);
			}
			else { FParadoxHackingTestAccess::Unbind(*Widget); }
			Widget->RemoveFromParent();
		}
		if (Case == 30) { F.Solve(Id); TestTrue(TEXT("No presentation override is needed for success"), F.Signal()); }
		break;
	}
	case 3: case 4:
		for (const auto& Letter : Before.Letters)
		{
			TSet<TCHAR> Unique; for (TCHAR Value : Letter.Pool) { Unique.Add(Value); }
			TestEqual(TEXT("Pool has configured unique candidates"), Unique.Num(), F.Terminal->LettersPerButton);
			TestTrue(TEXT("Pool includes target"), Letter.Pool.Contains(Letter.TargetLetter));
		}
		break;
	case 5: case 13: case 14:
		F.Advance(1.01);
		for (int32 Index = 0; Index < Before.Letters.Num(); ++Index)
		{
			const auto After = F.View(Id);
			TestEqual(TEXT("Pools persist"), After.Letters[Index].Pool, Before.Letters[Index].Pool);
			TestEqual(TEXT("Offsets persist"), After.Letters[Index].RefreshOffset, Before.Letters[Index].RefreshOffset);
			TestTrue(TEXT("Phase advances"), After.Letters[Index].NextRefreshTime > F.World->GetTimeSeconds());
			if (Case == 13) { TestEqual(TEXT("Synchronous slots share phase"), After.Letters[Index].NextRefreshTime, After.Letters[0].NextRefreshTime); }
		}
		break;
	case 6: case 7:
	{
		for (int32 Round = 0; Round < 100 && F.View(Id).Progress == 0; ++Round)
		{
			auto View = F.View(Id);
			for (int32 Index = 0; Index < View.Letters.Num(); ++Index)
			{
				if (View.Letters[Index].CurrentLetter == View.Letters[Index].TargetLetter)
				{ F.Terminal->SubmitLetter(F.Player, Id, View.Generation, Index, View.Letters[Index].CurrentLetter); break; }
			}
			if (F.View(Id).Progress == 0) { F.Advance(0.501); }
		}
		TestTrue(TEXT("Correct letter freezes"), F.View(Id).Progress > 0);
		if (Case == 6)
		{
			const auto Frozen = F.View(Id); F.Advance(1.01);
			for (int32 Index = 0; Index < Frozen.Letters.Num(); ++Index) { if (Frozen.Letters[Index].bFrozen) { TestEqual(TEXT("Frozen letter is stable"), F.View(Id).Letters[Index].CurrentLetter, Frozen.Letters[Index].CurrentLetter); } }
		}
		else
		{
			for (int32 Round = 0; Round < 100 && F.View(Id).ErrorCount == 0; ++Round)
			{
				auto View = F.View(Id);
				for (int32 Index = 0; Index < View.Letters.Num(); ++Index)
				{
					if (!View.Letters[Index].bFrozen && View.Letters[Index].CurrentLetter != View.Letters[Index].TargetLetter)
					{ F.Terminal->SubmitLetter(F.Player, Id, View.Generation, Index, View.Letters[Index].CurrentLetter); break; }
				}
				if (F.View(Id).ErrorCount == 0) { F.Advance(0.501); }
			}
			TestEqual(TEXT("Error clears progress"), F.View(Id).Progress, 0.f);
			TestEqual(TEXT("Error counted once"), F.View(Id).ErrorCount, 1);
			TestTrue(TEXT("Deadline not restarted"), F.View(Id).TimeRemaining <= Before.TimeRemaining);
			for (int32 Index = 0; Index < Before.Letters.Num(); ++Index) { TestEqual(TEXT("Error retains pools"), F.View(Id).Letters[Index].Pool, Before.Letters[Index].Pool); }
		}
		break;
	}
	case 8: F.Advance(0.01); TestEqual(TEXT("Timeout"), F.View(Id).State, EParadoxHackingAttemptState::TimedOut); TestFalse(TEXT("No signal on timeout"), F.Signal()); break;
	case 11: F.Player->SetActorLocation(FVector(10000,0,0)); F.Advance(0.1); TestEqual(TEXT("Moving away preserves attempt"), F.View(Id).State, EParadoxHackingAttemptState::Active); break;
	case 12:
	{
		const auto OtherId = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Other);
		TestTrue(TEXT("Different instigators have independent attempts"), OtherId != Id);
		TestTrue(TEXT("Duplicate request retains identity"), FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player) == Id);
		F.Solve(Id); TestEqual(TEXT("First success supersedes peers"), F.View(OtherId).State, EParadoxHackingAttemptState::Superseded); break;
	}
	case 15: case 16:
		F.Advance(0.1); TestEqual(TEXT("Replay waits duration"), F.View(Id).State, EParadoxHackingAttemptState::Active);
		F.Advance(0.2); TestEqual(TEXT("Semantic outcome"), F.View(Id).State, Case == 15 ? EParadoxHackingAttemptState::Success : EParadoxHackingAttemptState::Failed);
		TestEqual(TEXT("Recorded duration preserved"), F.View(Id).DurationSeconds, 0.25); TestEqual(TEXT("Signal follows semantic outcome"), F.Signal(), Case == 15); break;
	case 17: case 18:
	{
		F.Terminal->CancelAttempt(F.Player, Id, Before.Generation);
		int32 Easy = 0, Hard = 0;
		for (int32 Seed = 0; Seed < 128; ++Seed)
		{
			F.Terminal->Password = TEXT("H"); F.Terminal->LettersPerButton = 3; F.Terminal->TimeLimitSeconds = 1; F.Terminal->LetterRefreshInterval = 0.1;
			FGuid EasyId = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player, EParadoxHackingMode::Simulated, 0, false, Seed);
			Easy += FParadoxHackingTestAccess::WillSucceed(*F.Terminal, EasyId); F.Terminal->CancelAttempt(F.Player, EasyId, Before.Generation);
			F.Terminal->Password = TEXT("HEART"); F.Terminal->LettersPerButton = 26; F.Terminal->LetterRefreshInterval = 0.5;
			FGuid HardId = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player, EParadoxHackingMode::Simulated, 0, false, Seed);
			Hard += FParadoxHackingTestAccess::WillSucceed(*F.Terminal, HardId); F.Terminal->CancelAttempt(F.Player, HardId, Before.Generation);
		}
		TestTrue(TEXT("Configuration affects actual sampled difficulty"), Easy > Hard); break;
	}
	case 19: case 20: case 21:
	{
		const double SolveAt = FParadoxHackingTestAccess::SolveTime(*F.Terminal, Id);
		TestTrue(TEXT("Duplicate does not reroll"), FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player, EParadoxHackingMode::Simulated, 0, false, 1234) == Id);
		TestEqual(TEXT("Solve time remains stable"), FParadoxHackingTestAccess::SolveTime(*F.Terminal, Id), SolveAt);
		F.Advance(SolveAt + 0.01);
		const bool bSuccess = FParadoxHackingTestAccess::WillSucceed(*F.Terminal, Id);
		TestEqual(FString::Printf(TEXT("Precomputed outcome at %.3fs (world=%.3fs)"), SolveAt, F.World->GetTimeSeconds()),
			UEnum::GetValueAsString(F.View(Id).State), UEnum::GetValueAsString(bSuccess ? EParadoxHackingAttemptState::Success : EParadoxHackingAttemptState::TimedOut));
		TestEqual(TEXT("Completion at sampled time or deadline"), F.View(Id).DurationSeconds, SolveAt); break;
	}
	case 22: case 23: case 24:
		FParadoxHackingTestAccess::Reset(*F.Terminal);
		FParadoxHackingTestAccess::Finish(*F.Terminal, Id); F.Advance(31);
		TestEqual(TEXT("Stale callback cannot hack restored terminal"), F.Terminal->GetTerminalState(), EParadoxHackingTerminalState::Ready);
		TestFalse(TEXT("Restored signal inactive"), F.Signal());
		TestFalse(TEXT("Old click identity rejected"), F.Terminal->SubmitLetter(F.Player, Id, Before.Generation, 0, Before.Letters[0].CurrentLetter)); break;
	case 25: case 26:
	{
		auto* State = F.World->GetSubsystem<UWorldStateSubsystem>();
		TestTrue(TEXT("Registration finalizes"), State->FinalizeWorldStateRegistration().IsSuccess());
		TestTrue(TEXT("Ready baseline captured"), State->CaptureBaseline(FWorldStateCaptureRequest()).IsSuccess());
		F.Solve(Id); const auto Hacked = State->CaptureRuntimeSnapshot(FWorldStateCaptureRequest());
		TestTrue(TEXT("Hacked snapshot captured"), Hacked.IsSuccess());
		TestTrue(TEXT("Ready baseline restored"), State->RestoreBaseline(FWorldStateRestoreRequest()).IsSuccess());
		TestEqual(TEXT("Ready state restored"), F.Terminal->GetTerminalState(), EParadoxHackingTerminalState::Ready); TestFalse(TEXT("Signal inactive after baseline"), F.Signal());
		if (Case == 25)
		{
			FWorldStateRestoreRequest Request; Request.SnapshotId = Hacked.SnapshotId;
			TestTrue(TEXT("Hacked snapshot restored"), State->RestoreSnapshot(Request).IsSuccess());
			TestEqual(TEXT("Hacked state restored"), F.Terminal->GetTerminalState(), EParadoxHackingTerminalState::Hacked); TestTrue(TEXT("Signal reconstructed"), F.Signal());
		}
		break;
	}
	case 27:
		F.Solve(Id); { FPuzzleSignalState Signal; F.Terminal->Emitter->TryGetSignalState(F.Terminal->OutputSignalTag, Signal); const int64 Revision = Signal.Revision;
		FParadoxHackingTestAccess::Finish(*F.Terminal, Id); F.Terminal->Emitter->TryGetSignalState(F.Terminal->OutputSignalTag, Signal);
		TestEqual(TEXT("No duplicate publication"), Signal.Revision, Revision); } break;
	case 29: F.Terminal->Destroy(); F.Advance(31); TestTrue(TEXT("Terminal destruction safely tears down timers"), F.Terminal->IsActorBeingDestroyed()); break;
	default: AddError(TEXT("Unknown hacking scenario")); return false;
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxHackingActionIntegration,
	"Paradox.Hacking.SpatialActionRewindReplayAndInvestigation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxHackingActionIntegration::RunTest(const FString&)
{
	using namespace ParadoxHackingTests;
	for (int32 OutcomeCase : { 0, 1, 2 })
	{
		const bool bRecordedSuccess = OutcomeCase == 1;
		const bool bGateFailure = OutcomeCase == 2;
		FFixture F;
		// Exercise queued presentation with an isolated opt-in definition; production keeps Reject.
		TStrongObjectPtr<UParadoxHackTerminalActionDefinition> QueuedDefinition(NewObject<UParadoxHackTerminalActionDefinition>());
		QueuedDefinition->BlockedPolicy = EGameplayActionBlockedPolicy::Queue;
		F.Terminal->Interaction->InteractionDefinitions[0].GameplayActionDefinition = QueuedDefinition.Get();
		auto* Navigation = F.World->SpawnActor<AGridNavigationData>();
		auto Grid = MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Grid->GridId = FGuid::NewGuid(); Grid->Revisions.Topology = 1; Grid->Revisions.Traversal = 1;
		auto& Region = Grid->Regions.Add(Grid->GridId); Region.GridId = Grid->GridId; Region.GridTransform.CellSize = FVector(100,100,50);
		auto& Cell = Grid->Cells.AddDefaulted_GetRef(); Cell.Id.GridId = Grid->GridId; Cell.Id.Coord = FGridCellCoord(0,0,0);
		Cell.WorldCenter = Region.GridTransform.CellToWorld(Cell.Id.Coord); Cell.bWalkable = true;
		FString Error;
		if (!TestTrue(TEXT("Spatial grid publishes"), Navigation->PublishSnapshot(Grid, &Error))) { return false; }
		auto* SmartObjects = USmartObjectSubsystem::GetCurrent(F.World);
		if (F.Terminal->SmartObject->GetRegisteredHandle().IsValid()) { SmartObjects->UnregisterSmartObject(F.Terminal->SmartObject); }
		auto* SmartDefinition = NewObject<USmartObjectDefinition>(F.Terminal);
		auto& Slot = SmartDefinition->DebugAddSlot(); Slot.ID = FGuid::NewGuid(); Slot.Offset = FVector3f(Cell.WorldCenter);
		Slot.BehaviorDefinitions.Add(NewObject<UParadoxInteractionTestBehaviorDefinition>(SmartDefinition));
		TestTrue(TEXT("Spatial SmartObject validates"), SmartDefinition->Validate());
		F.Terminal->SmartObject->SetDefinition(SmartDefinition);
		TestTrue(TEXT("Spatial SmartObject registers"), SmartObjects->RegisterSmartObject(F.Terminal->SmartObject));
		F.Terminal->Interaction->RefreshInteractionSources(); F.Terminal->SetFlags(RF_WasLoaded);
		struct FEntity { UGameplayActionComponent* Actions; UIntentReplayComponent* Replay; };
		auto Initialize = [&F, &Cell](APawn* Pawn)
		{
			auto* Root = NewObject<USceneComponent>(Pawn); Pawn->AddInstanceComponent(Root); Pawn->SetRootComponent(Root); Root->RegisterComponent();
			Pawn->SetActorLocation(Cell.WorldCenter);
			auto* Actions = NewObject<UGameplayActionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent();
			auto* Replay = NewObject<UIntentReplayComponent>(Pawn); Replay->ActionComponentOverride = Actions;
			Replay->ExecutionStrategyClass = UParadoxCloneReplayExecutionStrategy::StaticClass();
			Pawn->AddInstanceComponent(Replay); Replay->RegisterComponent(); Replay->InitializeIntentReplay();
			return FEntity{Actions, Replay};
		};
		const auto Player = Initialize(F.Player); const auto Clone = Initialize(F.Other);
		UPuzzleEmitterComponent* Gate = bGateFailure ? AddTerminalGate(F, false) : nullptr;
		if (Gate)
		{
			TestFalse(TEXT("Closed gate rejects the normal spatial hacking request"),
				F.Terminal->StartHacking(F.Player, ParadoxGameplayTags::Origin_Player, Player.Actions).IsAccepted());
			Gate->SetSignalState(ParadoxGameplayTags::Puzzle_Signal_Pressed, true, nullptr);
		}
		auto* State = F.World->GetSubsystem<UWorldStateSubsystem>();
		State->FinalizeWorldStateRegistration();
		TestTrue(TEXT("Ready baseline captures"), State->CaptureBaseline(FWorldStateCaptureRequest()).IsSuccess());
		TestTrue(TEXT("Recording starts"), Player.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
		auto* Selection = NewObject<UParadoxSelectionComponent>(F.Controller);
		F.Controller->AddInstanceComponent(Selection); Selection->RegisterComponent();
		TestTrue(TEXT("Terminal selected for Start availability"), Selection->HandleSelectionPointerHit(
			FHitResult(F.Terminal, F.Terminal->TerminalMesh, F.Terminal->GetActorLocation(), FVector::UpVector), true));
		auto* Widget = CreateView(F, true);
		if (!TestNotNull(TEXT("Common UI view exists"), Widget)) { return false; }
		FParadoxHackingTestAccess::Bind(*Widget, F.Terminal, F.Controller, Selection);
		auto* Switcher = CastChecked<UWidgetSwitcher>(Widget->GetWidgetFromName(TEXT("HackingStateSwitcher")));
		auto* Start = CastChecked<UCommonButtonBase>(Widget->GetWidgetFromName(TEXT("StartHackingButton")));
		TestEqual(TEXT("Ready terminal selects page zero"), Switcher->GetActiveWidgetIndex(), 0);
		// Hold approach resources so the click's accepted queued state is visible before an attempt exists.
		auto* Blocker = NewObject<UGameplayActionDefinition>(); Blocker->JournalRequirement = EGameplayActionJournalRequirement::Disabled;
		Blocker->InstanceClass = UGameplayWaitAction::StaticClass(); Blocker->ActionTag = ParadoxGameplayTags::Action_InvestigationInspect;
		Blocker->ExecutionLocks.AddTag(GameplayActionTags::Lock_Movement);
		Blocker->DefaultParameters.AddProperties({FPropertyBagPropertyDesc(TEXT("Duration"), EPropertyBagPropertyType::Double)});
		Blocker->DefaultParameters.SetValueDouble(TEXT("Duration"), 1.0);
		const auto Blocked = Player.Actions->SubmitAction(UGameplayActionBlueprintLibrary::CreateActionRequest(Blocker).Request);
		TestTrue(TEXT("Approach blocker starts"), Blocked.IsAccepted());
		Widget->RefreshHackingView();
		if (!TestTrue(TEXT("Common Start interaction is available"), Start->IsInteractionEnabled())) { return false; }
		Start->OnClicked().Broadcast();
		const auto Queued = Player.Actions->GetQueuedActionHandles();
		if (!TestEqual(TEXT("Common Start submits one queued hacking action"), Queued.Num(), 1)) { return false; }
		const FGameplayActionHandle HackHandle = Queued[0];
		TestEqual(TEXT("Accepted Start immediately selects hacking page"), Switcher->GetActiveWidgetIndex(), 1);
		FParadoxHackingTestAccess::Unbind(*Widget);
		FParadoxHackingTestAccess::Bind(*Widget, F.Terminal, F.Controller, Selection);
		TestEqual(TEXT("Reselection reconstructs accepted approach page"), Switcher->GetActiveWidgetIndex(), 1);
		Player.Actions->CancelAction(Blocked.Handle, FGameplayTag());
		auto* Action = Cast<UParadoxHackTerminalAction>(Player.Actions->GetActionInstance(HackHandle));
		if (!TestNotNull(TEXT("Native HackTerminal action exists"), Action)) { return false; }
		TestTrue(TEXT("Hack runs in background"), Action->IsBackgroundExecution());
		TestFalse(TEXT("Approach claim released"), Action->HasInteractionClaim());
		TestTrue(TEXT("Owned locks released"), Action->GetHeldExecutionLocks().IsEmpty());
		TestTrue(TEXT("Declared interaction lock retained"), Action->GetExecutionLocks().HasTagExact(ParadoxGameplayTags::Lock_Interaction));
		auto* Wait = NewObject<UGameplayActionDefinition>(); Wait->JournalRequirement = EGameplayActionJournalRequirement::Disabled;
		Wait->InstanceClass = UGameplayWaitAction::StaticClass(); Wait->ActionTag = ParadoxGameplayTags::Action_InvestigationInspect;
		Wait->DefaultParameters.AddProperties({FPropertyBagPropertyDesc(TEXT("Duration"), EPropertyBagPropertyType::Double)});
		Wait->ExecutionLocks.AddTag(GameplayActionTags::Lock_Movement); Wait->DefaultParameters.SetValueDouble(TEXT("Duration"), 0.0);
		TestTrue(TEXT("Unrelated action can start"), Player.Actions->SubmitAction(UGameplayActionBlueprintLibrary::CreateActionRequest(Wait).Request).IsAccepted());
		F.Player->SetActorLocation(Cell.WorldCenter + FVector(10000,0,0)); F.Advance(0.25);
		FParadoxHackingAttemptSnapshot Attempt;
		TestTrue(TEXT("Attempt persists after leaving range"), F.Terminal->GetAttemptForInstigator(F.Player, Attempt));
		if (bRecordedSuccess) { FParadoxHackingTestAccess::Finish(*F.Terminal, Attempt.AttemptId); }
		else if (Gate)
		{
			Gate->SetSignalState(ParadoxGameplayTags::Puzzle_Signal_Pressed, false, nullptr);
			FGameplayActionResult Result;
			TestTrue(TEXT("Gate closure ends the background Gameplay Action immediately"), Player.Actions->GetActionResult(HackHandle, Result));
			TestEqual(TEXT("Gate closure is semantic failure"), Result.TerminalState, EGameplayActionState::Failed);
			TestEqual(TEXT("Attempt fails immediately"), F.View(Attempt.AttemptId).State, EParadoxHackingAttemptState::Failed);
			TestFalse(TEXT("Start disables immediately while gate is closed"), Start->IsInteractionEnabled());
		}
		else { Player.Actions->AbortAllActions(GameplayActionTags::Result_Aborted_SystemReset); }
		TestEqual(TEXT("Terminal action outcome selects success or retry page"), Switcher->GetActiveWidgetIndex(), bRecordedSuccess ? 2 : 0);
		TestTrue(TEXT("Track finalizes after terminal result"), Player.Replay->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate).Succeeded());
		auto* Track = Player.Replay->GetLastFinalizedTrack();
		if (!TestNotNull(TEXT("One semantic track exists"), Track)) { return false; }
		TestEqual(TEXT("Only one semantic hacking entry"), Track->GetEntryCount(), 1);
		FRecordedIntent Entry; Track->GetEntryByIndex(0, Entry);
		const auto Duration = Entry.OriginalResult.OutcomeParameters.GetValueDouble(TEXT("DurationSeconds"));
		const auto Success = Entry.OriginalResult.OutcomeParameters.GetValueBool(TEXT("bSucceeded"));
		TestTrue(TEXT("Outcome copied before cleanup"), Duration.HasValue() && Success.HasValue());
		if (Duration.HasValue()) { TestTrue(TEXT("Actual elapsed duration recorded"), FMath::IsNearlyEqual(Duration.GetValue(), 0.25, 0.001)); }
		if (Success.HasValue()) { TestEqual(TEXT("Rewind interruption records failure"), Success.GetValue(), bRecordedSuccess); }
		if (Gate) { Gate->SetSignalState(ParadoxGameplayTags::Puzzle_Signal_Pressed, true, nullptr); }
		TestTrue(TEXT("World baseline restores"), State->RestoreBaseline(FWorldStateRestoreRequest()).IsSuccess());
		Widget->RefreshHackingView(); TestEqual(TEXT("Restored Ready state selects retry page"), Switcher->GetActiveWidgetIndex(), 0);
		FParadoxHackingTestAccess::Unbind(*Widget);
		TestTrue(TEXT("Clone replay prepares"), Clone.Replay->PrepareReplay(Track, FIntentReplayPlaybackOptions()).WasAccepted());
		TestTrue(TEXT("Clone replay starts"), Clone.Replay->StartReplay().Succeeded());
		F.Advance(0.01);
		FParadoxHackingAttemptSnapshot ReplayAttempt;
		if (!TestTrue(TEXT("Clone owns replay attempt"), F.Terminal->GetAttemptForInstigator(F.Other, ReplayAttempt))) { return false; }
		TestEqual(TEXT("Clone uses semantic mode"), ReplayAttempt.Mode, EParadoxHackingMode::Replay);
		const auto Interruption = Clone.Replay->BeginExternalReplayInterruption(ParadoxGameplayTags::Result_Interrupted_ByInvestigation);
		TestTrue(TEXT("Investigation pauses timeline"), Interruption.Succeeded());
		TestEqual(TEXT("Background attempt is not queued for recovery"), Clone.Replay->GetActivePlaybackSession()->GetPendingExternalRecoveryCount(), 0);
		TestTrue(TEXT("Investigation preserves attempt"), F.View(ReplayAttempt.AttemptId).State == EParadoxHackingAttemptState::Active);
		TestTrue(TEXT("Timeline resumes without reissue"), Clone.Replay->ResumeReplay().Succeeded());
		F.Advance(0.26); F.Advance(0.01);
		TestEqual(TEXT("Expected failure does not stop replay"), Clone.Replay->GetPlaybackState(), EIntentReplayPlaybackState::Completed);
		TestEqual(TEXT("Semantic replay signal"), F.Signal(), bRecordedSuccess);
		bool bFoundTerminal = false;
		for (const auto& Event : Clone.Replay->GetActivePlaybackSession()->GetExecutionJournal()->GetEvents())
		{
			if (Event.bHasActionEvent && Event.ActionEvent.bHasResult)
			{
				bFoundTerminal = true;
				TestEqual(TEXT("Journal keeps real terminal outcome"), Event.ActionEvent.Result.TerminalState,
					bRecordedSuccess ? EGameplayActionState::Succeeded : EGameplayActionState::Failed);
			}
		}
		TestTrue(TEXT("Replay terminal outcome remains visible"), bFoundTerminal);
		// Replaying the same semantic result on an already Hacked terminal still waits its duration.
		FPuzzleSignalState PriorSignal; F.Terminal->Emitter->TryGetSignalState(F.Terminal->OutputSignalTag, PriorSignal);
		TestTrue(TEXT("Second replay prepares"), Clone.Replay->PrepareReplay(Track, FIntentReplayPlaybackOptions()).WasAccepted());
		TestTrue(TEXT("Second replay starts"), Clone.Replay->StartReplay().Succeeded()); F.Advance(0.01);
		FParadoxHackingAttemptSnapshot Second;
		TestTrue(TEXT("Second replay has an active attempt"), F.Terminal->GetAttemptForInstigator(F.Other, Second)
			&& Second.State == EParadoxHackingAttemptState::Active);
		if (bRecordedSuccess)
		{
			F.Advance(0.26); F.Advance(0.01);
			TestEqual(TEXT("Already Hacked replay preserves duration"), F.View(Second.AttemptId).DurationSeconds, Duration.GetValue());
			FPuzzleSignalState AfterSignal; F.Terminal->Emitter->TryGetSignalState(F.Terminal->OutputSignalTag, AfterSignal);
			TestEqual(TEXT("Already Hacked replay does not republish"), AfterSignal.Revision, PriorSignal.Revision);
			TestTrue(TEXT("Ready baseline restores for AI"), State->RestoreBaseline(FWorldStateRestoreRequest()).IsSuccess());
			TestTrue(TEXT("AI journal session starts"), Clone.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
			const auto GoalRequest = F.Terminal->StartHacking(F.Other, Origin_GoalDerivedTest, Clone.Actions);
			if (!TestTrue(*GoalRequest.DiagnosticMessage, GoalRequest.IsAccepted())) { return false; }
			FParadoxHackingAttemptSnapshot GoalAttempt; F.Terminal->GetAttemptForInstigator(F.Other, GoalAttempt);
			TestEqual(TEXT("Goal-derived request routes to simulation"), GoalAttempt.Mode, EParadoxHackingMode::Simulated);
			TestTrue(TEXT("AI exposes the sampled outcome"), GoalAttempt.bHasPlannedOutcome);
			const auto* GoalAction = Clone.Actions->GetActionInstance(GoalRequest.SubmissionResult.Handle);
			TestTrue(TEXT("AI action releases locks without finishing"), GoalAction && GoalAction->IsBackgroundExecution() && GoalAction->GetHeldExecutionLocks().IsEmpty());
			F.Other->SetActorLocation(Cell.WorldCenter + FVector(10000,0,0)); F.Advance(GoalAttempt.PlannedDurationSeconds + 0.01);
			TestEqual(TEXT("AI resolves its sampled outcome after moving away"), F.View(GoalAttempt.AttemptId).State,
				GoalAttempt.bPlannedSuccess ? EParadoxHackingAttemptState::Success : EParadoxHackingAttemptState::TimedOut);
			TestEqual(TEXT("AI action preserves sampled duration"), F.View(GoalAttempt.AttemptId).DurationSeconds, GoalAttempt.PlannedDurationSeconds);
			TestTrue(TEXT("AI journal finalizes"), Clone.Replay->RequestStopRecording(EIntentRecordingFinalizeMode::Immediate).Succeeded());
		}
		else
		{
			Clone.Replay->StopReplay(); F.Advance(0.3);
			TestEqual(TEXT("StopReplay explicitly cancels background hacking"), F.View(Second.AttemptId).State, EParadoxHackingAttemptState::Cancelled);
			TestFalse(TEXT("Stopped replay cannot emit success"), F.Signal());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxHackingInvalidCommands,
	"Paradox.Hacking.ValidationAndInitialSimulationOpportunity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxHackingInvalidCommands::RunTest(const FString&)
{
	using namespace ParadoxHackingTests;
	FFixture F; FString Diagnostic;
	for (const FString Invalid : { FString(), FString(TEXT("H EART")), FString(TEXT("H3ART")), FString(TEXT("CITTÀ")) })
	{
		F.Terminal->Password = Invalid;
		TestFalse(TEXT("Invalid password rejected"), F.Terminal->ValidateConfiguration(Diagnostic));
		TestFalse(TEXT("Invalid configuration cannot start"), FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player).IsValid());
	}
	F.Terminal->Password = TEXT("heart"); TestTrue(TEXT("Lowercase normalizes"), F.Terminal->ValidateConfiguration(Diagnostic));
	FGuid Id = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player); auto Snapshot = F.View(Id);
	TestTrue(TEXT("Diagnostics identify weak instigator"), Snapshot.InstigatorActor.Get() == F.Player);
	TestEqual(TEXT("Attempt uses uppercase target"), Snapshot.Password, FString(TEXT("HEART")));
	TestFalse(TEXT("Another instigator cannot submit"), F.Terminal->SubmitLetter(F.Other, Id, Snapshot.Generation, 0, Snapshot.Letters[0].CurrentLetter));
	TestFalse(TEXT("Wrong generation cannot submit"), F.Terminal->SubmitLetter(F.Player, Id, Snapshot.Generation + 1, 0, Snapshot.Letters[0].CurrentLetter));
	TestFalse(TEXT("Invalid index cannot submit"), F.Terminal->SubmitLetter(F.Player, Id, Snapshot.Generation, -1, TEXT("H")));
	TestFalse(TEXT("Stale display cannot submit"), F.Terminal->SubmitLetter(F.Player, Id, Snapshot.Generation, 0, TEXT("INVALID")));
	TestFalse(TEXT("Another instigator cannot cancel"), F.Terminal->CancelAttempt(F.Other, Id, Snapshot.Generation));
	Snapshot.Letters[0].Pool = TEXT("INVALID"); TestTrue(TEXT("Snapshot modification is isolated"), F.View(Id).Letters[0].Pool != Snapshot.Letters[0].Pool);
	F.Terminal->CancelAttempt(F.Player, Id, Snapshot.Generation);
	F.Terminal->LettersPerButton = 2; TestFalse(TEXT("K outside alphabet bounds rejected"), F.Terminal->ValidateConfiguration(Diagnostic));
	F.Terminal->LettersPerButton = 3; F.Terminal->TimeLimitSeconds = 0; TestFalse(TEXT("Zero time limit rejected"), F.Terminal->ValidateConfiguration(Diagnostic));
	F.Terminal->TimeLimitSeconds = 30; F.Terminal->Password = TEXT("H");
	const auto CapturedProperties = F.Terminal->WorldStateParticipant->CapturedProperties;
	F.Terminal->WorldStateParticipant->CapturedProperties.Reset();
	TestFalse(TEXT("Missing persistent capture rejected"), F.Terminal->ValidateConfiguration(Diagnostic));
	TestFalse(TEXT("Missing persistent capture prevents a new attempt"), FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player).IsValid());
	F.Terminal->WorldStateParticipant->CapturedProperties = CapturedProperties;
	bool bFoundZero = false;
	for (int32 Seed = 0; Seed < 128; ++Seed)
	{
		Id = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player, EParadoxHackingMode::Simulated, 0, false, Seed);
		TestTrue(TEXT("Simulation exposes a copied planned outcome"), F.View(Id).bHasPlannedOutcome);
		if (FParadoxHackingTestAccess::SolveTime(*F.Terminal, Id) == 0)
		{
			bFoundZero = true; F.Advance(0.01);
			TestEqual(TEXT("Initial opportunity completes at zero game duration"), F.View(Id).DurationSeconds, 0.0);
			TestEqual(TEXT("Initial opportunity succeeds"), F.View(Id).State, EParadoxHackingAttemptState::Success); break;
		}
		F.Terminal->CancelAttempt(F.Player, Id, F.View(Id).Generation);
	}
	TestTrue(TEXT("Simulation includes t=0 opportunity"), bFoundZero);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxHackingUnavailableView,
	"Paradox.Hacking.MissingBindingsLeaveAttemptUntouched", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxHackingUnavailableView::RunTest(const FString&)
{
	using namespace ParadoxHackingTests;
	FFixture F; const FGuid Id = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player);
	auto* Widget = CreateView(F, true);
	if (!TestNotNull(TEXT("Valid view exists first"), Widget)) { return false; }
	AddExpectedError(TEXT("requires BindWidget StartHackingButton"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("bindings: Start="), EAutomationExpectedErrorFlags::Contains, 1);
	Widget->LetterWidgetClass = nullptr; Widget->RefreshHackingView();
	TestFalse(TEXT("Missing class disables start"), CastChecked<UCommonButtonBase>(Widget->GetWidgetFromName(TEXT("StartHackingButton")))->IsInteractionEnabled());
	auto* Letter = Cast<UParadoxHackingLetterWidget>(CastChecked<UPanelWidget>(Widget->GetWidgetFromName(TEXT("LetterContainer")))->GetChildAt(0));
	if (!TestNotNull(TEXT("Valid dynamic letter exists before disabling the view"), Letter)) { return false; }
	TestFalse(TEXT("Missing class disables letter"), CastChecked<UCommonButtonBase>(Letter->GetWidgetFromName(TEXT("LetterButton")))->IsInteractionEnabled());
	TestEqual(TEXT("Attempt remains active with same identity"), F.View(Id).State, EParadoxHackingAttemptState::Active);
	FParadoxHackingTestAccess::Unbind(*Widget); Widget->RemoveFromParent();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxHackingCommonViewStates,
	"Paradox.Hacking.CommonViewStateSwitcher", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxHackingCommonViewStates::RunTest(const FString&)
{
	using namespace ParadoxHackingTests;
	FFixture F;
	auto* Widget = CreateView(F, false);
	if (!TestNotNull(TEXT("Common view exists"), Widget)) { return false; }
	auto* Switcher = CastChecked<UWidgetSwitcher>(Widget->GetWidgetFromName(TEXT("HackingStateSwitcher")));
	TestEqual(TEXT("Ready selects page 0"), Switcher->GetActiveWidgetIndex(), 0);
	FGuid Id = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player);
	Widget->RefreshHackingView(); // Private test starts bypass the action's attempt-start notification.
	TestEqual(TEXT("Active selects page 1"), Switcher->GetActiveWidgetIndex(), 1);
	auto Snapshot = F.View(Id);
	F.Terminal->CancelAttempt(F.Player, Id, Snapshot.Generation);
	TestEqual(TEXT("Cancellation selects page 0"), Switcher->GetActiveWidgetIndex(), 0);
	F.Terminal->TimeLimitSeconds = 0.1;
	Id = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player); F.Advance(0.11);
	TestEqual(TEXT("Timeout selects page 0"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("Timeout preserves failure"), F.View(Id).State, EParadoxHackingAttemptState::TimedOut);
	F.Terminal->TimeLimitSeconds = 30;
	Id = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player);
	Snapshot = F.View(Id);
	// Find an actual incorrect displayed letter without changing authoritative state.
	for (int32 Round = 0; Round < 50; ++Round)
	{
		Snapshot = F.View(Id); bool bSubmitted = false;
		for (int32 Index = 0; Index < Snapshot.Letters.Num(); ++Index)
		{
			if (Snapshot.Letters[Index].CurrentLetter != Snapshot.Letters[Index].TargetLetter)
			{
				F.Terminal->SubmitLetter(F.Player, Id, Snapshot.Generation, Index, Snapshot.Letters[Index].CurrentLetter);
				bSubmitted = true; break;
			}
		}
		if (bSubmitted) { break; }
		F.Advance(0.51);
	}
	TestTrue(TEXT("Incorrect letter was submitted"), F.View(Id).ErrorCount > 0);
	TestEqual(TEXT("Incorrect letter keeps active page 1"), Switcher->GetActiveWidgetIndex(), 1);
	const FGuid OtherId = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Other);
	FParadoxHackingTestAccess::Finish(*F.Terminal, OtherId);
	TestEqual(TEXT("Peer success supersedes own attempt"), F.View(Id).State, EParadoxHackingAttemptState::Superseded);
	TestEqual(TEXT("Hacked terminal always selects page 2"), Switcher->GetActiveWidgetIndex(), 2);
	FParadoxHackingTestAccess::Unbind(*Widget); FParadoxHackingTestAccess::Bind(*Widget, F.Terminal, F.Controller);
	TestEqual(TEXT("Reselection of Hacked terminal selects page 2"), Switcher->GetActiveWidgetIndex(), 2);
	AddExpectedError(TEXT("requires BindWidget StartHackingButton"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("SwitcherPages=2"), EAutomationExpectedErrorFlags::Contains, 1);
	Switcher->RemoveChildAt(2); Widget->RefreshHackingView();
	TestFalse(TEXT("Incomplete switcher disables input"), CastChecked<UCommonButtonBase>(Widget->GetWidgetFromName(TEXT("StartHackingButton")))->IsInteractionEnabled());
	TestEqual(TEXT("Incomplete view cannot change Hacked state"), F.Terminal->GetTerminalState(), EParadoxHackingTerminalState::Hacked);
	FParadoxHackingTestAccess::Unbind(*Widget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxHackingGateInvalidation,
	"Paradox.Hacking.GateInvalidationConcurrencyAndView", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxHackingGateInvalidation::RunTest(const FString&)
{
	using namespace ParadoxHackingTests;
	FFixture F;
	F.Terminal->LetterRefreshInterval = 10;
	auto* Gate = AddTerminalGate(F, false);
	TestTrue(TEXT("Terminal starts gate-blocked"), F.Terminal->Emitter->IsInvalidated(F.Terminal->OutputSignalTag));
	for (auto Mode : { EParadoxHackingMode::Interactive, EParadoxHackingMode::Simulated, EParadoxHackingMode::Replay })
	{
		TestFalse(TEXT("Blocked emitter rejects every attempt mode"), FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player, Mode, 10, true).IsValid());
	}
	Gate->SetSignalState(ParadoxGameplayTags::Puzzle_Signal_Pressed, true, nullptr);
	const FGuid PlayerId = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player);
	const FGuid ReplayId = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Other, EParadoxHackingMode::Replay, 10, true);
	APawn* AI = F.World->SpawnActor<APawn>();
	const FGuid SimulatedId = FParadoxHackingTestAccess::Begin(*F.Terminal, AI, EParadoxHackingMode::Simulated);
	TestTrue(TEXT("Concurrent modes start"), PlayerId.IsValid() && ReplayId.IsValid() && SimulatedId.IsValid());
	TestTrue(TEXT("Simulation has a future completion"), F.View(SimulatedId).PlannedDurationSeconds > 0.2);
	auto* Widget = CreateView(F, true);
	if (!TestNotNull(TEXT("Blueprint view exists"), Widget)) { return false; }
	auto* Switcher = CastChecked<UWidgetSwitcher>(Widget->GetWidgetFromName(TEXT("HackingStateSwitcher")));
	TestEqual(TEXT("Active hacking is page one"), Switcher->GetActiveWidgetIndex(), 1);
	F.Advance(0.2);
	Gate->SetSignalState(ParadoxGameplayTags::Puzzle_Signal_Pressed, false, nullptr);
	for (FGuid Id : { PlayerId, ReplayId, SimulatedId })
	{
		TestEqual(TEXT("Gate closure fails every concurrent mode immediately"), F.View(Id).State, EParadoxHackingAttemptState::Failed);
		TestTrue(TEXT("Failure records elapsed time rather than planned time"), FMath::IsNearlyEqual(F.View(Id).DurationSeconds, 0.2, 0.001));
	}
	TestFalse(TEXT("Gate failure never publishes success"), F.Signal());
	TestEqual(TEXT("UI returns to page zero without ticking"), Switcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("View observes semantic failure immediately"), Widget->GetViewedAttempt().State, EParadoxHackingAttemptState::Failed);
	TestFalse(TEXT("Failed attempt rejects stale clicks"), F.Terminal->SubmitLetter(F.Player, PlayerId, F.View(PlayerId).Generation, 0, F.View(PlayerId).Letters[0].CurrentLetter));
	Gate->SetSignalState(ParadoxGameplayTags::Puzzle_Signal_Pressed, true, nullptr);
	TestEqual(TEXT("Reopening does not resume failed attempt"), F.View(PlayerId).State, EParadoxHackingAttemptState::Failed);
	const FGuid Retry = FParadoxHackingTestAccess::Begin(*F.Terminal, F.Player);
	TestTrue(TEXT("Reopening admits a fresh attempt"), Retry.IsValid() && Retry != PlayerId);
	FParadoxHackingTestAccess::Finish(*F.Terminal, Retry);
	Gate->SetSignalState(ParadoxGameplayTags::Puzzle_Signal_Pressed, false, nullptr);
	TestEqual(TEXT("Subsequent gate closure preserves persistent Hacked"), F.Terminal->GetTerminalState(), EParadoxHackingTerminalState::Hacked);
	TestEqual(TEXT("Hacked terminal retains page two"), Switcher->GetActiveWidgetIndex(), 2);
	FParadoxHackingTestAccess::Unbind(*Widget);
	return true;
}

namespace ParadoxHackingTests
{
	class FRestoreHackingViewport final : public IAutomationLatentCommand
	{
	public:
		explicit FRestoreHackingViewport(UClass* InPreviousClass) : PreviousClass(InPreviousClass) {}
		virtual bool Update() override { if (GEngine) { GEngine->GameViewportClientClass = PreviousClass.Get(); } return true; }
	private:
		TStrongObjectPtr<UClass> PreviousClass;
	};

	class FCheckHackingPIE final : public IAutomationLatentCommand
	{
	public:
		FCheckHackingPIE(FAutomationTestBase* InTest, UClass* InViewClass, UClass* InLetterClass)
			: Test(InTest), ViewClass(InViewClass), LetterClass(InLetterClass), StartedReal(FPlatformTime::Seconds()) {}
		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld.Get() : nullptr;
			const double RealNow = FPlatformTime::Seconds();
			if (RealNow - StartedReal > 15)
			{
				Test->AddError(TEXT("PIE hacking did not finish within 15 real seconds.")); Cleanup(World); return true;
			}
			if (!World) { return false; }
			if (!Terminal.IsValid())
			{
				for (TActorIterator<AParadoxHackingTerminal> It(World); It; ++It) { Terminal = *It; break; }
				APlayerController* Controller = World->GetFirstPlayerController();
				if (!Terminal.IsValid() || !Controller || !Controller->GetPawn()) { Terminal.Reset(); return false; }
				Id = FParadoxHackingTestAccess::Begin(*Terminal, Controller->GetPawn(), EParadoxHackingMode::Replay, 0.4, true);
				if (!Test->TestTrue(TEXT("PIE attempt starts"), Id.IsValid())) { return true; }
				View.Reset(CreateWidget<UParadoxHackingTerminalWidget>(Controller, ViewClass.Get()));
				if (!Test->TestNotNull(TEXT("PIE Blueprint view exists"), View.Get())) { return true; }
				View->LetterWidgetClass = LetterClass.Get(); View->AddToViewport();
				FParadoxHackingTestAccess::Bind(*View, Terminal.Get(), Controller);
				Test->TestTrue(TEXT("PIE view binds the actual attempt"), View->GetViewedAttempt().AttemptId == Id);
				StartedGame = World->GetTimeSeconds(); return false;
			}
			if (Stage == 0 && World->GetTimeSeconds() - StartedGame >= 0.05)
			{
				Test->TestTrue(TEXT("PIE pauses"), UGameplayStatics::SetGamePaused(World, true));
				PausedGame = World->GetTimeSeconds(); PausedReal = RealNow;
				FParadoxHackingAttemptSnapshot Snapshot; Terminal->GetAttemptSnapshot(Id, Snapshot); PausedRemaining = Snapshot.TimeRemaining;
				Stage = 1;
			}
			else if (Stage == 1 && RealNow - PausedReal >= 0.2)
			{
				FParadoxHackingAttemptSnapshot Snapshot; Terminal->GetAttemptSnapshot(Id, Snapshot);
				Test->TestTrue(TEXT("Pause freezes authoritative countdown"), FMath::IsNearlyEqual(Snapshot.TimeRemaining, PausedRemaining, 0.001));
				Test->TestTrue(TEXT("Pause freezes game clock"), FMath::IsNearlyEqual(double(World->GetTimeSeconds()), PausedGame, 0.001));
				UGameplayStatics::SetGamePaused(World, false); UGameplayStatics::SetGlobalTimeDilation(World, 0.5f); Stage = 2;
			}
			else if (Stage == 2)
			{
				FParadoxHackingAttemptSnapshot Snapshot; Terminal->GetAttemptSnapshot(Id, Snapshot);
				if (Snapshot.State != EParadoxHackingAttemptState::Active)
				{
					Test->TestEqual(TEXT("PIE succeeds with dilation"), Snapshot.State, EParadoxHackingAttemptState::Success);
					Test->TestEqual(TEXT("Duration uses game seconds"), Snapshot.DurationSeconds, 0.4);
					Test->TestEqual(TEXT("Global dilation is applied"), UGameplayStatics::GetGlobalTimeDilation(World), 0.5f);
					Test->TestEqual(TEXT("PIE view observes completion"), View->GetViewedAttempt().State, EParadoxHackingAttemptState::Success);
					Cleanup(World); return true;
				}
			}
			return false;
		}
	private:
		void Cleanup(UWorld* World)
		{
			if (View.IsValid()) { FParadoxHackingTestAccess::Unbind(*View); View->RemoveFromParent(); View.Reset(); }
			if (World) { UGameplayStatics::SetGamePaused(World, false); UGameplayStatics::SetGlobalTimeDilation(World, 1.f); }
		}
		FAutomationTestBase* Test;
		TStrongObjectPtr<UClass> ViewClass, LetterClass;
		TStrongObjectPtr<UParadoxHackingTerminalWidget> View;
		TWeakObjectPtr<AParadoxHackingTerminal> Terminal;
		FGuid Id;
		double StartedReal, StartedGame = 0, PausedReal = 0, PausedGame = 0, PausedRemaining = 0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxHackingPIE,
	"Paradox.Hacking.PIE.PauseDilationAndBlueprintView", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxHackingPIE::RunTest(const FString&)
{
	using namespace ParadoxHackingTests;
	TStrongObjectPtr<UClass> ViewClass, LetterClass;
	{
		FFixture Fixture; auto* View = CreateView(Fixture, false);
		if (!TestNotNull(TEXT("Test Blueprint composes"), View)) { return false; }
		ViewClass.Reset(View->GetClass()); LetterClass.Reset(View->LetterWidgetClass.Get());
		FParadoxHackingTestAccess::Unbind(*View); View->RemoveFromParent();
	}
	UWorld* EditorWorld = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("Transient PIE map exists"), EditorWorld)) { return false; }
	EditorWorld->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	EditorWorld->SpawnActor<AParadoxHackingTerminal>();
	UClass* PreviousViewportClass = GEngine->GameViewportClientClass.Get();
	// CommonUI is enabled in the project; the isolated PIE fixture needs its supported viewport.
	GEngine->GameViewportClientClass = UCommonGameViewportClient::StaticClass();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCheckHackingPIE>(this, ViewClass.Get(), LetterClass.Get()));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRestoreHackingViewport>(PreviousViewportClass));
	return true;
}
#endif
