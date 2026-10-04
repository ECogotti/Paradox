#pragma once

#include "GameFramework/Actor.h"
#include "ParadoxAccretionExtension.generated.h"

class AParadoxBlackHole;
class UNiagaraComponent;
class UNiagaraSystem;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneComponent;
class USceneCaptureComponent2D;

UENUM(BlueprintType)
enum class EParadoxAccretionQuality : uint8 { Low, Medium, High };

USTRUCT(BlueprintType)
struct PARADOX_API FParadoxAccretionTuning
{
	GENERATED_BODY()
	/** World-space half extents around the gameplay-plane gizmo, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "100", Units = "cm"))
	FVector LocalExtent = FVector(6000, 6000, 1800);
	/** Control points are fractions of the source-to-gameplay distance in the route frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector BridgeControlA = FVector(0.33, 0.12, 0.05);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector BridgeControlB = FVector(0.72, -0.10, 0.03);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "0", ClampMax = "1"))
	float ForegroundIntensity = 0.20f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "0", ClampMax = "2"))
	float MidgroundIntensity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "0.005", ClampMax = "0.25"))
	float GasOpacity = 0.16f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "0", ClampMax = "4"))
	float Brightness = 1.20f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility", meta = (ClampMin = "10", ClampMax = "1000000", Units = "cm"))
	float LayerTransition = 450.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0", ClampMax = "4"))
	float SpeedMultiplier = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "2", ClampMax = "60", Units = "s"))
	float Lifetime = 18.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0", ClampMax = "1"))
	float Turbulence = 0.22f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detail", meta = (ClampMin = "0.1", ClampMax = "3"))
	float ParticleScale = 1.0f;
	/** -1 uses the same material Time input as the black hole; nonnegative values are for previews. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Motion", meta = (ClampMin = "-1"))
	float AnimationTimeOverride = -1.0f;
};

/** World-space gas, filaments and light fragments extending one black-hole disk. */
UCLASS(BlueprintType, Blueprintable, PrioritizeCategories = ("Paradox"))
class PARADOX_API AParadoxAccretionExtension : public AActor
{
	GENERATED_BODY()
public:
	AParadoxAccretionExtension();
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, BlueprintSetter = SetSourceBlackHole, Category = "Paradox|Accretion|Setup")
	TObjectPtr<AParadoxBlackHole> SourceBlackHole;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetEnabled, Category = "Paradox|Accretion|Features")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetGasEnabled, Category = "Paradox|Accretion|Features", meta = (EditCondition = "bEnabled"))
	bool bEnableGas = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFilamentsEnabled, Category = "Paradox|Accretion|Features", meta = (EditCondition = "bEnabled"))
	bool bEnableFilaments = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSparksEnabled, Category = "Paradox|Accretion|Features", meta = (EditCondition = "bEnabled"))
	bool bEnableSparks = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetQuality, Category = "Paradox|Accretion|Features", meta = (EditCondition = "bEnabled"))
	EParadoxAccretionQuality Quality = EParadoxAccretionQuality::Low;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Accretion|Tuning", meta = (EditCondition = "bEnabled", EditConditionHides))
	FParadoxAccretionTuning Tuning;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Accretion|Preview")
	bool bEnableEditorPreview = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Accretion|Debug")
	bool bEnableDebug = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> GameplayPlane;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, AdvancedDisplay, Category = "Paradox|Accretion|Status")
	bool bActive = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, AdvancedDisplay, Category = "Paradox|Accretion|Status")
	FString StateMessage;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, AdvancedDisplay, Category = "Paradox|Accretion|Status")
	int32 ActiveParticleCount = 0;

	UFUNCTION(BlueprintSetter, Category = "Paradox|Accretion") void SetSourceBlackHole(AParadoxBlackHole* InSource);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Accretion") void SetEnabled(bool bInEnabled);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Accretion") void SetGasEnabled(bool bInEnabled);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Accretion") void SetFilamentsEnabled(bool bInEnabled);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Accretion") void SetSparksEnabled(bool bInEnabled);
	UFUNCTION(BlueprintSetter, Category = "Paradox|Accretion") void SetQuality(EParadoxAccretionQuality InQuality);
	/** Rejects non-finite/out-of-range values without overwriting valid tuning. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Accretion") bool SetTuning(const FParadoxAccretionTuning& InTuning);
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Paradox|Accretion") bool RefreshExtension();
	UFUNCTION(BlueprintPure, Category = "Paradox|Accretion") TArray<UNiagaraComponent*> GetParticleComponents() const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Destroyed() override;
	virtual bool ShouldTickIfViewportsOnly() const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
	UPROPERTY(EditDefaultsOnly, Category = "Paradox|Accretion|Assets") TArray<TObjectPtr<UNiagaraSystem>> ParticleSystems;
	UPROPERTY(EditDefaultsOnly, Category = "Paradox|Accretion|Assets") TArray<TObjectPtr<UMaterialInterface>> ParticleMaterials;

private:
	bool ValidateTuning(const FParadoxAccretionTuning& Value) const;
	bool UpdateField();
	bool Fail(const FString& Message);
	void StopParticles();
	void DetachCapture();
	void UpdateCapture();
	UPROPERTY(VisibleAnywhere, Category = "Components") TArray<TObjectPtr<UNiagaraComponent>> ParticleComponents;
	UPROPERTY(Transient, DuplicateTransient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	UPROPERTY(Transient) TWeakObjectPtr<USceneCaptureComponent2D> RegisteredCapture;
	TArray<int32> ConfiguredCounts;
	bool bShuttingDown = false;
	bool bRefreshing = false;
	bool bFieldDirty = true;
	FTransform CachedActorTransform, CachedSourceTransform, CachedPlaneTransform;
	float CachedRadius = 0, CachedOuterRadius = 0, CachedRotationSpeed = 0;
	FLinearColor CachedInnerColor, CachedOuterColor;
	TWeakObjectPtr<class UTexture> CachedStructureTexture;
	FVector CachedStart, CachedA, CachedB, CachedPlane;
};
