#include "Misc/AutomationTest.h"
#include "GameplayActionTags.h"
#include "Paradox.h"
#include "ParadoxInteractionTestTypes.h"

namespace ParadoxInteractionTestTags
{
	UE_DEFINE_GAMEPLAY_TAG(Primary, "Interaction.Test.Catalog.Primary");
	UE_DEFINE_GAMEPLAY_TAG(Secondary, "Interaction.Test.Catalog.Secondary");
	UE_DEFINE_GAMEPLAY_TAG(Rejected, "Interaction.Test.Catalog.Rejected");
}

int32 UParadoxInteractionTestSuccessAction::ExecutionCount = 0;
int32 UParadoxInteractionTestSuccessAction::ClaimedExecutionCount = 0;
TWeakObjectPtr<AActor> UParadoxInteractionTestPreflightContextAction::LastRequester;
TWeakObjectPtr<AActor> UParadoxInteractionTestPreflightContextAction::LastTarget;
FGameplayTag UParadoxInteractionTestPreflightContextAction::LastOrigin;
int32 UParadoxInteractionTestPreflightContextAction::ValidationCount = 0;
int32 UParadoxInteractionTestPreflightContextAction::ExecutionCount = 0;

void UParadoxInteractionTestHoldAction::ExecuteInteraction_Implementation()
{
}

void UParadoxInteractionTestHoldAction::CompleteSuccessForTest()
{
	CompleteInteractionSuccess(
		GameplayActionTags::Result_Success,
		TEXT("Test interaction completed successfully."));
}

void UParadoxInteractionTestHoldAction::CompleteFailureForTest()
{
	CompleteInteractionFailure(
		ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest,
		TEXT("Test interaction failed explicitly."));
}

void UParadoxInteractionTestSuccessAction::ResetObservations()
{
	ExecutionCount = 0;
	ClaimedExecutionCount = 0;
}

void UParadoxInteractionTestSuccessAction::ExecuteInteraction_Implementation()
{
	++ExecutionCount;
	if (HasInteractionClaim())
	{
		++ClaimedExecutionCount;
	}
	CompleteInteractionSuccess(
		GameplayActionTags::Result_Success,
		TEXT("Immediate test interaction succeeded."));
}

void UParadoxInteractionTestPreflightContextAction::ResetObservations()
{
	LastRequester.Reset();
	LastTarget.Reset();
	LastOrigin = FGameplayTag();
	ValidationCount = 0;
	ExecutionCount = 0;
}

bool UParadoxInteractionTestPreflightContextAction::
CanSatisfyInteractionPreconditions_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	LastRequester = GetInteractionRequester();
	LastTarget = GetInteractionTarget();
	LastOrigin = GetOriginTag();
	++ValidationCount;
	if (!LastRequester.IsValid() || !LastTarget.IsValid())
	{
		OutFailureReason =
			ParadoxGameplayTags::Result_Failure_Interaction_InvalidRequest;
		OutDiagnostic = TEXT(
			"The interaction context was unavailable during action preflight.");
		return false;
	}
	return true;
}

void UParadoxInteractionTestPreflightContextAction::ExecuteInteraction_Implementation()
{
	++ExecutionCount;
	CompleteInteractionSuccess(
		GameplayActionTags::Result_Success,
		TEXT("Preflight context test interaction succeeded."));
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Actions/GameplayActionDefinition.h"
#include "Characters/ParadoxCharacter.h"
#include "Characters/ParadoxCloneCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "Controllers/PuzzleController.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxInteractionActionDefinition.h"
#include "Interaction/ParadoxEmitterInteractionAction.h"
#include "Interaction/ParadoxReceiverInteractionAction.h"
#include "IntentReplayTags.h"
#include "Journal/IntentExecutionJournal.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridTrafficReservation.h"
#include "ParadoxSelectionTestTypes.h"
#include "Presentation/GridCellVisualStyle.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "Components/IntentReplayComponent.h"
#include "Playback/IntentReplayPlaybackSession.h"
#include "Recording/IntentReplayTrack.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectSubsystem.h"
#include "StructUtils/PropertyBag.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace UE::Paradox::Interaction::Tests
{
	struct FScopedInteractionWorld
	{
		explicit FScopedInteractionWorld(
			const EWorldType::Type WorldType = EWorldType::Game)
		{
			Context = GEngine
				? &GEngine->CreateNewWorldContext(WorldType)
				: nullptr;
			World = UWorld::CreateWorld(
				WorldType,
				false,
				TEXT("ParadoxInteractionTestWorld"));
			if (World)
			{
				World->AddToRoot();
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		~FScopedInteractionWorld()
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

	TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> MakeGridSnapshot(
		const FGuid& GridId,
		const int64 TopologyRevision,
		const FGuid& OccupancyOwner = FGuid(),
		const FGuid& ReservationOwner = FGuid(),
		const int32 AffectedCellIndex = INDEX_NONE)
	{
		TSharedRef<FGridWorldSnapshot, ESPMode::ThreadSafe> Snapshot =
			MakeShared<FGridWorldSnapshot, ESPMode::ThreadSafe>();
		Snapshot->GridId = GridId;
		Snapshot->Revisions.Topology = TopologyRevision;
		Snapshot->Revisions.Traversal = 1;
		Snapshot->Revisions.Occupancy = TopologyRevision;
		FGridRegionData& Region = Snapshot->Regions.Add(GridId);
		Region.GridId = GridId;
		Region.GridTransform.CellSize = FVector(100.0, 100.0, 50.0);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FGridCellData& Cell = Snapshot->Cells.AddDefaulted_GetRef();
			Cell.Id.GridId = GridId;
			Cell.Id.Coord = FGridCellCoord(Index, 0, 0);
			Cell.WorldCenter = Region.GridTransform.CellToWorld(Cell.Id.Coord);
			Cell.bWalkable = true;
			if (Index == AffectedCellIndex)
			{
				if (OccupancyOwner.IsValid())
				{
					Cell.bOccupied = true;
					Cell.OccupancyOwners.Add(OccupancyOwner);
				}
				if (ReservationOwner.IsValid())
				{
					Cell.ReservationOwners.Add(ReservationOwner);
				}
			}
		}
		return Snapshot;
	}

	AActor* SpawnActor(UWorld& World, const TCHAR* Name)
	{
		FActorSpawnParameters Parameters;
		Parameters.Name = Name;
		Parameters.NameMode =
			FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
	}

	struct FInteractionExecutionFixture
	{
		explicit FInteractionExecutionFixture(
			const EWorldType::Type WorldType = EWorldType::Game)
			: Scope(WorldType)
		{
		}

		bool Initialize(
			const TSubclassOf<UParadoxInteractionActionBase> ActionClass,
			const bool bReplaySafeTarget = true,
			const bool bValidParameterSchema = true,
			const bool bEnableReplay = false,
			const TSubclassOf<APawn> RequesterClass = APawn::StaticClass())
		{
			NavigationData = Scope.World
				? Scope.World->SpawnActor<AGridNavigationData>()
				: nullptr;
			GridId = FGuid::NewGuid();
			FString PublishError;
			if (!NavigationData
				|| !NavigationData->PublishSnapshot(
					MakeGridSnapshot(GridId, 1),
					&PublishError))
			{
				return false;
			}

			Target = SpawnActor(*Scope.World, TEXT("ExecutionTarget"));
			FActorSpawnParameters RequesterParameters;
			RequesterParameters.Name = TEXT("ExecutionRequester");
			RequesterParameters.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Requester = Scope.World->SpawnActor<APawn>(
				RequesterClass,
				FTransform::Identity,
				RequesterParameters);
			SelectionController = Scope.World->SpawnActor<AParadoxSelectionTestController>();
			if (!Target || !Requester || !SelectionController)
			{
				return false;
			}
			SelectionController->Possess(Requester);
			if (bReplaySafeTarget)
			{
				Target->SetFlags(RF_WasLoaded);
			}
			if (!Requester->GetRootComponent())
			{
				USceneComponent* RequesterRoot =
					NewObject<USceneComponent>(Requester, TEXT("ExecutionRequesterRoot"));
				Requester->AddInstanceComponent(RequesterRoot);
				Requester->SetRootComponent(RequesterRoot);
				RequesterRoot->SetMobility(EComponentMobility::Movable);
				RequesterRoot->RegisterComponent();
			}

			TargetRoot =
				NewObject<USceneComponent>(Target, TEXT("ExecutionRoot"));
			Target->AddInstanceComponent(TargetRoot);
			Target->SetRootComponent(TargetRoot);
			TargetRoot->SetMobility(EComponentMobility::Movable);
			TargetRoot->RegisterComponent();

			Selectable = NewObject<UParadoxSelectableComponent>(
				Target,
				TEXT("ExecutionSelectable"));
			Target->AddInstanceComponent(Selectable);
			Selectable->RegisterComponent();

			SmartObjectDefinition = NewObject<USmartObjectDefinition>(
				Target,
				TEXT("ExecutionSmartObjectDefinition"));
			FSmartObjectSlotDefinition& Slot = SmartObjectDefinition->DebugAddSlot();
			Slot.ID = FGuid::NewGuid();
			Slot.Offset = FVector3f(50.0f, 0.0f, 0.0f);
			Slot.BehaviorDefinitions.Add(
				NewObject<UParadoxInteractionTestBehaviorDefinition>(
					SmartObjectDefinition));
			if (!SmartObjectDefinition->Validate())
			{
				return false;
			}

			SmartObject = NewObject<USmartObjectComponent>(Target, TEXT("ExecutionSmartObject"));
			Target->AddInstanceComponent(SmartObject);
			SmartObject->SetupAttachment(TargetRoot);
			SmartObject->SetDefinition(SmartObjectDefinition);
			SmartObject->RegisterComponent();

			ActionDefinition = NewObject<UGameplayActionDefinition>(
				Target,
				TEXT("ExecutionActionDefinition"));
			ActionDefinition->InstanceClass = ActionClass;
			ActionDefinition->ActionTag = ParadoxGameplayTags::Action_InvestigationInspect;
			ActionDefinition->JournalRequirement =
				EGameplayActionJournalRequirement::Optional;
			TArray<FPropertyBagPropertyDesc> ParameterDescs;
			ParameterDescs.Add({
				GET_MEMBER_NAME_CHECKED(FParadoxInteractionActionParameters, Target),
				EPropertyBagPropertyType::SoftObject,
				AActor::StaticClass()});
			if (bValidParameterSchema)
			{
				ParameterDescs.Add({
					GET_MEMBER_NAME_CHECKED(
						FParadoxInteractionActionParameters,
						InteractionTag),
					EPropertyBagPropertyType::Struct,
					FGameplayTag::StaticStruct()});
			}
			ActionDefinition->DefaultParameters.InitializeFromBagStruct(
				UPropertyBag::GetOrCreateFromDescs(ParameterDescs));

			Interaction = NewObject<UParadoxInteractionComponent>(
				Target,
				TEXT("ExecutionInteraction"));
			Target->AddInstanceComponent(Interaction);
			FParadoxInteractionDefinition& CatalogEntry =
				Interaction->InteractionDefinitions.AddDefaulted_GetRef();
			CatalogEntry.InteractionTag =
				ParadoxInteractionTestTags::Primary;
			CatalogEntry.GameplayActionDefinition = ActionDefinition;
			Interaction->RegisterComponent();

			Actions = Requester->FindComponentByClass<UGameplayActionComponent>();
			if (!Actions)
			{
				Actions = NewObject<UGameplayActionComponent>(
					Requester,
					TEXT("ExecutionActions"));
				Requester->AddInstanceComponent(Actions);
				Actions->RegisterComponent();
			}
			if (bEnableReplay)
			{
				Replay = Requester->FindComponentByClass<UIntentReplayComponent>();
				if (!Replay)
				{
					Replay = NewObject<UIntentReplayComponent>(
						Requester,
						TEXT("ExecutionReplay"));
					Requester->AddInstanceComponent(Replay);
					Replay->RegisterComponent();
				}
			}

			Scope.StartPlay();
			USmartObjectSubsystem* SmartObjects =
				USmartObjectSubsystem::GetCurrent(Scope.World);
			if (!SmartObjects)
			{
				return false;
			}
			if (!SmartObject->GetRegisteredHandle().IsValid()
				&& !SmartObjects->RegisterSmartObject(SmartObject))
			{
				return false;
			}
			Interaction->RefreshInteractionSources();
			SmartObjects->GetAllSlots(SmartObject->GetRegisteredHandle(), Slots);
			Slots.Sort();
			return Slots.Num() == 1;
		}

		FParadoxInteractionRequestResult Request()
		{
			return Interaction->RequestInteraction(
				Requester,
				ParadoxInteractionTestTags::Primary,
				ParadoxGameplayTags::Origin_Player,
				Requester);
		}

		FScopedInteractionWorld Scope;
		FGuid GridId;
		AGridNavigationData* NavigationData = nullptr;
		AActor* Target = nullptr;
		APawn* Requester = nullptr;
		AParadoxSelectionTestController* SelectionController = nullptr;
		USceneComponent* TargetRoot = nullptr;
		UParadoxSelectableComponent* Selectable = nullptr;
		USmartObjectDefinition* SmartObjectDefinition = nullptr;
		USmartObjectComponent* SmartObject = nullptr;
		UGameplayActionDefinition* ActionDefinition = nullptr;
		UParadoxInteractionComponent* Interaction = nullptr;
		UGameplayActionComponent* Actions = nullptr;
		UIntentReplayComponent* Replay = nullptr;
		TArray<FSmartObjectSlotHandle> Slots;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInteractionMultiSlotCatalogTest,
	"Paradox.Interaction.MultiSlotCatalogAvailability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInteractionMultiSlotCatalogTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Interaction::Tests;
	FScopedInteractionWorld Scope;
	if (!TestNotNull(TEXT("Interaction test world"), Scope.World))
	{
		return false;
	}

	AGridNavigationData* NavigationData =
		Scope.World->SpawnActor<AGridNavigationData>();
	const FGuid GridId = FGuid::NewGuid();
	FString PublishError;
	TestTrue(TEXT("Base interaction grid publishes"),
		NavigationData->PublishSnapshot(MakeGridSnapshot(GridId, 1), &PublishError));

	AActor* Target = SpawnActor(*Scope.World, TEXT("InteractionTarget"));
	AActor* Requester = SpawnActor(*Scope.World, TEXT("InteractionRequester"));
	FActorSpawnParameters OtherRequesterParameters;
	OtherRequesterParameters.Name = TEXT("OtherRequester");
	OtherRequesterParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* OtherRequester = Scope.World->SpawnActor<APawn>(
		APawn::StaticClass(),
		FTransform::Identity,
		OtherRequesterParameters);
	if (!TestNotNull(TEXT("Target Actor"), Target)
		|| !TestNotNull(TEXT("Requester Actor"), Requester)
		|| !TestNotNull(TEXT("Other requester Actor"), OtherRequester))
	{
		return false;
	}

	USceneComponent* Root = NewObject<USceneComponent>(Target, TEXT("InteractionRoot"));
	Target->AddInstanceComponent(Root);
	Target->SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);
	Root->RegisterComponent();

	USmartObjectDefinition* SmartObjectDefinition =
		NewObject<USmartObjectDefinition>(Target, TEXT("InteractionSmartObjectDefinition"));
	FSmartObjectSlotDefinition& FirstSlot = SmartObjectDefinition->DebugAddSlot();
	FirstSlot.ID = FGuid::NewGuid();
	FirstSlot.Offset = FVector3f(50.0f, 0.0f, 0.0f);
	FirstSlot.BehaviorDefinitions.Add(
		NewObject<UParadoxInteractionTestBehaviorDefinition>(SmartObjectDefinition));
	FSmartObjectSlotDefinition& SecondSlot = SmartObjectDefinition->DebugAddSlot();
	SecondSlot.ID = FGuid::NewGuid();
	SecondSlot.Offset = FVector3f(150.0f, 0.0f, 0.0f);
	SecondSlot.BehaviorDefinitions.Add(
		NewObject<UParadoxInteractionTestBehaviorDefinition>(SmartObjectDefinition));
	const FGameplayTag ActivityTag = ParadoxGameplayTags::Puzzle_Signal_Pressed.GetTag();
	SmartObjectDefinition->SetActivityTags(FGameplayTagContainer(ActivityTag));
	TestTrue(TEXT("Smart Object Definition validates without a Paradox production behavior type"),
		SmartObjectDefinition->Validate());

	USmartObjectComponent* SmartObject =
		NewObject<USmartObjectComponent>(Target, TEXT("SmartObject"));
	Target->AddInstanceComponent(SmartObject);
	SmartObject->SetupAttachment(Root);
	SmartObject->SetMobility(EComponentMobility::Movable);
	SmartObject->SetDefinition(SmartObjectDefinition);
	SmartObject->RegisterComponent();

	UParadoxInteractionComponent* Interaction =
		NewObject<UParadoxInteractionComponent>(Target, TEXT("Interaction"));
	Target->AddInstanceComponent(Interaction);
	FParadoxInteractionDefinition& FirstDefinition =
		Interaction->InteractionDefinitions.AddDefaulted_GetRef();
	FirstDefinition.InteractionTag = ParadoxInteractionTestTags::Primary;
	FirstDefinition.SlotActivityRequirements =
		FGameplayTagQuery::MakeQuery_MatchTag(ActivityTag);
	FParadoxInteractionDefinition& SecondDefinition =
		Interaction->InteractionDefinitions.AddDefaulted_GetRef();
	SecondDefinition.InteractionTag = ParadoxInteractionTestTags::Secondary;
	SecondDefinition.SlotActivityRequirements =
		FGameplayTagQuery::MakeQuery_MatchTag(ActivityTag);
	FParadoxInteractionDefinition& RejectedDefinition =
		Interaction->InteractionDefinitions.AddDefaulted_GetRef();
	RejectedDefinition.InteractionTag = ParadoxInteractionTestTags::Rejected;
	RejectedDefinition.SlotActivityRequirements = FGameplayTagQuery::MakeQuery_MatchTag(
		ParadoxGameplayTags::State_Barrier_Open);
	Interaction->RegisterComponent();

	UGridNavigationOccupancyComponent* RequesterOccupancy =
		NewObject<UGridNavigationOccupancyComponent>(Requester, TEXT("RequesterOccupancy"));
	Requester->AddInstanceComponent(RequesterOccupancy);
	RequesterOccupancy->RegisterComponent();
	RequesterOccupancy->Activate(true);
	TestTrue(TEXT("Requester occupancy has a stable identity"),
		RequesterOccupancy->OccupantId.IsValid());

	Scope.StartPlay();
	USmartObjectSubsystem* SmartObjects =
		USmartObjectSubsystem::GetCurrent(Scope.World);
	if (!TestNotNull(TEXT("Smart Object Subsystem"), SmartObjects))
	{
		return false;
	}
	if (!SmartObject->GetRegisteredHandle().IsValid())
	{
		TestTrue(TEXT("Smart Object registers explicitly when component order requires it"),
			SmartObjects->RegisterSmartObject(SmartObject));
	}
	Interaction->RefreshInteractionSources();
	TestTrue(TEXT("Smart Object has a registered handle"),
		SmartObject->GetRegisteredHandle().IsValid());

	FParadoxInteractionQueryResult Result =
		Interaction->QueryInteractionOptions(Requester);
	TestEqual(TEXT("Two definitions across two slots produce four options"),
		Result.Options.Num(), 4);
	if (Result.Options.Num() != 4)
	{
		AddError(FString::Printf(
			TEXT("Interaction query diagnostic: %s"),
			*Result.DiagnosticMessage));
		return false;
	}
	TestTrue(TEXT("Every base option is free"),
		Result.Options.FilterByPredicate(
			[](const FParadoxInteractionOption& Option)
			{
				return Option.State == EParadoxInteractionOptionState::Free;
			}).Num() == 4);
	TestEqual(TEXT("One slot can expose multiple interactions"),
		Result.Options.FilterByPredicate(
			[&Result](const FParadoxInteractionOption& Option)
			{
				return Option.SlotHandle == Result.Options[0].SlotHandle;
			}).Num(),
		2);

	const FParadoxInteractionQueryResult Tagged =
		Interaction->QueryInteractionOptionsByTag(
			Requester,
			ParadoxInteractionTestTags::Secondary);
	TestEqual(TEXT("Tag filtering retains one option per matching slot"),
		Tagged.Options.Num(), 2);
	TestTrue(TEXT("Activity Tag query rejects the mismatched catalog entry"),
		!Result.Options.ContainsByPredicate(
			[](const FParadoxInteractionOption& Option)
			{
				return Option.InteractionTag
					== ParadoxInteractionTestTags::Rejected;
			}));

	TArray<FSmartObjectSlotHandle> Slots;
	SmartObjects->GetAllSlots(SmartObject->GetRegisteredHandle(), Slots);
	Slots.Sort();
	if (!TestEqual(TEXT("Registered Smart Object exposes both slots"), Slots.Num(), 2))
	{
		return false;
	}
	const FSmartObjectClaimHandle OtherClaim = SmartObjects->MarkSlotAsClaimed(
		Slots[0],
		ESmartObjectClaimPriority::Normal,
		FConstStructView::Make(FSmartObjectActorUserData(OtherRequester)));
	TestTrue(TEXT("Test acquires an external Smart Object claim"), OtherClaim.IsValid());
	Result = Interaction->QueryInteractionOptions(Requester);
	TestEqual(TEXT("External claim occupies both options on its slot"),
		Result.Options.FilterByPredicate(
			[&Slots](const FParadoxInteractionOption& Option)
			{
				return Option.SlotHandle == Slots[0]
					&& Option.State == EParadoxInteractionOptionState::Occupied;
			}).Num(),
		2);
	TestTrue(TEXT("External Smart Object claim releases"),
		SmartObjects->MarkSlotAsFree(OtherClaim));

	const FSmartObjectClaimHandle SelfClaim = SmartObjects->MarkSlotAsClaimed(
		Slots[0],
		ESmartObjectClaimPriority::Normal,
		FConstStructView::Make(FSmartObjectActorUserData(Requester)));
	TestTrue(TEXT("Test acquires a requester-owned Smart Object claim"), SelfClaim.IsValid());
	Result = Interaction->QueryInteractionOptions(Requester);
	TestEqual(TEXT("Requester-owned Smart Object claim is ignored"),
		Result.Options.FilterByPredicate(
			[&Slots](const FParadoxInteractionOption& Option)
			{
				return Option.SlotHandle == Slots[0]
					&& Option.State == EParadoxInteractionOptionState::Free;
			}).Num(),
		2);
	SmartObjects->MarkSlotAsFree(SelfClaim);

	TestTrue(TEXT("Other GridWorld occupancy publishes"),
		NavigationData->PublishSnapshot(
			MakeGridSnapshot(GridId, 2, FGuid::NewGuid(), FGuid(), 0),
			&PublishError));
	Result = Interaction->QueryInteractionOptions(Requester);
	TestEqual(TEXT("Other GridWorld occupancy marks both options on its cell occupied"),
		Result.Options.FilterByPredicate(
			[](const FParadoxInteractionOption& Option)
			{
				return Option.GridCellId.Coord.X == 0
					&& Option.State == EParadoxInteractionOptionState::Occupied;
			}).Num(),
		2);

	TestTrue(TEXT("Requester GridWorld occupancy and reservation publish"),
		NavigationData->PublishSnapshot(
			MakeGridSnapshot(
				GridId,
				3,
				RequesterOccupancy->OccupantId,
				RequesterOccupancy->OccupantId,
				0),
			&PublishError));
	Result = Interaction->QueryInteractionOptions(Requester);
	TestEqual(TEXT("Requester occupancy and reservation are ignored"),
		Result.Options.FilterByPredicate(
			[](const FParadoxInteractionOption& Option)
			{
				return Option.GridCellId.Coord.X == 0
					&& Option.State == EParadoxInteractionOptionState::Free;
			}).Num(),
		2);

	TestTrue(TEXT("Other GridWorld reservation publishes"),
		NavigationData->PublishSnapshot(
			MakeGridSnapshot(GridId, 4, FGuid(), FGuid::NewGuid(), 0),
			&PublishError));
	Result = Interaction->QueryInteractionOptions(Requester);
	TestEqual(TEXT("Other GridWorld reservation marks the cell occupied"),
		Result.Options.FilterByPredicate(
			[](const FParadoxInteractionOption& Option)
			{
				return Option.GridCellId.Coord.X == 0
					&& Option.State == EParadoxInteractionOptionState::Occupied;
			}).Num(),
		2);

	TestTrue(TEXT("Clean GridWorld republishes before traffic claim"),
		NavigationData->PublishSnapshot(MakeGridSnapshot(GridId, 5), &PublishError));
	UObject* TrafficClaimant = Interaction;
	FGridTrafficGoalClaimRequest TrafficRequest;
	TrafficRequest.OwnerId = FGuid::NewGuid();
	TrafficRequest.Claimant = TrafficClaimant;
	TrafficRequest.Pawn = CastChecked<APawn>(OtherRequester);
	TrafficRequest.GoalCell = {
		NavigationData->GetSnapshot()->Cells[0].Id,
		NavigationData->GetSnapshot()->Cells[0].WorldCenter};
	TrafficRequest.AgentRadius = 42.0f;
	TrafficRequest.AgentHeight = 192.0f;
	TrafficRequest.AdditionalSeparation = 5.0f;
	TestTrue(TEXT("Other traffic owner claims the slot cell"),
		NavigationData->TryClaimTrafficGoal(TrafficRequest));
	Result = Interaction->QueryInteractionOptions(Requester);
	TestEqual(TEXT("Traffic reservation marks both options on its cell occupied"),
		Result.Options.FilterByPredicate(
			[](const FParadoxInteractionOption& Option)
			{
				return Option.GridCellId.Coord.X == 0
					&& Option.State == EParadoxInteractionOptionState::Occupied;
			}).Num(),
		2);
	NavigationData->ReleaseTrafficGoalClaims(TrafficClaimant);

	Target->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	Result = Interaction->QueryInteractionOptions(Requester);
	TestTrue(TEXT("Runtime Smart Object transform moves the first slot projection"),
		Result.Options.ContainsByPredicate(
			[](const FParadoxInteractionOption& Option)
			{
				return Option.SlotHandle.GetSlotIndex() == 0
					&& Option.GridCellId.Coord.X == 1;
			}));
	TestTrue(TEXT("Runtime Smart Object transform moves the second slot projection independently"),
		Result.Options.ContainsByPredicate(
			[](const FParadoxInteractionOption& Option)
			{
				return Option.SlotHandle.GetSlotIndex() == 1
					&& Option.GridCellId.Coord.X == 2;
			}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInteractionActionSubmissionTest,
	"Paradox.Interaction.Action.SubmissionCurrentPositionAndSuccessCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInteractionActionSubmissionTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Interaction::Tests;
	FInteractionExecutionFixture Fixture;
	if (!TestTrue(
		TEXT("Execution fixture initializes"),
		Fixture.Initialize(UParadoxInteractionTestHoldAction::StaticClass())))
	{
		return false;
	}

	USmartObjectSubsystem* SmartObjects =
		USmartObjectSubsystem::GetCurrent(Fixture.Scope.World);
	if (!TestNotNull(TEXT("Smart Object subsystem"), SmartObjects))
	{
		return false;
	}
	TestTrue(TEXT("Pure validation accepts the current cell"),
		Fixture.Interaction->CanRequestInteraction(
			Fixture.Requester,
			ParadoxInteractionTestTags::Primary));
	TestTrue(TEXT("Pure validation does not acquire a claim"),
		SmartObjects->CanBeClaimed(Fixture.Slots[0]));

	Fixture.Selectable->SelectionWidgetClass =
		UParadoxSelectionTestWidget::StaticClass();
	const FHitResult WidgetSelectionHit(
		Fixture.Target,
		nullptr,
		Fixture.Target->GetActorLocation(),
		FVector::UpVector);
	TestTrue(TEXT("Execution target selection creates the widget context"),
		Fixture.SelectionController->Selection->HandleSelectionPointerHit(
			WidgetSelectionHit,
			true));
	UWidgetComponent* WidgetComponent = Fixture.Selectable->GetInteractionWidget();
	UParadoxSelectionTestWidget* Widget = WidgetComponent
		? Cast<UParadoxSelectionTestWidget>(WidgetComponent->GetUserWidgetObject())
		: nullptr;
	if (!TestNotNull(TEXT("Interaction widget uses the configured native base"), Widget))
	{
		return false;
	}
	TestTrue(TEXT("Widget preflight uses the possessed Pawn requester"),
		Widget->CanRequestInteraction(
			ParadoxInteractionTestTags::Primary));
	const FParadoxInteractionRequestResult WidgetSubmission =
		Widget->RequestInteraction(
			ParadoxInteractionTestTags::Primary);
	if (!TestTrue(TEXT("Widget submits through the shared interaction API"),
		WidgetSubmission.IsAccepted()))
	{
		return false;
	}
	UParadoxInteractionTestHoldAction* WidgetAction =
		Cast<UParadoxInteractionTestHoldAction>(
			Fixture.Actions->GetActionInstance(
				WidgetSubmission.SubmissionResult.Handle));
	if (!TestNotNull(TEXT("Widget submission starts the configured action"), WidgetAction))
	{
		return false;
	}
	WidgetAction->CompleteFailureForTest();
	Fixture.SelectionController->Selection->ResetSelectionState();
	TestTrue(TEXT("Widget action cleanup returns the slot to availability"),
		SmartObjects->CanBeClaimed(Fixture.Slots[0]));

	const FVector InvalidPosition(200.0, 0.0, 0.0);
	Fixture.Requester->SetActorLocation(InvalidPosition);
	const FParadoxInteractionRequestResult InvalidPositionResult = Fixture.Request();
	TestEqual(TEXT("Distant requester fails structurally"),
		InvalidPositionResult.Status,
		EParadoxInteractionRequestStatus::InvalidCurrentPosition);
	TestTrue(TEXT("Invalid interaction never moves the requester"),
		Fixture.Requester->GetActorLocation().Equals(InvalidPosition));
	TestTrue(TEXT("Invalid interaction still owns no claim"),
		SmartObjects->CanBeClaimed(Fixture.Slots[0]));

	Fixture.Requester->SetActorLocation(FVector::ZeroVector);
	const FParadoxInteractionRequestResult Submitted = Fixture.Request();
	if (!TestTrue(TEXT("Valid interaction is accepted"), Submitted.IsAccepted()))
	{
		AddError(Submitted.DiagnosticMessage);
		return false;
	}
	UParadoxInteractionTestHoldAction* Action =
		Cast<UParadoxInteractionTestHoldAction>(
			Fixture.Actions->GetActionInstance(Submitted.SubmissionResult.Handle));
	if (!TestNotNull(TEXT("Accepted interaction owns the expected instance"), Action))
	{
		return false;
	}
	TestTrue(TEXT("Action owns the Smart Object claim while running"),
		Action->HasInteractionClaim());
	TestFalse(TEXT("Claimed slot is no longer externally claimable"),
		SmartObjects->CanBeClaimed(Fixture.Slots[0]));

	Action->CompleteSuccessForTest();
	FGameplayActionResult TerminalResult;
	TestTrue(TEXT("Successful action retains a terminal result"),
		Fixture.Actions->GetActionResult(
			Submitted.SubmissionResult.Handle,
			TerminalResult));
	TestEqual(TEXT("Action completed successfully"),
		TerminalResult.TerminalState,
		EGameplayActionState::Succeeded);
	TestTrue(TEXT("Success cleanup releases the claim"),
		SmartObjects->CanBeClaimed(Fixture.Slots[0]));

	TestEqual(TEXT("Scheduler pause creates a query-to-start race window"),
		Fixture.Actions->PauseActions(),
		EGameplayActionOperationResult::Succeeded);
	const FParadoxInteractionRequestResult Queued = Fixture.Request();
	if (!TestTrue(TEXT("Interaction is accepted while the scheduler is paused"), Queued.IsAccepted()))
	{
		return false;
	}
	UParadoxInteractionTestHoldAction* QueuedAction =
		Cast<UParadoxInteractionTestHoldAction>(
			Fixture.Actions->GetActionInstance(Queued.SubmissionResult.Handle));
	if (!TestNotNull(TEXT("Paused interaction retains its queued instance"), QueuedAction))
	{
		return false;
	}
	TestFalse(TEXT("Queued interaction owns no Smart Object claim"),
		QueuedAction->HasInteractionClaim());
	const FSmartObjectClaimHandle RacingClaim = SmartObjects->MarkSlotAsClaimed(
		Fixture.Slots[0],
		ESmartObjectClaimPriority::Normal,
		FConstStructView::Make(
			FSmartObjectActorUserData(Fixture.SelectionController)));
	if (!TestTrue(TEXT("Another requester wins the slot before action start"), RacingClaim.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Scheduler resumes and reevaluates the queued action"),
		Fixture.Actions->ResumeActions(),
		EGameplayActionOperationResult::Succeeded);
	FGameplayActionResult RaceResult;
	TestTrue(TEXT("Lost-slot race reaches a terminal result"),
		Fixture.Actions->GetActionResult(
			Queued.SubmissionResult.Handle,
			RaceResult));
	TestEqual(TEXT("Lost-slot race fails without executing concrete behavior"),
		RaceResult.TerminalState,
		EGameplayActionState::Failed);
	TestTrue(TEXT("Race failure never steals the other requester's claim"),
		!QueuedAction->HasInteractionClaim());
	TestTrue(TEXT("External race claim releases"),
		SmartObjects->MarkSlotAsFree(RacingClaim));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInteractionActionTerminalCleanupTest,
	"Paradox.Interaction.Action.AllTerminalPathsReleaseClaim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInteractionActionTerminalCleanupTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Interaction::Tests;
	FInteractionExecutionFixture Fixture;
	if (!TestTrue(
		TEXT("Terminal cleanup fixture initializes"),
		Fixture.Initialize(UParadoxInteractionTestHoldAction::StaticClass())))
	{
		return false;
	}
	USmartObjectSubsystem* SmartObjects =
		USmartObjectSubsystem::GetCurrent(Fixture.Scope.World);
	if (!TestNotNull(TEXT("Smart Object subsystem"), SmartObjects))
	{
		return false;
	}

	auto SubmitHoldAction = [&]() -> TPair<FGameplayActionHandle, UParadoxInteractionTestHoldAction*>
	{
		const FParadoxInteractionRequestResult Result = Fixture.Request();
		if (!Result.IsAccepted())
		{
			AddError(FString::Printf(
				TEXT("Hold action submission failed: %s"),
				*Result.DiagnosticMessage));
			return {};
		}
		return {
			Result.SubmissionResult.Handle,
			Cast<UParadoxInteractionTestHoldAction>(
				Fixture.Actions->GetActionInstance(Result.SubmissionResult.Handle))};
	};

	TPair<FGameplayActionHandle, UParadoxInteractionTestHoldAction*> Running =
		SubmitHoldAction();
	if (!TestNotNull(TEXT("Failure action starts"), Running.Value))
	{
		return false;
	}
	const FHitResult SelectionHit(
		Fixture.Target,
		nullptr,
		Fixture.Target->GetActorLocation(),
		FVector::UpVector);
	TestTrue(TEXT("Interaction target can be selected while its action owns a claim"),
		Fixture.SelectionController->Selection->HandleSelectionPointerHit(
			SelectionHit,
			true));
	Fixture.SelectionController->Selection->ResetSelectionState();
	TestTrue(TEXT("Selection reset does not release a running action's claim"),
		Running.Value->HasInteractionClaim());
	TestFalse(TEXT("Selection reset does not cancel the running action"),
		SmartObjects->CanBeClaimed(Fixture.Slots[0]));
	Running.Value->CompleteFailureForTest();
	TestTrue(TEXT("Failure releases the claim"),
		SmartObjects->CanBeClaimed(Fixture.Slots[0]));

	Running = SubmitHoldAction();
	if (!TestNotNull(TEXT("Cancelled action starts"), Running.Value))
	{
		return false;
	}
	TestEqual(TEXT("Cancel command succeeds"),
		Fixture.Actions->CancelAction(
			Running.Key,
			GameplayActionTags::Result_Cancelled_ByRequester),
		EGameplayActionOperationResult::Succeeded);
	TestTrue(TEXT("Cancel releases the claim"),
		SmartObjects->CanBeClaimed(Fixture.Slots[0]));

	Running = SubmitHoldAction();
	if (!TestNotNull(TEXT("Interrupted action starts"), Running.Value))
	{
		return false;
	}
	TestEqual(TEXT("Interrupt command succeeds"),
		Fixture.Actions->InterruptAction(
			Running.Key,
			GameplayActionTags::Result_Interrupted_External),
		EGameplayActionOperationResult::Succeeded);
	TestTrue(TEXT("Interrupt releases the claim"),
		SmartObjects->CanBeClaimed(Fixture.Slots[0]));

	Running = SubmitHoldAction();
	if (!TestNotNull(TEXT("Aborted action starts"), Running.Value))
	{
		return false;
	}
	TestEqual(TEXT("Abort command reaches the running interaction"),
		Fixture.Actions->AbortAllActions(
			GameplayActionTags::Result_Aborted_SystemReset),
		1);
	TestTrue(TEXT("Abort releases the claim before reset mutation"),
		SmartObjects->CanBeClaimed(Fixture.Slots[0]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInteractionActionHardeningTest,
	"Paradox.Interaction.Hardening.IdentitySchemaAndDefaultExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInteractionActionHardeningTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Interaction::Tests;
	{
		FInteractionExecutionFixture RuntimeTargetFixture;
		if (!TestTrue(
			TEXT("Runtime target fixture initializes"),
			RuntimeTargetFixture.Initialize(
				UParadoxInteractionTestHoldAction::StaticClass(),
				false)))
		{
			return false;
		}
		const FParadoxInteractionRequestResult Result = RuntimeTargetFixture.Request();
		TestEqual(TEXT("Runtime-created target is explicitly unrecordable"),
			Result.Status,
			EParadoxInteractionRequestStatus::UnrecordableTarget);
	}
	{
		FInteractionExecutionFixture PieAuthoredTargetFixture(EWorldType::PIE);
		if (!TestTrue(
			TEXT("PIE authored target fixture initializes"),
			PieAuthoredTargetFixture.Initialize(
				UParadoxInteractionTestSuccessAction::StaticClass(),
				false)))
		{
			return false;
		}
		TestFalse(
			TEXT("PIE duplicate does not depend on RF_WasLoaded"),
			PieAuthoredTargetFixture.Target->HasAnyFlags(RF_WasLoaded));
		const FParadoxInteractionRequestResult RuntimePieResult =
			PieAuthoredTargetFixture.Request();
		TestEqual(
			TEXT("Runtime-spawned PIE target remains unrecordable"),
			RuntimePieResult.Status,
			EParadoxInteractionRequestStatus::UnrecordableTarget);
		static_cast<UObject*>(PieAuthoredTargetFixture.Interaction)->PostDuplicate(
			EDuplicateMode::PIE);
		UParadoxInteractionTestSuccessAction::ResetObservations();
		const FParadoxInteractionRequestResult Result =
			PieAuthoredTargetFixture.Request();
		TestTrue(
			TEXT("PIE-duplicated authored target is accepted without RF_WasLoaded"),
			Result.IsAccepted());
		TestEqual(
			TEXT("PIE-authored target passes action-side identity validation"),
			UParadoxInteractionTestSuccessAction::ExecutionCount,
			1);
	}
	{
		FInteractionExecutionFixture InvalidSchemaFixture;
		if (!TestTrue(
			TEXT("Invalid schema fixture initializes"),
			InvalidSchemaFixture.Initialize(
				UParadoxInteractionTestHoldAction::StaticClass(),
				true,
				false)))
		{
			return false;
		}
		const FParadoxInteractionRequestResult Result = InvalidSchemaFixture.Request();
		TestEqual(TEXT("Missing required parameter is explicit"),
			Result.Status,
			EParadoxInteractionRequestStatus::ParameterSchemaMismatch);
#if WITH_EDITOR
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("Editor validation rejects the missing required parameter"),
			static_cast<UObject*>(InvalidSchemaFixture.Interaction)->IsDataValid(
				ValidationContext),
			EDataValidationResult::Invalid);
#endif
	}
	{
		FInteractionExecutionFixture DefaultActionFixture;
		if (!TestTrue(
			TEXT("Default action fixture initializes"),
			DefaultActionFixture.Initialize(
				UParadoxInteractionTestDefaultAction::StaticClass())))
		{
			return false;
		}
		const FParadoxInteractionRequestResult Submitted = DefaultActionFixture.Request();
		if (!TestTrue(TEXT("Base fallback action is accepted before execution"), Submitted.IsAccepted()))
		{
			return false;
		}
#if WITH_EDITOR
		FDataValidationContext ValidationContext;
		TestEqual(TEXT("Editor validation accepts a compatible interaction Definition"),
			static_cast<UObject*>(DefaultActionFixture.Interaction)->IsDataValid(
				ValidationContext),
			EDataValidationResult::Valid);
#endif
		FGameplayActionResult TerminalResult;
		TestTrue(TEXT("Base fallback reaches a terminal result"),
			DefaultActionFixture.Actions->GetActionResult(
				Submitted.SubmissionResult.Handle,
				TerminalResult));
		TestEqual(TEXT("Base fallback fails rather than hanging"),
			TerminalResult.TerminalState,
			EGameplayActionState::Failed);
		TestTrue(TEXT("Base fallback exposes NotImplemented reason"),
			TerminalResult.ReasonTag
				== ParadoxGameplayTags::Result_Failure_Interaction_NotImplemented);
	}
	{
		FInteractionExecutionFixture DestroyedTargetFixture;
		if (!TestTrue(
			TEXT("Destroyed target fixture initializes"),
			DestroyedTargetFixture.Initialize(
				UParadoxInteractionTestHoldAction::StaticClass())))
		{
			return false;
		}
		const FParadoxInteractionRequestResult Submitted =
			DestroyedTargetFixture.Request();
		UParadoxInteractionTestHoldAction* Action =
			Cast<UParadoxInteractionTestHoldAction>(
				DestroyedTargetFixture.Actions->GetActionInstance(
					Submitted.SubmissionResult.Handle));
		if (!TestTrue(TEXT("Target-destruction action is accepted"), Submitted.IsAccepted())
			|| !TestNotNull(TEXT("Target-destruction action starts"), Action))
		{
			return false;
		}
		DestroyedTargetFixture.Target->Destroy();
		FGameplayActionResult TerminalResult;
		TestTrue(TEXT("Destroyed target produces a terminal result"),
			DestroyedTargetFixture.Actions->GetActionResult(
				Submitted.SubmissionResult.Handle,
				TerminalResult));
		TestEqual(TEXT("Destroyed target fails the running interaction"),
			TerminalResult.TerminalState,
			EGameplayActionState::Failed);
		TestTrue(TEXT("Destroyed target clears the action claim state"),
			!Action->HasInteractionClaim());
	}
	{
		FInteractionExecutionFixture LostClaimFixture;
		if (!TestTrue(
			TEXT("Lost claim fixture initializes"),
			LostClaimFixture.Initialize(
				UParadoxInteractionTestHoldAction::StaticClass())))
		{
			return false;
		}
		const FParadoxInteractionRequestResult Submitted =
			LostClaimFixture.Request();
		UParadoxInteractionTestHoldAction* Action =
			Cast<UParadoxInteractionTestHoldAction>(
				LostClaimFixture.Actions->GetActionInstance(
					Submitted.SubmissionResult.Handle));
		if (!TestTrue(TEXT("Lost-claim action is accepted"), Submitted.IsAccepted())
			|| !TestNotNull(TEXT("Lost-claim action starts"), Action))
		{
			return false;
		}
		USmartObjectSubsystem* SmartObjects =
			USmartObjectSubsystem::GetCurrent(LostClaimFixture.Scope.World);
		if (!TestNotNull(TEXT("Lost-claim Smart Object subsystem"), SmartObjects))
		{
			return false;
		}
		TestTrue(TEXT("Smart Object unregister invalidates the running slot"),
			SmartObjects->UnregisterSmartObject(LostClaimFixture.SmartObject));
		FGameplayActionResult TerminalResult;
		TestTrue(TEXT("Lost claim produces a terminal action result"),
			LostClaimFixture.Actions->GetActionResult(
				Submitted.SubmissionResult.Handle,
				TerminalResult));
		TestEqual(TEXT("Lost claim fails the running interaction"),
			TerminalResult.TerminalState,
			EGameplayActionState::Failed);
		TestTrue(TEXT("Lost claim clears the public claim state"),
			!Action->HasInteractionClaim());
	}
	{
		FInteractionExecutionFixture DestroyedRequesterFixture;
		if (!TestTrue(
			TEXT("Destroyed requester fixture initializes"),
			DestroyedRequesterFixture.Initialize(
				UParadoxInteractionTestHoldAction::StaticClass())))
		{
			return false;
		}
		const FParadoxInteractionRequestResult Submitted =
			DestroyedRequesterFixture.Request();
		UParadoxInteractionTestHoldAction* Action =
			Cast<UParadoxInteractionTestHoldAction>(
				DestroyedRequesterFixture.Actions->GetActionInstance(
					Submitted.SubmissionResult.Handle));
		if (!TestTrue(TEXT("Requester-destruction action is accepted"), Submitted.IsAccepted())
			|| !TestNotNull(TEXT("Requester-destruction action starts"), Action))
		{
			return false;
		}
		DestroyedRequesterFixture.Requester->Destroy();
		USmartObjectSubsystem* SmartObjects =
			USmartObjectSubsystem::GetCurrent(DestroyedRequesterFixture.Scope.World);
		TestTrue(TEXT("Requester teardown clears the action claim state"),
			!Action->HasInteractionClaim());
		TestTrue(TEXT("Requester teardown releases the target slot"),
			SmartObjects
				&& SmartObjects->CanBeClaimed(
					DestroyedRequesterFixture.Slots[0]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInteractionIntentReplayTest,
	"Paradox.Interaction.Replay.SemanticTrackAndFreshRuntimeResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInteractionIntentReplayTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Interaction::Tests;
	FInteractionExecutionFixture Fixture;
	if (!TestTrue(
		TEXT("Replay fixture initializes"),
		Fixture.Initialize(
			UParadoxInteractionTestSuccessAction::StaticClass(),
			true,
			true,
			true)))
	{
		return false;
	}
	if (!TestNotNull(TEXT("Intent Replay component"), Fixture.Replay))
	{
		return false;
	}

	UParadoxInteractionTestSuccessAction::ResetObservations();
	TestTrue(TEXT("Recording starts"),
		Fixture.Replay->StartRecording(FIntentRecordingOptions()).Succeeded());
	const FParadoxInteractionRequestResult SourceResult = Fixture.Request();
	TestTrue(TEXT("Source interaction is accepted"), SourceResult.IsAccepted());
	TestEqual(TEXT("Source execution claimed before concrete behavior"),
		UParadoxInteractionTestSuccessAction::ClaimedExecutionCount,
		1);
	TestTrue(TEXT("Recording finalizes"),
		Fixture.Replay->RequestStopRecording(
			EIntentRecordingFinalizeMode::Immediate).Succeeded());

	UIntentReplayTrack* Track = Fixture.Replay->GetLastFinalizedTrack();
	if (!TestNotNull(TEXT("Interaction track finalized"), Track))
	{
		return false;
	}
	TestEqual(TEXT("One accepted interaction produces one intent"),
		Track->GetEntryCount(),
		1);
	FRecordedIntent SourceIntent;
	if (!TestTrue(TEXT("Recorded interaction entry is readable"),
		Track->GetEntryByIndex(0, SourceIntent)))
	{
		return false;
	}
	const FName TargetName =
		GET_MEMBER_NAME_CHECKED(FParadoxInteractionActionParameters, Target);
	const FName TagName =
		GET_MEMBER_NAME_CHECKED(FParadoxInteractionActionParameters, InteractionTag);
	const TValueOrError<FSoftObjectPath, EPropertyBagResult> RecordedTarget =
		SourceIntent.GetParameters().GetValueSoftPath(TargetName);
	const TValueOrError<FGameplayTag*, EPropertyBagResult> RecordedTag =
		SourceIntent.GetParameters().GetValueStruct<FGameplayTag>(TagName);
	TestTrue(TEXT("Track stores a soft Target"), RecordedTarget.HasValue());
	TestTrue(TEXT("Track stores the Interaction Tag"),
		RecordedTag.HasValue()
			&& RecordedTag.GetValue()
			&& *RecordedTag.GetValue()
				== ParadoxInteractionTestTags::Primary);
	if (RecordedTarget.HasValue())
	{
		TestEqual(TEXT("Soft Target identifies the world-authored target"),
			RecordedTarget.GetValue(),
			FSoftObjectPath(Fixture.Target));
	}
	const UPropertyBag* RecordedBag =
		SourceIntent.GetParameters().GetPropertyBagStruct();
	TestEqual(TEXT("Track stores no runtime slot, claim, transform, or Grid cell fields"),
		RecordedBag ? RecordedBag->GetPropertyDescs().Num() : 0,
		2);

	FIntentReplayPlaybackOptions PlaybackOptions;
	const FIntentReplayPrepareResult Prepared =
		Fixture.Replay->PrepareReplay(Track, PlaybackOptions);
	TestEqual(TEXT("Interaction replay prepares"),
		Prepared.Status,
		EIntentReplayPrepareStatus::Ready);
	TestTrue(TEXT("Interaction replay starts"), Fixture.Replay->StartReplay().Succeeded());
	TestEqual(TEXT("Replay creates a fresh concrete execution"),
		UParadoxInteractionTestSuccessAction::ExecutionCount,
		2);
	TestEqual(TEXT("Replay reacquires a fresh runtime claim"),
		UParadoxInteractionTestSuccessAction::ClaimedExecutionCount,
		2);
	TestEqual(TEXT("Replay completes"),
		Fixture.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Completed);

	UIntentReplayPlaybackSession* Session =
		Fixture.Replay->GetActivePlaybackSession();
	if (!TestNotNull(TEXT("Replay session remains inspectable"), Session))
	{
		return false;
	}
	bool bFoundReplayAccepted = false;
	bool bFoundReplaySuccess = false;
	for (const FIntentExecutionEvent& Event :
		Session->GetExecutionJournal()->GetEvents())
	{
		if (!Event.bHasActionEvent)
		{
			continue;
		}
		bFoundReplayAccepted |=
			Event.ActionEvent.EventType == EGameplayActionEventType::Accepted
			&& Event.ActionEvent.OriginTag == IntentReplayTags::Origin_Replay;
		bFoundReplaySuccess |=
			Event.ActionEvent.EventType == EGameplayActionEventType::Ended
			&& Event.ActionEvent.bHasResult
			&& Event.ActionEvent.Result.TerminalState
				== EGameplayActionState::Succeeded;
	}
	TestTrue(TEXT("Replay journal records replay origin"), bFoundReplayAccepted);
	TestTrue(TEXT("Replay journal records its own success"), bFoundReplaySuccess);

	FRecordedIntent SourceIntentAfterReplay;
	Track->GetEntryByIndex(0, SourceIntentAfterReplay);
	TestTrue(TEXT("Replay leaves source intent identity immutable"),
		SourceIntentAfterReplay.RecordedIntentId == SourceIntent.RecordedIntentId);
	TestEqual(TEXT("Replay leaves source track cardinality immutable"),
		Track->GetEntryCount(),
		1);

	const FVector InvalidReplayPosition(200.0, 0.0, 0.0);
	Fixture.Requester->SetActorLocation(InvalidReplayPosition);
	const FIntentReplayPrepareResult InvalidPrepared =
		Fixture.Replay->PrepareReplay(Track, PlaybackOptions);
	TestEqual(TEXT("Current Definition still prepares at a changed runtime position"),
		InvalidPrepared.Status,
		EIntentReplayPrepareStatus::Ready);
	AddExpectedError(
		TEXT("failed replay session"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestTrue(TEXT("Changed-position replay starts its normal submission path"),
		Fixture.Replay->StartReplay().Succeeded());
	TestEqual(TEXT("Invalid current position fails replay normally"),
		Fixture.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Failed);
	TestEqual(TEXT("Invalid replay position never executes concrete behavior"),
		UParadoxInteractionTestSuccessAction::ExecutionCount,
		2);
	TestTrue(TEXT("Invalid replay never moves the requester"),
		Fixture.Requester->GetActorLocation().Equals(InvalidReplayPosition));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInteractionPreflightContextCloneReplayTest,
	"Paradox.Interaction.Context.PreflightAndCloneReplayRequester",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInteractionPreflightContextCloneReplayTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::Interaction::Tests;
	FInteractionExecutionFixture Fixture;
	if (!TestTrue(
		TEXT("Clone requester fixture initializes"),
		Fixture.Initialize(
			UParadoxInteractionTestPreflightContextAction::StaticClass(),
			true,
			true,
			true,
			AParadoxCloneCharacter::StaticClass())))
	{
		return false;
	}
	if (!TestTrue(
		TEXT("Fixture requester is a shared Paradox Character"),
		Fixture.Requester->IsA<AParadoxCharacter>())
		|| !TestNotNull(TEXT("Clone Intent Replay component"), Fixture.Replay))
	{
		return false;
	}

	UParadoxInteractionTestPreflightContextAction::ResetObservations();
	if (!TestTrue(
		TEXT("Clone recording starts"),
		Fixture.Replay->StartRecording(FIntentRecordingOptions()).Succeeded()))
	{
		return false;
	}
	const FParadoxInteractionRequestResult SourceResult = Fixture.Request();
	TestTrue(TEXT("Clone-owned source interaction is accepted"), SourceResult.IsAccepted());
	TestTrue(
		TEXT("Preflight resolves the Gameplay Action Component owner as requester"),
		UParadoxInteractionTestPreflightContextAction::LastRequester.Get()
			== Fixture.Requester);
	TestTrue(
		TEXT("Preflight resolves the semantic soft Target before Action Init"),
		UParadoxInteractionTestPreflightContextAction::LastTarget.Get()
			== Fixture.Target);
	TestTrue(
		TEXT("Source preflight preserves Player origin independently from requester identity"),
		UParadoxInteractionTestPreflightContextAction::LastOrigin
			== ParadoxGameplayTags::Origin_Player);
	TestTrue(
		TEXT("Source action executes after phase-safe preflight"),
		UParadoxInteractionTestPreflightContextAction::ExecutionCount == 1);

	if (!TestTrue(
		TEXT("Clone source recording finalizes"),
		Fixture.Replay->RequestStopRecording(
			EIntentRecordingFinalizeMode::Immediate).Succeeded()))
	{
		return false;
	}
	UIntentReplayTrack* Track = Fixture.Replay->GetLastFinalizedTrack();
	if (!TestNotNull(TEXT("Clone source track exists"), Track))
	{
		return false;
	}

	UParadoxInteractionTestPreflightContextAction::ResetObservations();
	const FIntentReplayPrepareResult Prepared = Fixture.Replay->PrepareReplay(
		Track,
		FIntentReplayPlaybackOptions());
	TestEqual(
		TEXT("Clone interaction replay prepares"),
		Prepared.Status,
		EIntentReplayPrepareStatus::Ready);
	TestTrue(
		TEXT("Clone interaction replay starts"),
		Fixture.Replay->StartReplay().Succeeded());
	TestTrue(
		TEXT("Replay preflight resolves the recipient clone, not the recorded RequestSource"),
		UParadoxInteractionTestPreflightContextAction::LastRequester.Get()
			== Fixture.Requester);
	TestTrue(
		TEXT("Replay preflight re-resolves the current semantic Target"),
		UParadoxInteractionTestPreflightContextAction::LastTarget.Get()
			== Fixture.Target);
	TestTrue(
		TEXT("Replay origin remains diagnostic metadata"),
		UParadoxInteractionTestPreflightContextAction::LastOrigin
			== IntentReplayTags::Origin_Replay);
	TestTrue(
		TEXT("Replay creates and executes a fresh action"),
		UParadoxInteractionTestPreflightContextAction::ExecutionCount == 1);
	TestEqual(
		TEXT("Clone replay completes"),
		Fixture.Replay->GetPlaybackState(),
		EIntentReplayPlaybackState::Completed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInteractionGridCellStyleAssetTest,
	"Paradox.Interaction.GridCellStyleAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInteractionGridCellStyleAssetTest::RunTest(const FString& Parameters)
{
	const UGridCellVisualStyle* Style = LoadObject<UGridCellVisualStyle>(
		nullptr,
		TEXT("/Game/Data/GridWorld/DA_ParadoxGridCellStyle.DA_ParadoxGridCellStyle"));
	if (!TestNotNull(TEXT("Paradox GridWorld cell style asset loads"), Style))
	{
		return false;
	}
	TestNotNull(TEXT("Shared style has a cell mesh"), Style->CellMesh.Get());
	TestNotNull(TEXT("Shared style has a cell material"), Style->CellMaterial.Get());
	TestEqual(TEXT("Shared style uses the requested GridWorld block mesh"),
		GetPathNameSafe(Style->CellMesh),
		FString(TEXT("/GridWorldSystem/Presentation/SM_GridWorldBlock.SM_GridWorldBlock")));
	TestEqual(TEXT("Shared style uses the existing runtime cell material"),
		GetPathNameSafe(Style->CellMaterial),
		FString(TEXT("/GridWorldSystem/Presentation/M_GridRuntimeCell.M_GridRuntimeCell")));
	TestTrue(TEXT("Primary interaction color remains visible"),
		Style->PrimaryOverlayColor.A > 0.0f);
	TestTrue(TEXT("Secondary interaction color remains visible"),
		Style->SecondaryOverlayColor.A > 0.0f);
	TestFalse(TEXT("Primary and Secondary interaction states remain distinguishable"),
		Style->PrimaryOverlayColor.Equals(Style->SecondaryOverlayColor));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInteractionNativeDefinitionsTest,
	"Paradox.Interaction.Action.NativeDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInteractionNativeDefinitionsTest::RunTest(const FString& Parameters)
{
	const UParadoxReceiverInteractionActionDefinition* Receiver =
		GetDefault<UParadoxReceiverInteractionActionDefinition>();
	const UParadoxEmitterInteractionActionDefinition* Emitter =
		GetDefault<UParadoxEmitterInteractionActionDefinition>();
	TestEqual(TEXT("Receiver Definition selects the native action"), Receiver->InstanceClass.Get(), UParadoxReceiverInteractionAction::StaticClass());
	TestEqual(TEXT("Emitter Definition selects the native action"), Emitter->InstanceClass.Get(), UParadoxEmitterInteractionAction::StaticClass());
	for (const UGameplayActionDefinition* Definition : {static_cast<const UGameplayActionDefinition*>(Receiver), static_cast<const UGameplayActionDefinition*>(Emitter)})
	{
		TestTrue(TEXT("Standard interaction owns Movement lock"), Definition->ExecutionLocks.HasTagExact(GameplayActionTags::Lock_Movement));
		TestTrue(TEXT("Standard interaction owns Interaction lock"), Definition->ExecutionLocks.HasTagExact(ParadoxGameplayTags::Lock_Interaction));
		TestEqual(TEXT("Repeated clicks are rejected instead of queued"), Definition->BlockedPolicy, EGameplayActionBlockedPolicy::Reject);
		TestEqual(TEXT("Standard interaction is journal-ready"), Definition->JournalRequirement, EGameplayActionJournalRequirement::Optional);
		TestNotNull(TEXT("Target schema is present"), Definition->DefaultParameters.FindPropertyDescByName(ParadoxInteractionActionParameters::Target));
		TestNotNull(TEXT("InteractionTag schema is present"), Definition->DefaultParameters.FindPropertyDescByName(ParadoxInteractionActionParameters::InteractionTag));
		TestNotNull(TEXT("NavigationFilter schema is present"), Definition->DefaultParameters.FindPropertyDescByName(ParadoxInteractionActionParameters::NavigationFilter));
	}
	TestNotNull(TEXT("Receiver component selector is authored"), Receiver->DefaultParameters.FindPropertyDescByName(ParadoxInteractionActionParameters::ReceiverComponentName));
	TestNotNull(TEXT("Emitter component selector is authored"), Emitter->DefaultParameters.FindPropertyDescByName(ParadoxInteractionActionParameters::EmitterComponentName));
	TestNotNull(TEXT("Emitter exact SignalTag is authored"), Emitter->DefaultParameters.FindPropertyDescByName(ParadoxInteractionActionParameters::SignalTag));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInteractionStandardPuzzleEffectsTest,
	"Paradox.Interaction.Action.StandardReceiverAndEmitterEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInteractionStandardPuzzleEffectsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Interaction::Tests;
	{
		FInteractionExecutionFixture Fixture;
		if (!TestTrue(TEXT("Receiver interaction fixture initializes"), Fixture.Initialize(UParadoxInteractionTestHoldAction::StaticClass())))
		{
			return false;
		}
		UPuzzleReceiverComponent* Receiver = NewObject<UPuzzleReceiverComponent>(Fixture.Target, TEXT("DoorReceiver"));
		Fixture.Target->AddInstanceComponent(Receiver);
		Receiver->ActivationMode = EPuzzleReceiverActivationMode::Manual;
		Receiver->RegisterComponent();
		APuzzleController* PrerequisiteController = Fixture.Scope.World->SpawnActor<APuzzleController>();
		TestTrue(TEXT("Receiver prerequisites are supplied by a Controller"), Receiver->SetControllerRequest(PrerequisiteController, true));
		UParadoxReceiverInteractionActionDefinition* Definition = NewObject<UParadoxReceiverInteractionActionDefinition>(Fixture.Target);
		Definition->DefaultParameters.SetValueName(ParadoxInteractionActionParameters::ReceiverComponentName, Receiver->GetFName());
		Definition->DefaultParameters.SetValueEnum(ParadoxInteractionActionParameters::Command, EParadoxInteractionStateCommand::Activate);
		Fixture.Interaction->InteractionDefinitions[0].GameplayActionDefinition = Definition;
		Fixture.Interaction->RefreshInteractionSources();
		TestTrue(TEXT("Manual Receiver Activate request is accepted"), Fixture.Request().IsAccepted());
		TestTrue(TEXT("Native Receiver action applies activation"), Receiver->IsReceiverActive());
		Definition->DefaultParameters.SetValueEnum(ParadoxInteractionActionParameters::Command, EParadoxInteractionStateCommand::Deactivate);
		TestTrue(TEXT("Manual Receiver Deactivate request is accepted"), Fixture.Request().IsAccepted());
		TestFalse(TEXT("Native Receiver action applies deactivation"), Receiver->IsReceiverActive());
	}
	{
		FInteractionExecutionFixture Fixture;
		if (!TestTrue(TEXT("Emitter interaction fixture initializes"), Fixture.Initialize(UParadoxInteractionTestHoldAction::StaticClass())))
		{
			return false;
		}
		UPuzzleEmitterComponent* Emitter = NewObject<UPuzzleEmitterComponent>(Fixture.Target, TEXT("ConsoleEmitter"));
		Fixture.Target->AddInstanceComponent(Emitter);
		Emitter->RegisterComponent();
		UParadoxEmitterInteractionActionDefinition* Definition = NewObject<UParadoxEmitterInteractionActionDefinition>(Fixture.Target);
		Definition->DefaultParameters.SetValueName(ParadoxInteractionActionParameters::EmitterComponentName, Emitter->GetFName());
		Definition->DefaultParameters.SetValueStruct(ParadoxInteractionActionParameters::SignalTag, ParadoxGameplayTags::Puzzle_Signal_Pressed.GetTag());
		Definition->DefaultParameters.SetValueEnum(ParadoxInteractionActionParameters::Command, EParadoxInteractionStateCommand::Activate);
		Fixture.Interaction->InteractionDefinitions[0].GameplayActionDefinition = Definition;
		Fixture.Interaction->RefreshInteractionSources();
		TestTrue(TEXT("Unconsumed Emitter Activate request is accepted"), Fixture.Request().IsAccepted());
		FPuzzleSignalState SignalState;
		TestTrue(TEXT("Native Emitter action publishes the exact channel"), Emitter->TryGetSignalState(ParadoxGameplayTags::Puzzle_Signal_Pressed, SignalState));
		TestTrue(TEXT("Native Emitter action publishes On"), SignalState.bIsActive);
		Definition->DefaultParameters.SetValueEnum(ParadoxInteractionActionParameters::Command, EParadoxInteractionStateCommand::Deactivate);
		TestTrue(TEXT("Emitter Deactivate request ignores consumer gates"), Fixture.Request().IsAccepted());
		TestTrue(TEXT("Exact channel remains queryable after Off"), Emitter->TryGetSignalState(ParadoxGameplayTags::Puzzle_Signal_Pressed, SignalState));
		TestFalse(TEXT("Native Emitter action publishes Off"), SignalState.bIsActive);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxInteractionFailureContractTest,
	"Paradox.Interaction.FailureContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxInteractionFailureContractTest::RunTest(const FString& Parameters)
{
	UParadoxInteractionComponent* Interaction =
		NewObject<UParadoxInteractionComponent>(GetTransientPackage());
	const FParadoxInteractionQueryResult Result =
		Interaction->QueryInteractionOptions(nullptr);
	TestEqual(TEXT("Null requester fails explicitly"),
		Result.Status, EParadoxInteractionQueryStatus::InvalidRequester);
	TestTrue(TEXT("Null requester failure retains a diagnostic"),
		!Result.DiagnosticMessage.IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
