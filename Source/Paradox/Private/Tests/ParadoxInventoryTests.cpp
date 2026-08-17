#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actions/GameplayActionDefinition.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationModifierComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayActionTags.h"
#include "Interaction/ParadoxInteractionActionDefinition.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Interaction/ParadoxSelectionComponent.h"
#include "Inventory/ParadoxDropAction.h"
#include "Inventory/ParadoxDropTargetingComponent.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxInventoryInteractionActions.h"
#include "Inventory/ParadoxPickupableAction.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "Navigation/GridNavigationData.h"
#include "Paradox.h"
#include "Recording/IntentReplayTrack.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectSubsystem.h"
#include "Tests/ParadoxInventoryTestTypes.h"
#include "Tests/ParadoxSelectionTestTypes.h"
#include "Types/IntentReplayTypes.h"
#include "UObject/UnrealType.h"

struct FParadoxInventoryTestAccessor
{
	static void ConfigureAuthoredWorldPresence(
		AParadoxPickupableActor& Pickupable,
		const bool bUseCollision,
		const bool bUseNavigation)
	{
		Pickupable.bUseAuthoredWorldCollision = bUseCollision;
		Pickupable.bUseAuthoredNavigationInfluence = bUseNavigation;
	}

	static bool UsesAuthoredWorldCollision(const AParadoxPickupableActor& Pickupable)
	{
		return Pickupable.bUseAuthoredWorldCollision;
	}

	static bool UsesAuthoredNavigationInfluence(const AParadoxPickupableActor& Pickupable)
	{
		return Pickupable.bUseAuthoredNavigationInfluence;
	}

	static void EnforceNonBlockingPresence(AParadoxPickupableActor& Pickupable)
	{
		Pickupable.EnforceNonBlockingPresence();
	}

	static UStaticMeshComponent* EnsureDropPreview(
		UParadoxDropTargetingComponent& Targeting)
	{
		return Targeting.EnsureDropPreview();
	}

	static void SetDropPreviewVisible(
		UParadoxDropTargetingComponent& Targeting,
		const bool bVisible)
	{
		Targeting.SetDropPreviewVisible(bVisible);
	}

	static AActor* GetDropPreviewActor(
		const UParadoxDropTargetingComponent& Targeting)
	{
		return Targeting.DropPreviewActor.Get();
	}

	static UStaticMeshComponent* GetDropPreviewComponent(
		const UParadoxDropTargetingComponent& Targeting)
	{
		return Targeting.DropPreviewComponent.Get();
	}

	static void DestroyDropPreview(UParadoxDropTargetingComponent& Targeting)
	{
		Targeting.DestroyDropPreview();
	}
};

namespace UE::Paradox::Inventory::Tests
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

	struct FInventoryFixture
	{
		explicit FInventoryFixture(const TCHAR* WorldName)
			: Scope(WorldName)
		{
			if (!Scope.World)
			{
				return;
			}
			FActorSpawnParameters Parameters;
			Parameters.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = Scope.World->SpawnActor<AParadoxPlayerCharacter>(
				AParadoxPlayerCharacter::StaticClass(), FTransform::Identity, Parameters);
			First = Scope.World->SpawnActor<AParadoxInventoryTestPickupable>(
				AParadoxInventoryTestPickupable::StaticClass(),
				FTransform(FVector(100.0, 0.0, 0.0)), Parameters);
			Second = Scope.World->SpawnActor<AParadoxInventoryTestPickupable>(
				AParadoxInventoryTestPickupable::StaticClass(),
				FTransform(FVector(300.0, 200.0, 0.0)), Parameters);
			Scope.World->InitializeActorsForPlay(FURL());
			Scope.World->BeginPlay();
		}

		UParadoxInventoryComponent* Inventory() const
		{
			return Character ? Character->GetInventoryComponent() : nullptr;
		}

		FScopedTestWorld Scope;
		AParadoxPlayerCharacter* Character = nullptr;
		AParadoxInventoryTestPickupable* First = nullptr;
		AParadoxInventoryTestPickupable* Second = nullptr;
	};

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> MakeInteractionGridSnapshot(
		const FGuid& GridId)
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot =
			MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Snapshot->GridId = GridId;
		Snapshot->Revisions.Topology = 1;
		Snapshot->Revisions.Traversal = 1;
		Snapshot->Revisions.Occupancy = 1;
		FGridRegionData& Region = Snapshot->Regions.Add(GridId);
		Region.GridId = GridId;
		Region.GridTransform.CellSize = FVector(100.0, 100.0, 50.0);
		for (int32 X = -1; X <= 3; ++X)
		{
			for (int32 Y = -2; Y <= 2; ++Y)
			{
				FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
				Cell.Id.GridId = GridId;
				Cell.Id.Coord = FGridCellCoord(X, Y, 0);
				Cell.WorldCenter = Region.GridTransform.CellToWorld(Cell.Id.Coord);
				Cell.bWalkable = true;
			}
		}
		return Snapshot;
	}

	const TCHAR* ScenarioNames[] = {
		TEXT("01.Pickup.EmptySlot"),
		TEXT("02.Pickup.OccupiedSlotRejected"),
		TEXT("03.Swap.AtomicFinalState"),
		TEXT("04.Drop.AdjacentTransition"),
		TEXT("05.Drop.DistantMovementComposition"),
		TEXT("06.Drop.ExactInjectedPathContract"),
		TEXT("07.Drop.InvalidationHasNoRetarget"),
		TEXT("08.Drop.NoPathFailureContract"),
		TEXT("09.Targeting.CleanupWhenInactive"),
		TEXT("10.Passives.ExactlyOnce"),
		TEXT("11.PickupableActions.InvalidConfiguration"),
		TEXT("12.EmptyConfiguration.Safe"),
		TEXT("13.Character.PlayerCloneParity"),
		TEXT("14.Reset.BeforeMovement"),
		TEXT("15.Reset.DuringMovement"),
		TEXT("16.Reset.WhileTargeting"),
		TEXT("17.Lifetime.ItemDestruction"),
		TEXT("18.Integration.NativeComponents"),
		TEXT("19.Integration.AssetsAndNoBlueprintHooks"),
		TEXT("20.Concurrency.ReentrancyRejected"),
		TEXT("21.Interaction.PlayerPickupAndCloneReplayRequester"),
		TEXT("22.Interaction.SwapPreflightRequester"),
		TEXT("23.InteractionWidget.RemainsQueryableAfterDropNormalization"),
		TEXT("24.WorldPresence.AuthoredCollisionAndNavigation"),
		TEXT("25.WorldPresence.KeyCardDoesNotPublishOccupancy")
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FParadoxInventoryScenariosTest,
	"Paradox.Inventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FParadoxInventoryScenariosTest::GetTests(
	TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(UE::Paradox::Inventory::Tests::ScenarioNames); ++Index)
	{
		OutBeautifiedNames.Add(UE::Paradox::Inventory::Tests::ScenarioNames[Index]);
		OutTestCommands.Add(FString::FromInt(Index));
	}
}

bool FParadoxInventoryScenariosTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Inventory::Tests;
	const int32 Scenario = FCString::Atoi(*Parameters);
	FInventoryFixture Fixture(*FString::Printf(TEXT("ParadoxInventoryScenario%d"), Scenario + 1));
	if (!TestNotNull(TEXT("Transient inventory test world exists"), Fixture.Scope.World)
		|| !TestNotNull(TEXT("Player Character exists"), Fixture.Character)
		|| !TestNotNull(TEXT("First pickupable exists"), Fixture.First)
		|| !TestNotNull(TEXT("Second pickupable exists"), Fixture.Second)
		|| !TestNotNull(TEXT("Character inventory exists"), Fixture.Inventory()))
	{
		return false;
	}

	UParadoxInventoryComponent* Inventory = Fixture.Inventory();
	switch (Scenario)
	{
	case 0:
	{
		const FParadoxInventoryOperationResult Result = Inventory->TryEquip(Fixture.First);
		TestTrue(TEXT("Pickup succeeds"), Result.IsSuccess());
		TestTrue(TEXT("Slot reports occupied"), Inventory->HasItem());
		TestTrue(TEXT("Inventory points to item"), Inventory->GetEquippedItem() == Fixture.First);
		TestTrue(TEXT("Item points back to holder"), Fixture.First->GetCurrentHolder() == Fixture.Character);
		break;
	}
	case 1:
	{
		TestTrue(TEXT("Initial pickup succeeds"), Inventory->TryEquip(Fixture.First).IsSuccess());
		const FParadoxInventoryOperationResult Result = Inventory->TryEquip(Fixture.Second);
		TestTrue(TEXT("Second pickup is rejected as occupied"), Result.Status == EParadoxInventoryOperationStatus::SlotOccupied);
		TestTrue(TEXT("Original item remains equipped"), Inventory->GetEquippedItem() == Fixture.First);
		TestTrue(TEXT("Rejected item stays in world"), Fixture.Second->IsAvailableInWorld());
		break;
	}
	case 2:
	{
		const FTransform IncomingTransform = Fixture.Second->GetActorTransform();
		TestTrue(TEXT("Initial pickup succeeds"), Inventory->TryEquip(Fixture.First).IsSuccess());
		TestTrue(TEXT("Swap succeeds"), Inventory->TrySwap(Fixture.Second).IsSuccess());
		TestTrue(TEXT("Only incoming item is equipped"), Inventory->GetEquippedItem() == Fixture.Second);
		TestTrue(TEXT("Incoming holder is coherent"), Fixture.Second->GetCurrentHolder() == Fixture.Character);
		TestNull(TEXT("Outgoing holder is cleared"), Fixture.First->GetCurrentHolder());
		TestTrue(TEXT("Outgoing item occupies incoming exact transform"), Fixture.First->GetActorTransform().Equals(IncomingTransform));
		break;
	}
	case 3:
	{
		const FTransform Destination(FVector(200.0, 100.0, 25.0));
		TestTrue(TEXT("World pickupable Actor enables query collision"),
			Fixture.First->GetActorEnableCollision());
		TestTrue(TEXT("World pickupable mesh is query-only"),
			Fixture.First->GetPickupableMesh()->GetCollisionEnabled() == ECollisionEnabled::QueryOnly);
		TestTrue(TEXT("World pickupable mesh blocks the selection Visibility trace"),
			Fixture.First->GetPickupableMesh()->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);
		TestTrue(TEXT("World pickupable mesh remains non-blocking for Pawns"),
			Fixture.First->GetPickupableMesh()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore);
		const FParadoxInventoryOperationResult EquipResult = Inventory->TryEquip(Fixture.First);
		TestTrue(*FString::Printf(TEXT("Pickup before Drop succeeds: %s"), *EquipResult.DiagnosticMessage),
			EquipResult.IsSuccess());
		const ECollisionEnabled::Type HeldCollision =
			Fixture.First->GetPickupableMesh()->GetCollisionEnabled();
		TestFalse(TEXT("Held pickupable disables Actor collision"),
			Fixture.First->GetActorEnableCollision());
		TestTrue(*FString::Printf(TEXT("Held item remains collisionless (actual=%d)"),
			static_cast<int32>(HeldCollision)), HeldCollision == ECollisionEnabled::NoCollision);
		TestTrue(TEXT("Drop transition succeeds"), Inventory->TryDropAtTransform(Destination).IsSuccess());
		TestFalse(TEXT("Slot becomes empty"), Inventory->HasItem());
		TestTrue(TEXT("Item returns to world"), Fixture.First->IsAvailableInWorld());
		TestTrue(TEXT("Drop uses the exact validated transform"), Fixture.First->GetActorTransform().Equals(Destination));
		const ECollisionEnabled::Type RestoredCollision =
			Fixture.First->GetPickupableMesh()->GetCollisionEnabled();
		TestTrue(TEXT("Dropped pickupable re-enables Actor query collision"),
			Fixture.First->GetActorEnableCollision());
		TestTrue(*FString::Printf(TEXT("Dropped item restores query-only collision (actual=%d)"),
			static_cast<int32>(RestoredCollision)),
			RestoredCollision == ECollisionEnabled::QueryOnly);
		TestTrue(TEXT("Dropped mesh blocks the selection Visibility trace"),
			Fixture.First->GetPickupableMesh()->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);
		TestTrue(TEXT("Dropped mesh ignores Pawns"),
			Fixture.First->GetPickupableMesh()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore);
		TestFalse(TEXT("Dropped mesh emits no overlaps"),
			Fixture.First->GetPickupableMesh()->GetGenerateOverlapEvents());
		TestFalse(TEXT("Dropped mesh cannot affect Unreal navigation"),
			Fixture.First->GetPickupableMesh()->CanEverAffectNavigation());
		break;
	}
	case 4:
	{
		const UParadoxDropActionDefinition* Definition = LoadObject<UParadoxDropActionDefinition>(
			nullptr, TEXT("/Game/Data/GameplayActions/DA_ParadoxDrop.DA_ParadoxDrop"));
		if (TestNotNull(TEXT("Drop Definition asset resolves"), Definition))
		{
			TestTrue(TEXT("Drop composes the native action"), Definition->InstanceClass == UParadoxDropAction::StaticClass());
			TestTrue(TEXT("Drop owns movement lock"), Definition->ExecutionLocks.HasTagExact(GameplayActionTags::Lock_Movement));
			TestTrue(TEXT("Drop owns inventory lock"), Definition->ExecutionLocks.HasTagExact(ParadoxGameplayTags::Lock_Inventory));
		}
		break;
	}
	case 5:
	{
		const UScriptStruct* ParameterStruct = FParadoxDropActionParameters::StaticStruct();
		TestNotNull(TEXT("Drop stores exact path source"),
			ParameterStruct->FindPropertyByName(ParadoxDropActionParameters::PathSource));
		TestNotNull(TEXT("Drop stores injected path"),
			ParameterStruct->FindPropertyByName(ParadoxDropActionParameters::InjectedPath));
		TestNull(TEXT("Deprecated path cost is removed"),
			ParameterStruct->FindPropertyByName(TEXT("PathCost")));
		TestNull(TEXT("Deprecated execution cell is removed"),
			ParameterStruct->FindPropertyByName(TEXT("ExecutionCell")));
		const UParadoxDropActionDefinition* Definition = GetDefault<UParadoxDropActionDefinition>();
		TestTrue(TEXT("Drop defaults to exact injected movement"),
			Definition->DefaultParameters.GetValueEnum<EGridMovePathSource>(
				ParadoxDropActionParameters::PathSource).GetValue()
				== EGridMovePathSource::ExactInjectedPath);
		break;
	}
	case 6:
	{
		TestNotNull(TEXT("Drop captures the originally equipped item"), UParadoxDropAction::StaticClass()->FindPropertyByName(TEXT("CapturedItem")));
		TestNotNull(TEXT("Drop stores one immutable semantic parameter set"), UParadoxDropAction::StaticClass()->FindPropertyByName(TEXT("SemanticParameters")));
		TestTrue(TEXT("Dedicated invalidation failure tag is registered"), ParadoxGameplayTags::Result_Failure_Inventory_TargetInvalidated.GetTag().IsValid());
		break;
	}
	case 7:
	{
		TestTrue(TEXT("Terminal path failure is explicit"),
			EGridPathPreviewFailureReason::TerminalGoalUnavailable
				!= EGridPathPreviewFailureReason::NoPath);
		TestTrue(TEXT("No-path failure tag is registered"), ParadoxGameplayTags::Result_Failure_Inventory_NoReachableExecutionCell.GetTag().IsValid());
		break;
	}
	case 8:
	{
		UParadoxDropTargetingComponent* Targeting = GetDefault<AParadoxPlayerController>()->GetDropTargetingComponent();
		if (TestNotNull(TEXT("Controller owns targeting"), Targeting))
		{
			TestFalse(TEXT("Targeting defaults inactive"), Targeting->IsDropTargetingActive());
			TestTrue(TEXT("Inactive cancel is observable"), Targeting->CancelDropTargeting().Status == EParadoxDropTargetingStatus::NotActive);
			TestFalse(TEXT("Cleanup leaves targeting inactive"), Targeting->IsDropTargetingActive());
		}
		TestNull(TEXT("Deprecated all-cell cache is removed"),
			UParadoxDropTargetingComponent::StaticClass()->FindPropertyByName(TEXT("ValidCells")));
		TestNull(TEXT("Deprecated all-cell overlay handle is removed"),
			UParadoxDropTargetingComponent::StaticClass()->FindPropertyByName(TEXT("OverlayHandle")));
		TestNotNull(TEXT("Targeting owns configurable ghost material"),
			UParadoxDropTargetingComponent::StaticClass()->FindPropertyByName(TEXT("DropPreviewMaterial")));
		TestNotNull(TEXT("Targeting retains one transient ghost component"),
			UParadoxDropTargetingComponent::StaticClass()->FindPropertyByName(TEXT("DropPreviewComponent")));
		TestNotNull(TEXT("Targeting owns a separate transient visual Actor"),
			UParadoxDropTargetingComponent::StaticClass()->FindPropertyByName(TEXT("DropPreviewActor")));
		TestNotNull(TEXT("Controller exposes its shared path preview"),
			AParadoxPlayerController::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(
					AParadoxPlayerController,
					GetGridPathPreviewComponent)));

		UClass* AuthoredControllerClass = LoadClass<AParadoxPlayerController>(
			nullptr,
			TEXT("/Game/Characters/Astronaut/Blueprints/BP_PlayerController.BP_PlayerController_C"));
		const AParadoxPlayerController* AuthoredController = AuthoredControllerClass
			? Cast<AParadoxPlayerController>(AuthoredControllerClass->GetDefaultObject())
			: nullptr;
		const UParadoxDropTargetingComponent* AuthoredTargeting = AuthoredController
			? AuthoredController->GetDropTargetingComponent()
			: nullptr;
		TestNotNull(TEXT("Authored Player Controller resolves"), AuthoredController);
		TestNotNull(TEXT("BP_PlayerController configures the Drop ghost material"),
			AuthoredTargeting ? AuthoredTargeting->DropPreviewMaterial.Get() : nullptr);
		UClass* KeyCardClass = LoadClass<AParadoxPickupableActor>(
			nullptr,
			TEXT("/Game/Environment/SpaceShip/Blueprints/BP_KeyCard.BP_KeyCard_C"));
		const AParadoxPickupableActor* KeyCard = KeyCardClass
			? Cast<AParadoxPickupableActor>(KeyCardClass->GetDefaultObject())
			: nullptr;
		TestNotNull(TEXT("BP_KeyCard resolves for Drop preview"), KeyCard);
		TestNotNull(TEXT("BP_KeyCard supplies the source Static Mesh for its ghost"),
			KeyCard && KeyCard->GetPickupableMesh()
				? KeyCard->GetPickupableMesh()->GetStaticMesh().Get()
				: nullptr);

		FActorSpawnParameters ControllerParameters;
		ControllerParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AParadoxPlayerController* LiveController =
			Fixture.Scope.World->SpawnActor<AParadoxPuzzleOverlayTestController>(
				AParadoxPuzzleOverlayTestController::StaticClass(),
				FTransform::Identity,
				ControllerParameters);
		UParadoxDropTargetingComponent* LiveTargeting = LiveController
			? LiveController->GetDropTargetingComponent()
			: nullptr;
		if (TestNotNull(TEXT("Live targeting controller exists"), LiveController)
			&& TestNotNull(TEXT("Live targeting component exists"), LiveTargeting))
		{
			TestTrue(TEXT("Player Controller is a hidden non-visual Actor"),
				LiveController->IsHidden());
			UStaticMeshComponent* PreviewComponent =
				FParadoxInventoryTestAccessor::EnsureDropPreview(*LiveTargeting);
			AActor* PreviewActor =
				FParadoxInventoryTestAccessor::GetDropPreviewActor(*LiveTargeting);
			if (TestNotNull(TEXT("Drop ghost component is created"), PreviewComponent)
				&& TestNotNull(TEXT("Drop ghost has a presentation Actor"), PreviewActor))
			{
				TestTrue(TEXT("Drop ghost is not owned by the hidden Player Controller"),
					PreviewActor != LiveController
						&& PreviewComponent->GetOwner() == PreviewActor);
				FParadoxInventoryTestAccessor::SetDropPreviewVisible(*LiveTargeting, true);
				TestFalse(TEXT("Visible Drop ghost owner is not hidden"),
					PreviewActor->IsHidden());
				TestTrue(TEXT("Visible Drop ghost component renders"),
					PreviewComponent->IsVisible() && !PreviewComponent->bHiddenInGame);
				TestEqual(TEXT("Drop ghost remains collisionless"),
					PreviewComponent->GetCollisionEnabled(),
					ECollisionEnabled::NoCollision);
				TestFalse(TEXT("Drop ghost cannot affect navigation"),
					PreviewComponent->CanEverAffectNavigation());
			}
			FParadoxInventoryTestAccessor::DestroyDropPreview(*LiveTargeting);
			TestNull(TEXT("Drop ghost Actor cleanup is complete"),
				FParadoxInventoryTestAccessor::GetDropPreviewActor(*LiveTargeting));
			TestNull(TEXT("Drop ghost component cleanup is complete"),
				FParadoxInventoryTestAccessor::GetDropPreviewComponent(*LiveTargeting));
		}
		break;
	}
	case 9:
	{
		UParadoxInventoryTestPassiveEffect* Effect = Fixture.First->GetTestEffect();
		Inventory->TryEquip(Fixture.First);
		Inventory->TryDropAtTransform(FTransform(FVector(100.0, 100.0, 0.0)));
		Inventory->ClearInventoryForReset();
		TestEqual(TEXT("Passive applies exactly once"), Effect->ApplyCount, 1);
		TestEqual(TEXT("Passive removes exactly once"), Effect->RemoveCount, 1);
		break;
	}
	case 10:
	{
		Inventory->TryEquip(Fixture.First);
		UParadoxPickupableAction* Action = Fixture.First->AddEmptyTestAction();
		TestEqual(TEXT("Item exposes its special action"), Fixture.First->GetPickupableActions().Num(), 1);
		const FGameplayActionSubmissionResult Result = Action->EvaluateExecution(Fixture.Character);
		TestFalse(TEXT("Missing special-action Definition is rejected"), Result.IsAccepted());
		TestFalse(TEXT("Special-action failure remains diagnostic"), Result.DiagnosticMessage.IsEmpty());
		break;
	}
	case 11:
	{
		Fixture.First->ClearTestConfiguration();
		TestTrue(TEXT("Empty effect/action configuration still equips"), Inventory->TryEquip(Fixture.First).IsSuccess());
		TestTrue(TEXT("Empty configuration still drops"), Inventory->TryDropAtTransform(FTransform::Identity).IsSuccess());
		TestTrue(TEXT("Empty action query is safe"), Fixture.First->GetPickupableActions().IsEmpty());
		break;
	}
	case 12:
	{
		const AParadoxPlayerCharacter* Player = GetDefault<AParadoxPlayerCharacter>();
		const AParadoxCloneCharacter* Clone = GetDefault<AParadoxCloneCharacter>();
		TestNotNull(TEXT("Player inherits inventory"), Player->GetInventoryComponent());
		TestNotNull(TEXT("Clone inherits inventory"), Clone->GetInventoryComponent());
		TestTrue(TEXT("Player and clone share the same inventory class"), Player->GetInventoryComponent()->GetClass() == Clone->GetInventoryComponent()->GetClass());

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AParadoxCloneCharacter* LiveClone =
			Fixture.Scope.World->SpawnActor<AParadoxCloneCharacter>(
				AParadoxCloneCharacter::StaticClass(),
				FTransform(FVector(500.0, 0.0, 0.0)),
				SpawnParameters);
		UParadoxInventoryTestWidget* Widget =
			NewObject<UParadoxInventoryTestWidget>(Fixture.Scope.World);
		if (TestNotNull(TEXT("Live clone exists for explicit widget binding"), LiveClone)
			&& TestNotNull(TEXT("Inventory widget test object exists"), Widget))
		{
			Widget->SetInventoryCharacter(LiveClone);
			TestTrue(TEXT("Widget observes the explicitly supplied clone"),
				Widget->GetInventoryCharacter() == LiveClone);
			TestTrue(TEXT("Widget resolves the clone inventory rather than an owning Player pawn"),
				Widget->GetInventoryComponent() == LiveClone->GetInventoryComponent());
			Widget->SetInventoryCharacter(Fixture.Character);
			TestTrue(TEXT("Widget can rebind safely from clone to Player"),
				Widget->GetInventoryComponent() == Fixture.Inventory());
			Widget->SetInventoryCharacter(nullptr);
			TestNull(TEXT("Clearing the explicit widget source leaves it unbound"),
				Widget->GetInventoryComponent());
		}

		const UFunction* BeginTargetingFunction =
			UParadoxDropTargetingComponent::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(
					UParadoxDropTargetingComponent,
					BeginDropTargeting));
		const FObjectProperty* SourceCharacterProperty = BeginTargetingFunction
			? FindFProperty<FObjectProperty>(BeginTargetingFunction, TEXT("SourceCharacter"))
			: nullptr;
		TestNotNull(TEXT("Drop targeting requires an explicit Source Character parameter"),
			SourceCharacterProperty);
		TestTrue(TEXT("Drop targeting Source Character uses the shared Paradox Character base"),
			SourceCharacterProperty
				&& SourceCharacterProperty->PropertyClass == AParadoxCharacter::StaticClass());
		break;
	}
	case 13:
	{
		Inventory->TryEquip(Fixture.First);
		TestTrue(TEXT("Reset cleanup succeeds before movement"), Inventory->ClearInventoryForReset().IsSuccess());
		TestFalse(TEXT("Reset empties inventory"), Inventory->HasItem());
		TestTrue(TEXT("Pickupable waits for World State"), Fixture.First->GetPickupableState() == EParadoxPickupableState::RestorePending);
		break;
	}
	case 14:
	{
		Inventory->TryEquip(Fixture.First);
		const UParadoxDropActionDefinition* Definition = GetDefault<UParadoxDropActionDefinition>();
		TestTrue(TEXT("Drop movement is interruptible by time-loop abort"), Definition->bInterruptible);
		TestTrue(TEXT("Reset cleanup succeeds while a composed operation may exist"), Inventory->ClearInventoryForReset().IsSuccess());
		TestFalse(TEXT("Reset owns the final empty state"), Inventory->HasItem());
		break;
	}
	case 15:
	{
		UParadoxDropTargetingComponent* Targeting = GetDefault<AParadoxPlayerController>()->GetDropTargetingComponent();
		TestNotNull(TEXT("Reset-capable player targeting exists"), Targeting);
		TestFalse(TEXT("No targeting session leaks on defaults"), Targeting && Targeting->IsDropTargetingActive());
		TestTrue(TEXT("Inactive cleanup remains idempotent during reset paths"), Targeting && Targeting->CancelDropTargeting().Status == EParadoxDropTargetingStatus::NotActive);
		break;
	}
	case 16:
	{
		UParadoxInventoryTestPassiveEffect* Effect = Fixture.First->GetTestEffect();
		Inventory->TryEquip(Fixture.First);
		Fixture.First->Destroy();
		TestFalse(TEXT("Destroying equipped item clears slot"), Inventory->HasItem());
		TestEqual(TEXT("Destruction removes passive exactly once"), Effect->RemoveCount, 1);
		break;
	}
	case 17:
	{
		const AParadoxPickupableActor* Defaults = GetDefault<AParadoxPickupableActor>();
		TestNotNull(TEXT("Pickupable owns mesh"), Defaults->GetPickupableMesh());
		TestNotNull(TEXT("Pickupable owns selection"), Defaults->GetSelectableComponent());
		TestNotNull(TEXT("Pickupable owns interaction"), Defaults->GetInteractionComponent());
		TestNotNull(TEXT("Pickupable owns Smart Object"), Defaults->GetSmartObjectComponent());
		TestNotNull(TEXT("Pickupable owns GridWorld occupancy"), Defaults->GetOccupancyComponent());
		TestNotNull(TEXT("Pickupable owns a GridWorld navigation modifier"),
			Defaults->GetGridNavigationModifierComponent());
		TestNotNull(TEXT("Pickupable owns World State participation"), Defaults->GetWorldStateParticipantComponent());
		TestTrue(TEXT("World pickupable Actor permits selection queries"), Defaults->GetActorEnableCollision());
		TestTrue(TEXT("World pickupable mesh is query-only"),
			Defaults->GetPickupableMesh()
			&& Defaults->GetPickupableMesh()->GetCollisionEnabled() == ECollisionEnabled::QueryOnly);
		TestTrue(TEXT("World pickupable mesh blocks Visibility"),
			Defaults->GetPickupableMesh()
			&& Defaults->GetPickupableMesh()->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);
		TestTrue(TEXT("World pickupable mesh ignores Pawns"),
			Defaults->GetPickupableMesh()
			&& Defaults->GetPickupableMesh()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore);
		TestFalse(TEXT("Pickupable mesh cannot affect Unreal navigation"),
			Defaults->GetPickupableMesh() && Defaults->GetPickupableMesh()->CanEverAffectNavigation());
		TestFalse(TEXT("GridWorld occupancy never blocks path traversal"),
			Defaults->GetOccupancyComponent() && Defaults->GetOccupancyComponent()->bBlocksWhenConsidered);
		TestTrue(TEXT("GridWorld occupancy contributes a positive navigation cost"),
			Defaults->GetOccupancyComponent() && Defaults->GetOccupancyComponent()->AdditionalCost > 0);
		TestFalse(TEXT("GridWorld occupancy is not a reservation"),
			Defaults->GetOccupancyComponent() && Defaults->GetOccupancyComponent()->bIsReservation);
		TestFalse(TEXT("Authored world collision is opt-in"),
			FParadoxInventoryTestAccessor::UsesAuthoredWorldCollision(*Defaults));
		TestFalse(TEXT("Authored navigation influence is opt-in"),
			FParadoxInventoryTestAccessor::UsesAuthoredNavigationInfluence(*Defaults));
		TestFalse(TEXT("Default pickupable does not remove GridWorld cells"),
			Defaults->GetGridNavigationModifierComponent()
				&& Defaults->GetGridNavigationModifierComponent()->bBlockCells);
		TestTrue(TEXT("World State owns pickupable baseline existence"),
			Defaults->GetWorldStateParticipantComponent()
				&& Defaults->GetWorldStateParticipantComponent()->ExistencePolicy
					== EWorldStateExistencePolicy::RespawnAndDestroy);
		break;
	}
	case 18:
	{
		const AParadoxPickupableActor* Defaults = GetDefault<AParadoxPickupableActor>();
		const USmartObjectDefinition* SmartDefinition = Defaults->GetSmartObjectComponent()->GetDefinition();
		TestNotNull(TEXT("Native pickupable resolves Smart Object asset"), SmartDefinition);
		if (SmartDefinition)
		{
			TestEqual(TEXT("Smart Object provides four cardinal slots"), SmartDefinition->GetSlots().Num(), 4);
		}
		TestEqual(TEXT("Native pickupable catalog includes Pickup and Swap"), Defaults->GetInteractionComponent()->InteractionDefinitions.Num(), 2);
		TestNotNull(TEXT("Pickup Definition resolves"), LoadObject<UParadoxPickupInteractionActionDefinition>(nullptr, TEXT("/Game/Data/GameplayActions/DA_ParadoxPickupInteraction.DA_ParadoxPickupInteraction")));
		TestNotNull(TEXT("Swap Definition resolves"), LoadObject<UParadoxSwapInteractionActionDefinition>(nullptr, TEXT("/Game/Data/GameplayActions/DA_ParadoxSwapInteraction.DA_ParadoxSwapInteraction")));
		TestTrue(TEXT("Native actor requires no Blueprint effects"), Defaults->GetPassiveEffects().IsEmpty());
		break;
	}
	case 19:
	{
		UParadoxInventoryTestPassiveEffect* Effect = Fixture.First->GetTestEffect();
		Effect->bAttemptReentrantDrop = true;
		TestTrue(TEXT("Outer pickup completes"), Inventory->TryEquip(Fixture.First).IsSuccess());
		TestTrue(TEXT("Nested transition is rejected"), Effect->ReentrantStatus == EParadoxInventoryOperationStatus::OperationInProgress);
		TestTrue(TEXT("Reentrancy cannot break ownership"), Inventory->GetEquippedItem() == Fixture.First && Fixture.First->GetCurrentHolder() == Fixture.Character);
		break;
	}
	case 20:
	{
		AGridNavigationData* NavigationData =
			Fixture.Scope.World->SpawnActor<AGridNavigationData>();
		const FGuid GridId = FGuid::NewGuid();
		FString PublishError;
		if (!TestNotNull(TEXT("Inventory interaction Grid navigation exists"), NavigationData)
			|| !TestTrue(
				*FString::Printf(
					TEXT("Inventory interaction Grid snapshot publishes: %s"),
					*PublishError),
				NavigationData->PublishSnapshot(
					MakeInteractionGridSnapshot(GridId),
					&PublishError)))
		{
			return false;
		}

		Fixture.First->SetFlags(RF_WasLoaded);
		USmartObjectSubsystem* SmartObjects =
			USmartObjectSubsystem::GetCurrent(Fixture.Scope.World);
		if (!TestNotNull(TEXT("Inventory interaction Smart Object subsystem exists"), SmartObjects))
		{
			return false;
		}
		if (!Fixture.First->GetSmartObjectComponent()->GetRegisteredHandle().IsValid())
		{
			TestTrue(
				TEXT("Pickupable Smart Object registers for the interaction test"),
				SmartObjects->RegisterSmartObject(
					Fixture.First->GetSmartObjectComponent()));
		}
		Fixture.First->GetInteractionComponent()->RefreshInteractionSources();
		UIntentReplayComponent* PlayerReplay =
			Fixture.Character->GetIntentReplayComponent();
		if (!TestNotNull(TEXT("Player Intent Replay exists"), PlayerReplay))
		{
			return false;
		}
		if (!PlayerReplay->IsIntentReplayInitialized())
		{
			TestTrue(
				TEXT("Player Intent Replay initializes"),
				PlayerReplay->InitializeIntentReplay().Succeeded());
		}
		if (!TestTrue(
			TEXT("Player pickup recording starts"),
			PlayerReplay->StartRecording(FIntentRecordingOptions()).Succeeded()))
		{
			return false;
		}

		const FParadoxInteractionRequestResult PlayerRequest =
			Fixture.First->GetInteractionComponent()->RequestInteraction(
				Fixture.Character,
				ParadoxGameplayTags::Interaction_Inventory_Pickup,
				ParadoxGameplayTags::Origin_Player,
				Fixture.Character);
		TestTrue(
			*FString::Printf(
				TEXT("Player Pickup passes preflight with its Character context: %s"),
				*PlayerRequest.DiagnosticMessage),
			PlayerRequest.IsAccepted());
		TestTrue(
			TEXT("Player Pickup equips the target on the submitting Character"),
			Fixture.Inventory()->GetEquippedItem() == Fixture.First
				&& Fixture.First->GetCurrentHolder() == Fixture.Character);
		TestTrue(
			TEXT("Player pickup recording finalizes"),
			PlayerReplay->RequestStopRecording(
				EIntentRecordingFinalizeMode::Immediate).Succeeded());
		UIntentReplayTrack* Track = PlayerReplay->GetLastFinalizedTrack();
		if (!TestNotNull(TEXT("Player Pickup track exists"), Track))
		{
			return false;
		}

		const FTransform OriginalWorldTransform(FVector(100.0, 0.0, 0.0));
		TestTrue(
			TEXT("Source Player returns the pickupable to the world for clone replay"),
			Fixture.Inventory()->TryDropAtTransform(OriginalWorldTransform).IsSuccess());
		if (!Fixture.First->GetSmartObjectComponent()->GetRegisteredHandle().IsValid())
		{
			TestTrue(
				TEXT("Dropped pickupable Smart Object re-registers for replay"),
				SmartObjects->RegisterSmartObject(
					Fixture.First->GetSmartObjectComponent()));
		}
		Fixture.First->GetInteractionComponent()->RefreshInteractionSources();

		FActorSpawnParameters CloneParameters;
		CloneParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AParadoxCloneCharacter* Clone =
			Fixture.Scope.World->SpawnActor<AParadoxCloneCharacter>(
				AParadoxCloneCharacter::StaticClass(),
				FTransform::Identity,
				CloneParameters);
		if (!TestNotNull(TEXT("Replay clone exists"), Clone))
		{
			return false;
		}
		UIntentReplayComponent* CloneReplay = Clone->GetIntentReplayComponent();
		if (!TestNotNull(TEXT("Replay clone Intent Replay exists"), CloneReplay))
		{
			return false;
		}
		if (!CloneReplay->IsIntentReplayInitialized())
		{
			TestTrue(
				TEXT("Replay clone Intent Replay initializes"),
				CloneReplay->InitializeIntentReplay().Succeeded());
		}
		const FIntentReplayPrepareResult Prepared = CloneReplay->PrepareReplay(
			Track,
			FIntentReplayPlaybackOptions());
		TestEqual(
			TEXT("Pickup prepares on the recipient clone"),
			Prepared.Status,
			EIntentReplayPrepareStatus::Ready);
		TestTrue(
			TEXT("Recipient clone starts Pickup replay"),
			CloneReplay->StartReplay().Succeeded());
		TestTrue(
			TEXT("Replay Pickup uses the clone inventory, not the recorded Player or request source"),
			Clone->GetInventoryComponent()->GetEquippedItem() == Fixture.First
				&& Fixture.First->GetCurrentHolder() == Clone
				&& !Fixture.Inventory()->HasItem());
		TestEqual(
			TEXT("Clone Pickup replay completes"),
			CloneReplay->GetPlaybackState(),
			EIntentReplayPlaybackState::Completed);
		break;
	}
	case 21:
	{
		AGridNavigationData* NavigationData =
			Fixture.Scope.World->SpawnActor<AGridNavigationData>();
		const FGuid GridId = FGuid::NewGuid();
		FString PublishError;
		if (!TestNotNull(TEXT("Swap interaction Grid navigation exists"), NavigationData)
			|| !TestTrue(
				TEXT("Swap interaction Grid snapshot publishes"),
				NavigationData->PublishSnapshot(
					MakeInteractionGridSnapshot(GridId),
					&PublishError)))
		{
			return false;
		}

		Fixture.Second->SetFlags(RF_WasLoaded);
		USmartObjectSubsystem* SmartObjects =
			USmartObjectSubsystem::GetCurrent(Fixture.Scope.World);
		if (!TestNotNull(TEXT("Swap Smart Object subsystem exists"), SmartObjects))
		{
			return false;
		}
		if (!Fixture.Second->GetSmartObjectComponent()->GetRegisteredHandle().IsValid())
		{
			TestTrue(
				TEXT("Swap target Smart Object registers"),
				SmartObjects->RegisterSmartObject(
					Fixture.Second->GetSmartObjectComponent()));
		}
		Fixture.Second->GetInteractionComponent()->RefreshInteractionSources();
		TestTrue(
			TEXT("Player holds an outgoing item before Swap"),
			Fixture.Inventory()->TryEquip(Fixture.First).IsSuccess());
		Fixture.Character->SetActorLocation(FVector(200.0, 200.0, 0.0));

		const FParadoxInteractionRequestResult SwapRequest =
			Fixture.Second->GetInteractionComponent()->RequestInteraction(
				Fixture.Character,
				ParadoxGameplayTags::Interaction_Inventory_Swap,
				ParadoxGameplayTags::Origin_Player,
				Fixture.Character);
		TestTrue(
			*FString::Printf(
				TEXT("Swap resolves the Character and target during preflight: %s"),
				*SwapRequest.DiagnosticMessage),
			SwapRequest.IsAccepted());
		TestTrue(
			TEXT("Swap commits to the submitting Character inventory"),
			Fixture.Inventory()->GetEquippedItem() == Fixture.Second
				&& Fixture.Second->GetCurrentHolder() == Fixture.Character
				&& Fixture.First->GetCurrentHolder() == nullptr);
		break;
	}
	case 22:
	{
		TestTrue(
			TEXT("Pickup before interaction-widget regression succeeds"),
			Inventory->TryEquip(Fixture.First).IsSuccess());
		TestTrue(
			TEXT("Drop before interaction-widget regression succeeds"),
			Inventory->TryDropAtTransform(FTransform(FVector(200.0, 0.0, 0.0))).IsSuccess());

		UParadoxSelectableComponent* Selectable = Fixture.First->GetSelectableComponent();
		if (!TestNotNull(TEXT("Dropped pickupable remains selectable"), Selectable))
		{
			return false;
		}
		Selectable->SelectionWidgetClass = UParadoxSelectionTestWidget::StaticClass();

		FActorSpawnParameters ControllerParameters;
		ControllerParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AParadoxSelectionTestController* SelectionController =
			Fixture.Scope.World->SpawnActor<AParadoxSelectionTestController>(
				AParadoxSelectionTestController::StaticClass(),
				FTransform::Identity,
				ControllerParameters);
		if (!TestNotNull(TEXT("Selection controller exists"), SelectionController))
		{
			return false;
		}

		const FHitResult SelectionHit(
			Fixture.First,
			Fixture.First->GetPickupableMesh(),
			Fixture.First->GetActorLocation(),
			FVector::UpVector);
		TestTrue(
			TEXT("Dropped pickupable can be selected again"),
			SelectionController->Selection->HandleSelectionPointerHit(SelectionHit, true));
		UWidgetComponent* InteractionWidget = Selectable->GetInteractionWidget();
		if (!TestNotNull(TEXT("Selection recreates the interaction widget"), InteractionWidget))
		{
			return false;
		}
		TestEqual(
			TEXT("Shown interaction widget starts query-only"),
			InteractionWidget->GetCollisionEnabled(),
			ECollisionEnabled::QueryOnly);

		FParadoxInventoryTestAccessor::EnforceNonBlockingPresence(*Fixture.First);
		TestEqual(
			TEXT("Pickupable normalization preserves interaction-widget hit testing"),
			InteractionWidget->GetCollisionEnabled(),
			ECollisionEnabled::QueryOnly);
		TestEqual(
			TEXT("Interaction widget remains visible to the cursor trace"),
			InteractionWidget->GetCollisionResponseToChannel(ECC_Visibility),
			ECR_Block);
		TestEqual(
			TEXT("Interaction widget never blocks Pawns"),
			InteractionWidget->GetCollisionResponseToChannel(ECC_Pawn),
			ECR_Ignore);
		TestFalse(
			TEXT("Interaction widget never emits overlaps"),
			InteractionWidget->GetGenerateOverlapEvents());
		TestFalse(
			TEXT("Interaction widget cannot affect navigation"),
			InteractionWidget->CanEverAffectNavigation());
		break;
	}
	case 23:
	{
		AParadoxInventoryTestPickupable* AuthoredPickupable =
			Fixture.Scope.World->SpawnActorDeferred<AParadoxInventoryTestPickupable>(
				AParadoxInventoryTestPickupable::StaticClass(),
				FTransform(FVector(500.0, 0.0, 0.0)),
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!TestNotNull(TEXT("Authored-presence pickupable spawns deferred"), AuthoredPickupable))
		{
			return false;
		}

		FParadoxInventoryTestAccessor::ConfigureAuthoredWorldPresence(
			*AuthoredPickupable,
			true,
			true);
		UStaticMeshComponent* Mesh = AuthoredPickupable->GetPickupableMesh();
		UGridNavigationOccupancyComponent* Occupancy =
			AuthoredPickupable->GetOccupancyComponent();
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Occupancy->bBlocksWhenConsidered = true;
		Occupancy->AdditionalCost = 250;
		Occupancy->bIsReservation = false;
		AuthoredPickupable->FinishSpawning(
			FTransform(FVector(500.0, 0.0, 0.0)));
		if (!AuthoredPickupable->HasActorBegunPlay())
		{
			AuthoredPickupable->DispatchBeginPlay();
		}

		TestEqual(TEXT("World state preserves authored collision mode"),
			Mesh->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
		TestEqual(TEXT("World state preserves authored Pawn response"),
			Mesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
		TestTrue(TEXT("World collision opt-in enables overlaps"),
			Mesh->GetGenerateOverlapEvents());
		TestTrue(TEXT("World navigation opt-in enables Unreal navigation relevance"),
			Mesh->CanEverAffectNavigation());
		TestTrue(TEXT("World state preserves authored GridWorld blocking"),
			Occupancy->bBlocksWhenConsidered);
		TestTrue(TEXT("World navigation opt-in removes affected GridWorld cells"),
			AuthoredPickupable->GetGridNavigationModifierComponent()
				&& AuthoredPickupable->GetGridNavigationModifierComponent()->bBlockCells);
		TestTrue(TEXT("GridWorld modifier mirrors the authored occupancy extent"),
			AuthoredPickupable->GetGridNavigationModifierComponent()
				&& AuthoredPickupable->GetGridNavigationModifierComponent()->BoxExtent.Equals(
					Occupancy->BoxExtent));

		TestTrue(TEXT("Authored-presence pickup succeeds"),
			Inventory->TryEquip(AuthoredPickupable).IsSuccess());
		TestEqual(TEXT("Held item still disables collision"),
			Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("Held item still disables navigation relevance"),
			Mesh->CanEverAffectNavigation());
		TestFalse(TEXT("Held item still disables GridWorld occupancy"),
			Occupancy->IsActive());
		TestFalse(TEXT("Held item restores its GridWorld cells"),
			AuthoredPickupable->GetGridNavigationModifierComponent()
				&& AuthoredPickupable->GetGridNavigationModifierComponent()->bBlockCells);

		TestTrue(TEXT("Authored-presence drop succeeds"),
			Inventory->TryDropAtTransform(
				FTransform(FVector(600.0, 0.0, 0.0))).IsSuccess());
		TestEqual(TEXT("Drop restores authored collision mode"),
			Mesh->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
		TestEqual(TEXT("Drop restores authored Pawn response"),
			Mesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
		TestTrue(TEXT("Drop re-enables world overlaps"),
			Mesh->GetGenerateOverlapEvents());
		TestTrue(TEXT("Drop restores authored navigation relevance"),
			Mesh->CanEverAffectNavigation());
		TestTrue(TEXT("Drop restores authored blocking occupancy"),
			Occupancy->IsActive() && Occupancy->bBlocksWhenConsidered);
		TestTrue(TEXT("Drop removes the affected GridWorld cells again"),
			AuthoredPickupable->GetGridNavigationModifierComponent()
				&& AuthoredPickupable->GetGridNavigationModifierComponent()->bBlockCells);
		break;
	}
	case 24:
	{
		UClass* KeyCardClass = LoadClass<AParadoxPickupableActor>(
			nullptr,
			TEXT("/Game/Environment/SpaceShip/Blueprints/BP_KeyCard.BP_KeyCard_C"));
		if (!TestNotNull(TEXT("BP_KeyCard class resolves"), KeyCardClass))
		{
			return false;
		}
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AParadoxPickupableActor* KeyCard = Fixture.Scope.World->SpawnActor<AParadoxPickupableActor>(
			KeyCardClass,
			FTransform(FVector(700.0, 0.0, 0.0)),
			SpawnParameters);
		if (!TestNotNull(TEXT("BP_KeyCard runtime instance spawns"), KeyCard))
		{
			return false;
		}

		TestFalse(TEXT("BP_KeyCard does not opt into authored world collision"),
			FParadoxInventoryTestAccessor::UsesAuthoredWorldCollision(*KeyCard));
		TestFalse(TEXT("BP_KeyCard does not opt into navigation blocking"),
			FParadoxInventoryTestAccessor::UsesAuthoredNavigationInfluence(*KeyCard));
		TestFalse(TEXT("BP_KeyCard publishes no dynamic-agent occupancy"),
			KeyCard->GetOccupancyComponent() && KeyCard->GetOccupancyComponent()->IsActive());
		TestFalse(TEXT("BP_KeyCard does not remove GridWorld cells"),
			KeyCard->GetGridNavigationModifierComponent()
				&& KeyCard->GetGridNavigationModifierComponent()->bBlockCells);
		TestEqual(TEXT("BP_KeyCard ignores Pawn collision"),
			KeyCard->GetPickupableMesh()->GetCollisionResponseToChannel(ECC_Pawn),
			ECR_Ignore);
		TestFalse(TEXT("BP_KeyCard mesh cannot affect Unreal navigation"),
			KeyCard->GetPickupableMesh()->CanEverAffectNavigation());
		break;
	}
	default:
		AddError(FString::Printf(TEXT("Unknown inventory scenario command '%s'."), *Parameters));
		return false;
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
