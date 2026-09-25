#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Behavior/ParadoxCloneBehaviorCoordinatorComponent.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Health/ParadoxHealthComponent.h"
#include "Oxygen/ParadoxOxygenComponent.h"
#include "Oxygen/ParadoxOxygenDepletionDamageType.h"
#include "Oxygen/ParadoxOxygenWidget.h"
#include "Oxygen/ParadoxOxygenWorldSubsystem.h"
#include "Tests/ParadoxHealthTestTypes.h"
#include "Tests/ParadoxOxygenTestTypes.h"
#include "World/ParadoxWorldInitializer.h"

namespace UE::Paradox::Oxygen::Tests
{
	struct FScopedTestWorld
	{
		explicit FScopedTestWorld(const TCHAR* Name)
		{
			Context = GEngine
				? &GEngine->CreateNewWorldContext(EWorldType::Game)
				: nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			if (World)
			{
				World->AddToRoot();
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
				GameInstance = NewObject<UGameInstance>(GEngine);
				Context->OwningGameInstance = GameInstance;
				World->SetGameInstance(GameInstance);
			}
		}

		~FScopedTestWorld()
		{
			if (!World)
			{
				return;
			}
			World->DestroyWorld(true);
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World->RemoveFromRoot();
		}

		void StartPlay() const
		{
			if (!World || World->HasBegunPlay())
			{
				return;
			}
			World->CreateAISystem();
			World->SetGameMode(FURL());
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

		void TickSimulation(const float WallSeconds, const float Step = 0.05f) const
		{
			float Remaining = WallSeconds;
			while (Remaining > UE_SMALL_NUMBER)
			{
				const float Delta = FMath::Min(Remaining, Step);
				World->Tick(LEVELTICK_All, Delta);
				Remaining -= Delta;
			}
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
		UGameInstance* GameInstance = nullptr;
	};

	template <typename TCharacter>
	TCharacter* SpawnCharacter(UWorld& World, const FVector& Location = FVector::ZeroVector)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<TCharacter>(
			TCharacter::StaticClass(),
			FTransform(Location),
			Parameters);
	}

	AParadoxWorldInitializer* SpawnOxygenInitializer(
		UWorld& World,
		const EParadoxSharedOxygenConsumptionPolicy Policy =
			EParadoxSharedOxygenConsumptionPolicy::FixedWorldRate,
		const float DurationSeconds = 180.0f,
		const float BaseSpeed = 1.0f)
	{
		AParadoxWorldInitializer* Initializer =
			World.SpawnActor<AParadoxWorldInitializer>();
		if (Initializer)
		{
			Initializer->OxygenConfiguration.Mode =
				EParadoxOxygenMode::SharedGlobal;
			Initializer->OxygenConfiguration.SharedDurationSeconds =
				DurationSeconds;
			Initializer->OxygenConfiguration.SharedBaseConsumptionSpeed =
				BaseSpeed;
			Initializer->OxygenConfiguration.SharedConsumptionPolicy = Policy;
		}
		return Initializer;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxOxygenResourceOperationsTest,
	"Paradox.Oxygen.ResourceOperationsAndEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxOxygenResourceOperationsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Oxygen::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxOxygenResourceWorld"));
	AParadoxHealthTestCharacter* Character = Scope.World
		? SpawnCharacter<AParadoxHealthTestCharacter>(*Scope.World)
		: nullptr;
	if (!TestNotNull(TEXT("Oxygen test Character spawns"), Character))
	{
		return false;
	}
	Scope.StartPlay();
	UParadoxOxygenComponent* Oxygen = Character->GetOxygenComponent();
	if (!TestNotNull(TEXT("shared Character owns Oxygen"), Oxygen))
	{
		return false;
	}

	TestEqual(TEXT("designer duration defaults to 180 seconds"), Oxygen->GetOxygenDurationSeconds(), 180.0f);
	TestEqual(TEXT("new Oxygen starts full"), Oxygen->GetRemainingOxygenSeconds(), 180.0f);
	TestEqual(TEXT("normal speed defaults to x1"), Oxygen->GetEffectiveConsumptionSpeed(), 1.0f);
	TestEqual(TEXT("direct set uses seconds"), Oxygen->SetRemainingOxygenSeconds(120.0f), 120.0f);
	TestEqual(TEXT("direct consume reports actual seconds"), Oxygen->ConsumeOxygenSeconds(20.0f), 20.0f);
	TestEqual(TEXT("consume leaves 100 seconds"), Oxygen->GetRemainingOxygenSeconds(), 100.0f);
	TestEqual(TEXT("restore reports actual seconds"), Oxygen->RestoreOxygenSeconds(30.0f), 30.0f);
	TestEqual(TEXT("restore leaves 130 seconds"), Oxygen->GetRemainingOxygenSeconds(), 130.0f);
	TestEqual(TEXT("restore clamps at duration"), Oxygen->RestoreOxygenSeconds(100.0f), 50.0f);
	TestEqual(TEXT("normalized value derives from seconds"), Oxygen->GetNormalizedOxygen(), 1.0f);
	TestEqual(TEXT("negative consume is a no-op"), Oxygen->ConsumeOxygenSeconds(-1.0f), 0.0f);

	FParadoxOxygenSpeedModifierHandle DoubleSpeed =
		Oxygen->AddConsumptionSpeedModifier(Character, 2.0f);
	FParadoxOxygenSpeedModifierHandle HalfSpeed =
		Oxygen->AddConsumptionSpeedModifier(Character, 0.5f);
	TestTrue(TEXT("first speed handle is valid"), DoubleSpeed.IsValid());
	TestTrue(TEXT("same source may own a second independent handle"), HalfSpeed.IsValid());
	TestEqual(TEXT("multiplicative modifiers compose"), Oxygen->GetEffectiveConsumptionSpeed(), 1.0f);
	TestTrue(TEXT("exact speed handle removes its effect"), Oxygen->RemoveConsumptionSpeedModifier(DoubleSpeed));
	TestEqual(TEXT("remaining modifier is preserved"), Oxygen->GetEffectiveConsumptionSpeed(), 0.5f);
	TestFalse(TEXT("removed speed handle is stale"), Oxygen->RemoveConsumptionSpeedModifier(DoubleSpeed));
	TestFalse(
		TEXT("zero speed is rejected in favor of a blocker"),
		Oxygen->AddConsumptionSpeedModifier(Character, 0.0f).IsValid());

	FParadoxOxygenBlockHandle BlockA = Oxygen->AddConsumptionBlock(Character);
	FParadoxOxygenBlockHandle BlockB = Oxygen->AddConsumptionBlock(Character);
	TestTrue(TEXT("two source-owned blockers are accepted"), BlockA.IsValid() && BlockB.IsValid());
	TestTrue(TEXT("any blocker stops regular consumption"), Oxygen->IsConsumptionBlocked());
	TestTrue(TEXT("first blocker can be removed"), Oxygen->RemoveConsumptionBlock(BlockA));
	TestTrue(TEXT("second blocker still owns the block"), Oxygen->IsConsumptionBlocked());
	TestTrue(TEXT("second blocker can be removed"), Oxygen->RemoveConsumptionBlock(BlockB));
	TestFalse(TEXT("consumption resumes after the last blocker"), Oxygen->IsConsumptionBlocked());

	Oxygen->ResetOxygen();
	TestEqual(TEXT("reset restores full duration"), Oxygen->GetRemainingOxygenSeconds(), 180.0f);
	TestEqual(TEXT("reset restores base speed"), Oxygen->GetEffectiveConsumptionSpeed(), 1.0f);
	TestFalse(TEXT("reset clears blockers"), Oxygen->IsConsumptionBlocked());
	TestFalse(TEXT("reset invalidates old modifier handles"), Oxygen->RemoveConsumptionSpeedModifier(HalfSpeed));
	TestFalse(TEXT("reset invalidates old blocker handles"), Oxygen->RemoveConsumptionBlock(BlockB));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxOxygenSimulationClockTest,
	"Paradox.Oxygen.SimulationClockPauseAndDilation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxOxygenSimulationClockTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Oxygen::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxOxygenClockWorld"));
	AParadoxHealthTestCharacter* Character = Scope.World
		? SpawnCharacter<AParadoxHealthTestCharacter>(*Scope.World)
		: nullptr;
	APlayerState* PauserState = Scope.World
		? Scope.World->SpawnActor<APlayerState>()
		: nullptr;
	if (!TestNotNull(TEXT("clock test Character exists"), Character)
		|| !TestNotNull(TEXT("clock test pauser state exists"), PauserState))
	{
		return false;
	}
	Scope.StartPlay();
	UParadoxOxygenComponent* Oxygen = Character->GetOxygenComponent();
	Oxygen->SetRemainingOxygenSeconds(10.0f);
	Scope.TickSimulation(0.5f);
	TestEqual(
		TEXT("Oxygen does not consume before ActiveRun authorization"),
		Oxygen->GetRemainingOxygenSeconds(),
		10.0f);

	Oxygen->SetRunConsumptionActive(true);
	Scope.TickSimulation(1.0f);
	TestTrue(
		TEXT("x1 consumes approximately one oxygen second per simulation second"),
		FMath::IsNearlyEqual(Oxygen->GetRemainingOxygenSeconds(), 9.0f, 0.08f));

	const float BeforePause = Oxygen->GetRemainingOxygenSeconds();
	Scope.World->GetWorldSettings()->SetPauserPlayerState(PauserState);
	TestTrue(TEXT("test world enters Unreal pause"), Scope.World->IsPaused());
	Scope.TickSimulation(0.5f);
	TestTrue(
		TEXT("paused world does not consume Oxygen"),
		FMath::IsNearlyEqual(Oxygen->GetRemainingOxygenSeconds(), BeforePause, 0.01f));
	Scope.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	TestFalse(TEXT("test world resumes from Unreal pause"), Scope.World->IsPaused());

	Oxygen->SetRemainingOxygenSeconds(10.0f);
	Scope.World->GetWorldSettings()->SetTimeDilation(2.0f);
	Scope.TickSimulation(0.5f);
	TestTrue(
		TEXT("x2 world dilation consumes two seconds per wall-clock second"),
		FMath::IsNearlyEqual(Oxygen->GetRemainingOxygenSeconds(), 9.0f, 0.08f));
	Scope.World->GetWorldSettings()->SetTimeDilation(1.0f);
	Oxygen->SetRemainingOxygenSeconds(10.0f);
	Scope.World->GetWorldSettings()->SetTimeDilation(3.0f);
	Scope.TickSimulation(0.5f);
	TestTrue(
		TEXT("x3 world dilation consumes three seconds per wall-clock second"),
		FMath::IsNearlyEqual(Oxygen->GetRemainingOxygenSeconds(), 8.5f, 0.1f));
	Scope.World->GetWorldSettings()->SetTimeDilation(1.0f);

	Oxygen->SetRemainingOxygenSeconds(10.0f);
	const FParadoxOxygenSpeedModifierHandle DoubleSpeed =
		Oxygen->AddConsumptionSpeedModifier(Character, 2.0f);
	Scope.TickSimulation(0.5f);
	TestTrue(
		TEXT("x2 oxygen modifier consumes twice as fast in simulation time"),
		FMath::IsNearlyEqual(Oxygen->GetRemainingOxygenSeconds(), 9.0f, 0.08f));
	Oxygen->RemoveConsumptionSpeedModifier(DoubleSpeed);
	Oxygen->SetRemainingOxygenSeconds(180.0f);
	TestEqual(TEXT("countdown begins at 03:00"), Oxygen->GetWholeSecondsRemaining(), 180);
	Scope.TickSimulation(1.05f);
	TestEqual(TEXT("first whole-second boundary is 02:59"), Oxygen->GetWholeSecondsRemaining(), 179);
	Scope.TickSimulation(1.0f);
	TestEqual(TEXT("second whole-second boundary is 02:58"), Oxygen->GetWholeSecondsRemaining(), 178);

	const FParadoxOxygenBlockHandle Block = Oxygen->AddConsumptionBlock(Character);
	const float BeforeBlock = Oxygen->GetRemainingOxygenSeconds();
	Scope.TickSimulation(0.5f);
	TestTrue(
		TEXT("source blocker freezes analytical countdown"),
		FMath::IsNearlyEqual(Oxygen->GetRemainingOxygenSeconds(), BeforeBlock, 0.01f));
	Oxygen->RemoveConsumptionBlock(Block);
	Oxygen->SetRunConsumptionActive(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxOxygenDepletionTest,
	"Paradox.Oxygen.DepletionUsesNativeHealthKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxOxygenDepletionTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Oxygen::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxOxygenDepletionWorld"));
	AParadoxHealthTestCharacter* Character = Scope.World
		? SpawnCharacter<AParadoxHealthTestCharacter>(*Scope.World)
		: nullptr;
	if (!TestNotNull(TEXT("depletion test Character exists"), Character))
	{
		return false;
	}
	Scope.StartPlay();
	UParadoxOxygenComponent* Oxygen = Character->GetOxygenComponent();
	UParadoxHealthComponent* Health = Character->GetHealthComponent();
	UParadoxOxygenEventRecorder* Recorder =
		NewObject<UParadoxOxygenEventRecorder>(Scope.World);
	Oxygen->OnOxygenChanged.AddDynamic(
		Recorder,
		&UParadoxOxygenEventRecorder::HandleOxygenChanged);
	Oxygen->OnWholeSecondChanged.AddDynamic(
		Recorder,
		&UParadoxOxygenEventRecorder::HandleWholeSecondChanged);
	Oxygen->OnOxygenDepleted.AddDynamic(
		Recorder,
		&UParadoxOxygenEventRecorder::HandleOxygenDepleted);
	Oxygen->OnOxygenReset.AddDynamic(
		Recorder,
		&UParadoxOxygenEventRecorder::HandleOxygenReset);
	Health->OnDeath.AddDynamic(
		Recorder,
		&UParadoxOxygenEventRecorder::HandleHealthDeath);

	Oxygen->SetRemainingOxygenSeconds(1.0f);
	Recorder->EventOrder.Reset();
	TestEqual(TEXT("final second is consumed"), Oxygen->ConsumeOxygenSeconds(1.0f), 1.0f);
	TestTrue(TEXT("Oxygen commits depletion"), Oxygen->IsOxygenDepleted());
	TestEqual(TEXT("depletion is clamped to zero"), Oxygen->GetRemainingOxygenSeconds(), 0.0f);
	TestTrue(TEXT("Health receives the lethal consequence"), Health->IsDead());
	TestEqual(TEXT("depletion fires once"), Recorder->DepletedCount, 1);
	TestEqual(TEXT("Health death fires once"), Recorder->DeathCount, 1);
	TestEqual(
		TEXT("Health context classifies Oxygen depletion through UDamageType"),
		Recorder->LastDeathDamageTypeClass.Get(),
		UParadoxOxygenDepletionDamageType::StaticClass());
	if (TestEqual(TEXT("depletion order contains four notifications"), Recorder->EventOrder.Num(), 4))
	{
		TestEqual(TEXT("remaining value changes first"), Recorder->EventOrder[0], FName(TEXT("OxygenChanged")));
		TestEqual(TEXT("zero second notification is second"), Recorder->EventOrder[1], FName(TEXT("WholeSecondChanged")));
		TestEqual(TEXT("depletion notification precedes Health"), Recorder->EventOrder[2], FName(TEXT("OxygenDepleted")));
		TestEqual(TEXT("native Health death follows Oxygen"), Recorder->EventOrder[3], FName(TEXT("HealthDeath")));
	}
	TestEqual(TEXT("restore after accepted depletion is inert"), Oxygen->RestoreOxygenSeconds(30.0f), 0.0f);
	TestEqual(TEXT("repeated consume after depletion is inert"), Oxygen->ConsumeOxygenSeconds(1.0f), 0.0f);
	TestEqual(TEXT("depletion remains single"), Recorder->DepletedCount, 1);

	Oxygen->ResetOxygen();
	TestEqual(TEXT("resource reset restores duration"), Oxygen->GetRemainingOxygenSeconds(), 180.0f);
	TestFalse(TEXT("resource reset clears depletion"), Oxygen->IsOxygenDepleted());
	TestTrue(TEXT("Oxygen reset does not revive Health"), Health->IsDead());
	Health->ResetHealth();
	TestTrue(TEXT("Health owns revival"), Health->IsAlive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxOxygenWidgetExplicitSourceTest,
	"Paradox.Oxygen.Widget.ExplicitSourceAndRebinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxOxygenWidgetExplicitSourceTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Oxygen::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxOxygenWidgetWorld"));
	AParadoxHealthTestCharacter* First = Scope.World
		? SpawnCharacter<AParadoxHealthTestCharacter>(*Scope.World)
		: nullptr;
	AParadoxHealthTestCharacter* Second = Scope.World
		? SpawnCharacter<AParadoxHealthTestCharacter>(
			*Scope.World,
			FVector(300.0, 0.0, 0.0))
		: nullptr;
	Scope.StartPlay();
	UParadoxOxygenWidget* Widget = Scope.World
		? CreateWidget<UParadoxOxygenWidget>(
			Scope.World,
			UParadoxOxygenWidget::StaticClass())
		: nullptr;
	if (!TestNotNull(TEXT("first Oxygen source exists"), First)
		|| !TestNotNull(TEXT("second Oxygen source exists"), Second)
		|| !TestNotNull(TEXT("Oxygen widget exists without an owning Controller"), Widget))
	{
		return false;
	}
	Widget->TakeWidget();
	TestNull(TEXT("native Oxygen widget creates no visual root"), Widget->GetRootWidget());
	Widget->SetObservedOxygenComponent(First->GetOxygenComponent());
	First->GetOxygenComponent()->SetRemainingOxygenSeconds(25.0f);
	TestEqual(TEXT("explicit source drives display"), Widget->GetDisplayedRemainingSeconds(), 25.0f);
	TestEqual(TEXT("25 seconds is Low"), Widget->GetOxygenWarningState(), EParadoxOxygenWarningState::Low);
	TestEqual(TEXT("countdown helper formats seconds"), Widget->GetFormattedCountdownText().ToString(), FString(TEXT("00:25")));

	Widget->SetObservedOxygenComponent(Second->GetOxygenComponent());
	First->GetOxygenComponent()->SetRemainingOxygenSeconds(5.0f);
	TestEqual(TEXT("old source is unbound"), Widget->GetDisplayedRemainingSeconds(), 180.0f);
	Second->GetOxygenComponent()->SetRemainingOxygenSeconds(9.0f);
	TestEqual(TEXT("new source drives display"), Widget->GetDisplayedRemainingSeconds(), 9.0f);
	TestEqual(TEXT("nine seconds is Critical"), Widget->GetOxygenWarningState(), EParadoxOxygenWarningState::Critical);

	Second->Destroy();
	TestNull(TEXT("destroyed source is cleared"), Widget->GetObservedOxygenComponent());
	TestEqual(TEXT("missing source displays Depleted/unavailable"), Widget->GetOxygenWarningState(), EParadoxOxygenWarningState::Depleted);
	TestNull(
		TEXT("widget exposes no Owning Player fallback"),
		UParadoxOxygenWidget::StaticClass()->FindFunctionByName(TEXT("ReturnToOwningPlayer")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxSharedOxygenWidgetInitialSnapshotTest,
	"Paradox.Oxygen.Widget.SharedConfigurationIsVisibleBeforeFirstTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxSharedOxygenWidgetInitialSnapshotTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::Oxygen::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxSharedOxygenWidgetInitialSnapshotWorld"));
	if (!TestNotNull(
		TEXT("18-minute shared initializer spawns"),
		SpawnOxygenInitializer(
			*Scope.World,
			EParadoxSharedOxygenConsumptionPolicy::FixedWorldRate,
			1080.0f)))
	{
		return false;
	}
	AParadoxHealthTestCharacter* Character =
		SpawnCharacter<AParadoxHealthTestCharacter>(*Scope.World);
	UParadoxOxygenWidget* Widget = CreateWidget<UParadoxOxygenWidget>(
		Scope.World,
		UParadoxOxygenWidget::StaticClass());
	if (!TestNotNull(TEXT("shared Oxygen character exists"), Character)
		|| !TestNotNull(TEXT("shared Oxygen widget exists"), Widget))
	{
		return false;
	}

	Widget->TakeWidget();
	Widget->SetObservedOxygenComponent(Character->GetOxygenComponent());
	TestEqual(
		TEXT("pre-BeginPlay presentation initially sees the component fallback"),
		Widget->GetDisplayedWholeSecondsRemaining(),
		180);

	Scope.StartPlay();
	TestEqual(
		TEXT("shared duration is published during BeginPlay without a World tick"),
		Widget->GetDisplayedDurationSeconds(),
		1080.0f);
	TestEqual(
		TEXT("shared remaining Oxygen is published during BeginPlay"),
		Widget->GetDisplayedWholeSecondsRemaining(),
		1080);
	TestEqual(
		TEXT("countdown is correct before the first simulation tick"),
		Widget->GetFormattedCountdownText().ToString(),
		FString(TEXT("18:00")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCloneOxygenDepletionTest,
	"Paradox.Oxygen.CloneDepletionUsesSharedDeathPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCloneOxygenDepletionTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Oxygen::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxCloneOxygenWorld"));
	AParadoxCloneCharacter* Clone = Scope.World
		? SpawnCharacter<AParadoxCloneCharacter>(*Scope.World)
		: nullptr;
	if (!TestNotNull(TEXT("Clone exists"), Clone))
	{
		return false;
	}
	Scope.StartPlay();
	UParadoxOxygenComponent* Oxygen = Clone->GetOxygenComponent();
	Oxygen->ConsumeOxygenSeconds(Oxygen->GetOxygenDurationSeconds());
	TestTrue(TEXT("Clone Oxygen depletes"), Oxygen->IsOxygenDepleted());
	TestTrue(TEXT("shared Health kills the Clone"), Clone->GetHealthComponent()->IsDead());
	TestTrue(
		TEXT("existing Clone death path stops behavior"),
		Clone->GetBehaviorCoordinator()->IsStoppedForDeath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxOxygenWorldConfigurationTest,
	"Paradox.Oxygen.WorldConfigurationFallbackAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxOxygenWorldConfigurationTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Oxygen::Tests;
	{
		FScopedTestWorld Scope(TEXT("ParadoxOxygenPerPawnFallbackWorld"));
		Scope.StartPlay();
		const UParadoxOxygenWorldSubsystem* OxygenWorld = Scope.World
			? Scope.World->GetSubsystem<UParadoxOxygenWorldSubsystem>()
			: nullptr;
		if (TestNotNull(TEXT("Oxygen World subsystem exists without an initializer"), OxygenWorld))
		{
			TestTrue(TEXT("missing initializer is valid legacy configuration"), OxygenWorld->IsConfigurationValid());
			TestEqual(TEXT("missing initializer preserves Per-Pawn Oxygen"), OxygenWorld->GetOxygenMode(), EParadoxOxygenMode::PerPawn);
		}
	}

	{
		FScopedTestWorld Scope(TEXT("ParadoxOxygenDuplicateInitializerWorld"));
		TestNotNull(TEXT("first initializer spawns"), SpawnOxygenInitializer(*Scope.World));
		TestNotNull(TEXT("second initializer spawns"), SpawnOxygenInitializer(*Scope.World));
		AddExpectedError(
			TEXT("contains 2 Paradox World Initializers"),
			EAutomationExpectedErrorFlags::Contains,
			1);
		Scope.StartPlay();
		const UParadoxOxygenWorldSubsystem* OxygenWorld =
			Scope.World->GetSubsystem<UParadoxOxygenWorldSubsystem>();
		TestFalse(TEXT("duplicate initializers invalidate World configuration"), OxygenWorld->IsConfigurationValid());
	}

	{
		FScopedTestWorld Scope(TEXT("ParadoxOxygenInvalidInitializerWorld"));
		TestNotNull(
			TEXT("invalid initializer spawns"),
			SpawnOxygenInitializer(
				*Scope.World,
				EParadoxSharedOxygenConsumptionPolicy::FixedWorldRate,
				0.0f,
				1.0f));
		AddExpectedError(
			TEXT("has invalid shared Oxygen duration"),
			EAutomationExpectedErrorFlags::Contains,
			1);
		Scope.StartPlay();
		const UParadoxOxygenWorldSubsystem* OxygenWorld =
			Scope.World->GetSubsystem<UParadoxOxygenWorldSubsystem>();
		TestFalse(TEXT("invalid shared values invalidate World configuration"), OxygenWorld->IsConfigurationValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxSharedOxygenConsumptionPolicyTest,
	"Paradox.Oxygen.SharedGlobalConsumptionPolicies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxSharedOxygenConsumptionPolicyTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::Oxygen::Tests;
	auto VerifyPolicy = [this](
		const TCHAR* WorldName,
		const EParadoxSharedOxygenConsumptionPolicy Policy,
		const float ExpectedRemaining)
	{
		FScopedTestWorld Scope(WorldName);
		if (!TestNotNull(
			TEXT("shared initializer spawns"),
			SpawnOxygenInitializer(*Scope.World, Policy)))
		{
			return;
		}
		AParadoxHealthTestCharacter* First =
			SpawnCharacter<AParadoxHealthTestCharacter>(*Scope.World);
		AParadoxHealthTestCharacter* Second =
			SpawnCharacter<AParadoxHealthTestCharacter>(
				*Scope.World,
				FVector(300.0f, 0.0f, 0.0f));
		Scope.StartPlay();
		if (!TestNotNull(TEXT("first shared participant exists"), First)
			|| !TestNotNull(TEXT("second shared participant exists"), Second))
		{
			return;
		}
		UParadoxOxygenComponent* FirstOxygen = First->GetOxygenComponent();
		UParadoxOxygenComponent* SecondOxygen = Second->GetOxygenComponent();
		UParadoxOxygenWorldSubsystem* OxygenWorld =
			Scope.World->GetSubsystem<UParadoxOxygenWorldSubsystem>();
		if (!TestNotNull(TEXT("shared Oxygen subsystem exists"), OxygenWorld)
			|| !TestNotNull(TEXT("first Oxygen facade exists"), FirstOxygen)
			|| !TestNotNull(TEXT("second Oxygen facade exists"), SecondOxygen))
		{
			return;
		}
		TestTrue(TEXT("component reports Shared Global mode"), FirstOxygen->IsUsingSharedGlobalOxygen());
		FirstOxygen->SetRemainingOxygenSeconds(30.0f);
		TestEqual(TEXT("second facade observes the same reservoir"), SecondOxygen->GetRemainingOxygenSeconds(), 30.0f);
		FirstOxygen->SetRunConsumptionActive(true);
		SecondOxygen->SetRunConsumptionActive(true);
		TestEqual(TEXT("both avatars count as active participants"), OxygenWorld->GetActiveParticipantCount(), 2);
		Scope.TickSimulation(1.0f);
		TestTrue(
			TEXT("configured policy applies the expected shared rate"),
			FMath::IsNearlyEqual(
				OxygenWorld->GetSharedRemainingSeconds(),
				ExpectedRemaining,
				0.08f));
		FirstOxygen->SetRunConsumptionActive(false);
		SecondOxygen->SetRunConsumptionActive(false);
	};

	VerifyPolicy(
		TEXT("ParadoxOxygenFixedWorldRateWorld"),
		EParadoxSharedOxygenConsumptionPolicy::FixedWorldRate,
		29.0f);
	VerifyPolicy(
		TEXT("ParadoxOxygenPerAvatarWorld"),
		EParadoxSharedOxygenConsumptionPolicy::PerActiveAvatar,
		28.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxSharedOxygenCheckpointTest,
	"Paradox.Oxygen.SharedGlobalCheckpointRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxSharedOxygenCheckpointTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Oxygen::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxOxygenCheckpointWorld"));
	TestNotNull(TEXT("shared initializer spawns"), SpawnOxygenInitializer(*Scope.World));
	AParadoxHealthTestCharacter* Character =
		SpawnCharacter<AParadoxHealthTestCharacter>(*Scope.World);
	Scope.StartPlay();
	UParadoxOxygenWorldSubsystem* OxygenWorld =
		Scope.World->GetSubsystem<UParadoxOxygenWorldSubsystem>();
	UParadoxOxygenComponent* Oxygen = Character
		? Character->GetOxygenComponent()
		: nullptr;
	if (!TestNotNull(TEXT("shared checkpoint subsystem exists"), OxygenWorld)
		|| !TestNotNull(TEXT("shared checkpoint facade exists"), Oxygen))
	{
		return false;
	}

	TestEqual(TEXT("configured duration is the first checkpoint"), OxygenWorld->GetRunCheckpointRemainingSeconds(), 180.0f);
	Oxygen->SetRemainingOxygenSeconds(120.0f);
	TestTrue(TEXT("successful Time Travel can promote live Oxygen"), OxygenWorld->CommitCurrentAsRunCheckpoint());
	TestEqual(TEXT("promoted checkpoint preserves the live value"), OxygenWorld->GetRunCheckpointRemainingSeconds(), 120.0f);
	const FParadoxOxygenSpeedModifierHandle Modifier =
		Oxygen->AddConsumptionSpeedModifier(Character, 2.0f);
	const FParadoxOxygenBlockHandle Block =
		Oxygen->AddConsumptionBlock(Character);
	Oxygen->SetRemainingOxygenSeconds(55.0f);
	TestTrue(TEXT("failed run restores its exact starting checkpoint"), OxygenWorld->RestoreRunCheckpoint());
	TestEqual(TEXT("rollback restores 120 rather than full capacity"), Oxygen->GetRemainingOxygenSeconds(), 120.0f);
	TestEqual(TEXT("rollback clears active shared participants"), OxygenWorld->GetActiveParticipantCount(), 0);
	TestEqual(TEXT("rollback clears transient speed modifiers"), Oxygen->GetEffectiveConsumptionSpeed(), 1.0f);
	TestFalse(TEXT("rollback clears transient blockers"), Oxygen->IsConsumptionBlocked());
	TestFalse(TEXT("rollback invalidates old modifier handles"), Oxygen->RemoveConsumptionSpeedModifier(Modifier));
	TestFalse(TEXT("rollback invalidates old blocker handles"), Oxygen->RemoveConsumptionBlock(Block));
	return true;
}

#endif
