#pragma once

#include "Chaos/ChaosEngineInterface.h"
#include "CoreMinimal.h"
#include "FootstepTypes.generated.h"

class AActor;
class UMaterialInterface;
class UNiagaraSystem;
class UPhysicalMaterial;
class UPrimitiveComponent;
class USoundBase;

/** Foot identity authored on an animation notify. */
UENUM(BlueprintType)
enum class EFootstepFoot : uint8
{
	Unspecified,
	Left,
	Right
};

/** How the runtime component chooses the skeletal mesh used for socket queries. */
UENUM(BlueprintType)
enum class EFootstepMeshResolutionPolicy : uint8
{
	/** Prefer the mesh that fired the animation notify, otherwise cache the first skeletal mesh on the owner. */
	AnimationSourceOrOwner,

	/** Resolve the designer-selected component reference. */
	ExplicitComponent
};

/** Floor-query shape used once for every accepted footstep request. */
UENUM(BlueprintType)
enum class EFootstepTraceShape : uint8
{
	Line,
	Sphere
};

/**
 * Independently selectable diagnostic categories.
 *
 * Values are bit positions consumed by the component's int32 bitmask.
 */
UENUM(BlueprintType, meta = (Bitflags))
enum class EFootstepDebugCategory : uint8
{
	Notify,
	Socket,
	Trace,
	Hit,
	Surface,
	Response,
	Audio,
	Niagara,
	Decal,
	Diagnostics
};

/** Returns the int32 mask bit associated with a diagnostic category. */
FORCEINLINE int32 GetFootstepDebugCategoryMask(const EFootstepDebugCategory Category)
{
	return 1 << static_cast<uint8>(Category);
}

/** Lightweight authoring-time request submitted by an animation notify or another caller. */
USTRUCT(BlueprintType)
struct FOOTSTEPSYSTEM_API FFootstepRequest
{
	GENERATED_BODY()

	/** Foot whose contact authored this request. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep System|Request")
	EFootstepFoot Foot = EFootstepFoot::Unspecified;

	/** Optional socket or bone override. NAME_None uses the component's foot-specific configuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep System|Request")
	FName SocketOverride = NAME_None;

	/** Generic cosmetic strength. Runtime processing clamps finite values to [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep System|Request", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NormalizedIntensity = 1.0f;

	/** Returns a copy with a finite intensity clamped to [0, 1]. */
	FFootstepRequest GetSanitized() const;
};

/** Immutable public snapshot generated after the floor query has completed. */
USTRUCT(BlueprintType)
struct FOOTSTEPSYSTEM_API FFootstepEvent
{
	GENERATED_BODY()

	/** Actor that owns the component which generated this event. */
	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	TObjectPtr<AActor> InstigatorActor = nullptr;

	/** Actor reached by the floor query, when one was hit. */
	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	TObjectPtr<AActor> HitActor = nullptr;

	/** Primitive reached by the floor query, when one was hit. */
	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	TObjectPtr<UPrimitiveComponent> HitComponent = nullptr;

	/** Physical Material returned by the query. May be null for an otherwise valid floor hit. */
	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	TObjectPtr<UPhysicalMaterial> PhysicalMaterial = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	EFootstepFoot Foot = EFootstepFoot::Unspecified;

	/** Surface resolved from PhysicalMaterial, or SurfaceType_Default when no material was returned. */
	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	TEnumAsByte<EPhysicalSurface> SurfaceType = SurfaceType_Default;

	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	FVector SurfaceNormal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	FVector TraceStart = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	FVector TraceEnd = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	float NormalizedIntensity = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	float OwnerSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Footstep System|Event")
	bool bHadValidFloorHit = false;
};

/** Cosmetic feedback selected for one Unreal Physical Surface. */
USTRUCT(BlueprintType)
struct FOOTSTEPSYSTEM_API FFootstepSurfaceResponse
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Audio")
	TObjectPtr<USoundBase> Sound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Niagara")
	TObjectPtr<UNiagaraSystem> NiagaraSystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Decal")
	TObjectPtr<UMaterialInterface> DecalMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Audio", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Audio", meta = (ClampMin = "0.01"))
	float PitchMin = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Audio", meta = (ClampMin = "0.01"))
	float PitchMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Niagara", meta = (ClampMin = "0.0"))
	float NiagaraScale = 1.0f;

	/** X is projection depth; Y and Z are decal half-extents. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Decal", meta = (ClampMin = "0.0", Units = "Centimeters"))
	FVector DecalSize = FVector(4.0f, 12.0f, 12.0f);

	/** Zero leaves the decal alive indefinitely. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Decal", meta = (ClampMin = "0.0", Units = "Seconds"))
	float DecalLifeSpan = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Audio")
	bool bSpawnAudio = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Niagara")
	bool bSpawnNiagara = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Footstep System|Decal")
	bool bSpawnDecal = false;
};
