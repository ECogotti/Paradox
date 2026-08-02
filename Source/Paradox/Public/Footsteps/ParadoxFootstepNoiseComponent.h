#pragma once

#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Footsteps/ParadoxFootstepNoiseTypes.h"
#include "Types/PerceptionKnowledgeTypes.h"
#include "Types/FootstepTypes.h"
#include "ParadoxFootstepNoiseComponent.generated.h"

class UFootstepComponent;
class UParadoxFootstepNoiseProfile;
class UPerceptionKnowledgeSourceComponent;

/**
 * Converts generic footstep events into project semantic Hearing noise.
 *
 * The component never ticks and never talks directly to Intent Replay.
 */
UCLASS(ClassGroup = (Paradox), BlueprintType, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxFootstepNoiseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxFootstepNoiseComponent();

	UFUNCTION(BlueprintPure, Category = "Paradox|Footsteps|Perception")
	bool HasProcessedFootstep() const { return bHasProcessedFootstep; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Footsteps|Perception")
	EParadoxFootstepNoiseResult GetLastResult() const { return LastResult; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Footsteps|Perception")
	FString GetLastDiagnosticMessage() const { return LastDiagnosticMessage; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Footsteps|Perception")
	UParadoxFootstepNoiseProfile* GetNoiseProfile() const { return NoiseProfile.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Footsteps|Perception")
	bool IgnoresNoiseDuringCrouch() const { return bIgnoreNoiseDuringCrouch; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Footsteps|Debug")
	bool IsDebugEnabled() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend struct FParadoxFootstepNoiseTestAccessor;

	void ResolveDependencies();
	void BindToFootstepComponent();
	void UnbindFromFootstepComponent();
	void HandleFootstepGenerated(const FFootstepEvent& Event);
	EParadoxFootstepNoiseResult ProcessFootstepEvent(
		const FFootstepEvent& Event,
		FParadoxFootstepNoiseResponse& OutResponse,
		bool& bOutOwnerCrouched,
		float& OutEffectiveLoudness,
		FString& OutDiagnostic);
	FPerceptionKnowledgeOperationResult EmitNoise(
		UPerceptionKnowledgeSourceComponent& Source,
		const FPerceptionKnowledgeNoiseRequest& Request);
	void CompleteProcessing(
		const FFootstepEvent& Event,
		EParadoxFootstepNoiseResult Result,
		const FParadoxFootstepNoiseResponse* Response,
		bool bOwnerCrouched,
		float EffectiveLoudness,
		const FString& Diagnostic);
	void ReportResult(
		EParadoxFootstepNoiseResult Result,
		TEnumAsByte<EPhysicalSurface> SurfaceType,
		const FString& Diagnostic);
	void DrawProcessingDebug(
		const FFootstepEvent& Event,
		EParadoxFootstepNoiseResult Result,
		const FParadoxFootstepNoiseResponse* Response,
		bool bOwnerCrouched,
		float EffectiveLoudness) const;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Paradox|Footsteps|Dependencies",
		meta = (
			AllowPrivateAccess = "true",
			UseComponentPicker,
			AllowedClasses = "/Script/FootstepSystem.FootstepComponent"))
	FComponentReference FootstepComponentOverride;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Paradox|Footsteps|Dependencies",
		meta = (
			AllowPrivateAccess = "true",
			UseComponentPicker,
			AllowedClasses = "/Script/PerceptionKnowledge.PerceptionKnowledgeSourceComponent"))
	FComponentReference PerceptionSourceOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Footsteps|Perception", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxFootstepNoiseProfile> NoiseProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Footsteps|Perception", meta = (AllowPrivateAccess = "true"))
	bool bIgnoreNoiseDuringCrouch = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Footsteps|Debug", meta = (AllowPrivateAccess = "true"))
	bool bEnableDebug = false;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Paradox|Footsteps|Debug",
		meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "Seconds", EditCondition = "bEnableDebug"))
	float DebugDrawDuration = 1.5f;

	UPROPERTY(Transient)
	TObjectPtr<UFootstepComponent> BoundFootstepComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPerceptionKnowledgeSourceComponent> PerceptionSource = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Paradox|Footsteps|Perception", meta = (AllowPrivateAccess = "true"))
	EParadoxFootstepNoiseResult LastResult =
		EParadoxFootstepNoiseResult::InvalidEvent;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Paradox|Footsteps|Perception", meta = (AllowPrivateAccess = "true"))
	FString LastDiagnosticMessage;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Paradox|Footsteps|Perception", meta = (AllowPrivateAccess = "true"))
	bool bHasProcessedFootstep = false;

	TSet<EParadoxFootstepNoiseResult> ReportedResults;
	TSet<TEnumAsByte<EPhysicalSurface>> ReportedMissingSurfaces;
	bool bAcceptingEvents = false;

#if WITH_DEV_AUTOMATION_TESTS
	TFunction<
		FPerceptionKnowledgeOperationResult(
			UPerceptionKnowledgeSourceComponent&,
			const FPerceptionKnowledgeNoiseRequest&)> TestNoiseEmitter;
#endif
};
