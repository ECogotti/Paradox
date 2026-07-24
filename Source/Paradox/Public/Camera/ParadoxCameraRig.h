#pragma once

#include "Camera/CameraActor.h"
#include "ParadoxCameraRig.generated.h"

/** Controller-owned orthographic view target independent from every temporal avatar. */
UCLASS(Blueprintable)
class PARADOX_API AParadoxCameraRig : public ACameraActor
{
	GENERATED_BODY()

public:
	AParadoxCameraRig(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	void ApplyCameraPose(
		const FVector& InFocusLocation,
		const FRotator& InOrientation,
		float InCameraDistance,
		float InOrthoWidth);

	UFUNCTION(BlueprintPure, Category = "Paradox|Camera")
	FVector GetFocusLocation() const { return FocusLocation; }

private:
	UPROPERTY(Transient)
	FVector FocusLocation = FVector::ZeroVector;
};

