#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Actions/GameplayActionDefinition.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/IntentReplayComponent.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DamageType.h"
#include "GameplayActionTags.h"
#include "Health/ParadoxHealthComponent.h"
#include "Inventory/ParadoxInventoryActionButtonWidget.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxInventoryWidget.h"
#include "Inventory/ParadoxOxygenCanister.h"
#include "Inventory/ParadoxPickupableAction.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "Inventory/ParadoxUsePickupableAction.h"
#include "Oxygen/ParadoxOxygenComponent.h"
#include "Paradox.h"
#include "Recording/IntentReplayTrack.h"
#include "StructUtils/PropertyBag.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Tests/ParadoxHealthTestTypes.h"
#include "Tests/ParadoxInventoryTestTypes.h"
#include "Tests/ParadoxOxygenCanisterTestTypes.h"
#include "TimerManager.h"
#include "Types/IntentReplayTypes.h"
#include "Types/WorldStateTypes.h"

struct FParadoxInventoryWidgetTestAccessor
{
	static const TArray<TObjectPtr<UParadoxInventoryActionButtonWidget>>& GetGeneratedActionButtons(
		const UParadoxInventoryWidget& Widget)
	{
		return Widget.GeneratedActionButtons;
	}

	static bool IsPresentationEnabled(
		const UParadoxInventoryActionButtonWidget& Widget)
	{
		return Widget.bPresentationEnabled;
	}

	static bool HasPendingActionAvailabilityRefresh(
		const UParadoxInventoryWidget& Widget)
	{
		return Widget.PendingActionAvailabilityRefreshTimer.IsValid();
	}
};

namespace UE::Paradox::OxygenCanister::Tests
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

	template <typename TActor>
	TActor* SpawnActor(UWorld& World, const FVector& Location = FVector::ZeroVector)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<TActor>(
			TActor::StaticClass(),
			FTransform(Location),
			Parameters);
	}

	struct FCanisterFixture
	{
		explicit FCanisterFixture(const TCHAR* WorldName)
			: Scope(WorldName)
		{
			if (!Scope.World)
			{
				return;
			}
			Character = SpawnActor<AParadoxHealthTestCharacter>(*Scope.World);
			Canister = SpawnActor<AParadoxOxygenCanisterTestActor>(
				*Scope.World,
				FVector(100.0, 0.0, 0.0));
			Spare = SpawnActor<AParadoxPickupableActor>(
				*Scope.World,
				FVector(300.0, 0.0, 0.0));
			Scope.StartPlay();
		}

		UParadoxInventoryComponent* Inventory() const
		{
			return Character ? Character->GetInventoryComponent() : nullptr;
		}

		UParadoxOxygenComponent* Oxygen() const
		{
			return Character ? Character->GetOxygenComponent() : nullptr;
		}

		UParadoxPickupableAction* UseAction() const
		{
			const TArray<UParadoxPickupableAction*> Actions =
				Canister ? Canister->GetPickupableActions() : TArray<UParadoxPickupableAction*>();
			return Actions.IsEmpty() ? nullptr : Actions[0];
		}

		FScopedTestWorld Scope;
		AParadoxHealthTestCharacter* Character = nullptr;
		AParadoxOxygenCanisterTestActor* Canister = nullptr;
		AParadoxPickupableActor* Spare = nullptr;
	};

	bool InitializeReplay(UIntentReplayComponent* Replay)
	{
		return Replay
			&& (Replay->IsIntentReplayInitialized()
				|| Replay->InitializeIntentReplay().Succeeded());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxOxygenCanisterAssetsAndDefaultsTest,
	"Paradox.OxygenCanister.AssetsAndDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxOxygenCanisterAssetsAndDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UParadoxUsePickupableActionDefinition* Definition =
		LoadObject<UParadoxUsePickupableActionDefinition>(
			nullptr,
			TEXT("/Game/Data/GameplayActions/DA_ParadoxUse.DA_ParadoxUse"));
	UParadoxPickupableAction* Descriptor = LoadObject<UParadoxPickupableAction>(
		nullptr,
		TEXT("/Game/Data/Inventory/DA_ParadoxUsePickupableAction.DA_ParadoxUsePickupableAction"));
	if (!TestNotNull(TEXT("Use Gameplay Action Definition loads"), Definition)
		|| !TestNotNull(TEXT("Use pickupable descriptor loads"), Descriptor))
	{
		return false;
	}

	TestTrue(TEXT("Definition uses the native Use action"),
		Definition->InstanceClass == UParadoxUsePickupableAction::StaticClass());
	TestTrue(TEXT("Definition exposes the semantic Use tag"),
		Definition->ActionTag == ParadoxGameplayTags::Action_Inventory_Use);
	TestTrue(TEXT("Definition holds the Inventory lock"),
		Definition->ExecutionLocks.HasTagExact(ParadoxGameplayTags::Lock_Inventory));
	TestEqual(TEXT("Definition rejects blocked requests"),
		Definition->BlockedPolicy,
		EGameplayActionBlockedPolicy::Reject);
	TestTrue(TEXT("Definition enables journaling"),
		Definition->JournalRequirement != EGameplayActionJournalRequirement::Disabled);
	TestEqual(TEXT("Use records only its replay-safe pickupable parameter"),
		Definition->GetDefaultParameters().GetNumPropertiesInBag(),
		1);
	const FPropertyBagPropertyDesc* PickupableParameter =
		Definition->GetDefaultParameters().FindPropertyDescByName(
			ParadoxPickupableActionParameters::Pickupable);
	TestTrue(TEXT("Use parameter is a soft pickupable reference"),
		PickupableParameter
			&& PickupableParameter->ValueType == EPropertyBagPropertyType::SoftObject
			&& PickupableParameter->ValueTypeObject == AParadoxPickupableActor::StaticClass());
	TestEqual(TEXT("Descriptor label is Use"), Descriptor->DisplayName.ToString(), FString(TEXT("Use")));
	TestTrue(TEXT("Descriptor points to the Use Definition"),
		Descriptor->GameplayActionDefinition.LoadSynchronous() == Definition);

	const AParadoxOxygenCanister* Defaults = GetDefault<AParadoxOxygenCanister>();
	TestEqual(TEXT("Canister restores 30 seconds by default"),
		Defaults->GetOxygenRestoreSeconds(),
		30.0f);
	const TArray<UParadoxPickupableAction*> Actions = Defaults->GetPickupableActions();
	TestEqual(TEXT("Canister catalog contains exactly the Use action"), Actions.Num(), 1);
	TestTrue(TEXT("Canister catalog uses the authored descriptor"),
		Actions.Num() == 1 && Actions[0] == Descriptor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxOxygenCanisterUseTest,
	"Paradox.OxygenCanister.UsePickupDropSwapConsume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxOxygenCanisterUseTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::OxygenCanister::Tests;
	(void)Parameters;
	FCanisterFixture Fixture(TEXT("ParadoxOxygenCanisterUseWorld"));
	UParadoxInventoryComponent* Inventory = Fixture.Inventory();
	UParadoxOxygenComponent* Oxygen = Fixture.Oxygen();
	UParadoxPickupableAction* UseAction = Fixture.UseAction();
	if (!TestNotNull(TEXT("Inventory exists"), Inventory)
		|| !TestNotNull(TEXT("Oxygen exists"), Oxygen)
		|| !TestNotNull(TEXT("Use action exists"), UseAction))
	{
		return false;
	}

	Oxygen->SetRemainingOxygenSeconds(60.0f);
	TestTrue(TEXT("Pickup succeeds"), Inventory->TryEquip(Fixture.Canister).IsSuccess());
	TestEqual(TEXT("Pickup alone does not restore Oxygen"),
		Oxygen->GetRemainingOxygenSeconds(),
		60.0f);
	TestTrue(TEXT("Drop remains available before Use"),
		Inventory->TryDropAtTransform(FTransform(FVector(150.0, 0.0, 0.0))).IsSuccess());
	TestTrue(TEXT("Canister can be picked up again"),
		Inventory->TryEquip(Fixture.Canister).IsSuccess());
	TestTrue(TEXT("Swap away from canister remains unchanged"),
		Inventory->TrySwap(Fixture.Spare).IsSuccess());
	TestTrue(TEXT("Swap back to canister remains unchanged"),
		Inventory->TrySwap(Fixture.Canister).IsSuccess());
	TestEqual(TEXT("Pickup, Drop and Swap still do not restore Oxygen"),
		Oxygen->GetRemainingOxygenSeconds(),
		60.0f);

	UParadoxOxygenCanisterReentrantObserver* Observer =
		NewObject<UParadoxOxygenCanisterReentrantObserver>(Fixture.Scope.World);
	Observer->Inventory = Inventory;
	Observer->Canister = Fixture.Canister;
	Inventory->OnEquippedItemChanged.AddDynamic(
		Observer,
		&UParadoxOxygenCanisterReentrantObserver::HandleEquippedItemChanged);
	const FGameplayActionSubmissionResult Submission =
		UseAction->RequestExecute(Fixture.Character);
	TestTrue(TEXT("Generic Use is accepted"), Submission.IsAccepted());
	TestEqual(TEXT("Use restores 60 to 90 seconds"),
		Oxygen->GetRemainingOxygenSeconds(),
		90.0f);
	TestFalse(TEXT("Consumption empties the inventory slot"), Inventory->HasItem());
	TestEqual(TEXT("Canister enters the terminal run state"),
		Fixture.Canister->GetPickupableState(),
		EParadoxPickupableState::Consumed);
	TestFalse(TEXT("Consumed canister is unavailable in the world"),
		Fixture.Canister->IsAvailableInWorld());
	TestTrue(TEXT("Consumed canister remains hidden"), Fixture.Canister->IsHidden());
	TestFalse(TEXT("Consumed canister has no collision"),
		Fixture.Canister->GetActorEnableCollision());
	TestEqual(TEXT("Successful Use commits once"), Fixture.Canister->CommittedCount, 1);
	TestEqual(TEXT("Consumption publishes one slot transition"),
		Observer->InventoryTransitionCount,
		1);
	UParadoxOxygenCanisterTestPassiveEffect* Effect =
		Fixture.Canister->GetTestPassiveEffect();
	TestEqual(TEXT("Passive applies once per equip path"), Effect->ApplyCount, 3);
	TestEqual(TEXT("Drop, Swap and consumption remove the passive exactly once each"),
		Effect->RemoveCount,
		3);

	const FGameplayActionSubmissionResult Duplicate =
		UseAction->RequestExecute(Fixture.Character);
	TestFalse(TEXT("Double Use is rejected after consumption"), Duplicate.IsAccepted());
	const FParadoxPickupableUseResult ConsumedEvaluation =
		Inventory->EvaluateEquippedItemUse(Fixture.Canister);
	TestTrue(TEXT("Direct consumed-item evaluation reports its distinct reason"),
		ConsumedEvaluation.ReasonTag
			== ParadoxGameplayTags::Result_Failure_Inventory_ItemAlreadyConsumed);
	TestEqual(TEXT("Double Use cannot restore Oxygen twice"),
		Oxygen->GetRemainingOxygenSeconds(),
		90.0f);

	FCanisterFixture ClampFixture(TEXT("ParadoxOxygenCanisterClampWorld"));
	if (!TestNotNull(TEXT("Clamp fixture Use action exists"), ClampFixture.UseAction()))
	{
		return false;
	}
	ClampFixture.Oxygen()->SetRemainingOxygenSeconds(170.0f);
	TestTrue(TEXT("Clamp fixture equips"),
		ClampFixture.Inventory()->TryEquip(ClampFixture.Canister).IsSuccess());
	TestTrue(TEXT("Near-full Use is accepted"),
		ClampFixture.UseAction()->RequestExecute(ClampFixture.Character).IsAccepted());
	TestEqual(TEXT("Oxygen component clamps 170 plus 30 to 180"),
		ClampFixture.Oxygen()->GetRemainingOxygenSeconds(),
		180.0f);
	TestEqual(TEXT("Clamped Use still consumes the canister"),
		ClampFixture.Canister->GetPickupableState(),
		EParadoxPickupableState::Consumed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxOxygenCanisterFailuresTest,
	"Paradox.OxygenCanister.FailuresAndReentrancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxOxygenCanisterFailuresTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::OxygenCanister::Tests;
	(void)Parameters;
	{
		FCanisterFixture Fixture(TEXT("ParadoxOxygenCanisterUnsupportedWorld"));
		Fixture.Oxygen()->SetRemainingOxygenSeconds(60.0f);
		TestTrue(TEXT("Unsupported case equips a normal pickupable"),
			Fixture.Inventory()->TryEquip(Fixture.Spare).IsSuccess());
		const FGameplayActionSubmissionResult Result =
			Fixture.UseAction()->EvaluateExecution(Fixture.Character);
		TestFalse(TEXT("Ordinary pickupables reject generic Use by default"), Result.IsAccepted());
		TestTrue(TEXT("Unsupported Use reports its specific reason"),
			Result.ReasonTag == ParadoxGameplayTags::Result_Failure_Inventory_UseUnsupported);
		TestTrue(TEXT("Unsupported Use preserves the item"), Fixture.Inventory()->HasItem());
	}
	{
		FCanisterFixture Fixture(TEXT("ParadoxOxygenCanisterFullWorld"));
		TestTrue(TEXT("Full case equips"), Fixture.Inventory()->TryEquip(Fixture.Canister).IsSuccess());
		const FGameplayActionSubmissionResult Result =
			Fixture.UseAction()->EvaluateExecution(Fixture.Character);
		TestFalse(TEXT("Full Oxygen disables Use"), Result.IsAccepted());
		TestTrue(TEXT("Full Oxygen reports its specific reason"),
			Result.ReasonTag == ParadoxGameplayTags::Result_Failure_Oxygen_AlreadyFull);
		TestTrue(TEXT("Full failure keeps the item equipped"),
			Fixture.Inventory()->GetEquippedItem() == Fixture.Canister);
	}
	{
		FCanisterFixture Fixture(TEXT("ParadoxOxygenCanisterInvalidAmountWorld"));
		Fixture.Oxygen()->SetRemainingOxygenSeconds(60.0f);
		Fixture.Canister->SetRestoreSecondsForTest(0.0f);
		TestTrue(TEXT("Invalid amount case equips"), Fixture.Inventory()->TryEquip(Fixture.Canister).IsSuccess());
		const FGameplayActionSubmissionResult Result =
			Fixture.UseAction()->EvaluateExecution(Fixture.Character);
		TestFalse(TEXT("Non-positive amount disables Use"), Result.IsAccepted());
		TestTrue(TEXT("Invalid amount reports its specific reason"),
			Result.ReasonTag == ParadoxGameplayTags::Result_Failure_Oxygen_InvalidRestoreAmount);
		TestTrue(TEXT("Invalid amount preserves the item"), Fixture.Inventory()->HasItem());
	}
	{
		FCanisterFixture Fixture(TEXT("ParadoxOxygenCanisterMissingOxygenWorld"));
		UParadoxOxygenComponent* Oxygen = Fixture.Oxygen();
		TestNotNull(TEXT("Missing-component fixture starts with Oxygen"), Oxygen);
		if (Oxygen)
		{
			Oxygen->DestroyComponent();
		}
		TestTrue(TEXT("Missing Oxygen case equips"), Fixture.Inventory()->TryEquip(Fixture.Canister).IsSuccess());
		const FGameplayActionSubmissionResult Result =
			Fixture.UseAction()->EvaluateExecution(Fixture.Character);
		TestFalse(TEXT("Missing Oxygen disables Use"), Result.IsAccepted());
		TestTrue(TEXT("Missing Oxygen reports its specific reason"),
			Result.ReasonTag == ParadoxGameplayTags::Result_Failure_Oxygen_MissingComponent);
		TestTrue(TEXT("Missing Oxygen preserves the item"), Fixture.Inventory()->HasItem());
	}
	{
		FCanisterFixture Fixture(TEXT("ParadoxOxygenCanisterDeadOwnerWorld"));
		Fixture.Oxygen()->SetRemainingOxygenSeconds(60.0f);
		TestTrue(TEXT("Dead owner case equips"), Fixture.Inventory()->TryEquip(Fixture.Canister).IsSuccess());
		Fixture.Character->GetHealthComponent()->Kill(
			nullptr,
			Fixture.Character,
			UDamageType::StaticClass());
		const FGameplayActionSubmissionResult Result =
			Fixture.UseAction()->EvaluateExecution(Fixture.Character);
		TestFalse(TEXT("Dead Character cannot Use"), Result.IsAccepted());
		TestTrue(TEXT("Dead Character reports owner not operational"),
			Result.ReasonTag == ParadoxGameplayTags::Result_Failure_Inventory_OwnerNotOperational);
		TestTrue(TEXT("Death failure preserves the item"), Fixture.Inventory()->HasItem());
	}
	{
		FCanisterFixture Fixture(TEXT("ParadoxOxygenCanisterReentrantWorld"));
		Fixture.Oxygen()->SetRemainingOxygenSeconds(60.0f);
		TestTrue(TEXT("Reentrant case equips"), Fixture.Inventory()->TryEquip(Fixture.Canister).IsSuccess());
		UParadoxOxygenCanisterReentrantObserver* Observer =
			NewObject<UParadoxOxygenCanisterReentrantObserver>(Fixture.Scope.World);
		Observer->Inventory = Fixture.Inventory();
		Observer->Canister = Fixture.Canister;
		Fixture.Oxygen()->OnOxygenChanged.AddDynamic(
			Observer,
			&UParadoxOxygenCanisterReentrantObserver::HandleOxygenChanged);
		Fixture.Inventory()->OnEquippedItemChanged.AddDynamic(
			Observer,
			&UParadoxOxygenCanisterReentrantObserver::HandleEquippedItemChanged);

		const FParadoxPickupableUseResult Result =
			Fixture.Inventory()->TryUseEquippedItem(Fixture.Canister);
		TestTrue(TEXT("Outer Use succeeds"), Result.IsSuccess());
		TestEqual(TEXT("Synchronous Oxygen callback fires once"), Observer->OxygenChangedCount, 1);
		TestEqual(TEXT("Nested Drop sees the inventory transaction guard"),
			Observer->ReentrantDropStatus,
			EParadoxInventoryOperationStatus::OperationInProgress);
		TestFalse(TEXT("Nested Use is rejected"), Observer->ReentrantUseResult.IsSuccess());
		TestTrue(TEXT("Nested Use reports an unavailable transaction"),
			Observer->ReentrantUseResult.ReasonTag
				== ParadoxGameplayTags::Result_Failure_Inventory_ItemUnavailable);
		TestEqual(TEXT("Reentrant callbacks still restore exactly once"),
			Fixture.Oxygen()->GetRemainingOxygenSeconds(),
			90.0f);
		TestEqual(TEXT("Reentrant callbacks still consume exactly once"),
			Observer->InventoryTransitionCount,
			1);
		TestEqual(TEXT("Reentrant callbacks remove passives exactly once"),
			Fixture.Canister->GetTestPassiveEffect()->RemoveCount,
			1);
		TestEqual(TEXT("Reentrant callbacks commit one successful Use"),
			Fixture.Canister->CommittedCount,
			1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxOxygenCanisterWidgetDiscoveryTest,
	"Paradox.OxygenCanister.InventoryWidgetDiscovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxOxygenCanisterWidgetDiscoveryTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::OxygenCanister::Tests;
	(void)Parameters;
	FCanisterFixture Fixture(TEXT("ParadoxOxygenCanisterWidgetWorld"));
	UParadoxInventoryTestWidget* Widget = Fixture.Scope.World
		? CreateWidget<UParadoxInventoryTestWidget>(
			Fixture.Scope.World,
			UParadoxInventoryTestWidget::StaticClass())
		: nullptr;
	if (!TestNotNull(TEXT("Inventory widget test object exists"), Widget))
	{
		return false;
	}
	const FMargin ExpectedButtonPadding(3.0f, 5.0f, 7.0f, 9.0f);
	Widget->PickupableActionButtonPadding = ExpectedButtonPadding;
	Widget->TakeWidget();
	Widget->SetInventoryCharacter(Fixture.Character);
	Fixture.Oxygen()->SetRemainingOxygenSeconds(60.0f);
	TestTrue(TEXT("Widget fixture equips canister"),
		Fixture.Inventory()->TryEquip(Fixture.Canister).IsSuccess());
	const TArray<UParadoxPickupableAction*> Actions = Widget->GetPickupableActions();
	TestEqual(TEXT("Widget discovers Use from the pickupable catalog"), Actions.Num(), 1);
	TestTrue(TEXT("Widget enables Use from ordinary action preflight"),
		Actions.Num() == 1 && Widget->CanExecutePickupableAction(Actions[0]));

	const TArray<TObjectPtr<UParadoxInventoryActionButtonWidget>>& GeneratedButtons =
		FParadoxInventoryWidgetTestAccessor::GetGeneratedActionButtons(*Widget);
	if (TestEqual(TEXT("Widget generates one Use action button"), GeneratedButtons.Num(), 1)
		&& TestNotNull(TEXT("Generated Use action button exists"), GeneratedButtons[0].Get()))
	{
		TestFalse(
			TEXT("Use button initially observes the active pickup transaction"),
			FParadoxInventoryWidgetTestAccessor::IsPresentationEnabled(*GeneratedButtons[0]));
		const UVerticalBoxSlot* ActionSlot = Cast<UVerticalBoxSlot>(GeneratedButtons[0]->Slot);
		TestNotNull(TEXT("Generated Use action owns a Vertical Box slot"), ActionSlot);
		if (ActionSlot)
		{
			TestTrue(
				TEXT("Generated Use action receives the configured padding"),
				ActionSlot->GetPadding() == ExpectedButtonPadding);
		}
	}

	TestTrue(
		TEXT("Pickup schedules a deferred action-availability refresh"),
		FParadoxInventoryWidgetTestAccessor::HasPendingActionAvailabilityRefresh(*Widget));
	++GFrameCounter;
	Fixture.Scope.World->GetTimerManager().Tick(0.0f);
	++GFrameCounter;
	Fixture.Scope.World->GetTimerManager().Tick(0.001f);
	const TArray<TObjectPtr<UParadoxInventoryActionButtonWidget>>& RefreshedButtons =
		FParadoxInventoryWidgetTestAccessor::GetGeneratedActionButtons(*Widget);
	if (TestEqual(TEXT("Deferred refresh preserves the action catalog"), RefreshedButtons.Num(), 1)
		&& TestNotNull(TEXT("Deferred Use action button exists"), RefreshedButtons[0].Get()))
	{
		TestTrue(
			TEXT("Use button enables after pickup without movement or another action"),
			FParadoxInventoryWidgetTestAccessor::IsPresentationEnabled(*RefreshedButtons[0]));
	}

	Fixture.Oxygen()->SetRemainingOxygenSeconds(180.0f);
	Widget->RefreshInventoryPresentation(false);
	TestFalse(TEXT("Widget disables Use at full Oxygen through the same preflight"),
		Actions.Num() == 1 && Widget->CanExecutePickupableAction(Actions[0]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxOxygenCanisterReplayAndResetTest,
	"Paradox.OxygenCanister.ReplayAndWorldStateReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxOxygenCanisterReplayAndResetTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::OxygenCanister::Tests;
	(void)Parameters;
	FScopedTestWorld Scope(TEXT("ParadoxOxygenCanisterReplayWorld"));
	if (!TestNotNull(TEXT("Replay test World exists"), Scope.World))
	{
		return false;
	}
	AParadoxPlayerCharacter* Player = SpawnActor<AParadoxPlayerCharacter>(*Scope.World);
	AParadoxCloneCharacter* Clone = SpawnActor<AParadoxCloneCharacter>(
		*Scope.World,
		FVector(500.0, 0.0, 0.0));
	AParadoxOxygenCanisterTestActor* Canister =
		SpawnActor<AParadoxOxygenCanisterTestActor>(
			*Scope.World,
			FVector(100.0, 0.0, 0.0));
	Scope.StartPlay();
	if (!TestNotNull(TEXT("Replay Player exists"), Player)
		|| !TestNotNull(TEXT("Replay Clone exists"), Clone)
		|| !TestNotNull(TEXT("Replay canister exists"), Canister))
	{
		return false;
	}
	UParadoxPickupableAction* UseAction = Canister->GetPickupableActions().IsEmpty()
		? nullptr
		: Canister->GetPickupableActions()[0];
	UWorldStateSubsystem* WorldState = Scope.World->GetSubsystem<UWorldStateSubsystem>();
	if (!TestNotNull(TEXT("Replay Use action exists"), UseAction)
		|| !TestNotNull(TEXT("World State subsystem exists"), WorldState))
	{
		return false;
	}
	TestTrue(TEXT("World State registration finalizes"),
		WorldState->FinalizeWorldStateRegistration().IsSuccess());
	FWorldStateCaptureRequest CaptureRequest;
	CaptureRequest.Label = TEXT("OxygenCanisterBaseline");
	TestTrue(TEXT("Canister world baseline captures"),
		WorldState->CaptureBaseline(CaptureRequest).IsSuccess());

	Player->GetOxygenComponent()->SetRemainingOxygenSeconds(60.0f);
	TestTrue(TEXT("Player equips canister before recording"),
		Player->GetInventoryComponent()->TryEquip(Canister).IsSuccess());
	UIntentReplayComponent* PlayerReplay = Player->GetIntentReplayComponent();
	if (!TestTrue(TEXT("Player replay initializes"), InitializeReplay(PlayerReplay)))
	{
		return false;
	}
	TestTrue(TEXT("Use recording starts"),
		PlayerReplay->StartRecording(FIntentRecordingOptions()).Succeeded());
	TestTrue(TEXT("Recorded Player Use is accepted"),
		UseAction->RequestExecute(Player).IsAccepted());
	TestTrue(TEXT("Use recording finalizes"),
		PlayerReplay->RequestStopRecording(
			EIntentRecordingFinalizeMode::Immediate).Succeeded());
	UIntentReplayTrack* Track = PlayerReplay->GetLastFinalizedTrack();
	if (!TestNotNull(TEXT("Use track exists"), Track))
	{
		return false;
	}
	TestEqual(TEXT("Track contains only the Use intent"), Track->GetEntryCount(), 1);
	FRecordedIntent RecordedUse;
	if (!TestTrue(TEXT("Recorded Use entry is readable"),
		Track->GetEntryByIndex(0, RecordedUse)))
	{
		return false;
	}
	TestTrue(TEXT("Recorded intent has the Use semantic tag"),
		RecordedUse.ActionTag == ParadoxGameplayTags::Action_Inventory_Use);
	TestEqual(TEXT("Recorded Use stores no Oxygen value or runtime slot state"),
		RecordedUse.GetParameters().GetNumPropertiesInBag(),
		1);
	const TValueOrError<FSoftObjectPath, EPropertyBagResult> RecordedPickupable =
		RecordedUse.GetParameters().GetValueSoftPath(
			ParadoxPickupableActionParameters::Pickupable);
	TestTrue(TEXT("Recorded Use stores a soft pickupable reference"),
		RecordedPickupable.HasValue());
	if (RecordedPickupable.HasValue())
	{
		TestEqual(TEXT("Recorded soft reference identifies the canister"),
			RecordedPickupable.GetValue(),
			FSoftObjectPath(Canister));
	}
	TestEqual(TEXT("Player source Use consumes the canister"),
		Canister->GetPickupableState(),
		EParadoxPickupableState::Consumed);

	const FWorldStateRestoreResult Restore =
		WorldState->RestoreBaseline(FWorldStateRestoreRequest());
	TestTrue(TEXT("Normal World State lifecycle restores the consumed canister"),
		Restore.IsSuccess());
	TestEqual(TEXT("Restored canister returns to World state"),
		Canister->GetPickupableState(),
		EParadoxPickupableState::World);
	TestTrue(TEXT("Restored canister is available again"),
		Canister->IsAvailableInWorld());
	TestFalse(TEXT("Restored canister is visible"), Canister->IsHidden());

	Clone->GetOxygenComponent()->SetRemainingOxygenSeconds(55.0f);
	TestTrue(TEXT("Clone equips the restored canister"),
		Clone->GetInventoryComponent()->TryEquip(Canister).IsSuccess());
	UIntentReplayComponent* CloneReplay = Clone->GetIntentReplayComponent();
	if (!TestTrue(TEXT("Clone replay initializes"), InitializeReplay(CloneReplay)))
	{
		return false;
	}
	FIntentReplayPlaybackOptions PlaybackOptions;
	const FIntentReplayPrepareResult Prepared =
		CloneReplay->PrepareReplay(Track, PlaybackOptions);
	TestEqual(TEXT("Use replay prepares on the Clone"),
		Prepared.Status,
		EIntentReplayPrepareStatus::Ready);
	TestTrue(TEXT("Clone starts Use replay"), CloneReplay->StartReplay().Succeeded());
	TestEqual(TEXT("Clone resolves Use against its live Oxygen state"),
		Clone->GetOxygenComponent()->GetRemainingOxygenSeconds(),
		85.0f);
	TestFalse(TEXT("Replay consumes the Clone's equipped canister"),
		Clone->GetInventoryComponent()->HasItem());
	TestEqual(TEXT("Successful Use replay completes"),
		CloneReplay->GetPlaybackState(),
		EIntentReplayPlaybackState::Completed);

	TestTrue(TEXT("Baseline can restore the replay-consumed canister again"),
		WorldState->RestoreBaseline(FWorldStateRestoreRequest()).IsSuccess());
	Clone->GetOxygenComponent()->SetRemainingOxygenSeconds(180.0f);
	TestTrue(TEXT("Clone equips canister for divergent replay"),
		Clone->GetInventoryComponent()->TryEquip(Canister).IsSuccess());
	const FIntentReplayPrepareResult DivergentPrepared =
		CloneReplay->PrepareReplay(Track, PlaybackOptions);
	TestEqual(TEXT("Divergent Use remains schema-compatible"),
		DivergentPrepared.Status,
		EIntentReplayPrepareStatus::Ready);
	AddExpectedError(
		TEXT("IntentReplayComponent failed replay session"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestTrue(TEXT("Divergent replay starts normally"),
		CloneReplay->StartReplay().Succeeded());
	TestEqual(TEXT("Full Oxygen causes normal replay submission failure"),
		CloneReplay->GetPlaybackState(),
		EIntentReplayPlaybackState::Failed);
	TestTrue(TEXT("Failed divergent Use keeps the item equipped"),
		Clone->GetInventoryComponent()->GetEquippedItem() == Canister);
	TestEqual(TEXT("Failed divergent Use does not change Oxygen"),
		Clone->GetOxygenComponent()->GetRemainingOxygenSeconds(),
		180.0f);
	return true;
}

#endif
