#pragma once

#include "AlphaBlend.h"
#include "CoreMinimal.h"
#include "ParadoxCameraTypes.generated.h"

UENUM(BlueprintType)
enum class EParadoxCameraProjectionMode : uint8
{
	Orthographic,
	Perspective
};

/** Designer-facing defaults used by the free camera. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxCameraConfiguration
{
	GENERATED_BODY()

	/** Pitch sets the fixed inclination toward the map plane in both modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera")
	FRotator Orientation = FRotator(-60.0, 0.0, 0.0);

	/** Fixed pivot-to-camera distance in orthographic mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera|Orthographic", meta = (ClampMin = "1.0", Units = "cm"))
	float CameraDistance = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MovementSpeed = 1600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera|Orthographic", meta = (ClampMin = "1.0", Units = "cm"))
	float InitialOrthoWidth = 2400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera|Orthographic", meta = (ClampMin = "1.0", Units = "cm"))
	float MinimumOrthoWidth = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera|Orthographic", meta = (ClampMin = "1.0", Units = "cm"))
	float MaximumOrthoWidth = 4800.0f;

	/** Horizontal field of view in perspective mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera|Perspective", meta = (ClampMin = "1.0", ClampMax = "170.0", Units = "deg"))
	float PerspectiveFieldOfView = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera|Perspective", meta = (ClampMin = "1.0", Units = "cm"))
	float InitialCameraArmDistance = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera|Perspective", meta = (ClampMin = "1.0", Units = "cm"))
	float MinimumCameraArmDistance = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera|Perspective", meta = (ClampMin = "1.0", Units = "cm"))
	float MaximumCameraArmDistance = 4800.0f;

	/** Changes Ortho Width or Camera Arm Distance by this many centimeters per wheel step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera", meta = (ClampMin = "0.0", Units = "cm"))
	float ZoomUnitsPerStep = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera", meta = (ClampMin = "0.0", Units = "s"))
	float RecenterDuration = 0.3f;

	/** Duration of one discrete 90-degree yaw rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera", meta = (ClampMin = "0.01", Units = "s"))
	float RotationDuration = 0.3f;

	/** Non-overshooting easing used while moving between adjacent quarter turns. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Paradox|Camera",
		meta = (InvalidEnumValues = "Custom"))
	EAlphaBlendOption RotationEasing = EAlphaBlendOption::HermiteCubic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera", meta = (ClampMin = "0.0", Units = "cm"))
	float BoundaryMargin = 100.0f;

	/** Used only when no runtime viewport size is available, such as deterministic automation tests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Camera", meta = (ClampMin = "0.1"))
	float FallbackAspectRatio = 16.0f / 9.0f;
};

UENUM(BlueprintType)
enum class EParadoxCameraOperationStatus : uint8
{
	Succeeded,
	NotConfigured,
	MissingVolume,
	MultipleVolumes,
	InvalidConfiguration,
	VolumeTooSmall,
	RigSpawnFailed,
	InvalidWorld
};

/** Structured result for free-camera discovery and initialization. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxCameraOperationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Camera")
	EParadoxCameraOperationStatus Status = EParadoxCameraOperationStatus::NotConfigured;

	UPROPERTY(BlueprintReadOnly, Category = "Paradox|Camera")
	FString DiagnosticMessage;

	bool IsSuccess() const { return Status == EParadoxCameraOperationStatus::Succeeded; }
};
