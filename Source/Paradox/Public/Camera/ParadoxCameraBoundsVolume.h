#pragma once

#include "Camera/ParadoxCameraTypes.h"
#include "GameFramework/Actor.h"
#include "ParadoxCameraBoundsVolume.generated.h"

class UBoxComponent;

/**
 * Axis-aligned designer volume that bounds the complete orthographic camera footprint.
 *
 * It owns configuration only. The local PlayerController owns the runtime camera rig and input.
 */
UCLASS(Blueprintable)
class PARADOX_API AParadoxCameraBoundsVolume : public AActor
{
	GENERATED_BODY()

public:
	AParadoxCameraBoundsVolume();

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	bool IsCameraVolumeEnabled() const { return bCameraVolumeEnabled; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	UBoxComponent* GetBoundsComponent() const { return BoundsComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	FBox GetCameraWorldBounds() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	FVector GetCameraLogicalCenter() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	FParadoxCameraConfiguration GetEffectiveCameraConfiguration() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera|Debug")
	bool IsCameraDebugEnabled() const { return bEnableDebug; }

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> BoundsComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Camera", meta = (AllowPrivateAccess = "true"))
	bool bCameraVolumeEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Camera", meta = (AllowPrivateAccess = "true"))
	bool bOverrideGlobalConfiguration = false;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Paradox|Camera",
		meta = (AllowPrivateAccess = "true", EditCondition = "bOverrideGlobalConfiguration"))
	FParadoxCameraConfiguration CameraConfigurationOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Camera", meta = (AllowPrivateAccess = "true"))
	bool bOverrideLogicalCenter = false;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Paradox|Camera",
		meta = (AllowPrivateAccess = "true", EditCondition = "bOverrideLogicalCenter", MakeEditWidget))
	FVector LogicalCenterOverride = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Camera|Debug", meta = (AllowPrivateAccess = "true"))
	bool bEnableDebug = false;
};

