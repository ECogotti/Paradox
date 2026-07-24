#include "Camera/ParadoxCameraRig.h"

#include "Camera/CameraComponent.h"

AParadoxCameraRig::AParadoxCameraRig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
		Camera->SetAutoCalculateOrthoPlanes(true);
		Camera->bConstrainAspectRatio = false;
	}
}

void AParadoxCameraRig::ApplyCameraPose(
	const FVector& InFocusLocation,
	const FRotator& InOrientation,
	const float InCameraDistance,
	const float InOrthoWidth)
{
	FocusLocation = InFocusLocation;
	const FVector Forward = InOrientation.Vector();
	SetActorLocationAndRotation(
		FocusLocation - Forward * FMath::Max(1.0f, InCameraDistance),
		InOrientation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
		Camera->SetOrthoWidth(FMath::Max(1.0f, InOrthoWidth));
	}
}

