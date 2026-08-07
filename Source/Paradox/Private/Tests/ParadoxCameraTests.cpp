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
#include "EnhancedActionKeyMapping.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
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
			Controller.GetCurrentCameraOrientation(),
			Width,
			AspectRatio,
			OutCorners);
	}

	static float CalculateMaximumWidth(
		const AParadoxPlayerController& Controller,
		const float AspectRatio)
	{
		return Controller.CalculateMaximumCompatibleOrthoWidth(
			Controller.GetCurrentCameraOrientation(),
			AspectRatio);
	}

	static float CalculateRotationSafeMaximumWidth(
		const AParadoxPlayerController& Controller,
		const float AspectRatio)
	{
		return Controller.CalculateMaximumRotationSafeOrthoWidth(AspectRatio);
	}

	static FVector ClampFocus(
		const AParadoxPlayerController& Controller,
		const FVector& Focus,
		const float Width,
		const float AspectRatio)
	{
		return Controller.ClampCameraFocus(
			Focus,
			Controller.GetCurrentCameraOrientation(),
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

	static bool RequestRotation(
		AParadoxPlayerController& Controller,
		const int32 Direction)
	{
		return Controller.RequestCameraRotation(Direction);
	}

	static void TriggerRotateLeft(AParadoxPlayerController& Controller)
	{
		Controller.OnCameraRotateLeftTriggered();
	}

	static void TriggerRotateRight(AParadoxPlayerController& Controller)
	{
		Controller.OnCameraRotateRightTriggered();
	}

	static void ApplyZoomInput(
		AParadoxPlayerController& Controller,
		const float Value)
	{
		Controller.OnCameraZoomTriggered(FInputActionValue(Value));
	}

	static void Update(
		AParadoxPlayerController& Controller,
		const float RealDeltaSeconds)
	{
		Controller.UpdateFreeCamera(RealDeltaSeconds);
	}

	static bool IsRotating(const AParadoxPlayerController& Controller)
	{
		return Controller.bCameraRotationActive;
	}

	static int32 GetQuarterTurnIndex(
		const AParadoxPlayerController& Controller)
	{
		return Controller.CurrentCameraQuarterTurnIndex;
	}

	static FRotator GetOrientation(
		const AParadoxPlayerController& Controller)
	{
		return Controller.GetCurrentCameraOrientation();
	}

	static float GetYawOffset(
		const AParadoxPlayerController& Controller)
	{
		return Controller.GetCurrentCameraYawOffset();
	}

	static const FParadoxCameraConfiguration& GetConfiguration(
		const AParadoxPlayerController& Controller)
	{
		return Controller.ActiveCameraConfiguration;
	}

	static void SetFocus(
		AParadoxPlayerController& Controller,
		const FVector& Focus)
	{
		Controller.CameraFocusLocation = Focus;
	}

	static void SetMoveInput(
		AParadoxPlayerController& Controller,
		const FVector2D& Input)
	{
		Controller.CameraMoveInput = Input;
	}

	static UInputAction* GetRotateLeftAction(
		const AParadoxPlayerController& Controller)
	{
		return Controller.CameraRotateLeftAction;
	}

	static UInputAction* GetRotateRightAction(
		const AParadoxPlayerController& Controller)
	{
		return Controller.CameraRotateRightAction;
	}

	static UInputMappingContext* GetDefaultMappingContext(
		const AParadoxPlayerController& Controller)
	{
		return Controller.DefaultMappingContext;
	}

	static bool PerformsFullTickWhenPaused(
		const AParadoxPlayerController& Controller)
	{
		return Controller.bShouldPerformFullTickWhenPaused;
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
				TEXT("/Game/Characters/Astronaut/Blueprints/BP_PlayerController.BP_PlayerController_C"));
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
	FParadoxCameraQuarterTurnTest,
	"Paradox.Camera.QuarterTurnStateBoundsAndInputAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCameraQuarterTurnTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Camera::Tests;
	FScopedCameraWorld Scope(TEXT("ParadoxCameraQuarterTurnWorld"));
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

	TestNull(
		TEXT("Quarter-turn camera works without a possessed Pawn"),
		Controller->GetPawn());
	TestTrue(
		TEXT("Controller performs full input and camera tick while paused"),
		FParadoxCameraTestAccessor::PerformsFullTickWhenPaused(*Controller));
	const FParadoxCameraConfiguration& Configuration =
		FParadoxCameraTestAccessor::GetConfiguration(*Controller);
	const FRotator BaseOrientation = Configuration.Orientation;

	FParadoxCameraTestAccessor::TriggerRotateLeft(*Controller);
	TestTrue(
		TEXT("Q/left quarter turn starts in the inverted positive-yaw direction"),
		FParadoxCameraTestAccessor::IsRotating(*Controller));
	FParadoxCameraTestAccessor::TriggerRotateRight(*Controller);
	FParadoxCameraTestAccessor::Update(
		*Controller,
		Configuration.RotationDuration * 0.5f);
	const float MidYaw = FParadoxCameraTestAccessor::GetYawOffset(*Controller);
	TestTrue(
		TEXT("Concurrent E/right input is ignored without reversing the positive-yaw target"),
		MidYaw > 0.0f && MidYaw < 90.0f);
	FParadoxCameraTestAccessor::Update(
		*Controller,
		Configuration.RotationDuration);
	TestFalse(
		TEXT("Quarter turn returns to idle"),
		FParadoxCameraTestAccessor::IsRotating(*Controller));
	TestEqual(
		TEXT("Q/left turn commits one positive-yaw discrete step"),
		FParadoxCameraTestAccessor::GetQuarterTurnIndex(*Controller),
		1);
	TestTrue(
		TEXT("Q/left turn ends at exactly base yaw plus 90 degrees"),
		FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(
			BaseOrientation.Yaw + 90.0f,
			FParadoxCameraTestAccessor::GetOrientation(*Controller).Yaw),
			KINDA_SMALL_NUMBER));

	FParadoxCameraTestAccessor::TriggerRotateRight(*Controller);
	TestTrue(
		TEXT("E/right quarter turn starts after completion"),
		FParadoxCameraTestAccessor::IsRotating(*Controller));
	FParadoxCameraTestAccessor::Update(
		*Controller,
		Configuration.RotationDuration);
	TestEqual(
		TEXT("E/right turn applies negative Yaw and returns to the base step"),
		FParadoxCameraTestAccessor::GetQuarterTurnIndex(*Controller),
		0);

	for (int32 RotationIndex = 0; RotationIndex < 256; ++RotationIndex)
	{
		if (!FParadoxCameraTestAccessor::RequestRotation(*Controller, 1))
		{
			AddError(FString::Printf(
				TEXT("Rotation %d was unexpectedly rejected."),
				RotationIndex));
			break;
		}
		FParadoxCameraTestAccessor::Update(
			*Controller,
			Configuration.RotationDuration);
	}
	TestEqual(
		TEXT("Many quarter turns wrap to the exact discrete base index"),
		FParadoxCameraTestAccessor::GetQuarterTurnIndex(*Controller),
		0);
	TestTrue(
		TEXT("Many quarter turns produce no accumulated yaw drift"),
		FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(
			BaseOrientation.Yaw,
			FParadoxCameraTestAccessor::GetOrientation(*Controller).Yaw),
			KINDA_SMALL_NUMBER));

	FParadoxCameraTestAccessor::SetFocus(
		*Controller,
		FVector(100000.0, -100000.0, 0.0));
	TestTrue(
		TEXT("Bounds-constrained rotation starts"),
		FParadoxCameraTestAccessor::RequestRotation(*Controller, 1));
	for (int32 Step = 0; Step < 30; ++Step)
	{
		FParadoxCameraTestAccessor::Update(
			*Controller,
			Configuration.RotationDuration / 30.0f);
		TArray<FVector> Corners;
		if (!FParadoxCameraTestAccessor::CalculateFootprint(
			*Controller,
			Controller->GetCameraFocusLocation(),
			Controller->GetCurrentCameraOrthoWidth(),
			Configuration.FallbackAspectRatio,
			Corners))
		{
			AddError(TEXT("Intermediate rotated footprint could not be calculated."));
			continue;
		}
		const FBox Bounds = Volume->GetCameraWorldBounds();
		for (const FVector& Corner : Corners)
		{
			TestTrue(
				TEXT("Intermediate rotated corner remains inside X bounds"),
				Corner.X >= Bounds.Min.X + Configuration.BoundaryMargin - 1.0f
					&& Corner.X <= Bounds.Max.X - Configuration.BoundaryMargin + 1.0f);
			TestTrue(
				TEXT("Intermediate rotated corner remains inside Y bounds"),
				Corner.Y >= Bounds.Min.Y + Configuration.BoundaryMargin - 1.0f
					&& Corner.Y <= Bounds.Max.Y - Configuration.BoundaryMargin + 1.0f);
		}
	}

	UInputAction* RotateLeft =
		FParadoxCameraTestAccessor::GetRotateLeftAction(*Controller);
	UInputAction* RotateRight =
		FParadoxCameraTestAccessor::GetRotateRightAction(*Controller);
	UInputMappingContext* MappingContext =
		FParadoxCameraTestAccessor::GetDefaultMappingContext(*Controller);
	if (TestNotNull(TEXT("Left rotation action is assigned"), RotateLeft)
		&& TestNotNull(TEXT("Right rotation action is assigned"), RotateRight))
	{
		TestEqual(
			TEXT("Left rotation action is digital"),
			RotateLeft->ValueType,
			EInputActionValueType::Boolean);
		TestEqual(
			TEXT("Right rotation action is digital"),
			RotateRight->ValueType,
			EInputActionValueType::Boolean);
		TestTrue(TEXT("Left rotation triggers while paused"), RotateLeft->bTriggerWhenPaused);
		TestTrue(TEXT("Right rotation triggers while paused"), RotateRight->bTriggerWhenPaused);
		TestTrue(TEXT("Left action has no repeating trigger"), RotateLeft->Triggers.IsEmpty());
		TestTrue(TEXT("Right action has no repeating trigger"), RotateRight->Triggers.IsEmpty());
	}
	if (TestNotNull(TEXT("Default mapping context is assigned"), MappingContext))
	{
		bool bFoundLeftQ = false;
		bool bFoundRightE = false;
		for (const FEnhancedActionKeyMapping& Mapping : MappingContext->GetMappings())
		{
			bFoundLeftQ |= Mapping.Action == RotateLeft && Mapping.Key == EKeys::Q;
			bFoundRightE |= Mapping.Action == RotateRight && Mapping.Key == EKeys::E;
		}
		TestTrue(TEXT("Q maps to the left rotation action"), bFoundLeftQ);
		TestTrue(TEXT("E maps to the right rotation action"), bFoundRightE);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCameraScreenRelativeMovementTest,
	"Paradox.Camera.ScreenRelativeMovementAfterQuarterTurns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCameraScreenRelativeMovementTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Camera::Tests;
	FScopedCameraWorld Scope(TEXT("ParadoxCameraScreenRelativeMovementWorld"));
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

	const float RotationDuration =
		FParadoxCameraTestAccessor::GetConfiguration(*Controller).RotationDuration;
	for (int32 QuarterTurn = 0; QuarterTurn < 4; ++QuarterTurn)
	{
		const FRotator Orientation =
			FParadoxCameraTestAccessor::GetOrientation(*Controller);
		const FVector2D ExpectedForward = FVector2D(
			Orientation.Vector().X,
			Orientation.Vector().Y).GetSafeNormal();
		FParadoxCameraTestAccessor::SetFocus(
			*Controller,
			Volume->GetCameraLogicalCenter());
		const FVector Before = Controller->GetCameraFocusLocation();
		FParadoxCameraTestAccessor::SetMoveInput(
			*Controller,
			FVector2D(0.0f, 1.0f));
		FParadoxCameraTestAccessor::Update(*Controller, 0.05f);
		FParadoxCameraTestAccessor::SetMoveInput(
			*Controller,
			FVector2D::ZeroVector);
		const FVector Delta = Controller->GetCameraFocusLocation() - Before;
		const FVector2D ActualDelta(Delta.X, Delta.Y);
		TestTrue(
			*FString::Printf(
				TEXT("Forward movement follows screen top at quarter turn %d"),
				QuarterTurn),
			!ActualDelta.IsNearlyZero()
				&& FVector2D::DotProduct(
					ActualDelta.GetSafeNormal(),
					ExpectedForward) > 0.999f);

		TestTrue(
			TEXT("Next orientation starts"),
			FParadoxCameraTestAccessor::RequestRotation(*Controller, 1));
		FParadoxCameraTestAccessor::Update(*Controller, RotationDuration);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxCameraRotationSafeZoomTest,
	"Paradox.Camera.RotationSafeZoomAdaptsToCurrentVolume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxCameraRotationSafeZoomTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::Camera::Tests;
	UParadoxCameraSettings* Settings = GetMutableDefault<UParadoxCameraSettings>();
	const FParadoxCameraConfiguration SavedConfiguration =
		Settings->DefaultConfiguration;
	Settings->DefaultConfiguration.InitialOrthoWidth = 1500.0f;
	Settings->DefaultConfiguration.MinimumOrthoWidth = 800.0f;
	Settings->DefaultConfiguration.MaximumOrthoWidth = 2400.0f;

	{
		FScopedCameraWorld Scope(TEXT("ParadoxCameraRotationSafeZoomWorld"));
		AParadoxCameraBoundsVolume* Volume = Scope.SpawnVolume();
		if (Volume && Volume->GetBoundsComponent())
		{
			Volume->GetBoundsComponent()->SetBoxExtent(
				FVector(900.0, 900.0, 500.0));
		}
		AParadoxPlayerController* Controller = Scope.SpawnController();
		if (TestNotNull(TEXT("Narrow rotation volume exists"), Volume)
			&& TestNotNull(TEXT("Controller exists"), Controller)
			&& TestTrue(
				TEXT("Endpoint-compatible base camera initializes"),
				Controller->EnsureFreeCameraInitialized(true).IsSuccess()))
		{
			const FParadoxCameraConfiguration& Configuration =
				FParadoxCameraTestAccessor::GetConfiguration(*Controller);
			const float AspectRatio = Configuration.FallbackAspectRatio;
			const float NarrowSafeMaximum =
				FParadoxCameraTestAccessor::CalculateRotationSafeMaximumWidth(
					*Controller,
					AspectRatio);
			TestTrue(
				TEXT("Narrow volume requires a smaller width for intermediate angles"),
				NarrowSafeMaximum < Configuration.InitialOrthoWidth);
			TestTrue(
				TEXT("Initialization dynamically applies the current volume's rotation-safe limit"),
				FMath::IsNearlyEqual(
					Controller->GetCurrentCameraOrthoWidth(),
					NarrowSafeMaximum,
					0.1f));

			FParadoxCameraTestAccessor::ApplyZoomInput(*Controller, -1000.0f);
			TestTrue(
				TEXT("Manual zoom-out cannot exceed the current rotation-safe limit"),
				FMath::IsNearlyEqual(
					Controller->GetCurrentCameraOrthoWidth(),
					NarrowSafeMaximum,
					0.1f));
			const float WidthBeforeRotation =
				Controller->GetCurrentCameraOrthoWidth();
			TestTrue(
				TEXT("A quarter turn remains available at the safe zoom-out limit"),
				FParadoxCameraTestAccessor::RequestRotation(*Controller, 1));
			TestEqual(
				TEXT("Starting a rotation does not change zoom"),
				Controller->GetCurrentCameraOrthoWidth(),
				WidthBeforeRotation);
			FParadoxCameraTestAccessor::Update(
				*Controller,
				Configuration.RotationDuration);
			TestEqual(
				TEXT("Completing a rotation does not change safe zoom"),
				Controller->GetCurrentCameraOrthoWidth(),
				WidthBeforeRotation);

			Volume->GetBoundsComponent()->SetBoxExtent(
				FVector(3000.0, 3000.0, 500.0));
			const float ExpandedSafeMaximum =
				FParadoxCameraTestAccessor::CalculateRotationSafeMaximumWidth(
					*Controller,
					AspectRatio);
			TestTrue(
				TEXT("Rotation-safe width is recalculated from expanded current bounds"),
				ExpandedSafeMaximum > NarrowSafeMaximum);
			FParadoxCameraTestAccessor::ApplyZoomInput(*Controller, -1000.0f);
			TestTrue(
				TEXT("Designer Maximum Ortho Width remains the ceiling in a large volume"),
				FMath::IsNearlyEqual(
					Controller->GetCurrentCameraOrthoWidth(),
					FMath::Min(
						Configuration.MaximumOrthoWidth,
						ExpandedSafeMaximum),
					0.1f));

			Volume->GetBoundsComponent()->SetBoxExtent(
				FVector(900.0, 900.0, 500.0));
			FParadoxCameraTestAccessor::Update(*Controller, 0.0f);
			const float UpdatedNarrowMaximum =
				FParadoxCameraTestAccessor::CalculateRotationSafeMaximumWidth(
					*Controller,
					AspectRatio);
			TestTrue(
				TEXT("Runtime bounds shrink immediately reapplies the dynamic safe limit"),
				FMath::IsNearlyEqual(
					Controller->GetCurrentCameraOrthoWidth(),
					UpdatedNarrowMaximum,
					0.1f));
			TestTrue(
				TEXT("Rotation remains available after dynamic bounds retuning"),
				FParadoxCameraTestAccessor::RequestRotation(*Controller, -1));
		}
	}

	Settings->DefaultConfiguration = SavedConfiguration;
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
	Settings->DefaultConfiguration.RotationDuration = 0.0f;

	{
		FScopedCameraWorld Scope(TEXT("ParadoxCameraInvalidRotationDurationWorld"));
		Scope.SpawnVolume();
		AParadoxPlayerController* Controller = Scope.SpawnController();
		if (TestNotNull(TEXT("Invalid-duration controller exists"), Controller))
		{
			TestEqual(
				TEXT("Non-positive rotation duration is rejected"),
				Controller->EnsureFreeCameraInitialized(true).Status,
				EParadoxCameraOperationStatus::InvalidConfiguration);
		}
	}
	Settings->DefaultConfiguration = SavedConfiguration;
	Settings->DefaultConfiguration.RotationEasing = EAlphaBlendOption::Custom;

	{
		FScopedCameraWorld Scope(TEXT("ParadoxCameraInvalidRotationEasingWorld"));
		Scope.SpawnVolume();
		AParadoxPlayerController* Controller = Scope.SpawnController();
		if (TestNotNull(TEXT("Invalid-easing controller exists"), Controller))
		{
			TestEqual(
				TEXT("Custom rotation easing without a curve is rejected"),
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
