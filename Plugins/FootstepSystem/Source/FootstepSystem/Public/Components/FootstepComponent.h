#pragma once

#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Types/FootstepTypes.h"
#include "FootstepComponent.generated.h"

class FDataValidationContext;
class UFootstepProfile;
class USkeletalMeshComponent;
struct FHitResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FFootstepGeneratedEvent,
	const FFootstepEvent&,
	Event);

DECLARE_MULTICAST_DELEGATE_OneParam(
	FFootstepGeneratedNativeDelegate,
	const FFootstepEvent&);

/**
 * Resolves animation-driven footstep requests into neutral events and optional cosmetic feedback.
 *
 * The component never ticks, never emits AI noise, and does not replicate requests or feedback.
 */
UCLASS(ClassGroup = (FootstepSystem), BlueprintType, meta = (BlueprintSpawnableComponent))
class FOOTSTEPSYSTEM_API UFootstepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFootstepComponent();

	/**
	 * Submits a request using the configured mesh-resolution policy.
	 *
	 * @return True when OnFootstepGenerated was broadcast. A configured miss broadcast also returns true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Footstep System")
	bool SubmitFootstepRequest(const FFootstepRequest& Request);

	/**
	 * Submits an animation request while preserving the mesh that fired the notify.
	 *
	 * @param Request Lightweight animation-authored request.
	 * @param AnimationSourceMesh Mesh which fired the notify.
	 * @return True when OnFootstepGenerated was broadcast.
	 */
	bool SubmitFootstepRequestFromAnimation(
		const FFootstepRequest& Request,
		USkeletalMeshComponent* AnimationSourceMesh);

	/** Native observer invoked before the Blueprint delegate and before default cosmetic feedback. */
	FFootstepGeneratedNativeDelegate& OnFootstepGeneratedNative()
	{
		return FootstepGeneratedNative;
	}

	/** Blueprint observer invoked after native observers and before default cosmetic feedback. */
	UPROPERTY(BlueprintAssignable, Category = "Footstep System|Events")
	FFootstepGeneratedEvent OnFootstepGenerated;

	/** Returns the effective Global AND Local debug state. */
	UFUNCTION(BlueprintPure, Category = "Footstep System|Debug")
	bool IsDebugEnabled() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	friend struct FFootstepComponentTestAccessor;

	bool ProcessFootstepRequest(
		const FFootstepRequest& Request,
		USkeletalMeshComponent* AnimationSourceMesh);
	USkeletalMeshComponent* ResolveSkeletalMesh(USkeletalMeshComponent* AnimationSourceMesh);
	FName ResolveSocketName(const FFootstepRequest& Request) const;
	bool PerformFloorTrace(
		const FVector& TraceStart,
		const FVector& TraceEnd,
		FHitResult& OutHit) const;
	FFootstepEvent BuildFootstepEvent(
		const FFootstepRequest& Request,
		const FVector& TraceStart,
		const FVector& TraceEnd,
		const FHitResult& Hit,
		bool bHadValidHit) const;
	bool ResolveConfiguredResponse(
		TEnumAsByte<EPhysicalSurface> SurfaceType,
		FFootstepSurfaceResponse& OutResponse,
		bool& bOutUsedFallback);
	bool FinalizeFootstepEvent(
		const FFootstepEvent& Event,
		const FFootstepSurfaceResponse* Response,
		bool bUsedFallback);
	void ExecuteDefaultFeedback(
		const FFootstepEvent& Event,
		const FFootstepSurfaceResponse& Response);
	bool ShouldSpawnAudioFeedback(
		const FFootstepEvent& Event,
		const FFootstepSurfaceResponse& Response) const;
	bool ShouldSpawnNiagaraFeedback(
		const FFootstepEvent& Event,
		const FFootstepSurfaceResponse& Response) const;
	bool ShouldSpawnDecalFeedback(
		const FFootstepEvent& Event,
		const FFootstepSurfaceResponse& Response) const;
	void RecordNotifyTiming(const FFootstepRequest& Request);
	void ReportMissingSocket(FName SocketName);
	bool HasDebugCategory(EFootstepDebugCategory Category) const;
	void DrawNotifyDebug(const FFootstepRequest& Request, const FVector& Location) const;
	void DrawSocketDebug(FName SocketName, const FVector& Location) const;
	void DrawTraceDebug(
		const FVector& TraceStart,
		const FVector& TraceEnd,
		bool bHadValidHit) const;
	void DrawResolvedDebug(
		const FFootstepEvent& Event,
		const FFootstepSurfaceResponse* Response,
		bool bUsedFallback) const;
	void DrawFeedbackDebug(
		EFootstepDebugCategory Category,
		const FVector& Location,
		const FColor& Color,
		const FString& Label) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Profile", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UFootstepProfile> FootstepProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Mesh", meta = (AllowPrivateAccess = "true"))
	EFootstepMeshResolutionPolicy MeshResolutionPolicy =
		EFootstepMeshResolutionPolicy::AnimationSourceOrOwner;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Footstep System|Mesh",
		meta = (
			AllowPrivateAccess = "true",
			EditCondition = "MeshResolutionPolicy == EFootstepMeshResolutionPolicy::ExplicitComponent",
			UseComponentPicker,
			AllowedClasses = "/Script/Engine.SkeletalMeshComponent"))
	FComponentReference ExplicitSkeletalMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Sockets", meta = (AllowPrivateAccess = "true"))
	FName LeftFootSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Sockets", meta = (AllowPrivateAccess = "true"))
	FName RightFootSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Sockets", meta = (AllowPrivateAccess = "true"))
	FName DefaultFootSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Trace", meta = (AllowPrivateAccess = "true", Units = "Centimeters"))
	float TraceStartOffset = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Trace", meta = (AllowPrivateAccess = "true", ClampMin = "0.1", Units = "Centimeters"))
	float TraceLength = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Trace", meta = (AllowPrivateAccess = "true"))
	EFootstepTraceShape TraceShape = EFootstepTraceShape::Line;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Footstep System|Trace",
		meta = (
			AllowPrivateAccess = "true",
			ClampMin = "0.1",
			Units = "Centimeters",
			EditCondition = "TraceShape == EFootstepTraceShape::Sphere"))
	float SphereTraceRadius = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Trace", meta = (AllowPrivateAccess = "true"))
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Trace", meta = (AllowPrivateAccess = "true"))
	bool bTraceComplex = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Trace", meta = (AllowPrivateAccess = "true"))
	bool bIgnoreOwner = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Events", meta = (AllowPrivateAccess = "true"))
	bool bBroadcastOnNoFloorHit = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Feedback", meta = (AllowPrivateAccess = "true"))
	bool bEnableAudio = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Feedback", meta = (AllowPrivateAccess = "true"))
	bool bEnableNiagara = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Feedback", meta = (AllowPrivateAccess = "true"))
	bool bEnableDecals = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Debug", meta = (AllowPrivateAccess = "true"))
	bool bEnableDebug = false;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Footstep System|Debug",
		meta = (
			AllowPrivateAccess = "true",
			Bitmask,
			BitmaskEnum = "/Script/FootstepSystem.EFootstepDebugCategory",
			EditCondition = "bEnableDebug"))
	int32 DebugCategories = 0;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Footstep System|Debug",
		meta = (
			AllowPrivateAccess = "true",
			ClampMin = "0.0",
			Units = "Seconds",
			EditCondition = "bEnableDebug"))
	float DebugDrawDuration = 1.0f;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Footstep System|Debug",
		meta = (
			AllowPrivateAccess = "true",
			ClampMin = "0.0",
			Units = "Seconds",
			EditCondition = "bEnableDebug"))
	float RapidNotifyThreshold = 0.08f;

	TWeakObjectPtr<USkeletalMeshComponent> CachedOwnerSkeletalMesh;
	TWeakObjectPtr<USkeletalMeshComponent> CachedExplicitSkeletalMesh;
	FFootstepGeneratedNativeDelegate FootstepGeneratedNative;
	TSet<FName> ReportedMissingSockets;
	TSet<TEnumAsByte<EPhysicalSurface>> ReportedMissingSurfaces;
	double LastNotifyTimes[3];
	bool bAcceptingRequests = false;
	bool bReportedMissingMesh = false;
	bool bReportedMissingProfile = false;
};
