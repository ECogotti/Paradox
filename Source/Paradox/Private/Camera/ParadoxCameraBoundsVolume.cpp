#include "Camera/ParadoxCameraBoundsVolume.h"

#include "Components/BoxComponent.h"
#include "Settings/ParadoxCameraSettings.h"

AParadoxCameraBoundsVolume::AParadoxCameraBoundsVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("CameraBounds"));
	SetRootComponent(BoundsComponent);
	BoundsComponent->InitBoxExtent(FVector(3000.0, 3000.0, 500.0));
	BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsComponent->SetGenerateOverlapEvents(false);
	BoundsComponent->SetHiddenInGame(true);
	BoundsComponent->ShapeColor = FColor(32, 180, 255);
}

FBox AParadoxCameraBoundsVolume::GetCameraWorldBounds() const
{
	return BoundsComponent
		? BoundsComponent->Bounds.GetBox()
		: FBox(EForceInit::ForceInit);
}

FVector AParadoxCameraBoundsVolume::GetCameraLogicalCenter() const
{
	return bOverrideLogicalCenter
		? LogicalCenterOverride
		: GetCameraWorldBounds().GetCenter();
}

FParadoxCameraConfiguration AParadoxCameraBoundsVolume::GetEffectiveCameraConfiguration() const
{
	if (bOverrideGlobalConfiguration)
	{
		return CameraConfigurationOverride;
	}
	const UParadoxCameraSettings* Settings = GetDefault<UParadoxCameraSettings>();
	return Settings
		? Settings->DefaultConfiguration
		: FParadoxCameraConfiguration();
}

