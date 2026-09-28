#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actions/GameplayActionDefinition.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/ArrowComponent.h"
#include "Components/GridNavigationModifierComponent.h"
#include "Components/StaticMeshComponent.h"
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
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxItemSlotInteractionActions.h"
#include "Paradox.h"
#include "Puzzles/ParadoxDumbwaiterInteractionAction.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "SmartObjectComponent.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Tests/ParadoxDumbwaiterTestTypes.h"
#include "Types/WorldStateTypes.h"

struct FParadoxDumbwaiterTestAccessor
{
	static void SetInitiallyStoredPickupable(
		AParadoxDumbwaiter& Dumbwaiter,
		AParadoxInsertablePickupableActor* Cargo)
	{
		Dumbwaiter.StoredPickupable = Cargo;
	}
};

namespace UE::Paradox::Dumbwaiter::Tests
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TestCargoBattery, "Interaction.Test.Dumbwaiter.Cargo.Battery");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TestCargoVoltage12V, "Interaction.Test.Dumbwaiter.Cargo.Voltage12V");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TestCargoKey, "Interaction.Test.Dumbwaiter.Cargo.Key");

	struct FScopedWorld
	{
		explicit FScopedWorld(const TCHAR* Name)
		{
			Context = GEngine ? &GEngine->CreateNewWorldContext(EWorldType::Game) : nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			if (World)
			{
				World->AddToRoot();
				World->SetShouldTick(true);
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		~FScopedWorld()
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

		void StartPlay()
		{
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (!It->HasActorBegunPlay())
				{
					It->DispatchBeginPlay();
				}
			}
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	template <typename T>
	T* Spawn(UWorld& World, const FName Name, const FVector Location = FVector::ZeroVector)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = Name;
		SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<T>(T::StaticClass(), FTransform(Location), SpawnParameters);
	}

	FGameplayTagQuery MakeBatteryQuery()
	{
		FGameplayTagQueryExpression Expression;
		Expression.AllTagsMatch().AddTag(TestCargoBattery).AddTag(TestCargoVoltage12V);
		return FGameplayTagQuery::BuildQuery(Expression, TEXT("Dumbwaiter 12V battery"));
	}

	FGameplayTagContainer MakeBatteryTraits()
	{
		FGameplayTagContainer Traits;
		Traits.AddTag(TestCargoBattery);
		Traits.AddTag(TestCargoVoltage12V);
		return Traits;
	}

	struct FFixture
	{
		explicit FFixture(const TCHAR* WorldName, const bool bStartPlay = true)
			: Scope(WorldName)
		{
			if (!Scope.World)
			{
				return;
			}
			Source = Spawn<AParadoxDumbwaiterTestActor>(*Scope.World, TEXT("SourceDumbwaiter"));
			Destination = Spawn<AParadoxDumbwaiterTestActor>(
				*Scope.World, TEXT("DestinationDumbwaiter"), FVector(600.0f, 0.0f, 0.0f));
			Character = Spawn<AParadoxPlayerCharacter>(*Scope.World, TEXT("DumbwaiterRequester"));
			Cargo = Spawn<AParadoxDumbwaiterTestCargo>(
				*Scope.World, TEXT("DumbwaiterCargo"), FVector(100.0f, 0.0f, 0.0f));
			OtherCargo = Spawn<AParadoxDumbwaiterTestCargo>(
				*Scope.World, TEXT("OtherDumbwaiterCargo"), FVector(200.0f, 0.0f, 0.0f));
			Controller = NewObject<APuzzleController>(
				GetTransientPackage(),
				APuzzleController::StaticClass(),
				MakeUniqueObjectName(GetTransientPackage(), APuzzleController::StaticClass(), TEXT("DumbwaiterController")));
			if (Source && Destination)
			{
				Source->LinkedEndpoint = Destination;
				Destination->LinkedEndpoint = Source;
				Source->TransferOutDuration = 60.0f;
				Destination->TransferInDuration = 60.0f;
				Source->SetAcceptedQuery(MakeBatteryQuery());
				Destination->SetAcceptedQuery(MakeBatteryQuery());
				Destination->TransferAnchor->SetRelativeLocation(FVector(20.0f, 30.0f, 40.0f));
			}
			if (Cargo)
			{
				Cargo->SetTraits(MakeBatteryTraits());
			}
			if (OtherCargo)
			{
				OtherCargo->SetTraits(MakeBatteryTraits());
			}
			if (bStartPlay)
			{
				StartPlayAndActivate();
			}
		}

		void StartPlayAndActivate(
			const bool bActivateSource = true,
			const bool bActivateDestination = true)
		{
			Scope.StartPlay();
			Source->PuzzleReceiver->SetControllerRequest(Controller, bActivateSource);
			Destination->PuzzleReceiver->SetControllerRequest(Controller, bActivateDestination);
		}

		UParadoxInventoryComponent* Inventory() const
		{
			return Character ? Character->GetInventoryComponent() : nullptr;
		}

		bool EquipAndInsert(
			AParadoxDumbwaiterTestActor* Dumbwaiter = nullptr,
			AParadoxDumbwaiterTestCargo* Item = nullptr)
		{
			AParadoxDumbwaiterTestActor* Target = Dumbwaiter ? Dumbwaiter : Source;
			AParadoxDumbwaiterTestCargo* TargetCargo = Item ? Item : Cargo;
			return Inventory()
				&& Inventory()->TryEquip(TargetCargo).IsSuccess()
				&& Target
				&& Target->TryInsertCargo(Character).IsSuccess();
		}

		bool IsValid() const
		{
			return Scope.World && Source && Destination && Character && Cargo
				&& OtherCargo && Controller && Inventory();
		}

		FScopedWorld Scope;
		AParadoxDumbwaiterTestActor* Source = nullptr;
		AParadoxDumbwaiterTestActor* Destination = nullptr;
		AParadoxPlayerCharacter* Character = nullptr;
		AParadoxDumbwaiterTestCargo* Cargo = nullptr;
		AParadoxDumbwaiterTestCargo* OtherCargo = nullptr;
		APuzzleController* Controller = nullptr;
	};

	const TCHAR* ScenarioNames[] = {
		TEXT("01.Architecture.ComponentsAndNoTick"),
		TEXT("02.Insert.CompatibleAtomicOwnership"),
		TEXT("03.Insert.IncompatibleRejected"),
		TEXT("04.Insert.OccupiedRejected"),
		TEXT("05.Insert.BusyRejected"),
		TEXT("06.Send.EmptyRejected"),
		TEXT("07.PairCapacity.LinkedInsertRejectedAndRemovalReenables"),
		TEXT("08.Send.InactiveEndpointRejected"),
		TEXT("09.Send.AtomicPairLockAndBusyAvailability"),
		TEXT("10.Send.CommitMovesOwnershipExactlyOnce"),
		TEXT("11.Send.CompleteAndReverse"),
		TEXT("12.Pickup.UnlockedAndLockedPolicy"),
		TEXT("13.Interaction.SemanticDefinitionsAndAsset"),
		TEXT("14.WorldState.RestoresPendingAndCommittedTransfer"),
		TEXT("15.AuthoredCargoAndNoEmitter"),
		TEXT("16.Send.SuspendsAndRestoresCargoPresence")
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FParadoxDumbwaiterScenariosTest,
	"Paradox.Dumbwaiter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FParadoxDumbwaiterScenariosTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	for (int32 Index = 0;
		Index < UE_ARRAY_COUNT(UE::Paradox::Dumbwaiter::Tests::ScenarioNames);
		++Index)
	{
		OutBeautifiedNames.Add(UE::Paradox::Dumbwaiter::Tests::ScenarioNames[Index]);
		OutTestCommands.Add(FString::FromInt(Index));
	}
}

bool FParadoxDumbwaiterScenariosTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Dumbwaiter::Tests;
	const int32 Scenario = FCString::Atoi(*Parameters);
	const bool bDeferPlay = Scenario == 7 || Scenario == 14 || Scenario == 15;
	FFixture Fixture(
		*FString::Printf(TEXT("ParadoxDumbwaiterScenario%d"), Scenario + 1),
		!bDeferPlay);
	if (!TestTrue(TEXT("Dumbwaiter fixture is valid"), Fixture.IsValid()))
	{
		return false;
	}

	switch (Scenario)
	{
	case 0:
	{
		const AParadoxDumbwaiter* Defaults = GetDefault<AParadoxDumbwaiter>();
		TestFalse(TEXT("Dumbwaiter is concrete"), AParadoxDumbwaiter::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
		TestFalse(TEXT("Dumbwaiter does not tick"), Defaults->PrimaryActorTick.bCanEverTick);
		TestNotNull(TEXT("Selectable exists"), Defaults->GetSelectableComponent());
		TestNotNull(TEXT("Smart Object exists"), Defaults->GetSmartObjectComponent());
		TestNotNull(TEXT("Interaction component exists"), Defaults->GetInteractionComponent());
		TestNotNull(TEXT("World State participant exists"), Defaults->GetWorldStateParticipantComponent());
		TestNotNull(TEXT("Inherited transfer anchor exists"), Defaults->TransferAnchor.Get());
		TestEqual(TEXT("Transfer-Out completion is explicit"), Defaults->TransferOutCompletionMode, EParadoxTransferPhaseCompletionMode::Explicit);
		TestEqual(TEXT("Transfer-In completion is explicit"), Defaults->TransferInCompletionMode, EParadoxTransferPhaseCompletionMode::Explicit);
		break;
	}
	case 1:
	{
		UParadoxDumbwaiterPairOccupancyTestListener* SourceListener =
			NewObject<UParadoxDumbwaiterPairOccupancyTestListener>();
		UParadoxDumbwaiterPairOccupancyTestListener* DestinationListener =
			NewObject<UParadoxDumbwaiterPairOccupancyTestListener>();
		Fixture.Source->OnPairOccupancyChanged.AddDynamic(
			SourceListener,
			&UParadoxDumbwaiterPairOccupancyTestListener::HandlePairOccupancyChanged);
		Fixture.Destination->OnPairOccupancyChanged.AddDynamic(
			DestinationListener,
			&UParadoxDumbwaiterPairOccupancyTestListener::HandlePairOccupancyChanged);
		TestTrue(TEXT("Compatible cargo inserts"), Fixture.EquipAndInsert());
		TestFalse(TEXT("Inventory becomes empty"), Fixture.Inventory()->HasItem());
		TestEqual(TEXT("Dumbwaiter owns cargo"), Fixture.Source->GetStoredPickupable(), static_cast<AParadoxInsertablePickupableActor*>(Fixture.Cargo));
		TestEqual(TEXT("Cargo backlink names Dumbwaiter"), Fixture.Cargo->GetCurrentDumbwaiter(), static_cast<AParadoxDumbwaiter*>(Fixture.Source));
		TestNull(TEXT("Cargo has no Item Slot backlink"), Fixture.Cargo->GetCurrentItemSlot());
		TestTrue(TEXT("Cargo is aligned to transfer anchor"), Fixture.Cargo->GetActorTransform().Equals(Fixture.Source->TransferAnchor->GetComponentTransform()));
		TestTrue(TEXT("Source reports local pair occupancy"), Fixture.Source->IsOccupied() && !Fixture.Source->IsLinkedDumbwaiterOccupied());
		TestTrue(TEXT("Destination reports linked pair occupancy"), !Fixture.Destination->IsOccupied() && Fixture.Destination->IsLinkedDumbwaiterOccupied());
		TestEqual(TEXT("Source occupancy event fires once for Insert"), SourceListener->NotificationCount, 1);
		TestTrue(TEXT("Source event perspective is self occupied"), SourceListener->bLastSelfOccupied && !SourceListener->bLastLinkedOccupied);
		TestEqual(TEXT("Destination occupancy event fires once for partner Insert"), DestinationListener->NotificationCount, 1);
		TestTrue(TEXT("Destination event perspective is linked occupied"), !DestinationListener->bLastSelfOccupied && DestinationListener->bLastLinkedOccupied);
		break;
	}
	case 2:
	{
		FGameplayTagContainer KeyTraits;
		KeyTraits.AddTag(TestCargoKey);
		Fixture.Cargo->SetTraits(KeyTraits);
		TestTrue(TEXT("Cargo equips"), Fixture.Inventory()->TryEquip(Fixture.Cargo).IsSuccess());
		const FParadoxItemSlotOperationResult Result = Fixture.Source->TryInsertCargo(Fixture.Character);
		TestEqual(TEXT("Incompatible cargo is rejected"), Result.Status, EParadoxItemSlotOperationStatus::IncompatibleTraits);
		TestEqual(TEXT("Rejected cargo stays held"), Fixture.Inventory()->GetEquippedItem(), static_cast<AParadoxPickupableActor*>(Fixture.Cargo));
		break;
	}
	case 3:
	{
		TestTrue(TEXT("First cargo inserts"), Fixture.EquipAndInsert());
		TestTrue(TEXT("Second cargo equips"), Fixture.Inventory()->TryEquip(Fixture.OtherCargo).IsSuccess());
		TestEqual(TEXT("Occupied Dumbwaiter rejects Insert"), Fixture.Source->TryInsertCargo(Fixture.Character).Status, EParadoxItemSlotOperationStatus::SlotOccupied);
		TestEqual(TEXT("Original cargo remains authoritative"), Fixture.Source->GetStoredPickupable(), static_cast<AParadoxInsertablePickupableActor*>(Fixture.Cargo));
		break;
	}
	case 4:
	{
		TestTrue(TEXT("Cargo inserts"), Fixture.EquipAndInsert());
		const FParadoxTransferOperationResult Send = Fixture.Source->TrySendCargo(Fixture.Character);
		TestTrue(TEXT("Send starts"), Send.IsSuccess());
		TestTrue(TEXT("Second cargo equips while first is in transfer-out"), Fixture.Inventory()->TryEquip(Fixture.OtherCargo).IsSuccess());
		TestEqual(TEXT("Busy source rejects Insert before occupancy"), Fixture.Source->TryInsertCargo(Fixture.Character).Status, EParadoxItemSlotOperationStatus::OperationInProgress);
		Fixture.Source->ResetTransferEndpoint();
		break;
	}
	case 5:
	{
		TestEqual(TEXT("Empty Send is rejected"), Fixture.Source->EvaluateSendCargo(Fixture.Character).Status, EParadoxTransferOperationStatus::InvalidSubject);
		TestEqual(TEXT("Pair remains Idle"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Idle);
		break;
	}
	case 6:
	{
		TestTrue(TEXT("Source cargo inserts"), Fixture.EquipAndInsert());
		TestTrue(TEXT("Other cargo equips"), Fixture.Inventory()->TryEquip(Fixture.OtherCargo).IsSuccess());
		TestTrue(TEXT("Destination immediately sees linked occupancy"), Fixture.Destination->IsLinkedDumbwaiterOccupied());
		TestEqual(TEXT("Linked cargo rejects destination Insert evaluation"),
			Fixture.Destination->EvaluateAcceptCargo(Fixture.OtherCargo, Fixture.Character).Status,
			EParadoxItemSlotOperationStatus::SlotOccupied);
		TestEqual(TEXT("Linked cargo rejects destination Insert mutation"),
			Fixture.Destination->TryInsertCargo(Fixture.Character).Status,
			EParadoxItemSlotOperationStatus::SlotOccupied);
		TestTrue(TEXT("Destroying source cargo succeeds"), Fixture.Cargo->Destroy());
		TestFalse(TEXT("Cargo destruction clears source occupancy"), Fixture.Source->IsOccupied());
		TestFalse(TEXT("Cargo destruction clears linked occupancy"), Fixture.Destination->IsLinkedDumbwaiterOccupied());
		TestTrue(TEXT("Destination Insert is re-enabled after cargo removal"),
			Fixture.Destination->EvaluateAcceptCargo(Fixture.OtherCargo, Fixture.Character).IsSuccess());
		break;
	}
	case 7:
	{
		Fixture.StartPlayAndActivate(false, true);
		TestTrue(TEXT("Cargo can be equipped"), Fixture.Inventory()->TryEquip(Fixture.Cargo).IsSuccess());
		TestEqual(TEXT("Inactive source rejects Insert"), Fixture.Source->TryInsertCargo(Fixture.Character).Status, EParadoxItemSlotOperationStatus::SlotInactive);
		Fixture.Source->PuzzleReceiver->SetControllerRequest(Fixture.Controller, true);
		TestTrue(TEXT("Insert succeeds after activation"), Fixture.Source->TryInsertCargo(Fixture.Character).IsSuccess());
		Fixture.Destination->PuzzleReceiver->SetControllerRequest(Fixture.Controller, false);
		TestEqual(TEXT("Inactive destination rejects Send"), Fixture.Source->TrySendCargo(Fixture.Character).Status, EParadoxTransferOperationStatus::DestinationInactive);
		break;
	}
	case 8:
	{
		TestTrue(TEXT("Cargo inserts"), Fixture.EquipAndInsert());
		const FParadoxTransferOperationResult Send = Fixture.Source->TrySendCargo(Fixture.Character);
		TestTrue(TEXT("Send succeeds"), Send.IsSuccess());
		TestEqual(TEXT("Source locks immediately"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Sending);
		TestEqual(TEXT("Destination locks immediately"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Receiving);
		Fixture.Scope.World->Tick(ELevelTick::LEVELTICK_All, 0.25f);
		TestEqual(TEXT("Explicit Send remains in background Transfer-Out without a Blueprint completion"), Fixture.Source->GetTransferPhase(), EParadoxTransferPhase::TransferOut);
		TestEqual(TEXT("Pickup is blocked while transfer-out is pending"), Fixture.Source->EvaluatePickupCargo(Fixture.Character).Status, EParadoxItemSlotOperationStatus::OperationInProgress);
		TestEqual(TEXT("Duplicate Send is rejected"), Fixture.Source->TrySendCargo(Fixture.Character).Status, EParadoxTransferOperationStatus::SourceBusy);
		Fixture.Source->ResetTransferEndpoint();
		break;
	}
	case 9:
	{
		TestTrue(TEXT("Cargo inserts"), Fixture.EquipAndInsert());
		const FParadoxTransferOperationResult Send = Fixture.Source->TrySendCargo(Fixture.Character);
		TestTrue(TEXT("Send succeeds"), Send.IsSuccess());
		TestTrue(TEXT("Transfer-out completion commits"), Fixture.Source->CompleteTransferOutForTest(Send.OperationId));
		TestFalse(TEXT("Source becomes logically empty exactly at commit"), Fixture.Source->IsOccupied());
		TestEqual(TEXT("Destination becomes sole owner"), Fixture.Destination->GetStoredPickupable(), static_cast<AParadoxInsertablePickupableActor*>(Fixture.Cargo));
		TestEqual(TEXT("Cargo backlink moves to destination"), Fixture.Cargo->GetCurrentDumbwaiter(), static_cast<AParadoxDumbwaiter*>(Fixture.Destination));
		TestEqual(TEXT("Destination Pickup stays blocked through transfer-in"), Fixture.Destination->EvaluatePickupCargo(Fixture.Character).Status, EParadoxItemSlotOperationStatus::OperationInProgress);
		TestTrue(TEXT("Cargo is attached to destination anchor"), Fixture.Cargo->GetRootComponent()->GetAttachParent() == Fixture.Destination->TransferAnchor);
		break;
	}
	case 10:
	{
		TestTrue(TEXT("Cargo inserts"), Fixture.EquipAndInsert());
		const FParadoxTransferOperationResult Forward = Fixture.Source->TrySendCargo(Fixture.Character);
		TestTrue(TEXT("Forward Send starts"), Forward.IsSuccess());
		TestTrue(TEXT("Forward commit succeeds"), Fixture.Source->CompleteTransferOutForTest(Forward.OperationId));
		TestTrue(TEXT("Forward transfer-in completes"), Fixture.Destination->CompleteTransferInForTest(Forward.OperationId));
		TestEqual(TEXT("Both endpoints return Idle"), Fixture.Source->GetTransferState(), EParadoxTransferEndpointState::Idle);
		const FParadoxTransferOperationResult Reverse = Fixture.Destination->TrySendCargo(Fixture.Character);
		TestTrue(TEXT("Reverse Send starts"), Reverse.IsSuccess());
		TestTrue(TEXT("Reverse commit succeeds"), Fixture.Destination->CompleteTransferOutForTest(Reverse.OperationId));
		TestTrue(TEXT("Reverse transfer-in completes"), Fixture.Source->CompleteTransferInForTest(Reverse.OperationId));
		TestEqual(TEXT("Cargo returns to source"), Fixture.Source->GetStoredPickupable(), static_cast<AParadoxInsertablePickupableActor*>(Fixture.Cargo));
		break;
	}
	case 11:
	{
		TestTrue(TEXT("Cargo inserts"), Fixture.EquipAndInsert());
		Fixture.Source->SetLocked(true);
		TestEqual(TEXT("Locked cargo rejects ordinary Pickup"), Fixture.Source->TryPickupCargo(Fixture.Character).Status, EParadoxItemSlotOperationStatus::ItemLocked);
		Fixture.Source->SetLocked(false);
		TestTrue(TEXT("Unlocked cargo reuses inventory Pickup"), Fixture.Source->TryPickupCargo(Fixture.Character).IsSuccess());
		TestEqual(TEXT("Inventory owns retrieved cargo"), Fixture.Inventory()->GetEquippedItem(), static_cast<AParadoxPickupableActor*>(Fixture.Cargo));
		TestFalse(TEXT("Dumbwaiter becomes empty"), Fixture.Source->IsOccupied());
		break;
	}
	case 12:
	{
		const UGameplayActionDefinition* SendAsset = LoadObject<UGameplayActionDefinition>(
			nullptr,
			TEXT("/Game/Data/GameplayActions/DA_ParadoxSendDumbwaiter.DA_ParadoxSendDumbwaiter"));
		TestNotNull(TEXT("Native Send Definition asset exists"), SendAsset);
		TestTrue(TEXT("Send asset uses dedicated native class"), SendAsset && SendAsset->IsA<UParadoxSendDumbwaiterInteractionActionDefinition>());
		TestEqual(TEXT("Send asset selects the dedicated action"), SendAsset ? SendAsset->InstanceClass.Get() : nullptr, UParadoxSendDumbwaiterInteractionAction::StaticClass());
		TestTrue(
			TEXT("Send asset carries the semantic action tag"),
			SendAsset
				&& SendAsset->ActionTag
					== ParadoxGameplayTags::Action_Dumbwaiter_Send.GetTag());
		TestTrue(TEXT("Send action uses standard semantic Interaction base"), UParadoxSendDumbwaiterInteractionAction::StaticClass()->IsChildOf(UParadoxInteractionActionBase::StaticClass()));
		TestTrue(TEXT("Insert action remains shared with Item Slots"), UParadoxInsertItemInteractionAction::StaticClass()->IsChildOf(UParadoxInteractionActionBase::StaticClass()));
		TestTrue(TEXT("Pickup action remains shared with Item Slots"), UParadoxPickupFromItemSlotInteractionAction::StaticClass()->IsChildOf(UParadoxPickupInteractionAction::StaticClass()));
		bool bHasInsert = false;
		bool bHasPickup = false;
		bool bHasSend = false;
		for (const FParadoxInteractionDefinition& Definition : Fixture.Source->GetInteractionComponent()->InteractionDefinitions)
		{
			bHasInsert |= Definition.InteractionTag == ParadoxGameplayTags::Interaction_ItemSlot_Insert;
			bHasPickup |= Definition.InteractionTag == ParadoxGameplayTags::Interaction_ItemSlot_Pickup;
			bHasSend |= Definition.InteractionTag == ParadoxGameplayTags::Interaction_Dumbwaiter_Send;
		}
		TestTrue(TEXT("Catalog exposes semantic Insert"), bHasInsert);
		TestTrue(TEXT("Catalog exposes semantic Pickup"), bHasPickup);
		TestTrue(TEXT("Catalog exposes semantic Send"), bHasSend);
		break;
	}
	case 13:
	{
		TestTrue(TEXT("Occupied baseline setup succeeds"), Fixture.EquipAndInsert());
		UWorldStateSubsystem* WorldState = Fixture.Scope.World->GetSubsystem<UWorldStateSubsystem>();
		TestTrue(TEXT("World State registration finalizes"), WorldState && WorldState->FinalizeWorldStateRegistration().IsSuccess());
		FWorldStateCaptureRequest Capture;
		Capture.Label = TEXT("DumbwaiterOccupiedBaseline");
		TestTrue(TEXT("Occupied baseline captures"), WorldState && WorldState->CaptureBaseline(Capture).IsSuccess());
		const FParadoxTransferOperationResult Send = Fixture.Source->TrySendCargo(Fixture.Character);
		TestTrue(TEXT("Pending transfer starts"), Send.IsSuccess());
		TestTrue(TEXT("Restore cancels pending transfer"), WorldState->RestoreBaseline(FWorldStateRestoreRequest()).IsSuccess());
		TestEqual(TEXT("Pending restore returns cargo to source"), Fixture.Source->GetStoredPickupable(), static_cast<AParadoxInsertablePickupableActor*>(Fixture.Cargo));
		TestEqual(TEXT("Pair is Idle after pending restore"), Fixture.Destination->GetTransferState(), EParadoxTransferEndpointState::Idle);
		const FParadoxTransferOperationResult CommittedSend = Fixture.Source->TrySendCargo(Fixture.Character);
		TestTrue(TEXT("Committed transfer starts"), CommittedSend.IsSuccess());
		TestTrue(TEXT("Transfer reaches committed transfer-in"), Fixture.Source->CompleteTransferOutForTest(CommittedSend.OperationId));
		TestTrue(TEXT("Restore succeeds after commit"), WorldState->RestoreBaseline(FWorldStateRestoreRequest()).IsSuccess());
		TestEqual(TEXT("Committed restore returns cargo to captured source"), Fixture.Source->GetStoredPickupable(), static_cast<AParadoxInsertablePickupableActor*>(Fixture.Cargo));
		TestFalse(TEXT("Destination is empty after restore"), Fixture.Destination->IsOccupied());
		TestEqual(TEXT("Cargo backlink agrees after restore"), Fixture.Cargo->GetCurrentDumbwaiter(), static_cast<AParadoxDumbwaiter*>(Fixture.Source));
		break;
	}
	case 14:
	{
		FParadoxDumbwaiterTestAccessor::SetInitiallyStoredPickupable(*Fixture.Source, Fixture.Cargo);
		Fixture.StartPlayAndActivate();
		TestEqual(TEXT("Authored cargo initializes"), Fixture.Source->GetStoredPickupable(), static_cast<AParadoxInsertablePickupableActor*>(Fixture.Cargo));
		TestTrue(TEXT("Authored cargo enters inserted ownership state"), Fixture.Cargo->IsInserted());
		TestNull(TEXT("Dumbwaiter owns no Emitter"), Fixture.Source->FindComponentByClass<UPuzzleEmitterComponent>());
		break;
	}
	case 15:
	{
		Fixture.Cargo->ConfigureInsertedPresence(true, true);
		UStaticMeshComponent* Mesh = Fixture.Cargo->GetPickupableMesh();
		if (TestNotNull(TEXT("Cargo mesh exists"), Mesh))
		{
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Mesh->SetCollisionResponseToAllChannels(ECR_Block);
			Mesh->SetCanEverAffectNavigation(true);
		}
		Fixture.StartPlayAndActivate();
		TestTrue(TEXT("Cargo inserts with authored presence"), Fixture.EquipAndInsert());
		TestTrue(TEXT("Inserted cargo collision starts enabled"), Fixture.Cargo->GetActorEnableCollision());
		TestTrue(TEXT("Inserted cargo navigation influence starts enabled"),
			Mesh && Mesh->CanEverAffectNavigation()
				&& Fixture.Cargo->GetGridNavigationModifierComponent()->bBlockCells);

		const FParadoxTransferOperationResult CancelledSend =
			Fixture.Source->TrySendCargo(Fixture.Character);
		TestTrue(TEXT("First Send starts"), CancelledSend.IsSuccess());
		TestFalse(TEXT("Transfer-Out disables Actor collision"), Fixture.Cargo->GetActorEnableCollision());
		TestTrue(TEXT("Transfer-Out disables primitive collision"),
			Mesh && Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestTrue(TEXT("Transfer-Out disables navigation influence"),
			Mesh && !Mesh->CanEverAffectNavigation()
				&& !Fixture.Cargo->GetGridNavigationModifierComponent()->bBlockCells);
		TestTrue(TEXT("Cancellation releases the transaction"), Fixture.Source->ResetTransferEndpoint());
		TestTrue(TEXT("Cancellation restores Actor collision"), Fixture.Cargo->GetActorEnableCollision());
		TestTrue(TEXT("Cancellation restores authored primitive collision"),
			Mesh && Mesh->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
		TestTrue(TEXT("Cancellation restores authored navigation influence"),
			Mesh && Mesh->CanEverAffectNavigation()
				&& Fixture.Cargo->GetGridNavigationModifierComponent()->bBlockCells);

		const FParadoxTransferOperationResult CompletedSend =
			Fixture.Source->TrySendCargo(Fixture.Character);
		TestTrue(TEXT("Second Send starts"), CompletedSend.IsSuccess());
		TestTrue(TEXT("Commit succeeds while presence remains suspended"),
			Fixture.Source->CompleteTransferOutForTest(CompletedSend.OperationId));
		TestFalse(TEXT("Committed cargo remains collisionless during Transfer-In"),
			Fixture.Cargo->GetActorEnableCollision());
		TestTrue(TEXT("Transfer-In completion succeeds"),
			Fixture.Destination->CompleteTransferInForTest(CompletedSend.OperationId));
		TestTrue(TEXT("Completion restores Actor collision"), Fixture.Cargo->GetActorEnableCollision());
		TestTrue(TEXT("Completion restores authored primitive collision"),
			Mesh && Mesh->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
		TestTrue(TEXT("Completion restores authored navigation influence"),
			Mesh && Mesh->CanEverAffectNavigation()
				&& Fixture.Cargo->GetGridNavigationModifierComponent()->bBlockCells);
		break;
	}
	default:
		AddError(TEXT("Unknown Dumbwaiter scenario."));
		return false;
	}
	return true;
}

#endif
