#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/AudioComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"
#include "Paradox.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "PressurePlateTestTypes.h"
#include "SmartObjectComponent.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "TimerManager.h"

namespace
{
	const FName TAG_PressurePlateTestHeavy(TEXT("Heavy"));
	const FName TAG_PressurePlateTestAuthorized(TEXT("Authorized"));
}

namespace UE::Paradox::PressurePlate::Tests
{
	/** RAII-owned Game world supporting component BeginPlay, timers, and World State. */
	struct FScopedPressurePlateWorld
	{
		explicit FScopedPressurePlateWorld(const TCHAR* Name)
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

		~FScopedPressurePlateWorld()
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

		void Advance(float DeltaSeconds)
		{
			if (World)
			{
				++GFrameCounter;
				World->GetTimerManager().Tick(DeltaSeconds);
			}
		}

		void StartPlay()
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

	APressurePlateTestActor* SpawnPlate(
		UWorld& World,
		FName Name,
		float InPressDuration = 1.0f,
		float InReleaseDuration = 1.0f)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = Name;
		SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APressurePlateTestActor* Plate = World.SpawnActor<APressurePlateTestActor>(
			APressurePlateTestActor::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
		if (Plate)
		{
			Plate->PressDuration = InPressDuration;
			Plate->ReleaseDuration = InReleaseDuration;
			Plate->bEmitNoiseOnPressMovement = false;
			Plate->bEmitNoiseOnReleaseMovement = false;
		}
		return Plate;
	}

	APressurePlateTestOccupant* SpawnOccupant(UWorld& World, FName Name, const FVector& Location)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = Name;
		SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World.SpawnActor<APressurePlateTestOccupant>(
			APressurePlateTestOccupant::StaticClass(),
			FTransform(FRotator::ZeroRotator, Location),
			SpawnParameters);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPressurePlateArchitectureTest,
	"Paradox.PressurePlate.Architecture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPressurePlateArchitectureTest::RunTest(const FString& Parameters)
{
	const APressurePlate* Defaults = GetDefault<APressurePlate>();
	if (!TestNotNull(TEXT("Pressure Plate CDO exists"), Defaults))
	{
		return false;
	}

	TestFalse(TEXT("Pressure Plate native class is concrete"), APressurePlate::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
	TestTrue(TEXT("BillboardRoot is the actual Actor root"), Defaults->GetRootComponent() == Defaults->BillboardRoot);
	TestNull(TEXT("Pressure Plate suppresses the unused inherited SceneRoot"), Defaults->SceneRootComponent.Get());
	TestTrue(TEXT("FloorMesh attaches directly to BillboardRoot"), Defaults->FloorMesh && Defaults->FloorMesh->GetAttachParent() == Defaults->BillboardRoot);
	TestTrue(TEXT("PlateMesh attaches to FloorMesh"), Defaults->PlateMesh && Defaults->PlateMesh->GetAttachParent() == Defaults->FloorMesh);
	TestTrue(TEXT("OccupancyVolume attaches to stable FloorMesh"), Defaults->OccupancyVolume && Defaults->OccupancyVolume->GetAttachParent() == Defaults->FloorMesh);
	TestTrue(TEXT("MovementAudio attaches to moving PlateMesh"), Defaults->MovementAudio && Defaults->MovementAudio->GetAttachParent() == Defaults->PlateMesh);
	TestTrue(TEXT("MovementVFX attaches to moving PlateMesh"), Defaults->MovementVFX && Defaults->MovementVFX->GetAttachParent() == Defaults->PlateMesh);
	TestFalse(TEXT("PlateMesh can never affect navigation"), Defaults->PlateMesh && Defaults->PlateMesh->CanEverAffectNavigation());
	TestFalse(TEXT("OccupancyVolume cannot affect navigation"), Defaults->OccupancyVolume && Defaults->OccupancyVolume->CanEverAffectNavigation());
	TestTrue(TEXT("OccupancyVolume generates overlaps"), Defaults->OccupancyVolume && Defaults->OccupancyVolume->GetGenerateOverlapEvents());
	TestNotNull(TEXT("Pressure Plate owns one World State participant"), Defaults->WorldStateParticipant.Get());
	TestNotNull(TEXT("Pressure Plate owns one PerceptionKnowledge source"), Defaults->PerceptionSource.Get());
	TestNotNull(TEXT("Pressure Plate owns one Selectable Component"), Defaults->SelectableComponent.Get());
	TestTrue(TEXT("Pressure Plate opts into selected interaction-cell presentation"),
		Defaults->SelectableComponent
			&& Defaults->SelectableComponent->bShowInteractionCellsWhenSelected);
	TestNotNull(TEXT("Pressure Plate owns one Smart Object Component"), Defaults->SmartObjectComponent.Get());
	TestTrue(TEXT("Pressure Plate Smart Object attaches to BillboardRoot"),
		Defaults->SmartObjectComponent
			&& Defaults->SmartObjectComponent->GetAttachParent() == Defaults->BillboardRoot);
	TestNotNull(TEXT("Pressure Plate owns one Paradox Interaction Component"), Defaults->InteractionComponent.Get());
	TestNull(TEXT("Pressure Plate permits an unassigned Smart Object Definition"),
		Defaults->SmartObjectComponent ? Defaults->SmartObjectComponent->GetDefinition() : nullptr);
	TestEqual(TEXT("Pressure Plate permits an empty native interaction catalog"),
		Defaults->InteractionComponent ? Defaults->InteractionComponent->InteractionDefinitions.Num() : INDEX_NONE,
		0);
	TestEqual(
		TEXT("Pressure Plate defaults to Released initial input"),
		Defaults->InitialInputState,
		EPuzzleSwitchInitialInputState::Released);
	TestFalse(
		TEXT("World State does not capture the stationary Actor transform as competing authority"),
		Defaults->WorldStateParticipant && Defaults->WorldStateParticipant->bCaptureActorTransform);
	TestEqual(TEXT("Default output uses registered pressed signal"), Defaults->OutputSignalTag, ParadoxGameplayTags::Puzzle_Signal_Pressed.GetTag());
	TestTrue(
		TEXT("World State captures inherited authoritative active output"),
		Defaults->WorldStateParticipant
			&& Defaults->WorldStateParticipant->CapturedProperties.ContainsByPredicate(
				[](const FWorldStatePropertySelection& Selection)
				{
					return Selection.CaptureSourceId == FWorldStateCaptureSourceId::OwnerActor()
						&& Selection.PropertyName == TEXT("bIsActive");
				}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPressurePlateOccupancyMovementTest,
	"Paradox.PressurePlate.OccupancyAndMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPressurePlateOccupancyMovementTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::PressurePlate::Tests;
	FScopedPressurePlateWorld TestWorld(TEXT("PressurePlateOccupancyWorld"));
	if (!TestNotNull(TEXT("Pressure Plate test world exists"), TestWorld.World))
	{
		return false;
	}

	APressurePlateTestActor* Plate = SpawnPlate(*TestWorld.World, TEXT("TestPressurePlate"));
	APressurePlateTestOccupant* First = SpawnOccupant(*TestWorld.World, TEXT("FirstOccupant"), FVector(10000.0f, 0.0f, 0.0f));
	APressurePlateTestOccupant* Second = SpawnOccupant(*TestWorld.World, TEXT("SecondOccupant"), FVector(12000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("Pressure Plate exists"), Plate)
		|| !TestNotNull(TEXT("First occupant exists"), First)
		|| !TestNotNull(TEXT("Second occupant exists"), Second))
	{
		return false;
	}
	TestWorld.StartPlay();

	TestFalse(TEXT("Idle pressure plate does not Tick"), Plate->IsActorTickEnabled());
	Plate->SimulateBeginOverlap(First, First->FirstComponent);
	TestEqual(TEXT("First valid Actor occupies the plate"), Plate->GetOccupancyState(), EPressurePlateOccupancyState::Occupied);
	TestTrue(TEXT("Current occupant is the first accepted Actor"), Plate->GetCurrentOccupant() == First);
	TestEqual(TEXT("Free-to-Occupied emits one confirmed Press"), Plate->ConfirmedPressCount, 1);
	TestTrue(TEXT("Hold plate output activates"), Plate->IsSwitchActive());
	TestEqual(TEXT("Activation starts one downward movement"), Plate->MovementStartedCount, 1);
	TestTrue(TEXT("Movement enables Tick"), Plate->IsActorTickEnabled());

	Plate->SimulateBeginOverlap(First, First->SecondComponent);
	Plate->SimulateBeginOverlap(Second, Second->FirstComponent);
	TestEqual(TEXT("Another component of the same Actor does not press again"), Plate->ConfirmedPressCount, 1);
	TestTrue(TEXT("A second Actor cannot replace the current occupant on begin overlap"), Plate->GetCurrentOccupant() == First);

	Plate->Tick(0.5f);
	const float AlphaBeforeReversal = Plate->GetPlateMovementAlpha();
	TestTrue(TEXT("Plate reaches an intermediate alpha"), AlphaBeforeReversal > 0.0f && AlphaBeforeReversal < 1.0f);
	Plate->SimulateEndOverlap(First, First->FirstComponent);
	TestTrue(TEXT("Leaving one of two components preserves occupancy"), Plate->IsOccupied());
	Plate->SimulateEndOverlap(First, First->SecondComponent);
	TestFalse(TEXT("Leaving the final component frees the plate"), Plate->IsOccupied());
	TestEqual(TEXT("Occupied-to-Free emits one confirmed Release"), Plate->ConfirmedReleaseCount, 1);
	TestEqual(TEXT("Output reversal starts one upward movement"), Plate->MovementStartedCount, 2);
	TestEqual(TEXT("Mid-motion reversal does not jump alpha"), Plate->GetPlateMovementAlpha(), AlphaBeforeReversal);

	Plate->Tick(1.0f);
	TestEqual(TEXT("Released plate completes exactly raised"), Plate->GetPlateMovementAlpha(), 0.0f);
	TestFalse(TEXT("Completed movement disables idle Tick"), Plate->IsActorTickEnabled());
	TestEqual(TEXT("Exactly one occupant was accepted"), Plate->OccupantAcceptedCount, 1);
	TestEqual(TEXT("Exactly one occupant was released"), Plate->OccupantReleasedCount, 1);

	Plate->SimulateBeginOverlap(Second, Second->FirstComponent);
	TestTrue(TEXT("Plate can be occupied again"), Plate->GetCurrentOccupant() == Second);
	Second->Destroy();
	TestFalse(TEXT("Destroyed current occupant is cleared event-first"), Plate->IsOccupied());
	TestEqual(TEXT("Destroyed current occupant emits one additional Release"), Plate->ConfirmedReleaseCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPressurePlateTagsAndDelaysTest,
	"Paradox.PressurePlate.TagsAndDelays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPressurePlateTagsAndDelaysTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::PressurePlate::Tests;
	FScopedPressurePlateWorld TestWorld(TEXT("PressurePlateTagsWorld"));
	if (!TestNotNull(TEXT("Pressure Plate tag test world exists"), TestWorld.World))
	{
		return false;
	}

	APressurePlateTestActor* Plate = SpawnPlate(*TestWorld.World, TEXT("TaggedPressurePlate"), 0.0f, 0.0f);
	APressurePlateTestOccupant* First = SpawnOccupant(*TestWorld.World, TEXT("TaggedOccupant"), FVector(10000.0f, 0.0f, 0.0f));
	APressurePlateTestOccupant* Second = SpawnOccupant(*TestWorld.World, TEXT("ReplacementOccupant"), FVector(12000.0f, 0.0f, 0.0f));
	if (!Plate || !First || !Second)
	{
		return false;
	}
	TestWorld.StartPlay();

	Plate->RequiredOccupantActorTags.Add(TAG_PressurePlateTestHeavy);
	Plate->RequiredOccupantActorTags.Add(TAG_PressurePlateTestAuthorized);
	First->Tags.Add(TAG_PressurePlateTestHeavy);
	Plate->SimulateBeginOverlap(First, First->FirstComponent);
	TestFalse(TEXT("Owning only one of two required Actor Tags is rejected"), Plate->IsOccupied());
	First->Tags.Add(TAG_PressurePlateTestAuthorized);
	Plate->SimulateBeginOverlap(First, First->FirstComponent);
	TestTrue(TEXT("Owning all required Actor Tags is accepted"), Plate->GetCurrentOccupant() == First);

	const int32 MovementStartsBeforeReset = Plate->MovementStartedCount;
	Plate->ResetSwitch();
	TestFalse(TEXT("Reset clears transient physical occupancy"), Plate->IsOccupied());
	TestEqual(TEXT("Reset restores inherited Released input"), Plate->GetInputState(), EPuzzleSwitchInputState::Released);
	TestEqual(TEXT("Reset reconciliation does not manufacture feedback"), Plate->MovementStartedCount, MovementStartsBeforeReset);

	Plate->RequiredOccupantActorTags.Reset();
	Plate->ReleaseDelay = 1.0f;
	Plate->SimulateBeginOverlap(First, First->FirstComponent);
	Plate->SimulateEndOverlap(First, First->FirstComponent);
	TestEqual(TEXT("Free physical state owns inherited ReleasePending"), Plate->GetInputState(), EPuzzleSwitchInputState::ReleasePending);
	TestTrue(TEXT("ReleaseDelay preserves active Hold output"), Plate->IsSwitchActive());
	const int32 PressCountBeforeReplacement = Plate->ConfirmedPressCount;
	Plate->SimulateBeginOverlap(Second, Second->FirstComponent);
	TestEqual(TEXT("New occupant cancels pending release without another confirmed Press"), Plate->ConfirmedPressCount, PressCountBeforeReplacement);
	TestEqual(TEXT("Pending release cancellation restores Pressed input"), Plate->GetInputState(), EPuzzleSwitchInputState::Pressed);
	TestWorld.Advance(1.1f);
	TestTrue(TEXT("Invalidated release timer cannot deactivate the occupied plate"), Plate->IsSwitchActive());

	Plate->ResetSwitch();
	Plate->PressDelay = 1.0f;
	Plate->ReleaseDelay = 0.0f;
	Plate->SimulateBeginOverlap(First, First->FirstComponent);
	TestEqual(TEXT("Accepted occupant can own inherited PressPending"), Plate->GetInputState(), EPuzzleSwitchInputState::PressPending);
	Plate->SimulateEndOverlap(First, First->FirstComponent);
	TestEqual(TEXT("Leaving before PressDelay completes restores Released"), Plate->GetInputState(), EPuzzleSwitchInputState::Released);
	TestWorld.Advance(1.1f);
	TestFalse(TEXT("Cancelled PressDelay never activates the plate"), Plate->IsSwitchActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPressurePlateInitialPressedTest,
	"Paradox.PressurePlate.InitialPressedAndActorTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPressurePlateInitialPressedTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::PressurePlate::Tests;
	FScopedPressurePlateWorld TestWorld(TEXT("PressurePlateInitialPressedWorld"));
	if (!TestNotNull(TEXT("Initial Pressed test world exists"), TestWorld.World))
	{
		return false;
	}

	APressurePlateTestActor* Plate = SpawnPlate(*TestWorld.World, TEXT("InitialPressedPressurePlate"), 0.0f, 0.0f);
	APressurePlateTestOccupant* Occupant = SpawnOccupant(
		*TestWorld.World,
		TEXT("PhysicallyOverlappingOccupant"),
		FVector(0.0f, 0.0f, 40.0f));
	if (!TestNotNull(TEXT("Initial Pressed pressure plate exists"), Plate)
		|| !TestNotNull(TEXT("Physical overlap occupant exists"), Occupant))
	{
		return false;
	}

	Plate->InitialInputState = EPuzzleSwitchInitialInputState::Pressed;
	Plate->RequiredOccupantActorTags.Add(TAG_PressurePlateTestHeavy);
	Occupant->EnablePhysicalOverlap();
	TestWorld.StartPlay();

	TestFalse(TEXT("Missing required Actor Tag prevents physical occupancy"), Plate->IsOccupied());
	TestEqual(
		TEXT("Initial Pressed releases normally when the refreshed box has no accepted occupant"),
		Plate->GetInputState(),
		EPuzzleSwitchInputState::Released);
	TestFalse(TEXT("Rejected initial overlap leaves Hold output inactive"), Plate->IsSwitchActive());
	TestEqual(TEXT("Rejected initial overlap leaves the visual plate raised"), Plate->GetPlateMovementAlpha(), 0.0f);

	Occupant->Tags.Add(TAG_PressurePlateTestHeavy);
	Plate->ResetSwitch();
	TestTrue(TEXT("Reset overlap refresh accepts a matching ordinary Actor Tag"), Plate->GetCurrentOccupant() == Occupant);
	TestEqual(TEXT("Accepted initial overlap preserves Pressed input"), Plate->GetInputState(), EPuzzleSwitchInputState::Pressed);
	TestTrue(TEXT("Accepted initial overlap preserves active Hold output"), Plate->IsSwitchActive());
	TestEqual(TEXT("Accepted initial overlap snaps the visual plate pressed"), Plate->GetPlateMovementAlpha(), 1.0f);

	Occupant->Tags.Remove(TAG_PressurePlateTestHeavy);
	Plate->ResetSwitch();
	TestFalse(TEXT("World-style reset reevaluates Actor Tags instead of retaining stale occupancy"), Plate->IsOccupied());
	TestEqual(TEXT("Rejected reset overlap returns to Released"), Plate->GetInputState(), EPuzzleSwitchInputState::Released);
	TestFalse(TEXT("Rejected reset overlap returns output inactive"), Plate->IsSwitchActive());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPressurePlateWorldStateTest,
	"Paradox.PressurePlate.WorldStateRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPressurePlateWorldStateTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::PressurePlate::Tests;
	FScopedPressurePlateWorld TestWorld(TEXT("PressurePlateWorldStateWorld"));
	if (!TestNotNull(TEXT("Pressure Plate World State world exists"), TestWorld.World))
	{
		return false;
	}

	APressurePlateTestActor* Plate = SpawnPlate(*TestWorld.World, TEXT("WorldStatePressurePlate"));
	APressurePlateTestOccupant* Occupant = SpawnOccupant(*TestWorld.World, TEXT("WorldStateOccupant"), FVector(10000.0f, 0.0f, 0.0f));
	TestWorld.StartPlay();
	UWorldStateSubsystem* WorldState = TestWorld.World->GetSubsystem<UWorldStateSubsystem>();
	if (!TestNotNull(TEXT("World State pressure plate exists"), Plate)
		|| !TestNotNull(TEXT("World State occupant exists"), Occupant)
		|| !TestNotNull(TEXT("World State subsystem exists"), WorldState))
	{
		return false;
	}

	TestTrue(TEXT("World State registration finalizes"), WorldState->FinalizeWorldStateRegistration().IsSuccess());
	FWorldStateCaptureRequest CaptureRequest;
	TestTrue(TEXT("Pressure Plate baseline captures"), WorldState->CaptureBaseline(CaptureRequest).IsSuccess());

	Plate->SimulateBeginOverlap(Occupant, Occupant->FirstComponent);
	Plate->Tick(0.5f);
	TestTrue(TEXT("Gameplay mutates captured switch output"), Plate->IsSwitchActive());
	TestTrue(TEXT("Gameplay starts derived plate movement"), Plate->IsPlateMoving());
	const int32 MovementStartsBeforeRestore = Plate->MovementStartedCount;

	FWorldStateRestoreRequest RestoreRequest;
	const FWorldStateRestoreResult RestoreResult = WorldState->RestoreBaseline(RestoreRequest);
	TestTrue(TEXT("Pressure Plate baseline restores"), RestoreResult.IsSuccess());
	TestFalse(TEXT("World State clears transient occupant"), Plate->IsOccupied());
	TestFalse(TEXT("World State restores inherited inactive output"), Plate->IsSwitchActive());
	TestEqual(TEXT("World State snaps derived plate alpha"), Plate->GetPlateMovementAlpha(), 0.0f);
	TestFalse(TEXT("World State leaves movement Tick disabled"), Plate->IsPlateMoving());
	TestEqual(TEXT("World State restoration emits no movement-start presentation"), Plate->MovementStartedCount, MovementStartsBeforeRestore);

	FPuzzleSignalState RestoredSignal;
	TestTrue(TEXT("Restored emitter signal remains queryable"), Plate->PuzzleEmitterComponent->TryGetSignalState(Plate->OutputSignalTag, RestoredSignal));
	TestFalse(TEXT("Restored emitter signal matches inherited output"), RestoredSignal.bIsActive);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPressurePlateWorldStateInitialPressedTest,
	"Paradox.PressurePlate.WorldStateInitialPressedReconciliation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPressurePlateWorldStateInitialPressedTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::PressurePlate::Tests;
	FScopedPressurePlateWorld TestWorld(TEXT("PressurePlateWorldStateInitialPressedWorld"));
	if (!TestNotNull(TEXT("Initial Pressed World State world exists"), TestWorld.World))
	{
		return false;
	}

	APressurePlateTestActor* Plate = SpawnPlate(*TestWorld.World, TEXT("WorldStateInitialPressedPlate"), 0.0f, 0.0f);
	APressurePlateTestOccupant* Occupant = SpawnOccupant(
		*TestWorld.World,
		TEXT("WorldStateInitialPressedOccupant"),
		FVector(0.0f, 0.0f, 40.0f));
	if (!Plate || !Occupant)
	{
		return false;
	}

	Plate->InitialInputState = EPuzzleSwitchInitialInputState::Pressed;
	Plate->RequiredOccupantActorTags.Add(TAG_PressurePlateTestHeavy);
	Occupant->Tags.Add(TAG_PressurePlateTestHeavy);
	Occupant->EnablePhysicalOverlap();
	TestWorld.StartPlay();
	UWorldStateSubsystem* WorldState = TestWorld.World->GetSubsystem<UWorldStateSubsystem>();
	if (!TestNotNull(TEXT("Initial Pressed World State subsystem exists"), WorldState))
	{
		return false;
	}

	TestTrue(TEXT("Valid initial overlap preserves physical occupancy"), Plate->IsOccupied());
	TestEqual(TEXT("Valid initial overlap preserves Pressed input"), Plate->GetInputState(), EPuzzleSwitchInputState::Pressed);
	TestTrue(TEXT("Valid initial overlap preserves active output"), Plate->IsSwitchActive());
	TestTrue(TEXT("Initial Pressed World State registration finalizes"), WorldState->FinalizeWorldStateRegistration().IsSuccess());
	FWorldStateCaptureRequest CaptureRequest;
	TestTrue(TEXT("Initial Pressed baseline captures"), WorldState->CaptureBaseline(CaptureRequest).IsSuccess());

	Occupant->Tags.Remove(TAG_PressurePlateTestHeavy);
	FWorldStateRestoreRequest RestoreRequest;
	TestTrue(TEXT("Initial Pressed baseline restores with rejected overlap"), WorldState->RestoreBaseline(RestoreRequest).IsSuccess());
	TestFalse(TEXT("World State reset clears occupant that no longer passes Actor Tags"), Plate->IsOccupied());
	TestEqual(TEXT("Rejected post-reset overlap normally releases initial Pressed input"), Plate->GetInputState(), EPuzzleSwitchInputState::Released);
	TestFalse(TEXT("Rejected post-reset overlap deactivates Hold output"), Plate->IsSwitchActive());

	Occupant->Tags.Add(TAG_PressurePlateTestHeavy);
	TestTrue(TEXT("Initial Pressed baseline restores again with valid overlap"), WorldState->RestoreBaseline(RestoreRequest).IsSuccess());
	TestTrue(TEXT("Valid post-reset overlap reacquires the occupant"), Plate->GetCurrentOccupant() == Occupant);
	TestEqual(TEXT("Valid post-reset overlap retains restored Pressed input"), Plate->GetInputState(), EPuzzleSwitchInputState::Pressed);
	TestTrue(TEXT("Valid post-reset overlap retains active output"), Plate->IsSwitchActive());
	TestEqual(TEXT("Valid post-reset overlap snaps visual presentation pressed"), Plate->GetPlateMovementAlpha(), 1.0f);

	return true;
}

#endif
