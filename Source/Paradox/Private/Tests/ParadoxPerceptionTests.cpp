#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Characters/ParadoxPlayerCharacter.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

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

#endif
