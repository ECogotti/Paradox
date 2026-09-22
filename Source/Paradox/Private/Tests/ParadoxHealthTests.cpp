#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Behavior/ParadoxCloneBehaviorCoordinatorComponent.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "GameFramework/DamageType.h"
#include "Health/ParadoxHealthComponent.h"
#include "Health/ParadoxHealthWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Perception/ParadoxTemporalVisionComponent.h"
#include "Tests/ParadoxHealthTestTypes.h"
#include "TimeLoop/ParadoxTemporalEntityComponent.h"

namespace UE::Paradox::Health::Tests
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxHealthNativeDamageTest,
	"Paradox.Health.NativeDamageAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxHealthNativeDamageTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Health::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxHealthNativeDamageWorld"));
	AParadoxHealthTestCharacter* Character = Scope.World
		? SpawnCharacter<AParadoxHealthTestCharacter>(*Scope.World)
		: nullptr;
	if (!TestNotNull(TEXT("Health test Character spawns"), Character))
	{
		return false;
	}
	Scope.StartPlay();
	UParadoxHealthComponent* Health = Character->GetHealthComponent();
	if (!TestNotNull(TEXT("Shared Character owns Health"), Health))
	{
		return false;
	}
	TestTrue(TEXT("Character has begun play"), Character->HasActorBegunPlay());
	TestTrue(TEXT("Health component has begun play"), Health->HasBegunPlay());
	TestTrue(TEXT("Character accepts native damage"), Character->CanBeDamaged());
	TestTrue(TEXT("native any-damage delegate is bound"), Character->OnTakeAnyDamage.IsBound());
	TestEqual(TEXT("Character is authoritative"), Character->GetLocalRole(), ROLE_Authority);
	const FDamageEvent ProbeDamageEvent(UDamageType::StaticClass());
	TestTrue(
		TEXT("Pawn native damage policy accepts the hit"),
		Character->ShouldTakeDamage(75.0f, ProbeDamageEvent, nullptr, Character));

	UParadoxHealthEventRecorder* Recorder =
		NewObject<UParadoxHealthEventRecorder>(Scope.World);
	Health->OnHealthChanged.AddDynamic(
		Recorder,
		&UParadoxHealthEventRecorder::HandleHealthChanged);
	Health->OnDamageTaken.AddDynamic(
		Recorder,
		&UParadoxHealthEventRecorder::HandleDamageTaken);
	Health->OnDeath.AddDynamic(
		Recorder,
		&UParadoxHealthEventRecorder::HandleDeath);
	Health->OnHealed.AddDynamic(
		Recorder,
		&UParadoxHealthEventRecorder::HandleHealed);
	Health->OnHealthReset.AddDynamic(
		Recorder,
		&UParadoxHealthEventRecorder::HandleReset);
	Health->OnRevived.AddDynamic(
		Recorder,
		&UParadoxHealthEventRecorder::HandleRevived);

	TestEqual(
		TEXT("generic native damage reports its actual HP delta"),
		UGameplayStatics::ApplyDamage(
			Character, 75.0f, nullptr, Character, UDamageType::StaticClass()),
		75.0f);
	TestEqual(TEXT("generic damage changes authoritative HP"), Health->GetCurrentHealth(), 25.0f);
	if (TestEqual(TEXT("damage event order has two entries"), Recorder->EventOrder.Num(), 2))
	{
		TestEqual(TEXT("damage event precedes health change"), Recorder->EventOrder[0], FName(TEXT("DamageTaken")));
		TestEqual(TEXT("health change follows damage"), Recorder->EventOrder[1], FName(TEXT("HealthChanged")));
	}
	Recorder->EventOrder.Reset();
	TestEqual(
		TEXT("lethal overdamage is clamped to remaining HP"),
		UGameplayStatics::ApplyDamage(
			Character, 30.0f, nullptr, Character, UDamageType::StaticClass()),
		25.0f);
	TestEqual(TEXT("Health reaches zero"), Health->GetCurrentHealth(), 0.0f);
	TestTrue(TEXT("Health commits death"), Health->IsDead());
	TestEqual(TEXT("death fires exactly once"), Recorder->DeathCount, 1);
	TestEqual(TEXT("damage event exposes clamped amount"), Recorder->LastDamageApplied, 25.0f);
	if (TestEqual(TEXT("lethal damage event order has three entries"), Recorder->EventOrder.Num(), 3))
	{
		TestEqual(TEXT("lethal damage begins with damage"), Recorder->EventOrder[0], FName(TEXT("DamageTaken")));
		TestEqual(TEXT("lethal damage changes health second"), Recorder->EventOrder[1], FName(TEXT("HealthChanged")));
		TestEqual(TEXT("death notification is last"), Recorder->EventOrder[2], FName(TEXT("Death")));
	}
	TestEqual(
		TEXT("damage on a corpse reports zero"),
		UGameplayStatics::ApplyDamage(
			Character, 10.0f, nullptr, Character, UDamageType::StaticClass()),
		0.0f);
	TestEqual(TEXT("repeated lethal damage does not rebroadcast death"), Recorder->DeathCount, 1);
	TestEqual(TEXT("ordinary healing cannot revive"), Health->Heal(50.0f), 0.0f);

	Recorder->EventOrder.Reset();
	Health->ResetHealth();
	TestTrue(TEXT("explicit reset revives"), Health->IsAlive());
	TestEqual(TEXT("reset restores full health"), Health->GetCurrentHealth(), Health->GetMaxHealth());
	TestEqual(TEXT("reset event fires"), Recorder->ResetCount, 1);
	TestEqual(TEXT("revive event fires only for a dead life"), Recorder->RevivedCount, 1);
	if (TestEqual(TEXT("reset event order has three entries"), Recorder->EventOrder.Num(), 3))
	{
		TestEqual(TEXT("reset changes health first"), Recorder->EventOrder[0], FName(TEXT("HealthChanged")));
		TestEqual(TEXT("reset notification follows health"), Recorder->EventOrder[1], FName(TEXT("HealthReset")));
		TestEqual(TEXT("revive notification is last"), Recorder->EventOrder[2], FName(TEXT("Revived")));
	}

	Character->SetCanBeDamaged(false);
	TestEqual(
		TEXT("native damage gate is respected"),
		UGameplayStatics::ApplyDamage(
			Character, 10.0f, nullptr, Character, UDamageType::StaticClass()),
		0.0f);
	Character->SetCanBeDamaged(true);
	TestEqual(
		TEXT("zero native damage cannot mutate Health"),
		UGameplayStatics::ApplyDamage(
			Character, 0.0f, nullptr, Character, UDamageType::StaticClass()),
		0.0f);
	TestEqual(
		TEXT("negative native damage cannot mutate Health"),
		UGameplayStatics::ApplyDamage(
			Character, -10.0f, nullptr, Character, UDamageType::StaticClass()),
		0.0f);

	FHitResult Hit;
	TestEqual(
		TEXT("point damage uses the same native Health path"),
		UGameplayStatics::ApplyPointDamage(
			Character,
			10.0f,
			FVector::ForwardVector,
			Hit,
			nullptr,
			Character,
			UDamageType::StaticClass()),
		10.0f);
	TestEqual(TEXT("point damage reduced HP"), Health->GetCurrentHealth(), 90.0f);
	Recorder->EventOrder.Reset();
	TestEqual(TEXT("healing returns only the restored amount"), Health->Heal(20.0f), 10.0f);
	TestEqual(TEXT("healing clamps to MaxHealth"), Health->GetCurrentHealth(), 100.0f);
	if (TestEqual(TEXT("heal event order has two entries"), Recorder->EventOrder.Num(), 2))
	{
		TestEqual(TEXT("heal notification precedes health change"), Recorder->EventOrder[0], FName(TEXT("Healed")));
		TestEqual(TEXT("health change follows healing"), Recorder->EventOrder[1], FName(TEXT("HealthChanged")));
	}
	TestEqual(
		TEXT("Kill sends exactly remaining HP through native damage"),
		Health->Kill(nullptr, nullptr, UDamageType::StaticClass()),
		100.0f);
	TestEqual(TEXT("Kill enters the same death path"), Recorder->DeathCount, 2);

	Health->ResetHealth();
	const bool bRadialApplied = UGameplayStatics::ApplyRadialDamage(
		Scope.World,
		10.0f,
		Character->GetActorLocation(),
		200.0f,
		UDamageType::StaticClass(),
		TArray<AActor*>(),
		nullptr,
		nullptr,
		true,
		ECC_Visibility);
	TestTrue(TEXT("radial damage finds the Character"), bRadialApplied);
	TestEqual(TEXT("radial damage reaches Health"), Health->GetCurrentHealth(), 90.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxHealthWidgetExplicitSourceTest,
	"Paradox.Health.Widget.ExplicitSourceAndRebinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxHealthWidgetExplicitSourceTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Health::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxHealthWidgetWorld"));
	AParadoxHealthTestCharacter* First = Scope.World
		? SpawnCharacter<AParadoxHealthTestCharacter>(*Scope.World)
		: nullptr;
	AParadoxHealthTestCharacter* Second = Scope.World
		? SpawnCharacter<AParadoxHealthTestCharacter>(*Scope.World, FVector(300.0, 0.0, 0.0))
		: nullptr;
	Scope.StartPlay();
	UParadoxHealthWidget* Widget = Scope.World
		? CreateWidget<UParadoxHealthWidget>(Scope.World, UParadoxHealthWidget::StaticClass())
		: nullptr;
	if (!TestNotNull(TEXT("first Health source exists"), First)
		|| !TestNotNull(TEXT("second Health source exists"), Second)
		|| !TestNotNull(TEXT("Health widget exists without an owning Controller"), Widget))
	{
		return false;
	}
	Widget->TakeWidget();
	TestNull(TEXT("native Health widget creates no visual root"), Widget->GetRootWidget());
	Widget->SetObservedHealthComponent(First->GetHealthComponent());
	TestEqual(TEXT("explicit source is retained"), Widget->GetObservedHealthComponent(), First->GetHealthComponent());
	UGameplayStatics::ApplyDamage(
		First, 70.0f, nullptr, First, UDamageType::StaticClass());
	TestEqual(TEXT("widget follows explicit source HP"), Widget->GetDisplayedCurrentHealth(), 30.0f);
	TestEqual(TEXT("30 percent is Caution"), Widget->GetHealthDisplayState(), EParadoxHealthDisplayState::Caution);

	Widget->SetObservedHealthComponent(Second->GetHealthComponent());
	UGameplayStatics::ApplyDamage(
		First, 10.0f, nullptr, First, UDamageType::StaticClass());
	TestEqual(TEXT("old source no longer drives the widget"), Widget->GetDisplayedCurrentHealth(), 100.0f);
	UGameplayStatics::ApplyDamage(
		Second, 71.0f, nullptr, Second, UDamageType::StaticClass());
	TestEqual(TEXT("new source drives presentation"), Widget->GetDisplayedCurrentHealth(), 29.0f);
	TestEqual(TEXT("below Caution is Danger"), Widget->GetHealthDisplayState(), EParadoxHealthDisplayState::Danger);

	Second->Destroy();
	TestNull(TEXT("destroyed source is cleared immediately"), Widget->GetObservedHealthComponent());
	TestEqual(TEXT("missing source presents Dead/unavailable"), Widget->GetHealthDisplayState(), EParadoxHealthDisplayState::Dead);
	TestNull(
		TEXT("widget exposes no ReturnToOwningPlayer fallback"),
		UParadoxHealthWidget::StaticClass()->FindFunctionByName(TEXT("ReturnToOwningPlayer")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCloneHealthDeathTest,
	"Paradox.Health.CloneDeath.PassiveTemporalCorpse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCloneHealthDeathTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Health::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxCloneHealthDeathWorld"));
	AParadoxCloneCharacter* Clone = Scope.World
		? SpawnCharacter<AParadoxCloneCharacter>(*Scope.World)
		: nullptr;
	if (!TestNotNull(TEXT("Clone spawns"), Clone))
	{
		return false;
	}
	Scope.StartPlay();
	TestTrue(
		TEXT("test Clone receives a temporal index"),
		Clone->GetTemporalEntityComponent()->AssignPlayer(2));
	const float Applied = UGameplayStatics::ApplyDamage(
		Clone,
		Clone->GetHealthComponent()->GetMaxHealth(),
		nullptr,
		Clone,
		UDamageType::StaticClass());
	TestEqual(TEXT("Clone receives lethal native damage"), Applied, Clone->GetHealthComponent()->GetMaxHealth());
	TestTrue(TEXT("Clone Health is dead"), Clone->GetHealthComponent()->IsDead());
	TestTrue(TEXT("Clone behavior is terminally stopped"), Clone->GetBehaviorCoordinator()->IsStoppedForDeath());
	TestFalse(TEXT("dead Clone is not an active temporal observer"), Clone->GetTemporalVisionComponent()->IsTemporalDetectionAuthoritative());
	TestEqual(TEXT("dead Clone retains its temporal index"), Clone->GetTemporalEntityComponent()->GetTemporalIndex(), 2);

	UCapsuleComponent* Capsule = Clone->GetCapsuleComponent();
	TestEqual(TEXT("corpse capsule remains queryable as Pawn target"), Capsule->GetCollisionObjectType(), ECC_Pawn);
	TestEqual(TEXT("corpse capsule never blocks Pawns"), Capsule->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
	TestEqual(TEXT("corpse capsule has no blocking physics"), Capsule->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestFalse(TEXT("corpse capsule cannot affect navigation"), Capsule->CanEverAffectNavigation());
	if (USkeletalMeshComponent* SkeletalMesh = Clone->GetMesh())
	{
		TestEqual(TEXT("ragdoll mesh ignores Pawns"), SkeletalMesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
		TestFalse(TEXT("ragdoll mesh cannot affect navigation"), SkeletalMesh->CanEverAffectNavigation());
	}
	return true;
}

#endif
