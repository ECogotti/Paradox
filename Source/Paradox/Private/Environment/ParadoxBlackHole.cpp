#include "Environment/ParadoxBlackHole.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Paradox.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName BackgroundTextureParameter(TEXT("BackgroundTex"));
	const FName BackgroundReadyParameter(TEXT("BackgroundReady"));
	const FName RefractionStrengthParameter(TEXT("RefractionStrength"));
	const FName DiskStructureParameter(TEXT("DiskStructure"));
	struct FScalarBinding
	{
		FName Name;
		float AParadoxBlackHole::* Member;
	};

	const FScalarBinding ScalarBindings[] =
	{
		{ FName(TEXT("CoreRadius")), &AParadoxBlackHole::CoreRadius },
		{ FName(TEXT("DiskInnerRadius")), &AParadoxBlackHole::DiskInnerRadius },
		{ FName(TEXT("DiskOuterRadius")), &AParadoxBlackHole::DiskOuterRadius },
		{ FName(TEXT("DiskThickness")), &AParadoxBlackHole::DiskThickness },
		{ FName(TEXT("DiskDensity")), &AParadoxBlackHole::DiskDensity },
		{ FName(TEXT("RotationSpeed")), &AParadoxBlackHole::RotationSpeed },
		{ FName(TEXT("EmissionStrength")), &AParadoxBlackHole::EmissionStrength },
		{ FName(TEXT("HaloStrength")), &AParadoxBlackHole::HaloStrength },
		{ FName(TEXT("LensingStrength")), &AParadoxBlackHole::LensingStrength },
		{ FName(TEXT("RaySteps")), &AParadoxBlackHole::RaySteps },
		{ FName(TEXT("NoiseScale")), &AParadoxBlackHole::NoiseScale },
		{ FName(TEXT("NoiseAmount")), &AParadoxBlackHole::NoiseAmount },
		{ FName(TEXT("NoiseContrast")), &AParadoxBlackHole::NoiseContrast },
		{ FName(TEXT("HotspotStrength")), &AParadoxBlackHole::HotspotStrength },
		{ FName(TEXT("TurbulenceSpeed")), &AParadoxBlackHole::TurbulenceSpeed },
		{ FName(TEXT("StructureAmount")), &AParadoxBlackHole::StructureAmount },
		{ FName(TEXT("StructureScale")), &AParadoxBlackHole::StructureScale },
		{ FName(TEXT("FilamentStrength")), &AParadoxBlackHole::FilamentStrength },
		{ FName(TEXT("ClumpStrength")), &AParadoxBlackHole::ClumpStrength },
		{ FName(TEXT("StructureLifetime")), &AParadoxBlackHole::StructureLifetime },
		{ FName(TEXT("VortexStrength")), &AParadoxBlackHole::VortexStrength },
		{ FName(TEXT("DensityVariation")), &AParadoxBlackHole::DensityVariation },
		{ FName(TEXT("ThicknessVariation")), &AParadoxBlackHole::ThicknessVariation },
		{ FName(TEXT("RefractionMaxOffset")), &AParadoxBlackHole::RefractionMaxOffset },
		{ FName(TEXT("OrthoRefractionDistance")), &AParadoxBlackHole::OrthoRefractionDistance },
		{ FName(TEXT("DopplerStrength")), &AParadoxBlackHole::DopplerStrength },
		{ FName(TEXT("DopplerVelocity")), &AParadoxBlackHole::DopplerVelocity },
		{ FName(TEXT("DopplerColorStrength")), &AParadoxBlackHole::DopplerColorStrength },
	};

	bool IsFiniteColor(const FLinearColor& Color)
	{
		return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G)
			&& FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
	}

}

AParadoxBlackHole::AParadoxBlackHole()
{
	PrimaryActorTick.bCanEverTick = false;
	DefaultSceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	SetRootComponent(DefaultSceneRoot);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(DefaultSceneRoot);
	SpringArm->TargetArmLength = 300000.0f;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bUsePawnControlRotation = false;

	DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("DirectionalLight"));
	DirectionalLight->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	DirectionalLight->SetMobility(EComponentMobility::Movable);
	// The Unlit material needs no light. Designers can enable this optional scene light.
	DirectionalLight->SetIntensity(0.0f);

	Sphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sphere"));
	Sphere->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Sphere->SetMobility(EComponentMobility::Movable);
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sphere->SetGenerateOverlapEvents(false);
	Sphere->SetCanEverAffectNavigation(false);
	Sphere->SetCastShadow(false);
	Sphere->SetRelativeScale3D(FVector(3.0));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Sphere->SetStaticMesh(SphereMesh.Object);
	}

	BackgroundCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("BackgroundCapture"));
	BackgroundCapture->SetupAttachment(DefaultSceneRoot);
	BackgroundCapture->bCaptureEveryFrame = false;
	BackgroundCapture->bCaptureOnMovement = false;
	ConfigureCapture();

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterial(
		TEXT("/Game/Vfx/BlackHole/MI_BlackHole_FogSafe_Structured_Doppler.MI_BlackHole_FogSafe_Structured_Doppler"));
	if (DefaultMaterial.Succeeded())
	{
		BaseMaterial = DefaultMaterial.Object;
		ReadMaterialDefaults(BaseMaterial);
	}
	static ConstructorHelpers::FObjectFinderOptional<UParadoxBlackHoleMaterialVariants> DefaultVariants(
		TEXT("/Game/Vfx/BlackHole/DA_BlackHoleMaterialVariants.DA_BlackHoleMaterialVariants"));
	MaterialVariants = DefaultVariants.Get();
	uint8 SourceMask = 255;
	bSupportsFeatureVariants = IsValid(MaterialVariants) && MaterialVariants->FindSourceDefaults(BaseMaterial, SourceMask);
}

bool AParadoxBlackHole::ConfigureCapture()
{
	if (!IsValid(BackgroundCapture))
	{
		return false;
	}

	BackgroundCapture->CaptureSource = SCS_FinalColorHDR;
	BackgroundCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	BackgroundCapture->CompositeMode = SCCM_Overwrite;
	BackgroundCapture->bMainViewCamera = true;
	BackgroundCapture->bMainViewResolution = true;
	BackgroundCapture->bMainViewFamily = true;
	BackgroundCapture->MainViewResolutionDivisor = FIntPoint(1, 1);
	BackgroundCapture->bIgnoreScreenPercentage = false;
	BackgroundCapture->bInheritMainViewCameraPostProcessSettings = false;
	BackgroundCapture->bRenderInMainRenderer = false;
	BackgroundCapture->bCaptureOnMovement = false;
	BackgroundCapture->ProjectionType = CaptureProjection;
	BackgroundCapture->OrthoWidth = CaptureOrthoWidth;
	BackgroundCapture->SetCaptureSortPriority(10);
	BackgroundCapture->PostProcessBlendWeight = 1.0f;

	FPostProcessSettings& Settings = BackgroundCapture->PostProcessSettings;
	Settings.bOverride_AutoExposureMethod = true;
	Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Settings.AutoExposureApplyPhysicalCameraExposure = false;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = 0.0f;
	Settings.bOverride_BloomIntensity = true;
	Settings.BloomIntensity = 0.0f;
	Settings.bOverride_VignetteIntensity = true;
	Settings.VignetteIntensity = 0.0f;
	Settings.bOverride_MotionBlurAmount = true;
	Settings.MotionBlurAmount = 0.0f;
	Settings.bOverride_ColorGradingIntensity = true;
	Settings.ColorGradingIntensity = 0.0f;

	// Use the setter so serialized component overrides agree with the actual show flags.
	TArray<FEngineShowFlagsSetting> ShowFlagSettings = BackgroundCapture->GetShowFlagSettings();
	for (const TCHAR* FlagName : { TEXT("Bloom"), TEXT("MotionBlur"), TEXT("EyeAdaptation"),
		TEXT("LocalExposure"), TEXT("ColorGrading"), TEXT("Tonemapper") })
	{
		FEngineShowFlagsSetting* Setting = ShowFlagSettings.FindByPredicate(
			[FlagName](const FEngineShowFlagsSetting& Entry) { return Entry.ShowFlagName == FlagName; });
		if (!Setting)
		{
			Setting = &ShowFlagSettings.AddDefaulted_GetRef();
			Setting->ShowFlagName = FlagName;
		}
		Setting->Enabled = false;
	}
	BackgroundCapture->SetShowFlagSettings(ShowFlagSettings);

	// Installed Shipping builds fix this flag on. Do not report a neutral HDR capture as ready there.
	return !BackgroundCapture->ShowFlags.Tonemapper;
}

bool AParadoxBlackHole::InitializeBackground()
{
	if (IsTemplate())
	{
		// Class Defaults need the same edit conditions, without constructing runtime resources.
		uint8 SourceMask = 255;
		bSupportsFeatureVariants = IsValid(MaterialVariants) && MaterialVariants->FindSourceDefaults(BaseMaterial, SourceMask);
		return false;
	}
	if (bShuttingDown || bInitializationInProgress || !GetWorld())
	{
		return false;
	}
	TGuardValue<bool> InitializationGuard(bInitializationInProgress, true);
	StopCapture();

	if (!IsValid(Sphere) || !IsValid(BackgroundCapture) || !IsValid(Sphere->GetStaticMesh()))
	{
		return FailInitialization(TEXT("Sphere mesh or BackgroundCapture is unavailable."));
	}
	if (!FMath::IsFinite(RefractionStrength))
	{
		return FailInitialization(TEXT("RefractionStrength must be finite."));
	}
	RefractionStrength = FMath::Clamp(RefractionStrength, 0.0f, 1.0f);
	if (!ValidateMaterialParameters())
	{
		return FailInitialization(TEXT("All disk/noise/lensing/Doppler values and color channels must be finite."));
	}
	if ((CaptureProjection != ECameraProjectionMode::Perspective && CaptureProjection != ECameraProjectionMode::Orthographic)
		|| !FMath::IsFinite(CaptureOrthoWidth) || CaptureOrthoWidth <= 0.0f)
	{
		return FailInitialization(TEXT("CaptureProjection or CaptureOrthoWidth is invalid."));
	}

	UMaterialInterface* Source = IsValid(BaseMaterial) ? BaseMaterial.Get() : Sphere->GetMaterial(0);
	if (Source == BlackHoleMID || (Source && Source->GetOuter() == this && IsValid(MaterialSource)))
	{
		Source = IsValid(AuthoringMaterialSource) ? AuthoringMaterialSource.Get() : MaterialSource.Get();
	}
	if (!IsValid(Source))
	{
		return FailInitialization(TEXT("Assign the black-hole Material Instance to BaseMaterial or Sphere material slot 0."));
	}

	uint8 SourceMask = 255;
	bSupportsFeatureVariants = IsValid(MaterialVariants) && MaterialVariants->FindSourceDefaults(Source, SourceMask);
	if (bSupportsFeatureVariants && !bFeatureFlagsInitialized)
	{
		AssignFeaturePreferences(SourceMask);
		bFeatureFlagsInitialized = true;
	}
	const uint8 EffectiveMask = GetEffectiveFeatureMask();
	UMaterialInterface* RenderSource = Source;
	if (bSupportsFeatureVariants)
	{
		RenderSource = MaterialVariants->FindVariant(EffectiveMask);
		if (!IsValid(RenderSource))
		{
			ActiveFeatureMask = -1;
			FeatureStateMessage = FString::Printf(TEXT("Compiled material variant 0x%02X is missing or duplicated."), EffectiveMask);
			return FailInitialization(FeatureStateMessage);
		}
		FeatureStateMessage = FString::Printf(TEXT("Compiled variant 0x%02X; requested preferences 0x%02X."), EffectiveMask, GetRequestedFeatureMask());
	}
	else
	{
		FeatureStateMessage = TEXT("Legacy material: optimized feature toggles require a supported FogSafe profile and the material variant catalog.");
	}
	ActiveFeatureMask = bSupportsFeatureVariants ? EffectiveMask : -1;
	AuthoringMaterialSource = Source;
	const bool bCaptureEnabled = RefractionStrength > 0.0f
		&& (!bSupportsFeatureVariants || (EffectiveMask & 4) != 0)
		&& (GetWorld()->IsGameWorld() || bEnableEditorPreview) && GetNetMode() != NM_DedicatedServer;

	float ScalarValue = 0.0f;
	UTexture* TextureValue = nullptr;
	if (bCaptureEnabled && (!RenderSource->GetScalarParameterValue(FMaterialParameterInfo(BackgroundReadyParameter), ScalarValue)
		|| !RenderSource->GetScalarParameterValue(FMaterialParameterInfo(RefractionStrengthParameter), ScalarValue)))
	{
		return FailInitialization(TEXT("Material requires BackgroundReady and RefractionStrength scalar parameters with these exact names."));
	}

	if (!IsValid(BlackHoleMID) || BlackHoleMID->GetOuter() != this || MaterialSource != RenderSource)
	{
		BlackHoleMID = UMaterialInstanceDynamic::Create(RenderSource, this);
		MaterialSource = RenderSource;
	}
	if (!IsValid(BlackHoleMID))
	{
		return FailInitialization(TEXT("Could not create the black-hole dynamic material instance."));
	}
	Sphere->SetMaterial(0, BlackHoleMID);
	ApplyMaterialParameters();
	BlackHoleMID->SetScalarParameterValue(BackgroundReadyParameter, 0.0f);
	BlackHoleMID->SetScalarParameterValue(RefractionStrengthParameter, RefractionStrength);

	if (!bCaptureEnabled)
	{
		StopCapture();
		SetCaptureState(EParadoxBlackHoleCaptureState::Disabled, TEXT("Background capture disabled; disk and lens retain their material settings."));
		return true;
	}
	if (!ConfigureCapture())
	{
		return FailInitialization(TEXT("Neutral HDR capture is unavailable: Tonemapper must be disabled (the installed Shipping build fixes it on)."));
	}

	const bool bHasBackgroundParameter = RenderSource->GetTextureParameterValue(
		FMaterialParameterInfo(BackgroundTextureParameter), TextureValue);
	if (!bHasBackgroundParameter)
	{
		// The supplied master uses a fixed Texture Object, rather than a texture parameter.
		// Capture into that exact resource without altering or saving the material asset.
		TArray<UTexture*> UsedTextures;
		RenderSource->GetUsedTextures(UsedTextures);
		UTextureRenderTarget2D* FixedTarget = nullptr;
		for (UTexture* Texture : UsedTextures)
		{
			if (UTextureRenderTarget2D* Target = Cast<UTextureRenderTarget2D>(Texture))
			{
				if (FixedTarget && FixedTarget != Target)
				{
					return FailInitialization(TEXT("Material contains multiple fixed render targets; expose the background as Texture Object Parameter BackgroundTex."));
				}
				FixedTarget = Target;
			}
		}
		if (!IsValid(FixedTarget))
		{
			return FailInitialization(TEXT("Material needs Texture Object Parameter BackgroundTex or one fixed Render Target Texture Object."));
		}
		if (FixedTarget->RenderTargetFormat != RTF_RGBA16f || FixedTarget->bAutoGenerateMips
			|| FixedTarget->AddressX != TA_Clamp || FixedTarget->AddressY != TA_Clamp
			|| !FMath::IsNearlyEqual(FixedTarget->TargetGamma, 1.0f))
		{
			return FailInitialization(TEXT("Fixed background target must be RGBA16f, Gamma=1, Clamp X/Y, without auto mipmaps."));
		}
		BackgroundRT = FixedTarget;
	}
	else if (!IsValid(BackgroundRT) || BackgroundRT->GetOuter() != this)
	{
		BackgroundRT = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
		if (!IsValid(BackgroundRT))
		{
			return FailInitialization(TEXT("Could not create the background render target."));
		}
		BackgroundRT->RenderTargetFormat = RTF_RGBA16f;
		BackgroundRT->bForceLinearGamma = true;
		BackgroundRT->TargetGamma = 1.0f;
		BackgroundRT->Filter = TF_Bilinear;
		BackgroundRT->AddressX = TA_Clamp;
		BackgroundRT->AddressY = TA_Clamp;
		BackgroundRT->bAutoGenerateMips = false;
		BackgroundRT->bSupportsUAV = false;
		BackgroundRT->ClearColor = FLinearColor::Black;
		// Main View Resolution replaces the resource dimensions with the current main-view size.
		BackgroundRT->InitAutoFormat(512, 512);
	}
	if (!BackgroundRT->GetResource())
	{
		return FailInitialization(TEXT("Background render target has no rendering resource."));
	}

	BackgroundCapture->TextureTarget = BackgroundRT;
	BackgroundCapture->HiddenActors.AddUnique(this);
	BackgroundCapture->HideActorComponents(this, true);
	if (bHasBackgroundParameter)
	{
		BlackHoleMID->SetTextureParameterValue(BackgroundTextureParameter, BackgroundRT);
	}
	BackgroundCapture->bCaptureEveryFrame = true;
	if (BackgroundCapture->IsRegistered())
	{
		BackgroundCapture->CaptureSceneDeferred();
	}
	BackgroundReady = 1.0f;
	BlackHoleMID->SetScalarParameterValue(BackgroundReadyParameter, BackgroundReady);
	SetCaptureState(EParadoxBlackHoleCaptureState::Ready, bHasBackgroundParameter
		? TEXT("HDR main-view capture configured with an instance-owned render target.")
		: TEXT("HDR main-view capture configured with the material's fixed render target; use one black hole per view."));
	return true;
}

void AParadoxBlackHole::ReadMaterialDefaults(UMaterialInterface* InMaterial)
{
	if (!IsValid(InMaterial))
	{
		return;
	}
	for (const FScalarBinding& Binding : ScalarBindings)
	{
		InMaterial->GetScalarParameterValue(FMaterialParameterInfo(Binding.Name), this->*Binding.Member);
	}
	InMaterial->GetScalarParameterValue(FMaterialParameterInfo(RefractionStrengthParameter), RefractionStrength);
	InMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("InnerColor")), InnerColor);
	InMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("OuterColor")), OuterColor);
	InMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("DopplerApproachingColor")), DopplerApproachingColor);
	InMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("DopplerRecedingColor")), DopplerRecedingColor);
}

bool AParadoxBlackHole::ValidateMaterialParameters() const
{
	for (const FScalarBinding& Binding : ScalarBindings)
	{
		if (!FMath::IsFinite(this->*Binding.Member))
		{
			return false;
		}
	}
	return IsFiniteColor(InnerColor) && IsFiniteColor(OuterColor)
		&& IsFiniteColor(DopplerApproachingColor) && IsFiniteColor(DopplerRecedingColor);
}

void AParadoxBlackHole::ApplyMaterialParameters()
{
	if (!IsValid(BlackHoleMID))
	{
		return;
	}
	for (const FScalarBinding& Binding : ScalarBindings)
	{
		BlackHoleMID->SetScalarParameterValue(Binding.Name, this->*Binding.Member);
	}
	BlackHoleMID->SetVectorParameterValue(FName(TEXT("InnerColor")), InnerColor);
	BlackHoleMID->SetVectorParameterValue(FName(TEXT("OuterColor")), OuterColor);
	BlackHoleMID->SetVectorParameterValue(FName(TEXT("DopplerApproachingColor")), DopplerApproachingColor);
	BlackHoleMID->SetVectorParameterValue(FName(TEXT("DopplerRecedingColor")), DopplerRecedingColor);
	ApplyStructureTexture();
}

void AParadoxBlackHole::ApplyStructureTexture()
{
	if (!IsValid(BlackHoleMID) || !IsValid(AuthoringMaterialSource))
	{
		return;
	}
	UTexture* DefaultTexture = nullptr;
	const bool bHasStructureDefault = AuthoringMaterialSource->GetTextureParameterValue(FMaterialParameterInfo(DiskStructureParameter), DefaultTexture)
		|| (IsValid(MaterialSource) && MaterialSource->GetTextureParameterValue(FMaterialParameterInfo(DiskStructureParameter), DefaultTexture));
	if (bHasStructureDefault)
	{
		// Resolve from the source, not the MID: clearing the override must restore its default.
		BlackHoleMID->SetTextureParameterValue(DiskStructureParameter,
			IsValid(DiskStructureTexture) ? DiskStructureTexture.Get() : DefaultTexture);
	}
	if (bSupportsFeatureVariants)
	{
		// The old profiles use a fixed noise object. Preserve it when switching to the parameterized master.
		const FName NoiseParameter(TEXT("DiskNoise"));
		if (!AuthoringMaterialSource->GetTextureParameterValue(FMaterialParameterInfo(NoiseParameter), DefaultTexture))
		{
			TArray<UTexture*> Textures;
			AuthoringMaterialSource->GetUsedTextures(Textures);
			DefaultTexture = nullptr;
			UTexture* StructureDefault = nullptr;
			AuthoringMaterialSource->GetTextureParameterValue(FMaterialParameterInfo(DiskStructureParameter), StructureDefault);
			for (UTexture* Texture : Textures)
			{
				if (IsValid(Texture) && Texture != StructureDefault && Texture->IsA<UTexture2D>())
				{
					DefaultTexture = Texture;
					break;
				}
			}
		}
		if (DefaultTexture)
		{
			BlackHoleMID->SetTextureParameterValue(NoiseParameter, DefaultTexture);
		}
	}
}

void AParadoxBlackHole::SetDiskStructureTexture(UTexture2D* InTexture)
{
	if (InTexture && !IsValid(InTexture))
	{
		PARADOX_LOG_WARNING(TEXT("Black hole '%s' rejected an invalid structure texture."), *GetPathName());
		return;
	}
	DiskStructureTexture = InTexture;
	ApplyStructureTexture();
}

bool AParadoxBlackHole::SetScalarParameter(const FName ParameterName, const float InValue)
{
	if (ParameterName == RefractionStrengthParameter)
	{
		return SetRefractionStrength(InValue);
	}
	for (const FScalarBinding& Binding : ScalarBindings)
	{
		if (Binding.Name == ParameterName && FMath::IsFinite(InValue))
		{
			this->*Binding.Member = InValue;
			if (!IsValid(BlackHoleMID) || (bSupportsFeatureVariants && ActiveFeatureMask != GetEffectiveFeatureMask()))
			{
				return InitializeBackground();
			}
			BlackHoleMID->SetScalarParameterValue(ParameterName, InValue);
			return true;
		}
	}
	PARADOX_LOG_WARNING(TEXT("Black hole '%s' rejected scalar '%s': unsupported, managed, or non-finite value."),
		*GetPathName(), *ParameterName.ToString());
	return false;
}

bool AParadoxBlackHole::SetColorParameter(const FName ParameterName, const FLinearColor InValue)
{
	FLinearColor* Color = nullptr;
	if (ParameterName == TEXT("InnerColor"))
	{
		Color = &InnerColor;
	}
	else if (ParameterName == TEXT("OuterColor"))
	{
		Color = &OuterColor;
	}
	else if (ParameterName == TEXT("DopplerApproachingColor"))
	{
		Color = &DopplerApproachingColor;
	}
	else if (ParameterName == TEXT("DopplerRecedingColor"))
	{
		Color = &DopplerRecedingColor;
	}
	if (Color && IsFiniteColor(InValue))
	{
		*Color = InValue;
		if (!IsValid(BlackHoleMID))
		{
			return InitializeBackground();
		}
		BlackHoleMID->SetVectorParameterValue(ParameterName, InValue);
		return true;
	}
	PARADOX_LOG_WARNING(TEXT("Black hole '%s' rejected color '%s': unsupported parameter or non-finite color."),
		*GetPathName(), *ParameterName.ToString());
	return false;
}

void AParadoxBlackHole::SetCoreRadius(const float InValue)
{
	SetScalarParameter(FName(TEXT("CoreRadius")), InValue);
}

void AParadoxBlackHole::SetDiskInnerRadius(const float InValue)
{
	SetScalarParameter(FName(TEXT("DiskInnerRadius")), InValue);
}

void AParadoxBlackHole::SetDiskOuterRadius(const float InValue)
{
	SetScalarParameter(FName(TEXT("DiskOuterRadius")), InValue);
}

void AParadoxBlackHole::SetDiskThickness(const float InValue)
{
	SetScalarParameter(FName(TEXT("DiskThickness")), InValue);
}

void AParadoxBlackHole::SetDiskDensity(const float InValue)
{
	SetScalarParameter(FName(TEXT("DiskDensity")), InValue);
}

void AParadoxBlackHole::SetRotationSpeed(const float InValue)
{
	SetScalarParameter(FName(TEXT("RotationSpeed")), InValue);
}

void AParadoxBlackHole::SetEmissionStrength(const float InValue)
{
	SetScalarParameter(FName(TEXT("EmissionStrength")), InValue);
}

void AParadoxBlackHole::SetHaloStrength(const float InValue)
{
	SetScalarParameter(FName(TEXT("HaloStrength")), InValue);
}

void AParadoxBlackHole::SetLensingStrength(const float InValue)
{
	SetScalarParameter(FName(TEXT("LensingStrength")), InValue);
}

void AParadoxBlackHole::SetRaySteps(const float InValue)
{
	SetScalarParameter(FName(TEXT("RaySteps")), InValue);
}

void AParadoxBlackHole::SetNoiseScale(const float InValue)
{
	SetScalarParameter(FName(TEXT("NoiseScale")), InValue);
}

void AParadoxBlackHole::SetNoiseAmount(const float InValue)
{
	SetScalarParameter(FName(TEXT("NoiseAmount")), InValue);
}

void AParadoxBlackHole::SetNoiseContrast(const float InValue)
{
	SetScalarParameter(FName(TEXT("NoiseContrast")), InValue);
}

void AParadoxBlackHole::SetHotspotStrength(const float InValue)
{
	SetScalarParameter(FName(TEXT("HotspotStrength")), InValue);
}

void AParadoxBlackHole::SetTurbulenceSpeed(const float InValue)
{
	SetScalarParameter(FName(TEXT("TurbulenceSpeed")), InValue);
}

void AParadoxBlackHole::SetStructureAmount(const float InValue)
{
	SetScalarParameter(FName(TEXT("StructureAmount")), InValue);
}

void AParadoxBlackHole::SetStructureScale(const float InValue)
{
	SetScalarParameter(FName(TEXT("StructureScale")), InValue);
}

void AParadoxBlackHole::SetFilamentStrength(const float InValue)
{
	SetScalarParameter(FName(TEXT("FilamentStrength")), InValue);
}

void AParadoxBlackHole::SetClumpStrength(const float InValue)
{
	SetScalarParameter(FName(TEXT("ClumpStrength")), InValue);
}

void AParadoxBlackHole::SetStructureLifetime(const float InValue)
{
	SetScalarParameter(FName(TEXT("StructureLifetime")), InValue);
}

void AParadoxBlackHole::SetVortexStrength(const float InValue)
{
	SetScalarParameter(FName(TEXT("VortexStrength")), InValue);
}

void AParadoxBlackHole::SetDensityVariation(const float InValue)
{
	SetScalarParameter(FName(TEXT("DensityVariation")), InValue);
}

void AParadoxBlackHole::SetThicknessVariation(const float InValue)
{
	SetScalarParameter(FName(TEXT("ThicknessVariation")), InValue);
}

void AParadoxBlackHole::SetRefractionMaxOffset(const float InValue)
{
	SetScalarParameter(FName(TEXT("RefractionMaxOffset")), InValue);
}

void AParadoxBlackHole::SetOrthoRefractionDistance(const float InValue)
{
	SetScalarParameter(FName(TEXT("OrthoRefractionDistance")), InValue);
}

void AParadoxBlackHole::SetInnerColor(const FLinearColor InValue)
{
	SetColorParameter(FName(TEXT("InnerColor")), InValue);
}

void AParadoxBlackHole::SetOuterColor(const FLinearColor InValue)
{
	SetColorParameter(FName(TEXT("OuterColor")), InValue);
}

void AParadoxBlackHole::SetDopplerStrength(const float InValue)
{
	SetScalarParameter(FName(TEXT("DopplerStrength")), InValue);
}

void AParadoxBlackHole::SetDopplerVelocity(const float InValue)
{
	SetScalarParameter(FName(TEXT("DopplerVelocity")), InValue);
}

void AParadoxBlackHole::SetDopplerColorStrength(const float InValue)
{
	SetScalarParameter(FName(TEXT("DopplerColorStrength")), InValue);
}

void AParadoxBlackHole::SetDopplerApproachingColor(const FLinearColor InValue)
{
	SetColorParameter(FName(TEXT("DopplerApproachingColor")), InValue);
}

void AParadoxBlackHole::SetDopplerRecedingColor(const FLinearColor InValue)
{
	SetColorParameter(FName(TEXT("DopplerRecedingColor")), InValue);
}


void AParadoxBlackHole::RefreshBackground()
{
	InitializeBackground();
}

int32 AParadoxBlackHole::GetRequestedFeatureMask() const
{
	return (bEnableAccretionDisk ? 1 : 0) | (bEnableGravitationalLensing ? 2 : 0)
		| (bEnableRefraction ? 4 : 0) | (bEnableFineNoise ? 8 : 0)
		| (bEnableDiskStructures ? 16 : 0) | (bEnableVariableVolume ? 32 : 0)
		| (bEnableDoppler ? 64 : 0) | (bEnableGlow ? 128 : 0);
}

uint8 AParadoxBlackHole::GetEffectiveFeatureMask() const
{
	uint8 Mask = static_cast<uint8>(GetRequestedFeatureMask());
	if (LensingStrength <= 0.0f) Mask &= ~2;
	if (RefractionStrength <= 0.0f || (GetWorld() && !GetWorld()->IsGameWorld() && !bEnableEditorPreview)
		|| (GetWorld() && GetNetMode() == NM_DedicatedServer)) Mask &= ~4;
	if (NoiseAmount <= 0.0f) Mask &= ~8;
	if (StructureAmount <= 0.0f) Mask &= ~(16 | 32);
	if (DopplerStrength <= 0.0f || DopplerVelocity <= 0.0f || RotationSpeed == 0.0f) Mask &= ~64;
	if (HaloStrength <= 0.0f) Mask &= ~128;
	return UParadoxBlackHoleMaterialVariants::NormalizeMask(Mask);
}

void AParadoxBlackHole::AssignFeaturePreferences(const uint8 InMask)
{
	bEnableAccretionDisk = (InMask & 1) != 0;
	bEnableGravitationalLensing = (InMask & 2) != 0;
	bEnableRefraction = (InMask & 4) != 0;
	bEnableFineNoise = (InMask & 8) != 0;
	bEnableDiskStructures = (InMask & 16) != 0;
	bEnableVariableVolume = (InMask & 32) != 0;
	bEnableDoppler = (InMask & 64) != 0;
	bEnableGlow = (InMask & 128) != 0;
}

bool AParadoxBlackHole::SetFeatureMask(const int32 InMask)
{
	uint8 SourceMask = 255;
	UMaterialInterface* Source = IsValid(BaseMaterial) ? BaseMaterial.Get() : AuthoringMaterialSource.Get();
	if (InMask < 0 || InMask > 255 || !IsValid(MaterialVariants)
		|| !MaterialVariants->FindSourceDefaults(Source, SourceMask)
		|| !IsValid(MaterialVariants->FindVariant(static_cast<uint8>(InMask))))
	{
		FeatureStateMessage = TEXT("Feature change rejected: mask must be 0..255 and the material must have a complete compiled variant catalog.");
		PARADOX_LOG_WARNING(TEXT("Black hole '%s': %s"), *GetPathName(), *FeatureStateMessage);
		return false;
	}
	AssignFeaturePreferences(static_cast<uint8>(InMask));
	bFeatureFlagsInitialized = true;
	return IsTemplate() || !GetWorld() || InitializeBackground();
}

bool AParadoxBlackHole::SetFeatureEnabled(const EParadoxBlackHoleFeature Feature, const bool bEnabled)
{
	const uint8 Index = static_cast<uint8>(Feature);
	if (Index >= 8)
	{
		PARADOX_LOG_WARNING(TEXT("Black hole '%s' rejected an invalid feature index."), *GetPathName());
		return false;
	}
	const int32 Mask = GetRequestedFeatureMask();
	return SetFeatureMask(bEnabled ? Mask | (1 << Index) : Mask & ~(1 << Index));
}

void AParadoxBlackHole::SetAccretionDiskEnabled(bool bEnabled) { SetFeatureEnabled(EParadoxBlackHoleFeature::AccretionDisk, bEnabled); }
void AParadoxBlackHole::SetGravitationalLensingEnabled(bool bEnabled) { SetFeatureEnabled(EParadoxBlackHoleFeature::GravitationalLensing, bEnabled); }
void AParadoxBlackHole::SetRefractionEnabled(bool bEnabled) { SetFeatureEnabled(EParadoxBlackHoleFeature::Refraction, bEnabled); }
void AParadoxBlackHole::SetFineNoiseEnabled(bool bEnabled) { SetFeatureEnabled(EParadoxBlackHoleFeature::FineNoise, bEnabled); }
void AParadoxBlackHole::SetDiskStructuresEnabled(bool bEnabled) { SetFeatureEnabled(EParadoxBlackHoleFeature::DiskStructures, bEnabled); }
void AParadoxBlackHole::SetVariableVolumeEnabled(bool bEnabled) { SetFeatureEnabled(EParadoxBlackHoleFeature::VariableVolume, bEnabled); }
void AParadoxBlackHole::SetDopplerEnabled(bool bEnabled) { SetFeatureEnabled(EParadoxBlackHoleFeature::Doppler, bEnabled); }
void AParadoxBlackHole::SetGlowEnabled(bool bEnabled) { SetFeatureEnabled(EParadoxBlackHoleFeature::Glow, bEnabled); }

bool AParadoxBlackHole::SetRefractionStrength(const float InStrength)
{
	if (!FMath::IsFinite(InStrength))
	{
		PARADOX_LOG_WARNING(TEXT("Black hole '%s' rejected a non-finite refraction strength."), *GetPathName());
		return false;
	}
	RefractionStrength = FMath::Clamp(InStrength, 0.0f, 1.0f);
	return InitializeBackground();
}

bool AParadoxBlackHole::SetBaseMaterial(UMaterialInterface* InMaterial)
{
	if (!IsValid(InMaterial))
	{
		PARADOX_LOG_WARNING(TEXT("Black hole '%s' rejected an invalid base material."), *GetPathName());
		return false;
	}
	BaseMaterial = InMaterial;
	return InitializeBackground();
}

bool AParadoxBlackHole::SetCaptureProjection(const ECameraProjectionMode::Type InProjection, const float InOrthoWidth)
{
	if ((InProjection != ECameraProjectionMode::Perspective && InProjection != ECameraProjectionMode::Orthographic)
		|| !FMath::IsFinite(InOrthoWidth) || InOrthoWidth <= 0.0f)
	{
		PARADOX_LOG_WARNING(TEXT("Black hole '%s' rejected invalid projection/orthographic width."), *GetPathName());
		return false;
	}
	CaptureProjection = InProjection;
	CaptureOrthoWidth = InOrthoWidth;
	return InitializeBackground();
}

void AParadoxBlackHole::DisableBackground()
{
	SetRefractionStrength(0.0f);
}

void AParadoxBlackHole::StopCapture()
{
	BackgroundReady = 0.0f;
	if (IsValid(BackgroundCapture))
	{
		BackgroundCapture->bCaptureEveryFrame = false;
		BackgroundCapture->bCaptureOnMovement = false;
	}
	if (IsValid(BlackHoleMID))
	{
		BlackHoleMID->SetScalarParameterValue(BackgroundReadyParameter, 0.0f);
		BlackHoleMID->SetScalarParameterValue(RefractionStrengthParameter, 0.0f);
	}
}

bool AParadoxBlackHole::FailInitialization(const FString& Message)
{
	StopCapture();
	SetCaptureState(EParadoxBlackHoleCaptureState::Error, Message);
	return false;
}

void AParadoxBlackHole::SetCaptureState(const EParadoxBlackHoleCaptureState InState, const FString& Message)
{
	if (CaptureState == InState && CaptureStateMessage == Message)
	{
		return;
	}
	CaptureState = InState;
	CaptureStateMessage = Message;
	if (InState == EParadoxBlackHoleCaptureState::Error)
	{
		if (GetWorld() && GetWorld()->IsGameWorld())
		{
			PARADOX_LOG_ERROR(TEXT("Black hole '%s': %s"), *GetPathName(), *Message);
		}
		else
		{
			PARADOX_LOG_WARNING(TEXT("Black hole '%s': %s"), *GetPathName(), *Message);
		}
	}
}

void AParadoxBlackHole::ReleaseBackground()
{
	bShuttingDown = true;
	StopCapture();
	if (IsValid(BackgroundCapture))
	{
		BackgroundCapture->TextureTarget = nullptr;
	}
	if (IsValid(Sphere) && Sphere->GetMaterial(0) == BlackHoleMID)
	{
		Sphere->SetMaterial(0, AuthoringMaterialSource);
	}
	if (IsValid(BlackHoleMID) && IsValid(BackgroundRT) && BackgroundRT->GetOuter() == this)
	{
		UTexture* DefaultTexture = nullptr;
		if (IsValid(MaterialSource))
		{
			MaterialSource->GetTextureParameterValue(FMaterialParameterInfo(BackgroundTextureParameter), DefaultTexture);
		}
		BlackHoleMID->SetTextureParameterValue(BackgroundTextureParameter, DefaultTexture);
	}
	BlackHoleMID = nullptr;
	BackgroundRT = nullptr;
	MaterialSource = nullptr;
	AuthoringMaterialSource = nullptr;
	ActiveFeatureMask = -1;
	SetCaptureState(EParadoxBlackHoleCaptureState::Disabled, TEXT("Background resources released."));
}

void AParadoxBlackHole::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	InitializeBackground();
}

void AParadoxBlackHole::PostLoad()
{
	Super::PostLoad();
	if (IsTemplate())
	{
		uint8 SourceMask = 255;
		bSupportsFeatureVariants = IsValid(MaterialVariants) && MaterialVariants->FindSourceDefaults(BaseMaterial, SourceMask);
		if (bSupportsFeatureVariants && !bFeatureFlagsInitialized)
		{
			// Leave the migration marker false: placed actors may override this CDO's source profile.
			AssignFeaturePreferences(SourceMask);
		}
	}
}

#if WITH_EDITOR
void AParadoxBlackHole::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName Name = PropertyChangedEvent.GetPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(AParadoxBlackHole, bEnableAccretionDisk)
		|| Name == GET_MEMBER_NAME_CHECKED(AParadoxBlackHole, bEnableGravitationalLensing)
		|| Name == GET_MEMBER_NAME_CHECKED(AParadoxBlackHole, bEnableRefraction)
		|| Name == GET_MEMBER_NAME_CHECKED(AParadoxBlackHole, bEnableFineNoise)
		|| Name == GET_MEMBER_NAME_CHECKED(AParadoxBlackHole, bEnableDiskStructures)
		|| Name == GET_MEMBER_NAME_CHECKED(AParadoxBlackHole, bEnableVariableVolume)
		|| Name == GET_MEMBER_NAME_CHECKED(AParadoxBlackHole, bEnableDoppler)
		|| Name == GET_MEMBER_NAME_CHECKED(AParadoxBlackHole, bEnableGlow))
	{
		bFeatureFlagsInitialized = true;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
	InitializeBackground();
}
#endif

void AParadoxBlackHole::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	InitializeBackground();
}

void AParadoxBlackHole::BeginPlay()
{
	Super::BeginPlay();
	bShuttingDown = false;
	InitializeBackground();
}

void AParadoxBlackHole::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseBackground();
	Super::EndPlay(EndPlayReason);
}

void AParadoxBlackHole::Destroyed()
{
	ReleaseBackground();
	Super::Destroyed();
}
