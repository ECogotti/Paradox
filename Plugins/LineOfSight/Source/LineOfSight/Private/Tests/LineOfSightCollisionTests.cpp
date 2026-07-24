#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "LineOfSightComponent.h"
#include "PhysicsEngine/BodySetup.h"

namespace UE::LineOfSight::Tests
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

		void StartPlay() const
		{
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};

	ULineOfSightComponent* AddLineOfSightComponent(AActor& Owner)
	{
		ULineOfSightComponent* Component =
			NewObject<ULineOfSightComponent>(&Owner, TEXT("LineOfSight"));
		Owner.AddInstanceComponent(Component);
		Owner.SetRootComponent(Component);
		Component->RegisterComponent();
		Component->OnlyOneArc = true;
		Component->Only_Z_Rotation = true;
		Component->Radius1 = 0.0f;
		Component->Radius2 = 500.0f;
		Component->Angle1 = 35.0f;
		Component->Angle2 = 35.0f;
		Component->BeginAndEndOverlapEvent = false;
		Component->bUseAsyncCooking = false;
		return Component;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLineOfSightDynamicCollisionOptInTest,
	"LineOfSight.DynamicMeshCollision.OptInRefreshAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLineOfSightDynamicCollisionOptInTest::RunTest(const FString& Parameters)
{
	using namespace UE::LineOfSight::Tests;

	FScopedTestWorld TestWorld(TEXT("LineOfSightCollisionWorld"));
	if (!TestNotNull(TEXT("Transient test world exists"), TestWorld.World))
	{
		return false;
	}

	AActor* Owner = TestWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("LineOfSight owner exists"), Owner))
	{
		return false;
	}
	ULineOfSightComponent* LineOfSight = AddLineOfSightComponent(*Owner);
	if (!TestNotNull(TEXT("LineOfSight component exists"), LineOfSight))
	{
		return false;
	}

	TestFalse(
		TEXT("Procedural collision remains disabled by default"),
		LineOfSight->IsDynamicMeshCollisionEnabled());
	LineOfSight->StartLineTrace(
		UEngineTypes::ConvertToTraceType(ECC_Visibility),
		12);
	LineOfSight->StartBuildMesh();
	TestTrue(
		TEXT("Trace and visible mesh refresh synchronously"),
		LineOfSight->RefreshLineTraceAndMesh());

	const FProcMeshSection* CollisionOffSection =
		LineOfSight->GetProcMeshSection(0);
	if (TestNotNull(TEXT("Collision-off section exists"), CollisionOffSection))
	{
		TestFalse(
			TEXT("Compatibility section has no collision"),
			CollisionOffSection->bEnableCollision);
	}

	LineOfSight->SetDynamicMeshCollisionEnabled(true);
	LineOfSight->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	LineOfSight->SetGenerateOverlapEvents(true);
	TestTrue(
		TEXT("Opt-in collision refresh succeeds"),
		LineOfSight->RefreshLineTraceAndMesh());
	const FProcMeshSection* CollisionOnSection =
		LineOfSight->GetProcMeshSection(0);
	if (TestNotNull(TEXT("Collision-on section exists"), CollisionOnSection))
	{
		TestTrue(
			TEXT("Opt-in section participates in collision"),
			CollisionOnSection->bEnableCollision);
		const int32 InitialVertexCount = CollisionOnSection->ProcVertexBuffer.Num();
		LineOfSight->SetRadius2(650.0f);
		TestTrue(
			TEXT("Dynamic geometry refresh succeeds after a shape change"),
			LineOfSight->RefreshLineTraceAndMesh());
		const FProcMeshSection* UpdatedSection =
			LineOfSight->GetProcMeshSection(0);
		TestEqual(
			TEXT("Dynamic refresh preserves the collision topology"),
			UpdatedSection ? UpdatedSection->ProcVertexBuffer.Num() : 0,
			InitialVertexCount);
	}

	LineOfSight->StopBuildMesh();
	TestEqual(
		TEXT("Stopping mesh generation removes collision sections"),
		LineOfSight->GetNumSections(),
		0);
	LineOfSight->StopLineTrace();
	TestFalse(
		TEXT("Stopping LineOfSight leaves no active trace"),
		LineOfSight->LineOfSightIsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLineOfSightPhysicalOverlapTest,
	"LineOfSight.DynamicMeshCollision.PhysicalOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLineOfSightPhysicalOverlapTest::RunTest(const FString& Parameters)
{
	using namespace UE::LineOfSight::Tests;

	FScopedTestWorld TestWorld(TEXT("LineOfSightPhysicalOverlapWorld"));
	if (!TestNotNull(TEXT("Transient test world exists"), TestWorld.World))
	{
		return false;
	}

	AActor* Owner = TestWorld.World->SpawnActor<AActor>();
	ACharacter* Target = TestWorld.World->SpawnActor<ACharacter>(
		ACharacter::StaticClass(),
		FTransform(FVector(200.0f, 0.0f, 0.0f)));
	if (!TestNotNull(TEXT("LineOfSight owner exists"), Owner)
		|| !TestNotNull(TEXT("Overlap target exists"), Target))
	{
		return false;
	}

	ULineOfSightComponent* LineOfSight = AddLineOfSightComponent(*Owner);
	UCapsuleComponent* TargetPrimitive = Target->GetCapsuleComponent();
	TargetPrimitive->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TargetPrimitive->SetCollisionObjectType(ECC_Pawn);
	TargetPrimitive->SetCollisionResponseToAllChannels(ECR_Ignore);
	TargetPrimitive->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	TargetPrimitive->SetGenerateOverlapEvents(true);
	Target->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));

	LineOfSight->SetDynamicMeshCollisionEnabled(true);
	LineOfSight->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	LineOfSight->SetCollisionObjectType(ECC_WorldDynamic);
	LineOfSight->SetCollisionResponseToAllChannels(ECR_Ignore);
	LineOfSight->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	LineOfSight->SetGenerateOverlapEvents(true);
	TestWorld.StartPlay();

	LineOfSight->StartLineTrace(
		UEngineTypes::ConvertToTraceType(ECC_Visibility),
		24);
	LineOfSight->StartBuildMesh();
	TestTrue(
		TEXT("Collision mesh refresh succeeds"),
		LineOfSight->RefreshLineTraceAndMesh());
	LineOfSight->UpdateOverlaps();
	TargetPrimitive->UpdateOverlaps();
	TestEqual(
		TEXT("Target primitive is located inside the view triangle"),
		TargetPrimitive->GetComponentLocation(),
		FVector(200.0f, 0.0f, 0.0f));
	TestTrue(
		TEXT("LineOfSight remains query-enabled"),
		LineOfSight->IsQueryCollisionEnabled());
	TestEqual(
		TEXT("LineOfSight overlaps the Pawn object channel"),
		LineOfSight->GetCollisionResponseToChannel(ECC_Pawn),
		ECR_Overlap);
	TestEqual(
		TEXT("Target overlaps the LineOfSight object channel"),
		TargetPrimitive->GetCollisionResponseToChannel(ECC_WorldDynamic),
		ECR_Overlap);
	TestTrue(
		TEXT("Target capsule remains query-enabled"),
		TargetPrimitive->IsQueryCollisionEnabled());
	TestTrue(
		TEXT("Both components generate overlap events"),
		LineOfSight->GetGenerateOverlapEvents()
			&& TargetPrimitive->GetGenerateOverlapEvents());
	const UBodySetup* BodySetup = LineOfSight->GetBodySetup();
	TestNotNull(TEXT("Procedural mesh has a cooked body setup"), BodySetup);
	if (BodySetup)
	{
		TestTrue(
			TEXT("Procedural mesh owns exact simple collision prisms"),
			BodySetup->AggGeom.ConvexElems.Num() > 0);
		TestTrue(
			TEXT("Every simple collision prism has a cooked Chaos convex"),
			BodySetup->AggGeom.ConvexElems.Num() > 0
				&& !BodySetup->AggGeom.ConvexElems.ContainsByPredicate(
					[](const FKConvexElem& Convex)
					{
						return !Convex.GetChaosConvexMesh().IsValid();
					}));
	}
	TestTrue(
		TEXT("Procedural mesh created a physics state"),
		LineOfSight->IsPhysicsStateCreated());
	TestTrue(
		TEXT("Procedural mesh body instance is valid"),
		LineOfSight->BodyInstance.IsValidBodyInstance());
	TestTrue(
		TEXT("Target capsule created a physics state"),
		TargetPrimitive->IsPhysicsStateCreated());
	TestTrue(
		TEXT("Target capsule directly overlaps the procedural collision"),
		LineOfSight->ComponentOverlapComponent(
			TargetPrimitive,
			TargetPrimitive->GetComponentLocation(),
			TargetPrimitive->GetComponentQuat(),
			FCollisionQueryParams::DefaultQueryParam));
	TArray<FOverlapResult> FilteredOverlaps;
	LineOfSight->ComponentOverlapMulti(
		FilteredOverlaps,
		TestWorld.World,
		LineOfSight->GetComponentLocation(),
		LineOfSight->GetComponentQuat(),
		LineOfSight->GetCollisionObjectType());
	TestTrue(
		TEXT("Filtered component overlap query returns the target capsule"),
		FilteredOverlaps.ContainsByPredicate(
			[TargetPrimitive](const FOverlapResult& Result)
			{
				return Result.Component.Get() == TargetPrimitive;
			}));
	const bool bImmediateOverlap = LineOfSight->IsOverlappingActor(Target);
	TestTrue(
		TEXT("Explicit collision refresh publishes the physical overlap immediately"),
		bImmediateOverlap);
	TestWorld.World->Tick(LEVELTICK_All, 1.0f / 60.0f);
	LineOfSight->UpdateOverlaps();
	TargetPrimitive->UpdateOverlaps();
	TestTrue(
		TEXT("The procedural mesh retains its physical overlap after the physics tick"),
		LineOfSight->IsOverlappingActor(Target));
	LineOfSight->SetPauseTrace(true, false, true, false);
	TestFalse(
		TEXT("Pausing the trace authority clears physical overlap state"),
		LineOfSight->IsOverlappingActor(Target));
	TestEqual(
		TEXT("Paused dynamic collision is removed from queries"),
		LineOfSight->GetCollisionEnabled(),
		ECollisionEnabled::NoCollision);
	LineOfSight->SetPauseTrace(false, false, true, false);
	TestTrue(
		TEXT("Resuming trace rebuilds and reconciles physical overlap state"),
		LineOfSight->IsOverlappingActor(Target));

	LineOfSight->StopLineTrace();
	LineOfSight->UpdateOverlaps();
	TestFalse(
		TEXT("Stopping LineOfSight removes the physical overlap"),
		LineOfSight->IsOverlappingActor(Target));
	return true;
}

#endif
