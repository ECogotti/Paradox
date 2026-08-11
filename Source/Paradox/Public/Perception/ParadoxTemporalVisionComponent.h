#pragma once

#include "Engine/OverlapResult.h"
#include "LineOfSightComponent.h"
#include "Perception/ParadoxTemporalVisionTypes.h"
#include "ParadoxTemporalVisionComponent.generated.h"

class UPrimitiveComponent;
class USphereComponent;

/** Actor-level query state used to deduplicate multiple Pawn components. */
struct FParadoxTemporalActorOverlapState
{
	TSet<TWeakObjectPtr<UPrimitiveComponent>> Components;
	bool bPassesConeFilter = false;
	bool bBroadcastForCurrentAuthority = false;
};

/**
 * Clone-owned authoritative perception geometry.
 *
 * The plugin's traces shape the collisionless visible mesh. A Pawn-only sphere query supplies
 * broad-phase candidates without registering a large moving physics body. Candidates are filtered
 * by the configured cone range, angle and occlusion before temporal-order evaluation.
 */
UCLASS(ClassGroup = (Paradox), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxTemporalVisionComponent : public ULineOfSightComponent
{
	GENERATED_BODY()

public:
	UParadoxTemporalVisionComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Temporal Vision|Events")
	FParadoxTemporalOverlapEvent OnTemporalOverlapDetected;

	/** Builds an up-to-date visual mesh and prepares the Pawn sphere query with authority disabled. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Temporal Vision")
	bool PrepareTemporalVision(FString& OutFailure);

	/** Binds the clone-owned Pawn query shape. Intended for native composition setup. */
	void SetCandidateSphereComponent(USphereComponent* InCandidateSphere);

	/** Synchronizes the query-shape radius with the largest configured cone radius. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Temporal Vision")
	void SynchronizeCandidateSphere();

	/** Immediately runs the Pawn-only sphere query and reevaluates the active cone filter. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Temporal Vision")
	void RefreshTemporalCandidateFilter();

	UFUNCTION(BlueprintPure, Category = "Paradox|Temporal Vision")
	USphereComponent* GetCandidateSphereComponent() const { return CandidateSphere.Get(); }

	/** Pure geometric query using the currently configured cone range and half-angle. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Temporal Vision")
	bool IsWorldLocationWithinConfiguredCone(const FVector& WorldLocation) const;

	/** Releases actor-level candidate delivery for one synchronized run. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Temporal Vision")
	void EnableTemporalDetection(int32 InDetectionSessionId);

	/** Revokes authority before rewind, reset, presentation or teardown. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Temporal Vision")
	void DisableTemporalDetection(bool bClearPhysicalOverlapState = true);

	UFUNCTION(BlueprintPure, Category = "Paradox|Temporal Vision")
	bool IsTemporalDetectionAuthoritative() const { return bDetectionAuthoritative; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Temporal Vision")
	int32 GetDetectionSessionId() const { return DetectionSessionId; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Temporal Vision")
	int32 GetDeduplicatedOverlapActorCount() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Temporal Vision")
	FParadoxTemporalOverlapSnapshot GetLastPhysicalOverlap() const { return LastPhysicalOverlap; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Temporal Vision|Debug")
	FParadoxTemporalVisionDebugSnapshot GetDebugSnapshot() const;

protected:
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Fixed object channel used by the Pawn-only sphere query. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Temporal Vision|Collision")
	TEnumAsByte<ECollisionChannel> TemporalTargetObjectChannel = ECC_Pawn;

	/** Channel used only to shape the LineOfSight mesh around level occluders. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Temporal Vision|Mesh")
	TEnumAsByte<ECollisionChannel> MeshOcclusionTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Temporal Vision|Mesh", meta = (ClampMin = "1", UIMin = "1"))
	int32 TraceResolution = 60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Temporal Vision|Debug")
	bool bEnableDebug = false;

private:
	void ConfigureCandidateSphereShape();
	void QuerySphereCandidates();
	void TrackSphereOverlap(AActor* OtherActor, UPrimitiveComponent* OtherComponent);
	void EvaluateSphereCandidates();
	bool PassesDistanceAndAngleFilter(
		const AActor& OtherActor,
		const UPrimitiveComponent* RepresentativeComponent) const;
	bool HasClearTemporalLine(
		const AActor& OtherActor,
		const FVector& TargetLocation) const;
	float GetConfiguredOuterRadius() const;
	float GetConfiguredInnerRadius() const;
	float GetConfiguredHalfAngle() const;
	void BroadcastActorCandidate(
		AActor& OtherActor,
		UPrimitiveComponent* RepresentativeComponent,
		FParadoxTemporalActorOverlapState& State);
	FParadoxTemporalOverlapSnapshot MakeSnapshot(
		AActor& OtherActor,
		UPrimitiveComponent* RepresentativeComponent,
		int32 ComponentCount) const;
	void DrawTemporalDebug() const;

	TMap<TWeakObjectPtr<AActor>, FParadoxTemporalActorOverlapState> ActorOverlapStates;
	TArray<FOverlapResult> CandidateOverlapBuffer;

	UPROPERTY(Transient)
	TObjectPtr<USphereComponent> CandidateSphere;

	UPROPERTY(Transient)
	FParadoxTemporalOverlapSnapshot LastPhysicalOverlap;

	int32 DetectionSessionId = INDEX_NONE;
	bool bDetectionAuthoritative = false;
	bool bTemporalVisionPrepared = false;
};
