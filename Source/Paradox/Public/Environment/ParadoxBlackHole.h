#pragma once

#include "Camera/CameraTypes.h"
#include "GameFramework/Actor.h"
#include "Environment/ParadoxBlackHoleMaterialVariants.h"
#include "ParadoxBlackHole.generated.h"

class UDirectionalLightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneCaptureComponent2D;
class USceneComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UTexture2D;
class UTextureRenderTarget2D;

UENUM(BlueprintType)
enum class EParadoxBlackHoleCaptureState : uint8
{
	Uninitialized,
	Disabled,
	Ready,
	Error
};

/** Single-view black-hole proxy with an instance-owned, neutral HDR background capture. */
UCLASS(BlueprintType, Blueprintable, PrioritizeCategories = ("Paradox"))
class PARADOX_API AParadoxBlackHole : public AActor
{
	GENERATED_BODY()

public:
	AParadoxBlackHole();

	/** Atomic settings-menu operation. Bits follow EParadoxBlackHoleFeature (0..7). Preferences survive dependencies. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Black Hole|Features")
	bool SetFeatureMask(int32 InMask);

	UFUNCTION(BlueprintPure, Category = "Paradox|Black Hole|Features")
	int32 GetRequestedFeatureMask() const;

	/** Returns false for an invalid feature or a material without compiled variants. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Black Hole|Features")
	bool SetFeatureEnabled(EParadoxBlackHoleFeature Feature, bool bEnabled);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetAccretionDiskEnabled, Category = "Paradox|Black Hole|Features", meta = (DisplayPriority = "0"))
	bool bEnableAccretionDisk = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetGravitationalLensingEnabled, Category = "Paradox|Black Hole|Features", meta = (DisplayPriority = "1"))
	bool bEnableGravitationalLensing = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetRefractionEnabled, Category = "Paradox|Black Hole|Features", meta = (DisplayPriority = "2", EditCondition = "bEnableGravitationalLensing"))
	bool bEnableRefraction = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFineNoiseEnabled, Category = "Paradox|Black Hole|Features", meta = (DisplayPriority = "3", EditCondition = "bEnableAccretionDisk"))
	bool bEnableFineNoise = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDiskStructuresEnabled, Category = "Paradox|Black Hole|Features", meta = (DisplayPriority = "4", EditCondition = "bEnableAccretionDisk"))
	bool bEnableDiskStructures = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetVariableVolumeEnabled, Category = "Paradox|Black Hole|Features", meta = (DisplayPriority = "5", EditCondition = "bEnableAccretionDisk"))
	bool bEnableVariableVolume = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDopplerEnabled, Category = "Paradox|Black Hole|Features", meta = (DisplayPriority = "6", EditCondition = "bEnableAccretionDisk"))
	bool bEnableDoppler = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetGlowEnabled, Category = "Paradox|Black Hole|Features", meta = (DisplayPriority = "7"))
	bool bEnableGlow = true;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Features")
	void SetAccretionDiskEnabled(bool bEnabled);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Features")
	void SetGravitationalLensingEnabled(bool bEnabled);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Features")
	void SetRefractionEnabled(bool bEnabled);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Features")
	void SetFineNoiseEnabled(bool bEnabled);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Features")
	void SetDiskStructuresEnabled(bool bEnabled);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Features")
	void SetVariableVolumeEnabled(bool bEnabled);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Features")
	void SetDopplerEnabled(bool bEnabled);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Features")
	void SetGlowEnabled(bool bEnabled);

	/** Hard references keep every runtime configuration available in cooked builds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, AdvancedDisplay, Category = "Paradox|Black Hole|Features")
	TObjectPtr<UParadoxBlackHoleMaterialVariants> MaterialVariants;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, DuplicateTransient, AdvancedDisplay, Category = "Paradox|Black Hole|Status")
	bool bSupportsFeatureVariants = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, DuplicateTransient, AdvancedDisplay, Category = "Paradox|Black Hole|Status")
	int32 ActiveFeatureMask = -1;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, DuplicateTransient, AdvancedDisplay, Category = "Paradox|Black Hole|Status")
	FString FeatureStateMessage;

	/** Configure/reuse the MID and render target. Ready means configured, not a completed GPU readback. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Black Hole")
	bool InitializeBackground();

	/** Reapply configuration after changing the material or camera settings in the editor. */
	UFUNCTION(CallInEditor, Category = "Paradox|Black Hole")
	void RefreshBackground();

	/** Reject non-finite input; clamp finite values to 0..1. Zero stops capture without releasing resources. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Black Hole")
	bool SetRefractionStrength(float InStrength);

	/** Replace the source material while retaining its authored disk/noise/color parameters. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Black Hole")
	bool SetBaseMaterial(UMaterialInterface* InMaterial);

	/** Main View Camera handles pose/FOV; explicitly match projection mode and orthographic width here. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Black Hole")
	bool SetCaptureProjection(ECameraProjectionMode::Type InProjection, float InOrthoWidth = 400.0f);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Black Hole")
	void DisableBackground();

	/** Change an exposed scalar by its exact material name; managed BackgroundReady cannot be forced. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Black Hole|Material")
	bool SetScalarParameter(FName ParameterName, float InValue);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Black Hole|Material")
	bool SetColorParameter(FName ParameterName, FLinearColor InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetCoreRadius, Category = "Paradox|Black Hole|Disk", meta = (ClampMin = "0.0001"))
	float CoreRadius = 0.1f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Disk")
	void SetCoreRadius(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDiskInnerRadius, Category = "Paradox|Black Hole|Disk", meta = (ClampMin = "0.0001", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk)", EditConditionHides))
	float DiskInnerRadius = 0.18f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Disk")
	void SetDiskInnerRadius(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDiskOuterRadius, Category = "Paradox|Black Hole|Disk", meta = (ClampMin = "0.0001", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk)", EditConditionHides))
	float DiskOuterRadius = 0.8f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Disk")
	void SetDiskOuterRadius(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDiskThickness, Category = "Paradox|Black Hole|Disk", meta = (ClampMin = "0.005", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk)", EditConditionHides))
	float DiskThickness = 0.005f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Disk")
	void SetDiskThickness(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDiskDensity, Category = "Paradox|Black Hole|Disk", meta = (ClampMin = "0.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk)", EditConditionHides))
	float DiskDensity = 5.0f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Disk")
	void SetDiskDensity(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetRotationSpeed, Category = "Paradox|Black Hole|Disk", meta = (EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk)", EditConditionHides))
	float RotationSpeed = 0.35f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Disk")
	void SetRotationSpeed(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetEmissionStrength, Category = "Paradox|Black Hole|Disk", meta = (ClampMin = "0.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk || bEnableGlow)", EditConditionHides))
	float EmissionStrength = 6.0f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Disk")
	void SetEmissionStrength(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetHaloStrength, Category = "Paradox|Black Hole|Disk", meta = (ClampMin = "0.0", EditCondition = "!bSupportsFeatureVariants || (bEnableGlow)", EditConditionHides))
	float HaloStrength = 0.25f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Disk")
	void SetHaloStrength(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetLensingStrength, Category = "Paradox|Black Hole|Lensing", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "!bSupportsFeatureVariants || (bEnableGravitationalLensing)", EditConditionHides))
	float LensingStrength = 0.5f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Lensing")
	void SetLensingStrength(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetRaySteps, Category = "Paradox|Black Hole|Lensing", meta = (ClampMin = "64.0", ClampMax = "256.0", EditCondition = "!bSupportsFeatureVariants || (bEnableGravitationalLensing)", EditConditionHides))
	float RaySteps = 128.0f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Lensing")
	void SetRaySteps(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetNoiseScale, Category = "Paradox|Black Hole|Noise", meta = (ClampMin = "1.0", ClampMax = "32.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableFineNoise)", EditConditionHides))
	float NoiseScale = 8.0f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Noise")
	void SetNoiseScale(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetNoiseAmount, Category = "Paradox|Black Hole|Noise", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableFineNoise)", EditConditionHides))
	float NoiseAmount = 1.0f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Noise")
	void SetNoiseAmount(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetNoiseContrast, Category = "Paradox|Black Hole|Noise", meta = (ClampMin = "0.25", ClampMax = "4.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableFineNoise)", EditConditionHides))
	float NoiseContrast = 2.0f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Noise")
	void SetNoiseContrast(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetHotspotStrength, Category = "Paradox|Black Hole|Noise", meta = (ClampMin = "0.0", ClampMax = "8.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableFineNoise)", EditConditionHides))
	float HotspotStrength = 1.5f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Noise")
	void SetHotspotStrength(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetTurbulenceSpeed, Category = "Paradox|Black Hole|Noise", meta = (ClampMin = "0.0", ClampMax = "2.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableFineNoise)", EditConditionHides))
	float TurbulenceSpeed = 0.25f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Noise")
	void SetTurbulenceSpeed(float InValue);

	/** Blend the new filaments and variable volume; zero restores the previous FogSafe shader. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetStructureAmount, Category = "Paradox|Black Hole|Structure", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && (bEnableDiskStructures || bEnableVariableVolume))", EditConditionHides))
	float StructureAmount = 0.8f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Structure")
	void SetStructureAmount(float InValue);

	/** Integer angular repetitions of the flow/filament texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetStructureScale, Category = "Paradox|Black Hole|Structure", meta = (ClampMin = "1.0", ClampMax = "8.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && (bEnableDiskStructures || bEnableVariableVolume))", EditConditionHides))
	float StructureScale = 2.0f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Structure")
	void SetStructureScale(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFilamentStrength, Category = "Paradox|Black Hole|Structure", meta = (ClampMin = "0.0", ClampMax = "3.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableDiskStructures)", EditConditionHides))
	float FilamentStrength = 1.0f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Structure")
	void SetFilamentStrength(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetClumpStrength, Category = "Paradox|Black Hole|Structure", meta = (ClampMin = "0.0", ClampMax = "4.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableDiskStructures)", EditConditionHides))
	float ClumpStrength = 1.5f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Structure")
	void SetClumpStrength(float InValue);

	/** Lifetime of each of the two crossfaded flow phases, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetStructureLifetime, Category = "Paradox|Black Hole|Structure", meta = (ClampMin = "1.0", ClampMax = "30.0", Units = "s", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && (bEnableDiskStructures || bEnableVariableVolume))", EditConditionHides))
	float StructureLifetime = 8.0f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Structure")
	void SetStructureLifetime(float InValue);

	/** Maximum flow displacement in texture coordinates; bounded phases prevent indefinite stretching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetVortexStrength, Category = "Paradox|Black Hole|Structure", meta = (ClampMin = "0.0", ClampMax = "0.5", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && (bEnableDiskStructures || bEnableVariableVolume))", EditConditionHides))
	float VortexStrength = 0.2f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Structure")
	void SetVortexStrength(float InValue);

	/** Correlated density variation; zero keeps the previous density field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDensityVariation, Category = "Paradox|Black Hole|Structure", meta = (ClampMin = "0.0", ClampMax = "0.75", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableVariableVolume)", EditConditionHides))
	float DensityVariation = 0.35f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Structure")
	void SetDensityVariation(float InValue);

	/** Thickness variation and small midplane undulation; local thickness never falls below 0.005. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetThicknessVariation, Category = "Paradox|Black Hole|Structure", meta = (ClampMin = "0.0", ClampMax = "0.5", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableVariableVolume)", EditConditionHides))
	float ThicknessVariation = 0.3f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Structure")
	void SetThicknessVariation(float InValue);

	/** Optional linear RGBA flow/filament Texture2D. None uses the source material's DiskStructure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDiskStructureTexture, Category = "Paradox|Black Hole|Structure", meta = (EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && (bEnableDiskStructures || bEnableVariableVolume))", EditConditionHides))
	TObjectPtr<UTexture2D> DiskStructureTexture;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Structure")
	void SetDiskStructureTexture(UTexture2D* InTexture);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetRefractionMaxOffset, Category = "Paradox|Black Hole|Lensing", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "!bSupportsFeatureVariants || (bEnableGravitationalLensing && bEnableRefraction)", EditConditionHides))
	float RefractionMaxOffset = 0.35f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Lensing")
	void SetRefractionMaxOffset(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOrthoRefractionDistance, Category = "Paradox|Black Hole|Lensing", meta = (ClampMin = "0.0", ClampMax = "100.0", EditCondition = "!bSupportsFeatureVariants || (bEnableGravitationalLensing && bEnableRefraction)", EditConditionHides))
	float OrthoRefractionDistance = 4.0f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Lensing")
	void SetOrthoRefractionDistance(float InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetInnerColor, Category = "Paradox|Black Hole|Disk", meta = (EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk || bEnableGlow)", EditConditionHides))
	FLinearColor InnerColor = FLinearColor(0.473936f, 0.866877f, 1.0f, 1.0f);

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Disk")
	void SetInnerColor(FLinearColor InValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetOuterColor, Category = "Paradox|Black Hole|Disk", meta = (EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk)", EditConditionHides))
	FLinearColor OuterColor = FLinearColor(0.40625f, 0.505869f, 1.0f, 1.0f);

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Disk")
	void SetOuterColor(FLinearColor InValue);

	/** Blend Doppler brightness and color into disk emission. Zero reproduces the v2 disk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDopplerStrength, Category = "Paradox|Black Hole|Doppler", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableDoppler)", EditConditionHides))
	float DopplerStrength = 0.65f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Doppler")
	void SetDopplerStrength(float InValue);

	/** Orbital speed at the inner disk radius as a fraction of c; independent of animation speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDopplerVelocity, Category = "Paradox|Black Hole|Doppler", meta = (ClampMin = "0.0", ClampMax = "0.6", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableDoppler)", EditConditionHides))
	float DopplerVelocity = 0.30f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Doppler")
	void SetDopplerVelocity(float InValue);

	/** Blend toward the two absolute HDR colors; zero changes brightness only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDopplerColorStrength, Category = "Paradox|Black Hole|Doppler", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableDoppler)", EditConditionHides))
	float DopplerColorStrength = 0.35f;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Doppler")
	void SetDopplerColorStrength(float InValue);

	/** Absolute HDR RGB target for disk material moving toward the observer, including lensed images. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDopplerApproachingColor, Category = "Paradox|Black Hole|Doppler", meta = (EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableDoppler)", EditConditionHides))
	FLinearColor DopplerApproachingColor = FLinearColor(0.473936f, 0.866877f, 1.0f, 1.0f);

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Doppler")
	void SetDopplerApproachingColor(FLinearColor InValue);

	/** Absolute HDR RGB target for disk material moving away from the observer; any hue is supported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetDopplerRecedingColor, Category = "Paradox|Black Hole|Doppler", meta = (EditCondition = "!bSupportsFeatureVariants || (bEnableAccretionDisk && bEnableDoppler)", EditConditionHides))
	FLinearColor DopplerRecedingColor = FLinearColor(0.40625f, 0.505869f, 1.0f, 1.0f);

	UFUNCTION(BlueprintSetter, Category = "Paradox|Black Hole|Doppler")
	void SetDopplerRecedingColor(FLinearColor InValue);

	/** Managed capture flag sent to the material: zero until capture is configured. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, DuplicateTransient, AdvancedDisplay, Category = "Paradox|Black Hole|Status")
	float BackgroundReady = 0.0f;


	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> DefaultSceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Sphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneCaptureComponent2D> BackgroundCapture;

	/** Assign the existing black-hole Material Instance. If empty, use Sphere's material in slot zero. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Black Hole")
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Black Hole", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "!bSupportsFeatureVariants || (bEnableGravitationalLensing && bEnableRefraction)", EditConditionHides))
	float RefractionStrength = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Black Hole|Camera", meta = (EditCondition = "!bSupportsFeatureVariants || (bEnableGravitationalLensing && bEnableRefraction)", EditConditionHides))
	TEnumAsByte<ECameraProjectionMode::Type> CaptureProjection = ECameraProjectionMode::Perspective;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Black Hole|Camera", meta = (ClampMin = "0.001", Units = "cm", EditCondition = "!bSupportsFeatureVariants || (bEnableGravitationalLensing && bEnableRefraction && CaptureProjection == ECameraProjectionMode::Orthographic)", EditConditionHides))
	float CaptureOrthoWidth = 400.0f;

	/** Enable background capture in a realtime Level Editor viewport as well as during gameplay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Black Hole", meta = (EditCondition = "!bSupportsFeatureVariants || (bEnableGravitationalLensing && bEnableRefraction)", EditConditionHides))
	bool bEnableEditorPreview = true;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, DuplicateTransient, AdvancedDisplay, Category = "Paradox|Black Hole|Status")
	EParadoxBlackHoleCaptureState CaptureState = EParadoxBlackHoleCaptureState::Uninitialized;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, DuplicateTransient, AdvancedDisplay, Category = "Paradox|Black Hole|Status")
	FString CaptureStateMessage;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, DuplicateTransient, AdvancedDisplay, Category = "Paradox|Black Hole|Status")
	TObjectPtr<UMaterialInstanceDynamic> BlackHoleMID;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, DuplicateTransient, AdvancedDisplay, Category = "Paradox|Black Hole|Status")
	TObjectPtr<UTextureRenderTarget2D> BackgroundRT;

protected:
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;

private:
	uint8 GetEffectiveFeatureMask() const;
	void AssignFeaturePreferences(uint8 InMask);
	bool ConfigureCapture();
	bool ValidateMaterialParameters() const;
	void ApplyMaterialParameters();
	void ApplyStructureTexture();
	void ReadMaterialDefaults(UMaterialInterface* InMaterial);
	void StopCapture();
	void ReleaseBackground();
	bool FailInitialization(const FString& Message);
	void SetCaptureState(EParadoxBlackHoleCaptureState InState, const FString& Message);

	/** Retains the original material when Sphere's slot has been replaced by our transient MID. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> MaterialSource;

	/** Authoring profile supplies texture defaults independently of the selected compiled parent. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> AuthoringMaterialSource;

	/** Serialized after migration so existing NoDoppler/nonstructured profiles keep their appearance. */
	UPROPERTY()
	bool bFeatureFlagsInitialized = false;

	bool bInitializationInProgress = false;
	bool bShuttingDown = false;
};
