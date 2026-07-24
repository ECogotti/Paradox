#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Camera/ParadoxCameraBoundsVolume.h"
#include "Camera/ParadoxCameraRig.h"
#include "Components/BoxComponent.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Settings/ParadoxCameraSettings.h"

struct FParadoxCameraTestAccessor
{
	static bool CalculateFootprint(
		const AParadoxPlayerController& Controller,
		const FVector& Focus,
		const float Width,
		const float AspectRatio,
		TArray<FVector>& OutCorners)
	{
		return Controller.CalculateFootprint(
			Focus,
			Width,
			AspectRatio,
			OutCorners);
	}

	static float CalculateMaximumWidth(
		const AParadoxPlayerController& Controller,
		const float AspectRatio)
	{
		return Controller.CalculateMaximumCompatibleOrthoWidth(
			AspectRatio);
	}

	static FVector ClampFocus(
		const AParadoxPlayerController& Controller,
		const FVector& Focus,
		const float Width,
		const float AspectRatio)
	{
		return Controller.ClampCameraFocus(
			Focus,
			Width,
			AspectRatio);
	}

	static void SimulatePausedCameraPan(
		AParadoxPlayerController& Controller,
		const FVector2D Input,
		const float RealDeltaSeconds)
	{
		Controller.CameraMoveInput = Input;
		Controller.UpdateFreeCamera(RealDeltaSeconds);
		Controller.CameraMoveInput = FVector2D::ZeroVector;
	}
};

namespace UE::Paradox::Camera::Tests
{
	struct FScopedCameraWorld
	{
		explicit FScopedCameraWorld(const TCHAR* Name)
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

		~FScopedCameraWorld()
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

		AParadoxPlayerController* SpawnController() const
		{
			UClass* ControllerClass = LoadObject<UClass>(
				nullptr,
				TEXT("/Game/TopDown/Blueprints/BP_PlayerController.BP_PlayerController_C"));
			if (!ControllerClass
				|| !ControllerClass->IsChildOf(
					AParadoxPlayerController::StaticClass()))
			{
				return nullptr;
			}
			FActorSpawnParameters Parameters;
			Parameters.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AParadoxPlayerController* Controller =
				World->SpawnActor<AParadoxPlayerController>(
				ControllerClass,
				FTransform::Identity,
				Parameters);
			if (Controller && !Controller->PlayerCameraManager)
			{
				// Raw automation worlds do not always execute the complete
				// GameMode/controller bootstrap that creates the camera manager.
				Controller->SpawnPlayerCameraManager();
			}
			return Controller;
		}

		AParadoxCameraBoundsVolume* SpawnVolume(
			const FVector& Location = FVector::ZeroVector) const
		{
			FActorSpawnParameters Parameters;
			Parameters.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<AParadoxCameraBoundsVolume>(
				AParadoxCameraBoundsVolume::StaticClass(),
				FTransform(Location),
				Parameters);
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCameraInitializationTest,
	"Paradox.Camera.InitializationAndProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCameraInitializationTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Camera::Tests;
	FScopedCameraWorld Scope(TEXT("ParadoxCameraInitializationWorld"));
	if (!TestNotNull(TEXT("Camera test world exists"), Scope.World))
	{
		return false;
	}
	AParadoxPlayerController* Controller = Scope.SpawnController();
	if (!TestNotNull(TEXT("Project player controller spawns"), Controller))
	{
		return false;
	}

	const FParadoxCameraOperationResult OptionalResult =
		Controller->EnsureFreeCameraInitialized(false);
	TestEqual(
		TEXT("A map without a volume preserves its legacy camera"),
		OptionalResult.Status,
		EParadoxCameraOperationStatus::NotConfigured);

	AParadoxCameraBoundsVolume* Volume = Scope.SpawnVolume();
	TestNotNull(TEXT("Camera bounds volume spawns"), Volume);
	const FParadoxCameraOperationResult RequiredResult =
		Controller->EnsureFreeCameraInitialized(true);
	TestTrue(TEXT("One valid volume initializes the camera"), RequiredResult.IsSuccess());
	AParadoxCameraRig* Rig = Controller->GetFreeCameraRig();
	if (TestNotNull(TEXT("Independent camera rig is created"), Rig))
	{
		TestTrue(
			TEXT("Controller view target is the independent rig"),
			Controller->GetViewTarget() == Rig);
		TestEqual(
			TEXT("Rig uses orthographic projection"),
			Rig->GetCameraComponent()->ProjectionMode,
			ECameraProjectionMode::Orthographic);
		TestTrue(
			TEXT("Rig is not the possessed pawn"),
			!Rig->IsA<APawn>());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCameraBoundsTest,
	"Paradox.Camera.FootprintBoundsAndRealTimePan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCameraBoundsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Camera::Tests;
	FScopedCameraWorld Scope(TEXT("ParadoxCameraBoundsWorld"));
	AParadoxCameraBoundsVolume* Volume = Scope.SpawnVolume();
	AParadoxPlayerController* Controller = Scope.SpawnController();
	if (!TestNotNull(TEXT("Volume exists"), Volume)
		|| !TestNotNull(TEXT("Controller exists"), Controller)
		|| !TestTrue(
			TEXT("Camera initializes"),
			Controller->EnsureFreeCameraInitialized(true).IsSuccess()))
	{
		return false;
	}

	for (const float AspectRatio : { 4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f })
	{
		const float Width = FMath::Min(
			1000.0f,
			FParadoxCameraTestAccessor::CalculateMaximumWidth(
				*Controller,
				AspectRatio));
		const FVector Clamped =
			FParadoxCameraTestAccessor::ClampFocus(
				*Controller,
				FVector(100000.0, -100000.0, 0.0),
				Width,
				AspectRatio);
		TArray<FVector> Corners;
		TestTrue(
			*FString::Printf(TEXT("Footprint calculates at aspect %.3f"), AspectRatio),
			FParadoxCameraTestAccessor::CalculateFootprint(
				*Controller,
				Clamped,
				Width,
				AspectRatio,
				Corners));
		const FBox Bounds = Volume->GetCameraWorldBounds();
		const float Margin =
			Volume->GetEffectiveCameraConfiguration().BoundaryMargin;
		for (const FVector& Corner : Corners)
		{
			TestTrue(
				*FString::Printf(TEXT("Corner X is contained at aspect %.3f"), AspectRatio),
				Corner.X >= Bounds.Min.X + Margin - 1.0f
					&& Corner.X <= Bounds.Max.X - Margin + 1.0f);
			TestTrue(
				*FString::Printf(TEXT("Corner Y is contained at aspect %.3f"), AspectRatio),
				Corner.Y >= Bounds.Min.Y + Margin - 1.0f
					&& Corner.Y <= Bounds.Max.Y - Margin + 1.0f);
		}
	}

	const FVector Before = Controller->GetCameraFocusLocation();
	FParadoxCameraTestAccessor::SimulatePausedCameraPan(
		*Controller,
		FVector2D(1.0f, 0.0f),
		0.1f);
	TestFalse(
		TEXT("Explicit real delta advances the camera independently from world delta"),
		Controller->GetCameraFocusLocation().Equals(Before));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCameraInvalidConfigurationTest,
	"Paradox.Camera.InvalidConfigurationAndDuplicateAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCameraInvalidConfigurationTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Camera::Tests;
	UParadoxCameraSettings* Settings =
		GetMutableDefault<UParadoxCameraSettings>();
	const FParadoxCameraConfiguration SavedConfiguration =
		Settings->DefaultConfiguration;
	Settings->DefaultConfiguration.MinimumOrthoWidth = 3000.0f;
	Settings->DefaultConfiguration.MaximumOrthoWidth = 1000.0f;

	{
		FScopedCameraWorld Scope(TEXT("ParadoxCameraInvalidConfigurationWorld"));
		Scope.SpawnVolume();
		AParadoxPlayerController* Controller = Scope.SpawnController();
		if (TestNotNull(TEXT("Controller exists"), Controller))
		{
			TestEqual(
				TEXT("Inverted zoom range is rejected"),
				Controller->EnsureFreeCameraInitialized(true).Status,
				EParadoxCameraOperationStatus::InvalidConfiguration);
		}
	}
	Settings->DefaultConfiguration = SavedConfiguration;

	{
		FScopedCameraWorld Scope(TEXT("ParadoxCameraDuplicateVolumeWorld"));
		Scope.SpawnVolume(FVector(-1000.0, 0.0, 0.0));
		Scope.SpawnVolume(FVector(1000.0, 0.0, 0.0));
		AParadoxPlayerController* Controller = Scope.SpawnController();
		if (TestNotNull(TEXT("Duplicate-volume controller exists"), Controller))
		{
			TestEqual(
				TEXT("Duplicate camera authorities are rejected"),
				Controller->EnsureFreeCameraInitialized(true).Status,
				EParadoxCameraOperationStatus::MultipleVolumes);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
