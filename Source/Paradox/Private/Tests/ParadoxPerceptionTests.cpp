#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/ParadoxCloneCharacter.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Components/SphereComponent.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Perception/ParadoxTemporalVisionComponent.h"

namespace UE::Paradox::Perception::Tests
{
	struct FScopedTestWorld
	{
		explicit FScopedTestWorld(const TCHAR* Name)
		{
			Context = GEngine
				? &GEngine->CreateNewWorldContext(EWorldType::Game)
				: nullptr;
			World = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				FName(Name));
			if (World)
			{
				World->AddToRoot();
				World->CreateAISystem();
				World->SetShouldTick(true);
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		void StartPlay() const
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

		UWorld* World = nullptr;
		FWorldContext* Context = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPlayerPerceptionFacingTest,
	"Paradox.Perception.PlayerSightUsesPawnFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPlayerPerceptionFacingTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::Perception::Tests;

	FScopedTestWorld Scope(TEXT("ParadoxPlayerPerceptionFacingWorld"));
	if (!TestNotNull(TEXT("Perception test world"), Scope.World))
	{
		return false;
	}

	UClass* ControllerClass = LoadClass<AParadoxPlayerController>(
		nullptr,
		TEXT("/Game/Characters/Astronaut/Blueprints/BP_PlayerController.BP_PlayerController_C"));
	AParadoxPlayerController* Controller =
		ControllerClass
			? Scope.World->SpawnActor<AParadoxPlayerController>(
				ControllerClass)
			: nullptr;
	AParadoxPlayerCharacter* Pawn =
		Scope.World->SpawnActor<AParadoxPlayerCharacter>();
	if (!TestNotNull(TEXT("Paradox Player Controller"), Controller)
		|| !TestNotNull(TEXT("Paradox Player Character"), Pawn))
	{
		return false;
	}

	Controller->Possess(Pawn);
	Controller->SetControlRotation(FRotator(0.0, -70.0, 0.0));
	Pawn->SetActorRotation(FRotator(0.0, 125.0, 0.0));

	TestFalse(
		TEXT("Test separates top-down Control Rotation from Pawn facing"),
		Controller->GetControlRotation().Vector().Equals(
			Pawn->GetActorForwardVector(),
			0.01));

	FVector EyeLocation = FVector::ZeroVector;
	FRotator EyeRotation = FRotator::ZeroRotator;
	Controller->GetActorEyesViewPoint(EyeLocation, EyeRotation);
	TestTrue(
		TEXT("Controller gameplay eyes use the Pawn facing"),
		EyeRotation.Vector().Equals(
			Pawn->GetActorForwardVector(),
			0.01));

	UPerceptionKnowledgeListenerComponent* Listener =
		Controller->GetPerceptionKnowledgeListener();
	if (!TestNotNull(TEXT("Player perception listener"), Listener))
	{
		return false;
	}

	FVector ListenerLocation = FVector::ZeroVector;
	FVector ListenerDirection = FVector::ZeroVector;
	TestTrue(
		TEXT("Listener has a valid possessed-Pawn viewpoint"),
		Listener->GetListenerViewpoint(
			ListenerLocation,
			ListenerDirection));
	TestTrue(
		TEXT("Native Sight direction follows the Pawn instead of Control Rotation"),
		ListenerDirection.Equals(
			Pawn->GetActorForwardVector(),
			0.01));

	Pawn->SetActorRotation(FRotator(0.0, 25.0, 0.0));
	TestTrue(
		TEXT("Listener viewpoint remains valid after a physical Pawn turn"),
		Listener->GetListenerViewpoint(
			ListenerLocation,
			ListenerDirection));
	TestTrue(
		TEXT("Sight direction immediately reflects the new Pawn facing"),
		ListenerDirection.Equals(
			Pawn->GetActorForwardVector(),
			0.01));
	TestFalse(
		TEXT("Pawn turn does not require changing the independent Control Rotation"),
		ListenerDirection.Equals(
			Controller->GetControlRotation().Vector(),
			0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxTemporalVisionSphereFilterTest,
	"Paradox.Perception.TemporalVisionSphereFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxTemporalVisionSphereFilterTest::RunTest(
	const FString& Parameters)
{
	using namespace UE::Paradox::Perception::Tests;

	FScopedTestWorld Scope(TEXT("ParadoxTemporalVisionSphereWorld"));
	if (!TestNotNull(TEXT("Temporal Vision test world"), Scope.World))
	{
		return false;
	}

	AParadoxCloneCharacter* Clone =
		Scope.World->SpawnActor<AParadoxCloneCharacter>(
			AParadoxCloneCharacter::StaticClass(),
			FVector::ZeroVector,
			FRotator::ZeroRotator);
	AParadoxPlayerCharacter* Target =
		Scope.World->SpawnActor<AParadoxPlayerCharacter>(
			AParadoxPlayerCharacter::StaticClass(),
			FVector(450.0, 0.0, 0.0),
			FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Temporal Vision clone"), Clone)
		|| !TestNotNull(TEXT("Temporal Vision Pawn target"), Target))
	{
		return false;
	}
	Scope.StartPlay();

	UParadoxTemporalVisionComponent* Vision = Clone->GetTemporalVisionComponent();
	USphereComponent* CandidateSphere = Clone->GetTemporalVisionCandidateSphere();
	if (!TestNotNull(TEXT("Temporal Vision component"), Vision)
		|| !TestNotNull(TEXT("Temporal Vision candidate sphere"), CandidateSphere))
	{
		return false;
	}

	FString PreparationFailure;
	if (!TestTrue(
		TEXT("Temporal Vision prepares its collisionless mesh and Pawn query sphere"),
		Vision->PrepareTemporalVision(PreparationFailure)))
	{
		AddError(PreparationFailure);
		return false;
	}
	TestEqual(
		TEXT("Pawn query sphere remains outside persistent physics broad phase"),
		CandidateSphere->GetCollisionEnabled(),
		ECollisionEnabled::NoCollision);
	TestFalse(
		TEXT("Pawn query sphere does not maintain component overlap events"),
		CandidateSphere->GetGenerateOverlapEvents());
	Vision->EnableTemporalDetection(7);
	Scope.World->Tick(ELevelTick::LEVELTICK_All, 1.0f / 60.0f);
	TestEqual(
		TEXT("Pawn inside sphere, range and angle becomes one filtered pair"),
		Vision->GetDeduplicatedOverlapActorCount(),
		1);

	Clone->SetActorRotation(FRotator(0.0, 90.0, 0.0));
	Vision->RefreshTemporalCandidateFilter();
	TestEqual(
		TEXT("Pawn still inside sphere but outside cone angle is rejected"),
		Vision->GetDeduplicatedOverlapActorCount(),
		0);

	Clone->SetActorRotation(FRotator::ZeroRotator);
	Target->SetActorLocation(FVector(1200.0, 0.0, 0.0));
	Vision->SetRadius2(1300.0f);
	Vision->RefreshTemporalCandidateFilter();
	TestEqual(
		TEXT("Runtime radius change resizes the registered sphere"),
		CandidateSphere->GetUnscaledSphereRadius(),
		1300.0f);
	TestEqual(
		TEXT("Pawn entering the expanded range is accepted"),
		Vision->GetDeduplicatedOverlapActorCount(),
		1);

	Clone->SetActorLocation(FVector(1500.0, 0.0, 0.0));
	Vision->RefreshTemporalCandidateFilter();
	TestEqual(
		TEXT("Collisionless sphere query follows a moving clone and reapplies cone angle"),
		Vision->GetDeduplicatedOverlapActorCount(),
		0);
	Clone->SetActorLocation(FVector::ZeroVector);
	Vision->RefreshTemporalCandidateFilter();
	TestEqual(
		TEXT("Collisionless sphere query reacquires after clone movement"),
		Vision->GetDeduplicatedOverlapActorCount(),
		1);

	AActor* Occluder = Scope.World->SpawnActor<AActor>(
		AActor::StaticClass(),
		FVector(600.0, 0.0, 0.0),
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Temporal Vision occluder"), Occluder))
	{
		return false;
	}
	UBoxComponent* OccluderCollision = NewObject<UBoxComponent>(
		Occluder,
		TEXT("TemporalVisionTestOccluder"));
	Occluder->AddInstanceComponent(OccluderCollision);
	Occluder->SetRootComponent(OccluderCollision);
	OccluderCollision->InitBoxExtent(FVector(30.0, 200.0, 200.0));
	OccluderCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	OccluderCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	OccluderCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	OccluderCollision->RegisterComponent();
	OccluderCollision->SetWorldLocation(FVector(600.0, 0.0, 0.0));
	const TArray<FVector> UnoccludedVertices =
		Vision->GetVertexArrayLocalPositionNoRotation();
	Vision->RefreshLineTraceAndMesh();
	const TArray<FVector> OccludedVertices =
		Vision->GetVertexArrayLocalPositionNoRotation();
	bool bVisualMeshDeformed = false;
	const int32 ComparableVertexCount = FMath::Min(
		UnoccludedVertices.Num(),
		OccludedVertices.Num());
	for (int32 VertexIndex = 0;
		VertexIndex < ComparableVertexCount;
		++VertexIndex)
	{
		if (OccludedVertices[VertexIndex].SizeSquared2D()
			+ FMath::Square(1.0f)
			< UnoccludedVertices[VertexIndex].SizeSquared2D())
		{
			bVisualMeshDeformed = true;
			break;
		}
	}
	TestTrue(
		TEXT("Collisionless visual mesh still deforms against Visibility obstacles"),
		bVisualMeshDeformed);
	Vision->RefreshTemporalCandidateFilter();
	TestEqual(
		TEXT("Visibility occluder rejects an otherwise valid sphere candidate"),
		Vision->GetDeduplicatedOverlapActorCount(),
		0);

	return true;
}

#endif
