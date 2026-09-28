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
	const EParadoxCameraProjectionMode InProjectionMode,
	const float InCameraDistance,
	const float InOrthoWidth,
	const float InPerspectiveFieldOfView)
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
		if (InProjectionMode == EParadoxCameraProjectionMode::Perspective)
		{
			Camera->bOverrideAspectRatioAxisConstraint = true;
			Camera->SetAspectRatioAxisConstraint(EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV);
			Camera->SetProjectionMode(ECameraProjectionMode::Perspective);
			Camera->SetFieldOfView(InPerspectiveFieldOfView);
		}
		else
		{
			Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
			Camera->SetOrthoWidth(FMath::Max(1.0f, InOrthoWidth));
		}
	}
}
