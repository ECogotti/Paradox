// Copyright Epic Games, Inc. All Rights Reserved.

#include "Controllers/ParadoxPlayerController.h"
#include "Actions/GameplayActionDefinition.h"
#include "Actions/GridMoveToCellActionDefinition.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Camera/CameraComponent.h"
#include "Camera/ParadoxCameraBoundsVolume.h"
#include "Camera/ParadoxCameraRig.h"
#include "Components/GameplayActionComponent.h"
#include "Components/TacticalPauseActionQueueComponent.h"
#include "DrawDebugHelpers.h"
#include "GameplayActionTags.h"
#include "InputAction.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "Characters/ParadoxPlayerCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "AI/GridWorldPathFollowingComponent.h"
#include "Interaction/GridCellPointerComponent.h"
#include "Prediction/GridPathPreviewComponent.h"
#include "Presentation/GridPathLineVisualizationSubsystem.h"
#include "Presentation/GridRuntimeVisualizationSubsystem.h"
#include "InputActionValue.h"
#include "EnhancedInputSubsystems.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Engine/LocalPlayer.h"
#include "GameModes/ParadoxGameMode.h"
#include "Paradox.h"
#include "Presentation/ParadoxOutcomePresentationComponent.h"
#include "Subsystems/TacticalPauseWorldSubsystem.h"
#include "TimeLoop/ParadoxChronoSpawn.h"
#include "TimeLoop/ParadoxTimeLoopComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

namespace
{
	TAutoConsoleVariable<int32> CVarParadoxCameraDebug(
		TEXT("Paradox.Camera.Debug"),
		0,
		TEXT("Globally enables Paradox free-camera bounds and footprint debug drawing."),
		ECVF_Cheat);
}

AParadoxPlayerController::AParadoxPlayerController()
{
	bIsTouch = false;
	bMoveToMouseCursor = false;
	bHasCachedDestination = false;
	// PlayerController already pause-ticks in UE 5.8; full tick keeps pointer projection, camera,
	// Enhanced Input, and path-preview replanning active while gameplay simulation is paused.
	bShouldPerformFullTickWhenPaused = true;

	// Use GridWorld's precise follower so bounds can enforce center-constrained or cell-by-cell movement.
	PathFollowingComponent = CreateDefaultSubobject<UGridWorldPathFollowingComponent>(
		TEXT("Path Following Component"));
	GridCellPointerComponent = CreateDefaultSubobject<UGridCellPointerComponent>(
		TEXT("Grid Cell Pointer Component"));
	GridPathPreviewComponent = CreateDefaultSubobject<UGridPathPreviewComponent>(
		TEXT("Grid Path Preview Component"));
	OutcomePresentationComponent =
		CreateDefaultSubobject<UParadoxOutcomePresentationComponent>(
			TEXT("Outcome Presentation Component"));

	// configure the controller
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	CachedDestination = FVector::ZeroVector;
	FollowTime = 0.f;
	CameraRigClass = AParadoxCameraRig::StaticClass();

	static ConstructorHelpers::FObjectFinder<UGameplayActionDefinition> MoveToGridCellDefinitionFinder(
		TEXT("/GameplayActionsGridWorld/Definitions/DA_GameplayAction_MoveToGridCell.DA_GameplayAction_MoveToGridCell"));
	if (MoveToGridCellDefinitionFinder.Succeeded())
	{
		MoveToGridCellActionDefinition = MoveToGridCellDefinitionFinder.Object;
	}
	GridPathPreviewComponent->GoalContentionPolicy = MoveGoalContentionPolicy;
}

void AParadoxPlayerController::BeginPlay()
{
	Super::BeginPlay();
	EnsureFreeCameraInitialized(false);
	TacticalPauseSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UTacticalPauseWorldSubsystem>()
		: nullptr;
	if (TacticalPauseSubsystem)
	{
		TacticalPauseSubsystem->OnResumedNative().AddUObject(
			this,
			&AParadoxPlayerController::HandleTacticalPauseResumed);
	}
	if (bPresentActivePath)
	{
		if (bPresentActivePathAsCells)
		{
			if (UGridRuntimeVisualizationSubsystem* Cells = GetWorld()->GetSubsystem<UGridRuntimeVisualizationSubsystem>())
			{
				Cells->EnableVisualization();
			}
		}
		if (bPresentActivePathAsLine)
		{
			if (UGridPathLineVisualizationSubsystem* Lines = GetWorld()->GetSubsystem<UGridPathLineVisualizationSubsystem>())
			{
				Lines->EnableLineVisualization();
			}
		}
	}
	if (PathFollowingComponent)
	{
		PathFollowingComponent->SetActivePathPresentationRenderers(
			bPresentActivePathAsCells,
			bPresentActivePathAsLine);
		PathFollowingComponent->SetActivePathPresentationEnabled(bPresentActivePath);
	}
	if (GridPathPreviewComponent && MoveToGridCellActionDefinition)
	{
		const FInstancedPropertyBag& Defaults = MoveToGridCellActionDefinition->GetDefaultParameters();
		const TValueOrError<UClass*, EPropertyBagResult> FilterClass =
			Defaults.GetValueClass(GridMoveToCellActionParameters::FilterClass);
		if (FilterClass.HasValue())
		{
			GridPathPreviewComponent->NavigationFilter = FilterClass.GetValue();
		}
		const TValueOrError<float, EPropertyBagResult> Separation =
			Defaults.GetValueFloat(GridMoveToCellActionParameters::AdditionalGoalSeparation);
		if (Separation.HasValue())
		{
			GridPathPreviewComponent->AdditionalGoalSeparation = Separation.GetValue();
		}
		GridPathPreviewComponent->GoalContentionPolicy = MoveGoalContentionPolicy;
	}
}

void AParadoxPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TacticalPauseSubsystem)
	{
		TacticalPauseSubsystem->OnResumedNative().RemoveAll(this);
	}
	ClearPlannedMovePresentation(false);
	TacticalPauseSubsystem = nullptr;
	if (IsValid(FreeCameraRig) && FreeCameraRig->GetOwner() == this)
	{
		FreeCameraRig->Destroy();
	}
	FreeCameraRig = nullptr;
	CameraBoundsVolume = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AParadoxPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	UpdateFreeCamera(FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.0f, 0.1f));
	if (!IsLocalPlayerController() || bIsTouch)
	{
		return;
	}
	if (IsChronoSpawnSelectionActive())
	{
		UpdateChronoSpawnHover(false);
		if (GridPathPreviewComponent)
		{
			GridPathPreviewComponent->ClearPreview();
		}
	}
	else if (bEnablePointerPathPrediction && IsMovementInputAllowed())
	{
		UpdatePointerPrediction(false);
	}
	else if (GridPathPreviewComponent)
	{
		GridPathPreviewComponent->ClearPreview();
	}
}

void AParadoxPlayerController::SetupInputComponent()
{
	// set up gameplay key bindings
	Super::SetupInputComponent();

	// Only set up input on local player controllers
	if (IsLocalPlayerController())
	{
		// Enhanced Input filters action triggers during world pause unless each action opts in.
		if (SetDestinationClickAction)
		{
			SetDestinationClickAction->bTriggerWhenPaused = true;
		}
		if (SetDestinationTouchAction)
		{
			SetDestinationTouchAction->bTriggerWhenPaused = true;
		}
		if (RewindAction)
		{
			RewindAction->bTriggerWhenPaused = true;
		}
		if (CameraMoveAction)
		{
			CameraMoveAction->bTriggerWhenPaused = true;
		}
		if (CameraZoomAction)
		{
			CameraZoomAction->bTriggerWhenPaused = true;
		}
		if (CameraRecenterAction)
		{
			CameraRecenterAction->bTriggerWhenPaused = true;
		}

		// Add Input Mapping Context
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}

		// Set up action bindings
		if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent))
		{
			// Setup mouse input events
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Started, this, &AParadoxPlayerController::OnInputStarted);
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Triggered, this, &AParadoxPlayerController::OnSetDestinationTriggered);
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Completed, this, &AParadoxPlayerController::OnSetDestinationReleased);
			EnhancedInputComponent->BindAction(SetDestinationClickAction, ETriggerEvent::Canceled, this, &AParadoxPlayerController::OnSetDestinationReleased);

			// Setup touch input events
			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Started, this, &AParadoxPlayerController::OnInputStarted);
			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Triggered, this, &AParadoxPlayerController::OnTouchTriggered);
			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Completed, this, &AParadoxPlayerController::OnTouchReleased);
			EnhancedInputComponent->BindAction(SetDestinationTouchAction, ETriggerEvent::Canceled, this, &AParadoxPlayerController::OnTouchReleased);
			if (RewindAction)
			{
				EnhancedInputComponent->BindAction(
					RewindAction,
					ETriggerEvent::Started,
					this,
					&AParadoxPlayerController::OnRewindTriggered);
			}
			if (CameraMoveAction)
			{
				EnhancedInputComponent->BindAction(
					CameraMoveAction,
					ETriggerEvent::Triggered,
					this,
					&AParadoxPlayerController::OnCameraMoveTriggered);
				EnhancedInputComponent->BindAction(
					CameraMoveAction,
					ETriggerEvent::Completed,
					this,
					&AParadoxPlayerController::OnCameraMoveCompleted);
				EnhancedInputComponent->BindAction(
					CameraMoveAction,
					ETriggerEvent::Canceled,
					this,
					&AParadoxPlayerController::OnCameraMoveCompleted);
			}
			if (CameraZoomAction)
			{
				EnhancedInputComponent->BindAction(
					CameraZoomAction,
					ETriggerEvent::Triggered,
					this,
					&AParadoxPlayerController::OnCameraZoomTriggered);
			}
			if (CameraRecenterAction)
			{
				EnhancedInputComponent->BindAction(
					CameraRecenterAction,
					ETriggerEvent::Started,
					this,
					&AParadoxPlayerController::OnCameraRecenterTriggered);
			}
		}
		else
		{
			PARADOX_LOG_ERROR(
				TEXT("'%s' failed to find an Enhanced Input Component; the controller requires Enhanced Input."),
				*GetNameSafe(this));
		}
	}
}

void AParadoxPlayerController::OnInputStarted()
{
	if (IsChronoSpawnSelectionActive())
	{
		bHasCachedDestination = false;
		UpdateChronoSpawnHover(bIsTouch);
		return;
	}
	if (!IsMovementInputAllowed())
	{
		bHasCachedDestination = false;
		return;
	}

	// Update the move destination to wherever the cursor is pointing at
	bHasCachedDestination = false;
	UpdateCachedDestination();
}

void AParadoxPlayerController::OnSetDestinationTriggered()
{
	if (IsChronoSpawnSelectionActive())
	{
		UpdateChronoSpawnHover(bIsTouch);
		return;
	}
	if (!IsMovementInputAllowed())
	{
		return;
	}

	// We flag that the input is being pressed
	FollowTime += GetWorld()->GetDeltaSeconds();
	
	// Update the move destination to wherever the cursor is pointing at
	UpdateCachedDestination();
}

void AParadoxPlayerController::OnSetDestinationReleased()
{
	if (IsChronoSpawnSelectionActive())
	{
		TrySelectChronoSpawn(bIsTouch);
		FollowTime = 0.f;
		bHasCachedDestination = false;
		return;
	}
	if (!IsMovementInputAllowed())
	{
		FollowTime = 0.f;
		bHasCachedDestination = false;
		return;
	}

	if (bHasCachedDestination)
	{
		const bool bWasTacticalPlanning = IsTacticalPlanningActive();
		FGameplayActionSubmissionResult Submission;
		FGridPathPreviewResult CommittedPreview;
		if (bEnablePointerPathPrediction && GridPathPreviewComponent)
		{
			FGridInjectedPath InjectedPath;
			if (!GridPathPreviewComponent->PreparePreviewForCommit(InjectedPath, CommittedPreview))
			{
				PARADOX_LOG_WARNING(
					TEXT("Predicted GridWorld move was not committed for controller '%s': preview status %d, reason %d."),
					*GetNameSafe(this),
					static_cast<int32>(CommittedPreview.Status),
					static_cast<int32>(CommittedPreview.FailureReason));
				FollowTime = 0.f;
				bHasCachedDestination = false;
				return;
			}
			Submission = RequestMoveAlongGridPath(InjectedPath);
		}
		else
		{
			Submission = RequestMoveToGridCell(CachedDestination);
		}
		if (Submission.IsAccepted())
		{
			if (CommittedPreview.GoalCell.IsValid())
			{
				if (bWasTacticalPlanning)
				{
					// Keep the exact planned path visible and mark its goal while the player
					// continues evaluating alternatives under the paused cursor.
					PresentPlannedMove(CommittedPreview.GoalCell);
				}
				else
				{
					SuppressedPreviewGoalCell = CommittedPreview.GoalCell;
					GridPathPreviewComponent->ClearPreview();
				}
			}
			if (FXCursor)
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(
					this,
					FXCursor,
					CachedDestination,
					FRotator::ZeroRotator,
					FVector(1.f),
					true,
					true,
					ENCPoolMethod::None,
					true);
			}
		}
		else
		{
			PARADOX_LOG_WARNING(
				TEXT("Move To Grid Cell submission failed for controller '%s': %s"),
				*GetNameSafe(this),
				*Submission.DiagnosticMessage);
		}
	}

	FollowTime = 0.f;
	bHasCachedDestination = false;
}

// Triggered every frame when the input is held down
void AParadoxPlayerController::OnTouchTriggered()
{
	bIsTouch = true;
	OnSetDestinationTriggered();
}

void AParadoxPlayerController::OnTouchReleased()
{
	bIsTouch = false;
	if (IsChronoSpawnSelectionActive())
	{
		TrySelectChronoSpawn(true);
		FollowTime = 0.f;
		bHasCachedDestination = false;
		return;
	}
	OnSetDestinationReleased();
}

void AParadoxPlayerController::OnRewindTriggered()
{
	const FParadoxTimeLoopOperationResult Result = RequestTimeRewind();
	if (!Result.IsSuccess())
	{
		PARADOX_LOG_WARNING(
			TEXT("Rewind input was rejected for controller '%s': %s"),
			*GetNameSafe(this),
			*Result.DiagnosticMessage);
	}
}

void AParadoxPlayerController::OnCameraMoveTriggered(const FInputActionValue& Value)
{
	CameraMoveInput = Value.Get<FVector2D>();
	if (!CameraMoveInput.IsNearlyZero())
	{
		bCameraRecenterActive = false;
	}
}

void AParadoxPlayerController::OnCameraMoveCompleted()
{
	CameraMoveInput = FVector2D::ZeroVector;
}

void AParadoxPlayerController::OnCameraZoomTriggered(const FInputActionValue& Value)
{
	if (!IsFreeCameraReady())
	{
		return;
	}

	const float AspectRatio = GetCameraAspectRatio();
	const float MaximumCompatible = CalculateMaximumCompatibleOrthoWidth(AspectRatio);
	const float EffectiveMaximum = FMath::Min(
		ActiveCameraConfiguration.MaximumOrthoWidth,
		MaximumCompatible);
	const float EffectiveMinimum = FMath::Min(
		ActiveCameraConfiguration.MinimumOrthoWidth,
		EffectiveMaximum);
	CurrentOrthoWidth = FMath::Clamp(
		CurrentOrthoWidth
			- Value.Get<float>() * ActiveCameraConfiguration.ZoomUnitsPerStep,
		FMath::Max(1.0f, EffectiveMinimum),
		FMath::Max(1.0f, EffectiveMaximum));
	CameraFocusLocation = ClampCameraFocus(
		CameraFocusLocation,
		CurrentOrthoWidth,
		AspectRatio);
	UpdateFreeCameraPose(AspectRatio);
}

void AParadoxPlayerController::OnCameraRecenterTriggered()
{
	RequestCameraRecenter();
}

void AParadoxPlayerController::UpdateCachedDestination()
{
	if (!IsMovementInputAllowed())
	{
		bHasCachedDestination = false;
		return;
	}

	// We look for the location in the world where the player has pressed the input
	FHitResult Hit;
	bool bHitSuccessful = false;
	if (bIsTouch)
	{
		bHitSuccessful = GetHitResultUnderFinger(ETouchIndex::Touch1, ECollisionChannel::ECC_Visibility, true, Hit);
	}
	else
	{
		bHitSuccessful = GetHitResultUnderCursor(ECollisionChannel::ECC_Visibility, true, Hit);
	}

	// If we hit a surface, cache the location
	if (bHitSuccessful && bEnablePointerPathPrediction && GridCellPointerComponent)
	{
		const FGridCellPointerResult PointerResult = GridCellPointerComponent->UpdateFromHitResult(Hit);
		if (PointerResult.Status == EGridCellPointerStatus::Success)
		{
			CachedDestination = PointerResult.WorldCenter;
			bHasCachedDestination = true;
			if (GridPathPreviewComponent)
			{
				GridPathPreviewComponent->UpdatePreviewForController(this, PointerResult.CellId);
			}
		}
		else
		{
			bHasCachedDestination = false;
			if (GridPathPreviewComponent)
			{
				GridPathPreviewComponent->ClearPreview();
			}
		}
	}
	else if (bHitSuccessful)
	{
		CachedDestination = Hit.Location;
		bHasCachedDestination = true;
	}
}

void AParadoxPlayerController::UpdatePointerPrediction(bool bUseTouchInput)
{
	if (!IsMovementInputAllowed()
		|| !GridCellPointerComponent
		|| !GridPathPreviewComponent)
	{
		return;
	}

	FGridCellPointerResult PointerResult;
	if (bUseTouchInput)
	{
		FHitResult Hit;
		if (GetHitResultUnderFinger(ETouchIndex::Touch1, ECC_Visibility, false, Hit))
		{
			PointerResult = GridCellPointerComponent->UpdateFromHitResult(Hit);
		}
	}
	else
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		if (GetMousePosition(MouseX, MouseY))
		{
			PointerResult = GridCellPointerComponent->UpdateFromScreenPosition(
				this,
				FVector2D(MouseX, MouseY));
		}
	}

	if (PointerResult.Status != EGridCellPointerStatus::Success)
	{
		if (GridPathPreviewComponent->GetLatestPreview().Status != EGridQueryStatus::Cancelled)
		{
			GridPathPreviewComponent->ClearPreview();
		}
		bHasCachedDestination = false;
		return;
	}
	if (SuppressedPreviewGoalCell.IsValid())
	{
		if (PointerResult.CellId == SuppressedPreviewGoalCell)
		{
			return;
		}
		SuppressedPreviewGoalCell = FGridCellId();
	}
	CachedDestination = PointerResult.WorldCenter;
	bHasCachedDestination = true;
	GridPathPreviewComponent->UpdatePreviewForController(this, PointerResult.CellId);
}

void AParadoxPlayerController::UpdateChronoSpawnHover(const bool bUseTouchInput)
{
	UParadoxTimeLoopComponent* TimeLoop = GetTimeLoopComponent();
	if (!TimeLoop)
	{
		return;
	}

	FHitResult Hit;
	const bool bHit = bUseTouchInput
		? GetHitResultUnderFinger(ETouchIndex::Touch1, ECC_Visibility, false, Hit)
		: GetHitResultUnderCursor(ECC_Visibility, false, Hit);
	TimeLoop->UpdateHoveredChronoSpawn(
		bHit ? Cast<AParadoxChronoSpawn>(Hit.GetActor()) : nullptr);
}

void AParadoxPlayerController::TrySelectChronoSpawn(const bool bUseTouchInput)
{
	UParadoxTimeLoopComponent* TimeLoop = GetTimeLoopComponent();
	if (!TimeLoop)
	{
		return;
	}

	FHitResult Hit;
	const bool bHit = bUseTouchInput
		? GetHitResultUnderFinger(ETouchIndex::Touch1, ECC_Visibility, true, Hit)
		: GetHitResultUnderCursor(ECC_Visibility, true, Hit);
	AParadoxChronoSpawn* Spawn =
		bHit ? Cast<AParadoxChronoSpawn>(Hit.GetActor()) : nullptr;
	const FParadoxTimeLoopOperationResult Result = TimeLoop->SelectChronoSpawn(Spawn);
	if (!Result.IsSuccess())
	{
		PARADOX_LOG_WARNING(
			TEXT("Chrono Spawn click was rejected for controller '%s': %s"),
			*GetNameSafe(this),
			*Result.DiagnosticMessage);
	}
}

UParadoxTimeLoopComponent* AParadoxPlayerController::GetTimeLoopComponent() const
{
	const UWorld* World = GetWorld();
	const AParadoxGameMode* GameMode =
		World ? Cast<AParadoxGameMode>(World->GetAuthGameMode()) : nullptr;
	return GameMode ? GameMode->GetTimeLoopComponent() : nullptr;
}

bool AParadoxPlayerController::IsChronoSpawnSelectionActive() const
{
	const UParadoxTimeLoopComponent* TimeLoop = GetTimeLoopComponent();
	return TimeLoop
		&& TimeLoop->IsTimeLoopEnabled()
		&& TimeLoop->GetCurrentPhase() == EParadoxTimeLoopPhase::ChronoSpawnSelection;
}

bool AParadoxPlayerController::IsMovementInputAllowed() const
{
	const UParadoxTimeLoopComponent* TimeLoop = GetTimeLoopComponent();
	return !TimeLoop || TimeLoop->IsMovementAllowed();
}

bool AParadoxPlayerController::IsTacticalPlanningActive() const
{
	return TacticalPauseSubsystem && TacticalPauseSubsystem->IsPaused();
}

FParadoxCameraOperationResult AParadoxPlayerController::EnsureFreeCameraInitialized(
	const bool bRequiredForTimeLoop)
{
	if (IsFreeCameraReady())
	{
		return CameraInitializationResult;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		CameraInitializationResult.Status = EParadoxCameraOperationStatus::InvalidWorld;
		CameraInitializationResult.DiagnosticMessage =
			TEXT("The free camera cannot initialize without a valid world.");
		return CameraInitializationResult;
	}

	TArray<AParadoxCameraBoundsVolume*> EnabledVolumes;
	for (TActorIterator<AParadoxCameraBoundsVolume> It(World); It; ++It)
	{
		if (It->IsCameraVolumeEnabled())
		{
			EnabledVolumes.Add(*It);
		}
	}
	if (EnabledVolumes.IsEmpty())
	{
		CameraInitializationResult.Status = bRequiredForTimeLoop
			? EParadoxCameraOperationStatus::MissingVolume
			: EParadoxCameraOperationStatus::NotConfigured;
		CameraInitializationResult.DiagnosticMessage = bRequiredForTimeLoop
			? TEXT("The time-loop map has no enabled Paradox Camera Bounds Volume.")
			: TEXT("No Paradox Camera Bounds Volume is present; the map keeps its legacy camera.");
		return CameraInitializationResult;
	}
	if (EnabledVolumes.Num() != 1)
	{
		CameraInitializationResult.Status = EParadoxCameraOperationStatus::MultipleVolumes;
		CameraInitializationResult.DiagnosticMessage = FString::Printf(
			TEXT("Expected exactly one enabled Paradox Camera Bounds Volume, found %d."),
			EnabledVolumes.Num());
		return CameraInitializationResult;
	}

	CameraBoundsVolume = EnabledVolumes[0];
	ActiveCameraConfiguration =
		CameraBoundsVolume->GetEffectiveCameraConfiguration();
	const float AspectRatio = GetCameraAspectRatio();
	FString ConfigurationFailure;
	if (!ValidateCameraConfiguration(
			*CameraBoundsVolume,
			ActiveCameraConfiguration,
			AspectRatio,
			ConfigurationFailure))
	{
		CameraInitializationResult.Status =
			CalculateMaximumCompatibleOrthoWidth(AspectRatio)
					+ KINDA_SMALL_NUMBER
				< ActiveCameraConfiguration.MinimumOrthoWidth
				? EParadoxCameraOperationStatus::VolumeTooSmall
				: EParadoxCameraOperationStatus::InvalidConfiguration;
		CameraInitializationResult.DiagnosticMessage = ConfigurationFailure;
		CameraBoundsVolume = nullptr;
		return CameraInitializationResult;
	}

	if (!CameraRigClass)
	{
		CameraRigClass = AParadoxCameraRig::StaticClass();
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FreeCameraRig = World->SpawnActor<AParadoxCameraRig>(
		CameraRigClass,
		FTransform::Identity,
		SpawnParameters);
	if (!FreeCameraRig)
	{
		CameraInitializationResult.Status =
			EParadoxCameraOperationStatus::RigSpawnFailed;
		CameraInitializationResult.DiagnosticMessage =
			TEXT("Failed to spawn the configured Paradox Camera Rig.");
		CameraBoundsVolume = nullptr;
		return CameraInitializationResult;
	}

	CurrentOrthoWidth = ActiveCameraConfiguration.InitialOrthoWidth;
	CameraFocusLocation = CameraBoundsVolume->GetCameraLogicalCenter();
	CameraFocusLocation = ClampCameraFocus(
		CameraFocusLocation,
		CurrentOrthoWidth,
		AspectRatio);
	UpdateFreeCameraPose(AspectRatio);
	SetViewTarget(FreeCameraRig);

	CameraInitializationResult.Status = EParadoxCameraOperationStatus::Succeeded;
	CameraInitializationResult.DiagnosticMessage = FString::Printf(
		TEXT("Initialized independent orthographic camera '%s' using volume '%s'."),
		*GetNameSafe(FreeCameraRig),
		*GetNameSafe(CameraBoundsVolume));
	PARADOX_LOG_INFO(TEXT("%s"), *CameraInitializationResult.DiagnosticMessage);
	return CameraInitializationResult;
}

void AParadoxPlayerController::RequestCameraRecenter()
{
	if (!IsFreeCameraReady())
	{
		return;
	}

	FVector RequestedTarget = CameraBoundsVolume->GetCameraLogicalCenter();
	if (const UParadoxTimeLoopComponent* TimeLoop = GetTimeLoopComponent();
		TimeLoop
			&& TimeLoop->GetCurrentPhase() == EParadoxTimeLoopPhase::ActiveRun
			&& IsValid(GetPawn())
			&& !GetPawn()->IsHidden())
	{
		RequestedTarget.X = GetPawn()->GetActorLocation().X;
		RequestedTarget.Y = GetPawn()->GetActorLocation().Y;
	}
	RequestedTarget.Z = CameraBoundsVolume->GetCameraLogicalCenter().Z;

	CameraRecenterStart = CameraFocusLocation;
	CameraRecenterTarget = ClampCameraFocus(
		RequestedTarget,
		CurrentOrthoWidth,
		GetCameraAspectRatio());
	CameraRecenterElapsed = 0.0f;
	bCameraRecenterActive = true;
}

void AParadoxPlayerController::UpdateFreeCamera(const float RealDeltaSeconds)
{
	if (!IsFreeCameraReady())
	{
		return;
	}

	const float AspectRatio = GetCameraAspectRatio();
	const float MaximumCompatible =
		CalculateMaximumCompatibleOrthoWidth(AspectRatio);
	const float EffectiveMaximum = FMath::Max(
		1.0f,
		FMath::Min(
			ActiveCameraConfiguration.MaximumOrthoWidth,
			MaximumCompatible));
	if (MaximumCompatible + KINDA_SMALL_NUMBER
		< ActiveCameraConfiguration.MinimumOrthoWidth)
	{
		if (!bWarnedRuntimeAspectConstraint)
		{
			bWarnedRuntimeAspectConstraint = true;
			PARADOX_LOG_WARNING(
				TEXT("Viewport aspect ratio %.3f makes the configured minimum Ortho Width %.1f incompatible with camera volume '%s'; containment takes precedence with width %.1f."),
				AspectRatio,
				ActiveCameraConfiguration.MinimumOrthoWidth,
				*GetNameSafe(CameraBoundsVolume),
				EffectiveMaximum);
		}
	}
	else
	{
		bWarnedRuntimeAspectConstraint = false;
	}
	CurrentOrthoWidth = FMath::Min(CurrentOrthoWidth, EffectiveMaximum);

	if (!CameraMoveInput.IsNearlyZero())
	{
		const FVector Forward3D = ActiveCameraConfiguration.Orientation.Vector();
		const FVector Right3D =
			FRotationMatrix(ActiveCameraConfiguration.Orientation).GetUnitAxis(EAxis::Y);
		const FVector2D Forward(Forward3D.X, Forward3D.Y);
		const FVector2D Right(Right3D.X, Right3D.Y);
		const FVector2D PlanarDelta =
			Right.GetSafeNormal() * CameraMoveInput.X
			+ Forward.GetSafeNormal() * CameraMoveInput.Y;
		CameraFocusLocation += FVector(
			PlanarDelta.X,
			PlanarDelta.Y,
			0.0f) * ActiveCameraConfiguration.MovementSpeed * RealDeltaSeconds;
		bCameraRecenterActive = false;
	}
	else if (bCameraRecenterActive)
	{
		CameraRecenterElapsed += RealDeltaSeconds;
		const float Duration = ActiveCameraConfiguration.RecenterDuration;
		const float Alpha = Duration <= KINDA_SMALL_NUMBER
			? 1.0f
			: FMath::Clamp(CameraRecenterElapsed / Duration, 0.0f, 1.0f);
		CameraFocusLocation = FMath::Lerp(
			CameraRecenterStart,
			CameraRecenterTarget,
			FMath::SmoothStep(0.0f, 1.0f, Alpha));
		if (Alpha >= 1.0f)
		{
			bCameraRecenterActive = false;
		}
	}

	CameraFocusLocation = ClampCameraFocus(
		CameraFocusLocation,
		CurrentOrthoWidth,
		AspectRatio);
	UpdateFreeCameraPose(AspectRatio);
	DrawFreeCameraDebug(AspectRatio);
}

void AParadoxPlayerController::UpdateFreeCameraPose(const float AspectRatio)
{
	if (!IsValid(FreeCameraRig))
	{
		return;
	}
	CameraFocusLocation = ClampCameraFocus(
		CameraFocusLocation,
		CurrentOrthoWidth,
		AspectRatio);
	FreeCameraRig->ApplyCameraPose(
		CameraFocusLocation,
		ActiveCameraConfiguration.Orientation,
		ActiveCameraConfiguration.CameraDistance,
		CurrentOrthoWidth);
}

float AParadoxPlayerController::GetCameraAspectRatio() const
{
	int32 SizeX = 0;
	int32 SizeY = 0;
	GetViewportSize(SizeX, SizeY);
	return SizeX > 0 && SizeY > 0
		? static_cast<float>(SizeX) / static_cast<float>(SizeY)
		: FMath::Max(0.1f, ActiveCameraConfiguration.FallbackAspectRatio);
}

bool AParadoxPlayerController::CalculateFootprint(
	const FVector& FocusLocation,
	const float OrthoWidth,
	const float AspectRatio,
	TArray<FVector>& OutCorners) const
{
	OutCorners.Reset(4);
	const FVector Forward = ActiveCameraConfiguration.Orientation.Vector();
	if (FMath::Abs(Forward.Z) <= KINDA_SMALL_NUMBER
		|| OrthoWidth <= 0.0f
		|| AspectRatio <= 0.0f)
	{
		return false;
	}

	const FRotationMatrix Rotation(ActiveCameraConfiguration.Orientation);
	const FVector Right = Rotation.GetUnitAxis(EAxis::Y);
	const FVector Up = Rotation.GetUnitAxis(EAxis::Z);
	const float HalfWidth = OrthoWidth * 0.5f;
	const float HalfHeight = HalfWidth / AspectRatio;
	for (const float HorizontalSign : { -1.0f, 1.0f })
	{
		for (const float VerticalSign : { -1.0f, 1.0f })
		{
			const FVector Offset =
				Right * HalfWidth * HorizontalSign
				+ Up * HalfHeight * VerticalSign;
			const FVector PlanarOffset =
				Offset - Forward * (Offset.Z / Forward.Z);
			OutCorners.Add(FocusLocation + PlanarOffset);
		}
	}
	return true;
}

bool AParadoxPlayerController::CalculateFootprintExtents(
	const float OrthoWidth,
	const float AspectRatio,
	FVector2D& OutExtents) const
{
	TArray<FVector> Corners;
	if (!CalculateFootprint(
		FVector::ZeroVector,
		OrthoWidth,
		AspectRatio,
		Corners))
	{
		OutExtents = FVector2D::ZeroVector;
		return false;
	}

	OutExtents = FVector2D::ZeroVector;
	for (const FVector& Corner : Corners)
	{
		OutExtents.X = FMath::Max(OutExtents.X, FMath::Abs(Corner.X));
		OutExtents.Y = FMath::Max(OutExtents.Y, FMath::Abs(Corner.Y));
	}
	return true;
}

float AParadoxPlayerController::CalculateMaximumCompatibleOrthoWidth(
	const float AspectRatio) const
{
	if (!IsValid(CameraBoundsVolume))
	{
		return 0.0f;
	}

	FVector2D UnitExtents;
	if (!CalculateFootprintExtents(1.0f, AspectRatio, UnitExtents))
	{
		return 0.0f;
	}
	const FVector BoundsExtent =
		CameraBoundsVolume->GetCameraWorldBounds().GetExtent();
	const float AvailableX = FMath::Max(
		0.0f,
		BoundsExtent.X - ActiveCameraConfiguration.BoundaryMargin);
	const float AvailableY = FMath::Max(
		0.0f,
		BoundsExtent.Y - ActiveCameraConfiguration.BoundaryMargin);
	const float WidthX = UnitExtents.X > SMALL_NUMBER
		? AvailableX / UnitExtents.X
		: TNumericLimits<float>::Max();
	const float WidthY = UnitExtents.Y > SMALL_NUMBER
		? AvailableY / UnitExtents.Y
		: TNumericLimits<float>::Max();
	return FMath::Max(0.0f, FMath::Min(WidthX, WidthY));
}

FVector AParadoxPlayerController::ClampCameraFocus(
	const FVector& RequestedFocus,
	const float OrthoWidth,
	const float AspectRatio) const
{
	if (!IsValid(CameraBoundsVolume))
	{
		return RequestedFocus;
	}

	FVector2D FootprintExtents;
	if (!CalculateFootprintExtents(
		OrthoWidth,
		AspectRatio,
		FootprintExtents))
	{
		return CameraBoundsVolume->GetCameraLogicalCenter();
	}

	const FBox Bounds = CameraBoundsVolume->GetCameraWorldBounds();
	const float Margin = ActiveCameraConfiguration.BoundaryMargin;
	const float MinimumX = Bounds.Min.X + Margin + FootprintExtents.X;
	const float MaximumX = Bounds.Max.X - Margin - FootprintExtents.X;
	const float MinimumY = Bounds.Min.Y + Margin + FootprintExtents.Y;
	const float MaximumY = Bounds.Max.Y - Margin - FootprintExtents.Y;
	const FVector BoundsCenter = Bounds.GetCenter();

	FVector Clamped = RequestedFocus;
	Clamped.X = MinimumX <= MaximumX
		? FMath::Clamp(RequestedFocus.X, MinimumX, MaximumX)
		: BoundsCenter.X;
	Clamped.Y = MinimumY <= MaximumY
		? FMath::Clamp(RequestedFocus.Y, MinimumY, MaximumY)
		: BoundsCenter.Y;
	Clamped.Z = CameraBoundsVolume->GetCameraLogicalCenter().Z;
	return Clamped;
}

bool AParadoxPlayerController::ValidateCameraConfiguration(
	const AParadoxCameraBoundsVolume& Volume,
	const FParadoxCameraConfiguration& Configuration,
	const float AspectRatio,
	FString& OutFailure) const
{
	const auto IsFinitePositive = [](const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	};
	if (!IsFinitePositive(Configuration.CameraDistance)
		|| !IsFinitePositive(Configuration.MinimumOrthoWidth)
		|| !IsFinitePositive(Configuration.MaximumOrthoWidth)
		|| !IsFinitePositive(Configuration.InitialOrthoWidth)
		|| !IsFinitePositive(Configuration.FallbackAspectRatio)
		|| !FMath::IsFinite(Configuration.MovementSpeed)
		|| Configuration.MovementSpeed < 0.0f
		|| !FMath::IsFinite(Configuration.ZoomUnitsPerStep)
		|| Configuration.ZoomUnitsPerStep < 0.0f
		|| !FMath::IsFinite(Configuration.RecenterDuration)
		|| Configuration.RecenterDuration < 0.0f
		|| !FMath::IsFinite(Configuration.BoundaryMargin)
		|| Configuration.BoundaryMargin < 0.0f)
	{
		OutFailure = TEXT("The camera configuration contains a non-finite, negative, or zero-required value.");
		return false;
	}
	if (Configuration.MinimumOrthoWidth > Configuration.MaximumOrthoWidth)
	{
		OutFailure = TEXT("Camera Minimum Ortho Width is greater than Maximum Ortho Width.");
		return false;
	}
	if (Configuration.InitialOrthoWidth < Configuration.MinimumOrthoWidth
		|| Configuration.InitialOrthoWidth > Configuration.MaximumOrthoWidth)
	{
		OutFailure = TEXT("Camera Initial Ortho Width is outside the configured minimum/maximum range.");
		return false;
	}
	if (FMath::Abs(Configuration.Orientation.Vector().Z) <= KINDA_SMALL_NUMBER)
	{
		OutFailure = TEXT("Camera orientation is parallel to the map plane and cannot produce a bounded footprint.");
		return false;
	}
	if (!Volume.GetCameraWorldBounds().IsValid)
	{
		OutFailure = TEXT("The camera bounds volume has invalid world bounds.");
		return false;
	}

	const float MaximumCompatible =
		CalculateMaximumCompatibleOrthoWidth(AspectRatio);
	if (MaximumCompatible + KINDA_SMALL_NUMBER
		< Configuration.MinimumOrthoWidth)
	{
		OutFailure = FString::Printf(
			TEXT("Camera volume '%s' supports at most Ortho Width %.1f at aspect %.3f, below the configured minimum %.1f."),
			*GetNameSafe(&Volume),
			MaximumCompatible,
			AspectRatio,
			Configuration.MinimumOrthoWidth);
		return false;
	}
	if (Configuration.InitialOrthoWidth > MaximumCompatible + KINDA_SMALL_NUMBER)
	{
		OutFailure = FString::Printf(
			TEXT("Initial Ortho Width %.1f does not fit camera volume '%s' at aspect %.3f."),
			Configuration.InitialOrthoWidth,
			*GetNameSafe(&Volume),
			AspectRatio);
		return false;
	}

	const FVector LogicalCenter = Volume.GetCameraLogicalCenter();
	const FVector ClampedCenter = ClampCameraFocus(
		LogicalCenter,
		Configuration.InitialOrthoWidth,
		AspectRatio);
	if (!FVector2D(LogicalCenter.X, LogicalCenter.Y).Equals(
		FVector2D(ClampedCenter.X, ClampedCenter.Y),
		1.0f))
	{
		OutFailure = FString::Printf(
			TEXT("Logical camera center %s is incompatible with volume '%s' and the initial footprint."),
			*LogicalCenter.ToCompactString(),
			*GetNameSafe(&Volume));
		return false;
	}
	return true;
}

void AParadoxPlayerController::DrawFreeCameraDebug(
	const float AspectRatio) const
{
#if ENABLE_DRAW_DEBUG
	if (!IsValid(CameraBoundsVolume)
		|| !CameraBoundsVolume->IsCameraDebugEnabled()
		|| CVarParadoxCameraDebug.GetValueOnGameThread() == 0)
	{
		return;
	}

	const FBox Bounds = CameraBoundsVolume->GetCameraWorldBounds();
	DrawDebugBox(
		GetWorld(),
		Bounds.GetCenter(),
		Bounds.GetExtent(),
		FColor::Cyan,
		false,
		0.0f,
		0,
		3.0f);
	TArray<FVector> Corners;
	if (CalculateFootprint(
		CameraFocusLocation,
		CurrentOrthoWidth,
		AspectRatio,
		Corners)
		&& Corners.Num() == 4)
	{
		const int32 Order[] = { 0, 1, 3, 2, 0 };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			DrawDebugLine(
				GetWorld(),
				Corners[Order[Index]],
				Corners[Order[Index + 1]],
				FColor::Green,
				false,
				0.0f,
				0,
				3.0f);
		}
	}
	DrawDebugPoint(
		GetWorld(),
		CameraFocusLocation,
		12.0f,
		FColor::Yellow,
		false,
		0.0f);
#endif
}

void AParadoxPlayerController::PresentPlannedMove(const FGridCellId& GoalCell)
{
	if (!GoalCell.IsValid())
	{
		return;
	}
	if (UGridRuntimeVisualizationSubsystem* Visualization = GetWorld()
		? GetWorld()->GetSubsystem<UGridRuntimeVisualizationSubsystem>()
		: nullptr)
	{
		if (PlannedMoveGoalCell.IsValid() && PlannedMoveGoalCell != GoalCell)
		{
			Visualization->SetCellSelected(PlannedMoveGoalCell, false);
		}
		Visualization->SetCellSelected(GoalCell, true);
	}
	PlannedMoveGoalCell = GoalCell;
}

void AParadoxPlayerController::ClearPlannedMovePresentation(bool bSuppressCurrentGoal)
{
	if (bSuppressCurrentGoal && PlannedMoveGoalCell.IsValid())
	{
		SuppressedPreviewGoalCell = PlannedMoveGoalCell;
	}
	if (PlannedMoveGoalCell.IsValid())
	{
		if (UGridRuntimeVisualizationSubsystem* Visualization = GetWorld()
			? GetWorld()->GetSubsystem<UGridRuntimeVisualizationSubsystem>()
			: nullptr)
		{
			Visualization->SetCellSelected(PlannedMoveGoalCell, false);
		}
	}
	PlannedMoveGoalCell = FGridCellId();
	if (GridPathPreviewComponent)
	{
		GridPathPreviewComponent->ClearPreview();
	}
}

void AParadoxPlayerController::HandleTacticalPauseResumed(const FTacticalPauseStateChange& Change)
{
	// The Gameplay Action bridge now owns execution and active-path presentation.
	ClearPlannedMovePresentation(true);
}

FGameplayActionSubmissionResult AParadoxPlayerController::RequestMoveToGridCell(
	const FVector Destination)
{
	return SubmitGridMoveRequest(EGridMovePathSource::Destination, Destination, nullptr);
}

FGameplayActionSubmissionResult AParadoxPlayerController::RequestMoveAlongGridPath(
	const FGridInjectedPath& InjectedPath)
{
	return SubmitGridMoveRequest(
		EGridMovePathSource::ExactInjectedPath,
		CachedDestination,
		&InjectedPath);
}

FParadoxTimeLoopOperationResult AParadoxPlayerController::RequestTimeRewind()
{
	if (UParadoxTimeLoopComponent* TimeLoop = GetTimeLoopComponent())
	{
		return TimeLoop->RequestTimeRewind();
	}

	FParadoxTimeLoopOperationResult Result;
	Result.Status = EParadoxTimeLoopOperationStatus::RejectedDisabled;
	Result.Phase = EParadoxTimeLoopPhase::Disabled;
	Result.DiagnosticMessage =
		TEXT("The current authoritative GameMode has no Paradox time-loop authority.");
	return Result;
}

FGameplayActionSubmissionResult AParadoxPlayerController::SubmitGridMoveRequest(
	EGridMovePathSource PathSource,
	const FVector& Destination,
	const FGridInjectedPath* InjectedPath)
{
	FGameplayActionSubmissionResult Failure;
	Failure.Status = EGameplayActionSubmissionStatus::RejectedInvalidRequest;
	Failure.ReasonTag = GameplayActionTags::Result_Failure_InvalidRequest;
	if (!IsMovementInputAllowed())
	{
		Failure.DiagnosticMessage =
			TEXT("Player movement is disabled outside the ActiveRun time-loop phase.");
		return Failure;
	}

	AParadoxPlayerCharacter* ParadoxCharacter = Cast<AParadoxPlayerCharacter>(GetPawn());
	UGameplayActionComponent* ActionComponent =
		ParadoxCharacter
			? ParadoxCharacter->GetGameplayActionComponent()
			: nullptr;
	if (!ActionComponent)
	{
		Failure.DiagnosticMessage =
			TEXT("The possessed Paradox Player Character has no Gameplay Action Component.");
		return Failure;
	}
	if (!MoveToGridCellActionDefinition)
	{
		Failure.DiagnosticMessage =
			TEXT("Move To Grid Cell Action Definition is not configured.");
		return Failure;
	}

	FGameplayActionRequestCreationResult Creation =
		UGameplayActionBlueprintLibrary::CreateActionRequest(
			MoveToGridCellActionDefinition);
	if (!Creation.WasCreated())
	{
		Failure.DiagnosticMessage = Creation.DiagnosticMessage;
		return Failure;
	}

	CachedDestination = Destination;
	PendingMovePathSource = PathSource;
	PendingInjectedPath = InjectedPath != nullptr ? *InjectedPath : FGridInjectedPath();
	auto CopyParameter = [this, &Creation](const FName ParameterName, const FName PropertyName)
	{
		const FProperty* Property = FindFProperty<FProperty>(StaticClass(), PropertyName);
		return UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
			Creation.Request,
			ParameterName,
			Property,
			Property ? Property->ContainerPtrToValuePtr<void>(this) : nullptr);
	};
	if (CopyParameter(
		GridMoveToCellActionParameters::PathSource,
		GET_MEMBER_NAME_CHECKED(AParadoxPlayerController, PendingMovePathSource))
		!= EGameplayActionParameterAccessResult::Success)
	{
		Failure.DiagnosticMessage = TEXT("The Move To Grid Cell Definition does not expose a compatible PathSource parameter.");
		return Failure;
	}
	if (CopyParameter(
		GridMoveToCellActionParameters::InjectedPath,
		GET_MEMBER_NAME_CHECKED(AParadoxPlayerController, PendingInjectedPath))
		!= EGameplayActionParameterAccessResult::Success)
	{
		Failure.DiagnosticMessage = TEXT("The Move To Grid Cell Definition does not expose a compatible InjectedPath parameter.");
		return Failure;
	}
	if (CopyParameter(
		GridMoveToCellActionParameters::GoalContentionPolicy,
		GET_MEMBER_NAME_CHECKED(AParadoxPlayerController, MoveGoalContentionPolicy))
		!= EGameplayActionParameterAccessResult::Success)
	{
		Failure.DiagnosticMessage = TEXT("The Move To Grid Cell Definition does not expose a compatible GoalContentionPolicy parameter.");
		return Failure;
	}
	const FProperty* GoalLocationProperty = FindFProperty<FProperty>(
		StaticClass(),
		GET_MEMBER_NAME_CHECKED(AParadoxPlayerController, CachedDestination));
	const EGameplayActionParameterAccessResult ParameterResult =
		UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
			Creation.Request,
			GridMoveToCellActionParameters::GoalLocation,
			GoalLocationProperty,
			GoalLocationProperty
				? GoalLocationProperty->ContainerPtrToValuePtr<void>(this)
				: nullptr);
	if (ParameterResult != EGameplayActionParameterAccessResult::Success)
	{
		Failure.DiagnosticMessage =
			TEXT("The Move To Grid Cell Definition does not expose a compatible GoalLocation parameter.");
		return Failure;
	}

	if (MoveRequestPriority == MAX_int32)
	{
		MoveRequestPriority = 0;
	}
	UGameplayActionBlueprintLibrary::SetRequestPriority(
		Creation.Request,
		++MoveRequestPriority);
	UGameplayActionBlueprintLibrary::SetRequestContext(
		Creation.Request,
		ParadoxGameplayTags::Origin_Player,
		this,
		FGameplayActionCorrelationData());

	if (IsTacticalPlanningActive())
	{
		UTacticalPauseActionQueueComponent* PlanningComponent =
			ParadoxCharacter->GetTacticalPauseActionQueueComponent();
		if (!PlanningComponent)
		{
			Failure.DiagnosticMessage =
				TEXT("The possessed Paradox Character has no Tactical Pause Action Queue Component.");
			return Failure;
		}
		return PlanningComponent->SubmitOrReplaceNextAction(Creation.Request);
	}

	return ActionComponent->SubmitAction(Creation.Request);
}
