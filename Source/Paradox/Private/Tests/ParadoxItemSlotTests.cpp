#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/ArrowComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Controllers/PuzzleController.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayActionTags.h"
#include "Interaction/ParadoxInteractionActionDefinition.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Inventory/ParadoxInsertablePickupableActor.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxItemSlotInteractionActions.h"
#include "Inventory/ParadoxPuzzleItemSlotActor.h"
#include "Paradox.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Signals/PuzzleSignalTypes.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Tests/ParadoxInventoryTestTypes.h"
#include "Tests/ParadoxItemSlotTestTypes.h"
#include "UObject/UnrealType.h"

namespace UE::Paradox::ItemSlot::Tests
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

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	FGameplayTagQuery MakeBatteryQuery()
	{
		FGameplayTagQueryExpression Expression;
		Expression.AllTagsMatch()
			.AddTag(ParadoxGameplayTags::Item_Type_Battery)
			.AddTag(ParadoxGameplayTags::Item_Battery_Voltage_12V);
		return FGameplayTagQuery::BuildQuery(Expression, TEXT("12V battery"));
	}

	FGameplayTagContainer MakeBatteryTraits()
	{
		FGameplayTagContainer Traits;
		Traits.AddTag(ParadoxGameplayTags::Item_Type_Battery);
		Traits.AddTag(ParadoxGameplayTags::Item_Battery_Voltage_12V);
		return Traits;
	}

	FGameplayTagContainer MakeKeyTraits()
	{
		FGameplayTagContainer Traits;
		Traits.AddTag(ParadoxGameplayTags::Item_Type_Key);
		Traits.AddTag(ParadoxGameplayTags::Item_Access_Level_2);
		return Traits;
	}

	struct FFixture
	{
		explicit FFixture(const TCHAR* WorldName)
			: Scope(WorldName)
		{
			if (!Scope.World)
			{
				return;
			}
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = Scope.World->SpawnActor<AParadoxPlayerCharacter>(
				AParadoxPlayerCharacter::StaticClass(), FTransform::Identity, Spawn);
			OtherCharacter = Scope.World->SpawnActor<AParadoxPlayerCharacter>(
				AParadoxPlayerCharacter::StaticClass(),
				FTransform(FVector(0.0, 300.0, 0.0)), Spawn);
			Item = Scope.World->SpawnActor<AParadoxItemSlotTestInsertable>(
				AParadoxItemSlotTestInsertable::StaticClass(),
				FTransform(FVector(100.0, 0.0, 0.0)), Spawn);
			OtherItem = Scope.World->SpawnActor<AParadoxItemSlotTestInsertable>(
				AParadoxItemSlotTestInsertable::StaticClass(),
				FTransform(FVector(200.0, 0.0, 0.0)), Spawn);
			OrdinaryItem = Scope.World->SpawnActor<AParadoxInventoryTestPickupable>(
				AParadoxInventoryTestPickupable::StaticClass(),
				FTransform(FVector(300.0, 0.0, 0.0)), Spawn);
			Slot = Scope.World->SpawnActor<AParadoxItemSlotTestActor>(
				AParadoxItemSlotTestActor::StaticClass(),
				FTransform(FVector(500.0, 0.0, 0.0)), Spawn);
			PuzzleSlot = Scope.World->SpawnActor<AParadoxPuzzleItemSlotTestActor>(
				AParadoxPuzzleItemSlotTestActor::StaticClass(),
				FTransform(FVector(700.0, 0.0, 0.0)), Spawn);
			if (Item)
			{
				Item->SetTraits(MakeBatteryTraits());
			}
			if (OtherItem)
			{
				OtherItem->SetTraits(MakeBatteryTraits());
			}
			if (Slot)
			{
				Slot->SetAcceptedQuery(MakeBatteryQuery());
				Slot->GetInsertAnchor()->SetRelativeLocation(FVector(12.0, 23.0, 34.0));
				Slot->GetInsertAnchor()->SetRelativeRotation(FRotator(5.0, 45.0, 10.0));
			}
			Scope.World->InitializeActorsForPlay(FURL());
			Scope.World->BeginPlay();
			for (TActorIterator<AActor> It(Scope.World); It; ++It)
			{
				if (!It->HasActorBegunPlay())
				{
					It->DispatchBeginPlay();
				}
			}
		}

		UParadoxInventoryComponent* Inventory() const
		{
			return Character ? Character->GetInventoryComponent() : nullptr;
		}

		bool EquipAndInsert(AParadoxItemSlotActor* TargetSlot = nullptr)
		{
			AParadoxItemSlotActor* Destination = TargetSlot ? TargetSlot : Slot;
			return Inventory()
				&& Inventory()->TryEquip(Item).IsSuccess()
				&& Destination
				&& Destination->TryInsertItem(Character).IsSuccess();
		}

		FScopedTestWorld Scope;
		AParadoxPlayerCharacter* Character = nullptr;
		AParadoxPlayerCharacter* OtherCharacter = nullptr;
		AParadoxItemSlotTestInsertable* Item = nullptr;
		AParadoxItemSlotTestInsertable* OtherItem = nullptr;
		AParadoxInventoryTestPickupable* OrdinaryItem = nullptr;
		AParadoxItemSlotTestActor* Slot = nullptr;
		AParadoxPuzzleItemSlotTestActor* PuzzleSlot = nullptr;
	};

	const TCHAR* ScenarioNames[] = {
		TEXT("01.Insert.CompatibleAtomicOwnership"),
		TEXT("02.Insert.IncompatibleNoMutation"),
		TEXT("03.Insert.EmptyInventory"),
		TEXT("04.Insert.OccupiedSlot"),
		TEXT("05.Insert.InactiveSlot"),
		TEXT("06.Availability.RequesterRelative"),
		TEXT("07.Passives.RemovedExactlyOnce"),
		TEXT("08.Pickup.UnlockedIntoEmptyInventory"),
		TEXT("09.Pickup.LockedUnavailable"),
		TEXT("10.Release.LockIgnoredInternally"),
		TEXT("11.Pickup.OccupiedInventory"),
		TEXT("12.Regression.OrdinaryWorldPickup"),
		TEXT("13.Pickup.ReusesExistingActionFlow"),
		TEXT("14.Presentation.InsertAnchorAlignment"),
		TEXT("15.WorldState.EmptyBaseline"),
		TEXT("16.WorldState.OccupiedBaseline"),
		TEXT("17.Reset.ActionAbortContract"),
		TEXT("18.Perception.EventDrivenStates"),
		TEXT("19.Blueprint.ConfigurableSurface"),
		TEXT("20.Puzzle.ActiveOccupiedOutput"),
		TEXT("21.Puzzle.InactiveOccupiedOutput"),
		TEXT("22.Puzzle.ReceiverDrivenActivation"),
		TEXT("23.Puzzle.InputOutputComposition"),
		TEXT("24.Puzzle.ControllerLocalGatesRemainLocal"),
		TEXT("25.Extension.SafeCompatibilityAndActivityHooks"),
		TEXT("26.Replay.CloneRequesterRelative"),
		TEXT("27.Lifetime.InsertedItemDestruction"),
		TEXT("28.Concurrency.ReentrantInsertRejected"),
		TEXT("29.Integration.NativeAssetsNoTick")
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FParadoxItemSlotScenariosTest,
	"Paradox.ItemSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FParadoxItemSlotScenariosTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	for (int32 Index = 0;
		Index < UE_ARRAY_COUNT(UE::Paradox::ItemSlot::Tests::ScenarioNames);
		++Index)
	{
		OutBeautifiedNames.Add(UE::Paradox::ItemSlot::Tests::ScenarioNames[Index]);
		OutTestCommands.Add(FString::FromInt(Index));
	}
}

bool FParadoxItemSlotScenariosTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::ItemSlot::Tests;
	const int32 Scenario = FCString::Atoi(*Parameters);
	FFixture Fixture(*FString::Printf(TEXT("ParadoxItemSlotScenario%d"), Scenario + 1));
	if (!TestNotNull(TEXT("Transient Item Slot world exists"), Fixture.Scope.World)
		|| !TestNotNull(TEXT("Requester exists"), Fixture.Character)
		|| !TestNotNull(TEXT("Requester inventory exists"), Fixture.Inventory())
		|| !TestNotNull(TEXT("Insertable exists"), Fixture.Item)
		|| !TestNotNull(TEXT("Item Slot exists"), Fixture.Slot)
		|| !TestNotNull(TEXT("Puzzle Item Slot exists"), Fixture.PuzzleSlot))
	{
		return false;
	}

	switch (Scenario)
	{
	case 0:
	{
		TestTrue(TEXT("Compatible insert succeeds"), Fixture.EquipAndInsert());
		TestFalse(TEXT("Inventory becomes empty"), Fixture.Inventory()->HasItem());
		TestTrue(TEXT("Slot owns item"), Fixture.Slot->GetInsertedItem() == Fixture.Item);
		TestTrue(TEXT("Item points back to Slot"), Fixture.Item->GetCurrentItemSlot() == Fixture.Slot);
		TestNull(TEXT("Inserted item has no Character holder"), Fixture.Item->GetCurrentHolder());
		TestTrue(TEXT("Inserted state is authoritative"), Fixture.Item->IsInserted());
		break;
	}
	case 1:
	{
		Fixture.Item->SetTraits(MakeKeyTraits());
		TestTrue(TEXT("Pickup succeeds before compatibility validation"), Fixture.Inventory()->TryEquip(Fixture.Item).IsSuccess());
		const FParadoxItemSlotOperationResult Result = Fixture.Slot->TryInsertItem(Fixture.Character);
		TestTrue(TEXT("Tag query rejection is explicit"), Result.Status == EParadoxItemSlotOperationStatus::IncompatibleTraits);
		TestTrue(TEXT("Rejected item remains equipped"), Fixture.Inventory()->GetEquippedItem() == Fixture.Item);
		TestFalse(TEXT("Rejected Slot remains empty"), Fixture.Slot->IsOccupied());
		break;
	}
	case 2:
	{
		const FParadoxItemSlotOperationResult Result = Fixture.Slot->TryInsertItem(Fixture.Character);
		TestTrue(TEXT("Empty inventory fails safely"), Result.Status == EParadoxItemSlotOperationStatus::InvalidItem);
		TestFalse(TEXT("Slot remains empty"), Fixture.Slot->IsOccupied());
		break;
	}
	case 3:
	{
		TestTrue(TEXT("First insert succeeds"), Fixture.EquipAndInsert());
		TestTrue(TEXT("Second item equips"), Fixture.Inventory()->TryEquip(Fixture.OtherItem).IsSuccess());
		const FParadoxItemSlotOperationResult Result = Fixture.Slot->TryInsertItem(Fixture.Character);
		TestTrue(TEXT("Occupied Slot rejects Insert"), Result.Status == EParadoxItemSlotOperationStatus::SlotOccupied);
		TestTrue(TEXT("Original item remains in Slot"), Fixture.Slot->GetInsertedItem() == Fixture.Item);
		break;
	}
	case 4:
	{
		Fixture.Slot->SetRequiredActive(false);
		Fixture.Slot->NotifySlotActiveStateMayHaveChanged();
		TestTrue(TEXT("Item equips"), Fixture.Inventory()->TryEquip(Fixture.Item).IsSuccess());
		TestTrue(TEXT("Inactive Slot rejects Insert"), Fixture.Slot->TryInsertItem(Fixture.Character).Status == EParadoxItemSlotOperationStatus::SlotInactive);
		TestTrue(TEXT("Item remains held"), Fixture.Inventory()->GetEquippedItem() == Fixture.Item);
		break;
	}
	case 5:
	{
		Fixture.OtherItem->SetTraits(MakeKeyTraits());
		TestTrue(TEXT("First requester equips compatible item"), Fixture.Inventory()->TryEquip(Fixture.Item).IsSuccess());
		TestTrue(TEXT("Second requester equips incompatible item"), Fixture.OtherCharacter->GetInventoryComponent()->TryEquip(Fixture.OtherItem).IsSuccess());
		TestTrue(TEXT("Compatible requester sees Insert"), Fixture.Slot->CanAcceptItem(Fixture.Item, Fixture.Character));
		TestFalse(TEXT("Incompatible requester does not see Insert"), Fixture.Slot->CanAcceptItem(Fixture.OtherItem, Fixture.OtherCharacter));
		break;
	}
	case 6:
	{
		UParadoxItemSlotTestPassiveEffect* Effect = Fixture.Item->GetTestEffect();
		TestTrue(TEXT("Insert succeeds"), Fixture.EquipAndInsert());
		TestEqual(TEXT("Passive applied once while held"), Effect->ApplyCount, 1);
		TestEqual(TEXT("Passive removed once on Insert"), Effect->RemoveCount, 1);
		break;
	}
	case 7:
	{
		UParadoxItemSlotTestPassiveEffect* Effect = Fixture.Item->GetTestEffect();
		TestTrue(TEXT("Insert succeeds"), Fixture.EquipAndInsert());
		TestTrue(TEXT("Unlocked pickup succeeds"), Fixture.Slot->TryPickupInsertedItem(Fixture.Character).IsSuccess());
		TestTrue(TEXT("Inventory owns returned item"), Fixture.Inventory()->GetEquippedItem() == Fixture.Item);
		TestFalse(TEXT("Slot becomes empty"), Fixture.Slot->IsOccupied());
		TestEqual(TEXT("Passive reapplies exactly once"), Effect->ApplyCount, 2);
		break;
	}
	case 8:
	{
		TestTrue(TEXT("Insert succeeds"), Fixture.EquipAndInsert());
		Fixture.Slot->SetLocked(true);
		const FParadoxItemSlotOperationResult Result = Fixture.Slot->EvaluatePickupInsertedItem(Fixture.Character);
		TestTrue(TEXT("Locked item is unavailable"), Result.Status == EParadoxItemSlotOperationStatus::ItemLocked);
		TestTrue(TEXT("Locked item remains inserted"), Fixture.Item->IsInserted());
		break;
	}
	case 9:
	{
		TestTrue(TEXT("Insert succeeds"), Fixture.EquipAndInsert());
		Fixture.Slot->SetLocked(true);
		const FTransform ReleaseTransform(FVector(900.0, 100.0, 20.0));
		TestTrue(TEXT("Internal release ignores user lock"), Fixture.Slot->ReleaseForTest(ReleaseTransform).IsSuccess());
		TestTrue(TEXT("Released item returns to world"), Fixture.Item->IsAvailableInWorld());
		break;
	}
	case 10:
	{
		TestTrue(TEXT("Insert succeeds"), Fixture.EquipAndInsert());
		TestTrue(TEXT("Requester equips another item"), Fixture.Inventory()->TryEquip(Fixture.OtherItem).IsSuccess());
		TestTrue(TEXT("Pickup rejects occupied inventory"), Fixture.Slot->TryPickupInsertedItem(Fixture.Character).Status == EParadoxItemSlotOperationStatus::InventoryOccupied);
		TestTrue(TEXT("Inserted ownership remains unchanged"), Fixture.Slot->GetInsertedItem() == Fixture.Item);
		break;
	}
	case 11:
	{
		TestTrue(TEXT("Ordinary pickup remains unchanged"), Fixture.Inventory()->TryEquip(Fixture.OrdinaryItem).IsSuccess());
		TestTrue(TEXT("Ordinary item has Character ownership"), Fixture.OrdinaryItem->GetCurrentHolder() == Fixture.Character);
		break;
	}
	case 12:
	{
		TestTrue(TEXT("Slot pickup derives from existing Pickup Action"), UParadoxPickupFromItemSlotInteractionAction::StaticClass()->IsChildOf(UParadoxPickupInteractionAction::StaticClass()));
		TestTrue(TEXT("Insert uses the standard Interaction Action base"), UParadoxInsertItemInteractionAction::StaticClass()->IsChildOf(UParadoxInteractionActionBase::StaticClass()));
		break;
	}
	case 13:
	{
		TestTrue(TEXT("Insert succeeds"), Fixture.EquipAndInsert());
		TestTrue(TEXT("Item is attached to Arrow anchor"), Fixture.Item->GetRootComponent()->GetAttachParent() == Fixture.Slot->GetInsertAnchor());
		TestTrue(TEXT("Item matches anchor transform"), Fixture.Item->GetActorTransform().Equals(Fixture.Slot->GetInsertAnchor()->GetComponentTransform()));
		break;
	}
	case 14:
	{
		UWorldStateSubsystem* WorldState = Fixture.Scope.World->GetSubsystem<UWorldStateSubsystem>();
		TestTrue(TEXT("WorldState registration finalizes"), WorldState && WorldState->FinalizeWorldStateRegistration().IsSuccess());
		FWorldStateCaptureRequest Capture;
		Capture.Label = TEXT("EmptyItemSlotBaseline");
		TestTrue(TEXT("Empty baseline captures"), WorldState && WorldState->CaptureBaseline(Capture).IsSuccess());
		TestTrue(TEXT("Post-baseline insert succeeds"), Fixture.EquipAndInsert());
		const FWorldStateRestoreResult Restore = WorldState->RestoreBaseline(FWorldStateRestoreRequest());
		TestTrue(TEXT("Empty baseline restores"), Restore.IsSuccess());
		TestFalse(TEXT("Slot returns empty"), Fixture.Slot->IsOccupied());
		TestTrue(TEXT("Item returns to world ownership"), Fixture.Item->IsAvailableInWorld());
		break;
	}
	case 15:
	{
		TestTrue(TEXT("Occupied baseline setup succeeds"), Fixture.EquipAndInsert());
		UWorldStateSubsystem* WorldState = Fixture.Scope.World->GetSubsystem<UWorldStateSubsystem>();
		TestTrue(TEXT("WorldState registration finalizes"), WorldState && WorldState->FinalizeWorldStateRegistration().IsSuccess());
		FWorldStateCaptureRequest Capture;
		Capture.Label = TEXT("OccupiedItemSlotBaseline");
		TestTrue(TEXT("Occupied baseline captures"), WorldState && WorldState->CaptureBaseline(Capture).IsSuccess());
		TestTrue(TEXT("Item can leave after capture"), Fixture.Slot->TryPickupInsertedItem(Fixture.Character).IsSuccess());
		const FWorldStateRestoreResult Restore = WorldState->RestoreBaseline(FWorldStateRestoreRequest());
		TestTrue(TEXT("Occupied baseline restores"), Restore.IsSuccess());
		TestTrue(TEXT("Slot restores item"), Fixture.Slot->GetInsertedItem() == Fixture.Item);
		TestTrue(TEXT("Item restores backlink"), Fixture.Item->GetCurrentItemSlot() == Fixture.Slot && Fixture.Item->IsInserted());
		break;
	}
	case 16:
	{
		const UParadoxInsertItemInteractionActionDefinition* InsertDefinition = GetDefault<UParadoxInsertItemInteractionActionDefinition>();
		const UParadoxPickupFromItemSlotInteractionActionDefinition* PickupDefinition = GetDefault<UParadoxPickupFromItemSlotInteractionActionDefinition>();
		TestTrue(TEXT("Insert movement is interruptible by reset"), InsertDefinition->bInterruptible);
		TestTrue(TEXT("Slot pickup movement is interruptible by reset"), PickupDefinition->bInterruptible);
		TestTrue(TEXT("Both reuse standard cleanup lifecycle"), InsertDefinition->InstanceClass->IsChildOf(UParadoxInteractionActionBase::StaticClass()) && PickupDefinition->InstanceClass->IsChildOf(UParadoxInteractionActionBase::StaticClass()));
		break;
	}
	case 17:
	{
		TestEqual(TEXT("Slot publishes four semantic states"), Fixture.Slot->GetPerceptionSourceComponent()->GetExposedStateCount(), 4);
		TestTrue(TEXT("Insert succeeds"), Fixture.EquipAndInsert());
		TestEqual(TEXT("Event-driven update replaces values without adding state spam"), Fixture.Slot->GetPerceptionSourceComponent()->GetExposedStateCount(), 4);
		break;
	}
	case 18:
	{
		TestNotNull(TEXT("InsertableTraits is reflected"), AParadoxInsertablePickupableActor::StaticClass()->FindPropertyByName(TEXT("InsertableTraits")));
		TestNotNull(TEXT("AcceptedItemQuery is reflected"), AParadoxItemSlotActor::StaticClass()->FindPropertyByName(TEXT("AcceptedItemQuery")));
		TestNotNull(TEXT("Lock policy is reflected"), AParadoxItemSlotActor::StaticClass()->FindPropertyByName(TEXT("bLockInsertedItem")));
		TestTrue(TEXT("InsertAnchor is an Arrow Component"), Fixture.Slot->GetInsertAnchor()->IsA<UArrowComponent>());
		break;
	}
	case 19:
	{
		TestTrue(TEXT("Active Puzzle Slot accepts item"), Fixture.EquipAndInsert(Fixture.PuzzleSlot));
		FPuzzleSignalState State;
		TestTrue(TEXT("Puzzle output exists"), Fixture.PuzzleSlot->GetPuzzleEmitterComponent()->TryGetSignalState(ParadoxGameplayTags::Puzzle_Signal_ItemSlotSatisfied, State));
		TestTrue(TEXT("Active occupied output is true"), State.bIsValid && State.bIsActive);
		break;
	}
	case 20:
	{
		TestTrue(TEXT("Puzzle Slot accepts item"), Fixture.EquipAndInsert(Fixture.PuzzleSlot));
		Fixture.PuzzleSlot->SetAdditionalActive(false);
		Fixture.PuzzleSlot->NotifySlotActiveStateMayHaveChanged();
		FPuzzleSignalState State;
		Fixture.PuzzleSlot->GetPuzzleEmitterComponent()->TryGetSignalState(ParadoxGameplayTags::Puzzle_Signal_ItemSlotSatisfied, State);
		TestFalse(TEXT("Inactive occupied output is false"), State.bIsActive);
		break;
	}
	case 21:
	{
		Fixture.PuzzleSlot->SetRequireReceiver(true);
		Fixture.PuzzleSlot->NotifySlotActiveStateMayHaveChanged();
		TestFalse(TEXT("Required unpowered Receiver closes Slot"), Fixture.PuzzleSlot->IsSlotActive());
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APuzzleController* Controller = Fixture.Scope.World->SpawnActor<APuzzleController>(APuzzleController::StaticClass(), FTransform::Identity, Spawn);
		TestTrue(TEXT("Controller request changes Receiver"), Fixture.PuzzleSlot->GetPuzzleReceiverComponent()->SetControllerRequest(Controller, true));
		TestTrue(TEXT("Receiver delegate opens Slot without Tick"), Fixture.PuzzleSlot->IsSlotActive());
		break;
	}
	case 22:
	{
		Fixture.PuzzleSlot->SetRequireReceiver(true);
		Fixture.PuzzleSlot->NotifySlotActiveStateMayHaveChanged();
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APuzzleController* InputController = Fixture.Scope.World->SpawnActor<APuzzleController>(APuzzleController::StaticClass(), FTransform::Identity, Spawn);
		Fixture.PuzzleSlot->GetPuzzleReceiverComponent()->SetControllerRequest(InputController, true);
		TestTrue(TEXT("Powered Slot accepts item"), Fixture.EquipAndInsert(Fixture.PuzzleSlot));
		FPuzzleSignalState State;
		TestTrue(TEXT("Separate output emitter publishes result"), Fixture.PuzzleSlot->GetPuzzleEmitterComponent()->TryGetSignalState(ParadoxGameplayTags::Puzzle_Signal_ItemSlotSatisfied, State) && State.bIsActive);
		TestTrue(TEXT("Actor composes distinct Receiver and Emitter endpoints"), Fixture.PuzzleSlot->GetPuzzleReceiverComponent() && Fixture.PuzzleSlot->GetPuzzleEmitterComponent());
		break;
	}
	case 23:
	{
		TestNotNull(TEXT("Controller input bindings own local gate data"), FPuzzleInputBinding::StaticStruct()->FindPropertyByName(TEXT("EmitterGates")));
		TestNull(TEXT("Emitter does not own Controller-local gates"), UPuzzleEmitterComponent::StaticClass()->FindPropertyByName(TEXT("EmitterGates")));
		break;
	}
	case 24:
	{
		Fixture.Slot->SetAdditionalAcceptance(false);
		TestTrue(TEXT("Item equips"), Fixture.Inventory()->TryEquip(Fixture.Item).IsSuccess());
		TestTrue(TEXT("Additional compatibility hook is diagnostic"), Fixture.Slot->EvaluateAcceptItem(Fixture.Item, Fixture.Character).Status == EParadoxItemSlotOperationStatus::AdditionalValidationFailed);
		Fixture.Slot->SetAdditionalAcceptance(true);
		Fixture.Slot->SetAdditionalActive(false);
		Fixture.Slot->NotifySlotActiveStateMayHaveChanged();
		TestFalse(TEXT("Additional activity hook cannot bypass public query"), Fixture.Slot->IsSlotActive());
		break;
	}
	case 25:
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AParadoxCloneCharacter* Clone = Fixture.Scope.World->SpawnActor<AParadoxCloneCharacter>(
			AParadoxCloneCharacter::StaticClass(), FTransform(FVector(0.0, 600.0, 0.0)), Spawn);
		TestNotNull(TEXT("Clone requester exists"), Clone);
		TestTrue(TEXT("Clone owns the same inventory capability"), Clone && Clone->GetInventoryComponent());
		TestTrue(TEXT("Clone can equip current-world insertable"), Clone && Clone->GetInventoryComponent()->TryEquip(Fixture.Item).IsSuccess());
		TestTrue(TEXT("Clone Insert is requester-relative"), Clone && Fixture.Slot->TryInsertItem(Clone).IsSuccess());
		break;
	}
	case 26:
	{
		TestTrue(TEXT("Insert succeeds"), Fixture.EquipAndInsert());
		Fixture.Item->Destroy();
		TestFalse(TEXT("Destroyed inserted item clears Slot"), Fixture.Slot->IsOccupied());
		break;
	}
	case 27:
	{
		UParadoxItemSlotTestPassiveEffect* Effect = Fixture.Item->GetTestEffect();
		Effect->bAttemptReentrantInsert = true;
		Effect->ReentrantSlot = Fixture.Slot;
		TestTrue(TEXT("Outer Insert succeeds"), Fixture.EquipAndInsert());
		TestTrue(TEXT("Nested Insert is rejected"), Effect->ReentrantStatus == EParadoxItemSlotOperationStatus::OperationInProgress);
		TestTrue(TEXT("Reentrancy preserves Slot-only ownership"), Fixture.Item->IsInserted() && !Fixture.Inventory()->HasItem());
		break;
	}
	case 28:
	{
		const AParadoxItemSlotActor* Defaults = GetDefault<AParadoxItemSlotActor>();
		TestNotNull(TEXT("Native Slot owns selection"), Defaults->GetSelectableComponent());
		TestNotNull(TEXT("Native Slot owns interaction"), Defaults->GetInteractionComponent());
		TestNotNull(TEXT("Native Slot owns Smart Object"), Defaults->GetSmartObjectComponent());
		TestNotNull(TEXT("Native Slot owns WorldState"), Defaults->GetWorldStateParticipantComponent());
		TestNotNull(TEXT("Native Slot owns Perception"), Defaults->GetPerceptionSourceComponent());
		TestFalse(TEXT("Native Slot has no Tick"), Defaults->PrimaryActorTick.bCanEverTick);
		const USmartObjectDefinition* SmartDefinition = Defaults->GetSmartObjectComponent()->GetDefinition();
		TestNotNull(TEXT("Slot Smart Object asset resolves"), SmartDefinition);
		TestEqual(TEXT("Slot Smart Object has four approach positions"), SmartDefinition ? SmartDefinition->GetSlots().Num() : 0, 4);
		TestEqual(TEXT("Native Slot catalog includes Insert and Pickup"), Defaults->GetInteractionComponent()->InteractionDefinitions.Num(), 2);
		const UParadoxInsertItemInteractionActionDefinition* InsertAsset = LoadObject<UParadoxInsertItemInteractionActionDefinition>(nullptr, TEXT("/Game/Data/GameplayActions/DA_ParadoxInsertItem.DA_ParadoxInsertItem"));
		const UParadoxPickupFromItemSlotInteractionActionDefinition* PickupAsset = LoadObject<UParadoxPickupFromItemSlotInteractionActionDefinition>(nullptr, TEXT("/Game/Data/GameplayActions/DA_ParadoxPickupFromItemSlot.DA_ParadoxPickupFromItemSlot"));
		TestNotNull(TEXT("Insert Definition asset resolves"), InsertAsset);
		TestNotNull(TEXT("Slot Pickup Definition asset resolves"), PickupAsset);
		TestTrue(TEXT("Insert Definition owns inventory lock"), InsertAsset && InsertAsset->ExecutionLocks.HasTagExact(ParadoxGameplayTags::Lock_Inventory));
		TestTrue(TEXT("Pickup Definition owns inventory lock"), PickupAsset && PickupAsset->ExecutionLocks.HasTagExact(ParadoxGameplayTags::Lock_Inventory));
		break;
	}
	default:
		AddError(FString::Printf(TEXT("Unknown Item Slot scenario '%s'."), *Parameters));
		return false;
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
