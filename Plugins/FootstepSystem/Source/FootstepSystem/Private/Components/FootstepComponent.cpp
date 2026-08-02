#include "Components/FootstepComponent.h"

#include "Components/DecalComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/FootstepProfile.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "FootstepSystemModule.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Misc/DataValidation.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Sound/SoundBase.h"

#define LOCTEXT_NAMESPACE "FootstepComponent"

namespace
{
	const TCHAR* GetFootLabel(const EFootstepFoot Foot)
	{
		switch (Foot)
		{
		case EFootstepFoot::Left:
			return TEXT("Left");
		case EFootstepFoot::Right:
			return TEXT("Right");
		default:
			return TEXT("Unspecified");
		}
	}

	FString GetSurfaceLabel(const EPhysicalSurface SurfaceType)
	{
		if (const UPhysicsSettings* PhysicsSettings = GetDefault<UPhysicsSettings>())
		{
			for (const FPhysicalSurfaceName& Surface : PhysicsSettings->PhysicalSurfaces)
			{
				if (Surface.Type == SurfaceType && !Surface.Name.IsNone())
				{
					return Surface.Name.ToString();
				}
			}
		}

		return SurfaceType == SurfaceType_Default
			? TEXT("Default")
			: FString::Printf(TEXT("SurfaceType%d"), static_cast<int32>(SurfaceType));
	}

	float GetSafeNonNegative(const float Value, const float Fallback = 0.0f)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : Fallback;
	}

	float GetSafePositive(const float Value, const float Fallback = 1.0f)
	{
		return FMath::IsFinite(Value) && Value > 0.0f ? Value : Fallback;
	}
}

UFootstepComponent::UFootstepComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
	for (double& LastNotifyTime : LastNotifyTimes)
	{
		LastNotifyTime = -TNumericLimits<double>::Max();
	}
}

void UFootstepComponent::BeginPlay()
{
	Super::BeginPlay();
	bAcceptingRequests = true;

	if (MeshResolutionPolicy == EFootstepMeshResolutionPolicy::AnimationSourceOrOwner)
	{
		ResolveSkeletalMesh(nullptr);
	}

	if (!FootstepProfile && !bReportedMissingProfile)
	{
		FOOTSTEPSYSTEM_LOG_WARNING(
			TEXT("Footstep component '%s' on '%s' has no profile; events remain available but default feedback is disabled."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
		bReportedMissingProfile = true;
	}

	if (LeftFootSocket.IsNone() && RightFootSocket.IsNone() && DefaultFootSocket.IsNone())
	{
		FOOTSTEPSYSTEM_LOG_WARNING(
			TEXT("Footstep component '%s' on '%s' has no configured foot sockets; requests require a socket override."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
	}
}

void UFootstepComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bAcceptingRequests = false;
	CachedOwnerSkeletalMesh.Reset();
	CachedExplicitSkeletalMesh.Reset();
	ReportedMissingSockets.Reset();
	ReportedMissingSurfaces.Reset();
	FootstepGeneratedNative.Clear();
	Super::EndPlay(EndPlayReason);
}

bool UFootstepComponent::SubmitFootstepRequest(const FFootstepRequest& Request)
{
	return ProcessFootstepRequest(Request, nullptr);
}

bool UFootstepComponent::SubmitFootstepRequestFromAnimation(
	const FFootstepRequest& Request,
	USkeletalMeshComponent* AnimationSourceMesh)
{
	return ProcessFootstepRequest(Request, AnimationSourceMesh);
}

bool UFootstepComponent::IsDebugEnabled() const
{
	return bEnableDebug && IsFootstepSystemDebugEnabled();
}

bool UFootstepComponent::ProcessFootstepRequest(
	const FFootstepRequest& Request,
	USkeletalMeshComponent* AnimationSourceMesh)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FootstepSystem_ProcessFootstepRequest);

	if (!bAcceptingRequests || !IsActive())
	{
		if (HasDebugCategory(EFootstepDebugCategory::Diagnostics))
		{
			FOOTSTEPSYSTEM_LOG_WARNING(
				TEXT("Footstep request rejected because component '%s' on '%s' is inactive or ending play."),
				*GetNameSafe(this),
				*GetNameSafe(GetOwner()));
		}
		return false;
	}

	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!IsValid(Owner) || !World)
	{
		if (HasDebugCategory(EFootstepDebugCategory::Diagnostics))
		{
			FOOTSTEPSYSTEM_LOG_ERROR(
				TEXT("Footstep component '%s' cannot process a request without a valid owner and world."),
				*GetNameSafe(this));
		}
		return false;
	}

	const FFootstepRequest SanitizedRequest = Request.GetSanitized();
	RecordNotifyTiming(SanitizedRequest);

	USkeletalMeshComponent* SkeletalMesh = ResolveSkeletalMesh(AnimationSourceMesh);
	if (!SkeletalMesh)
	{
		if (!bReportedMissingMesh)
		{
			FOOTSTEPSYSTEM_LOG_WARNING(
				TEXT("Footstep component '%s' on '%s' could not resolve a registered skeletal mesh."),
				*GetNameSafe(this),
				*GetNameSafe(Owner));
			bReportedMissingMesh = true;
		}
		return false;
	}
	bReportedMissingMesh = false;

	DrawNotifyDebug(SanitizedRequest, SkeletalMesh->GetComponentLocation());

	const FName SocketName = ResolveSocketName(SanitizedRequest);
	if (SocketName.IsNone() || !SkeletalMesh->DoesSocketExist(SocketName))
	{
		ReportMissingSocket(SocketName);
		return false;
	}

	const FVector SocketLocation = SkeletalMesh->GetSocketLocation(SocketName);
	const FVector TraceStart = SocketLocation + FVector::UpVector * TraceStartOffset;
	const FVector TraceEnd = TraceStart - FVector::UpVector * FMath::Max(0.1f, TraceLength);
	DrawSocketDebug(SocketName, SocketLocation);

	FHitResult Hit;
	const bool bHadValidHit = PerformFloorTrace(TraceStart, TraceEnd, Hit);
	DrawTraceDebug(TraceStart, TraceEnd, bHadValidHit);

	const FFootstepEvent Event = BuildFootstepEvent(
		SanitizedRequest,
		TraceStart,
		TraceEnd,
		Hit,
		bHadValidHit);

	FFootstepSurfaceResponse Response;
	bool bUsedFallback = false;
	const bool bHasResponse = bHadValidHit
		&& ResolveConfiguredResponse(Event.SurfaceType, Response, bUsedFallback);

	return FinalizeFootstepEvent(
		Event,
		bHasResponse ? &Response : nullptr,
		bUsedFallback);
}

USkeletalMeshComponent* UFootstepComponent::ResolveSkeletalMesh(
	USkeletalMeshComponent* AnimationSourceMesh)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}

	auto IsUsableMesh = [World](USkeletalMeshComponent* Candidate)
	{
		return IsValid(Candidate)
			&& Candidate->IsRegistered()
			&& Candidate->GetWorld() == World;
	};

	if (MeshResolutionPolicy == EFootstepMeshResolutionPolicy::ExplicitComponent)
	{
		if (USkeletalMeshComponent* Cached = CachedExplicitSkeletalMesh.Get();
			IsUsableMesh(Cached))
		{
			return Cached;
		}

		USkeletalMeshComponent* ExplicitMesh = Cast<USkeletalMeshComponent>(
			ExplicitSkeletalMeshComponent.GetComponent(Owner));
		if (IsUsableMesh(ExplicitMesh))
		{
			CachedExplicitSkeletalMesh = ExplicitMesh;
			return ExplicitMesh;
		}
		return nullptr;
	}

	if (IsUsableMesh(AnimationSourceMesh))
	{
		return AnimationSourceMesh;
	}

	if (USkeletalMeshComponent* Cached = CachedOwnerSkeletalMesh.Get();
		IsUsableMesh(Cached))
	{
		return Cached;
	}

	USkeletalMeshComponent* OwnerMesh =
		Owner->FindComponentByClass<USkeletalMeshComponent>();
	if (IsUsableMesh(OwnerMesh))
	{
		CachedOwnerSkeletalMesh = OwnerMesh;
		return OwnerMesh;
	}
	return nullptr;
}

FName UFootstepComponent::ResolveSocketName(const FFootstepRequest& Request) const
{
	if (!Request.SocketOverride.IsNone())
	{
		return Request.SocketOverride;
	}

	switch (Request.Foot)
	{
	case EFootstepFoot::Left:
		return LeftFootSocket;
	case EFootstepFoot::Right:
		return RightFootSocket;
	default:
		return DefaultFootSocket;
	}
}

bool UFootstepComponent::PerformFloorTrace(
	const FVector& TraceStart,
	const FVector& TraceEnd,
	FHitResult& OutHit) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(FootstepSystemFloorTrace),
		bTraceComplex);
	QueryParams.bReturnPhysicalMaterial = true;
	if (bIgnoreOwner && GetOwner())
	{
		QueryParams.AddIgnoredActor(GetOwner());
	}

	if (TraceShape == EFootstepTraceShape::Sphere)
	{
		return World->SweepSingleByChannel(
			OutHit,
			TraceStart,
			TraceEnd,
			FQuat::Identity,
			TraceChannel.GetValue(),
			FCollisionShape::MakeSphere(FMath::Max(0.1f, SphereTraceRadius)),
			QueryParams);
	}

	return World->LineTraceSingleByChannel(
		OutHit,
		TraceStart,
		TraceEnd,
		TraceChannel.GetValue(),
		QueryParams);
}

FFootstepEvent UFootstepComponent::BuildFootstepEvent(
	const FFootstepRequest& Request,
	const FVector& TraceStart,
	const FVector& TraceEnd,
	const FHitResult& Hit,
	const bool bHadValidHit) const
{
	FFootstepEvent Event;
	Event.InstigatorActor = GetOwner();
	Event.Foot = Request.Foot;
	Event.TraceStart = TraceStart;
	Event.TraceEnd = TraceEnd;
	Event.NormalizedIntensity = Request.NormalizedIntensity;
	Event.OwnerSpeed = GetOwner() ? GetOwner()->GetVelocity().Size() : 0.0f;
	Event.bHadValidFloorHit = bHadValidHit;

	if (!bHadValidHit)
	{
		Event.WorldLocation = TraceEnd;
		return Event;
	}

	Event.HitActor = Hit.GetActor();
	Event.HitComponent = Hit.GetComponent();
	Event.PhysicalMaterial = Hit.PhysMaterial.Get();
	Event.SurfaceType = UPhysicalMaterial::DetermineSurfaceType(Event.PhysicalMaterial);
	Event.WorldLocation = Hit.ImpactPoint;
	Event.SurfaceNormal = Hit.ImpactNormal.GetSafeNormal(
		KINDA_SMALL_NUMBER,
		FVector::UpVector);
	return Event;
}

bool UFootstepComponent::ResolveConfiguredResponse(
	const TEnumAsByte<EPhysicalSurface> SurfaceType,
	FFootstepSurfaceResponse& OutResponse,
	bool& bOutUsedFallback)
{
	if (!FootstepProfile)
	{
		if (!bReportedMissingProfile)
		{
			FOOTSTEPSYSTEM_LOG_WARNING(
				TEXT("Footstep component '%s' on '%s' has no profile; the resolved event will not spawn default feedback."),
				*GetNameSafe(this),
				*GetNameSafe(GetOwner()));
			bReportedMissingProfile = true;
		}
		bOutUsedFallback = false;
		return false;
	}
	bReportedMissingProfile = false;

	if (FootstepProfile->ResolveResponse(
		SurfaceType,
		OutResponse,
		bOutUsedFallback))
	{
		return true;
	}

	if (!ReportedMissingSurfaces.Contains(SurfaceType))
	{
		FOOTSTEPSYSTEM_LOG_WARNING(
			TEXT("Footstep profile '%s' has no response or fallback for surface '%s' on owner '%s'."),
			*GetNameSafe(FootstepProfile),
			*GetSurfaceLabel(SurfaceType),
			*GetNameSafe(GetOwner()));
		ReportedMissingSurfaces.Add(SurfaceType);
	}
	return false;
}

bool UFootstepComponent::FinalizeFootstepEvent(
	const FFootstepEvent& Event,
	const FFootstepSurfaceResponse* Response,
	const bool bUsedFallback)
{
	DrawResolvedDebug(Event, Response, bUsedFallback);

	if (!Event.bHadValidFloorHit && !bBroadcastOnNoFloorHit)
	{
		return false;
	}

	FootstepGeneratedNative.Broadcast(Event);
	OnFootstepGenerated.Broadcast(Event);

	if (!Event.bHadValidFloorHit
		|| !Response
		|| !::IsValid(this)
		|| !::IsValid(GetOwner())
		|| !GetWorld())
	{
		return true;
	}

	ExecuteDefaultFeedback(Event, *Response);
	return true;
}

void UFootstepComponent::ExecuteDefaultFeedback(
	const FFootstepEvent& Event,
	const FFootstepSurfaceResponse& Response)
{
	UWorld* World = GetWorld();
	if (!World
		|| World->GetNetMode() == NM_DedicatedServer
		|| Event.NormalizedIntensity <= 0.0f)
	{
		return;
	}

	if (ShouldSpawnAudioFeedback(Event, Response))
	{
		const float Volume =
			GetSafeNonNegative(Response.VolumeMultiplier) * Event.NormalizedIntensity;
		float PitchMin = GetSafePositive(Response.PitchMin);
		float PitchMax = GetSafePositive(Response.PitchMax);
		if (PitchMin > PitchMax)
		{
			Swap(PitchMin, PitchMax);
		}
		const float Pitch = FMath::FRandRange(PitchMin, PitchMax);
		UGameplayStatics::PlaySoundAtLocation(
			this,
			Response.Sound,
			Event.WorldLocation,
			FRotator::ZeroRotator,
			Volume,
			Pitch,
			0.0f,
			nullptr,
			nullptr,
			Event.InstigatorActor);
		DrawFeedbackDebug(
			EFootstepDebugCategory::Audio,
			Event.WorldLocation,
			FColor::Blue,
			FString::Printf(TEXT("Audio: %s"), *GetNameSafe(Response.Sound)));
	}

	if (ShouldSpawnNiagaraFeedback(Event, Response))
	{
		const float Scale =
			GetSafeNonNegative(Response.NiagaraScale) * Event.NormalizedIntensity;
		const FRotator Rotation =
			FRotationMatrix::MakeFromZ(Event.SurfaceNormal).Rotator();
		UNiagaraComponent* SpawnedNiagara =
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				this,
				Response.NiagaraSystem,
				Event.WorldLocation,
				Rotation,
				FVector(Scale),
				true,
				true,
				ENCPoolMethod::None,
				true);
		if (!SpawnedNiagara)
		{
			FOOTSTEPSYSTEM_LOG_WARNING(
				TEXT("Failed to spawn Niagara system '%s' for owner '%s'."),
				*GetNameSafe(Response.NiagaraSystem),
				*GetNameSafe(GetOwner()));
		}
		else
		{
			DrawFeedbackDebug(
				EFootstepDebugCategory::Niagara,
				Event.WorldLocation,
				FColor::Purple,
				FString::Printf(
					TEXT("Niagara: %s"),
					*GetNameSafe(Response.NiagaraSystem)));
		}
	}

	if (ShouldSpawnDecalFeedback(Event, Response))
	{
		const FVector DecalSize(
			GetSafeNonNegative(Response.DecalSize.X) * Event.NormalizedIntensity,
			GetSafeNonNegative(Response.DecalSize.Y) * Event.NormalizedIntensity,
			GetSafeNonNegative(Response.DecalSize.Z) * Event.NormalizedIntensity);
		UDecalComponent* SpawnedDecal =
			UGameplayStatics::SpawnDecalAtLocation(
				this,
				Response.DecalMaterial,
				DecalSize,
				Event.WorldLocation,
				Event.SurfaceNormal.Rotation(),
				GetSafeNonNegative(Response.DecalLifeSpan));
		if (!SpawnedDecal)
		{
			FOOTSTEPSYSTEM_LOG_WARNING(
				TEXT("Failed to spawn decal material '%s' for owner '%s'."),
				*GetNameSafe(Response.DecalMaterial),
				*GetNameSafe(GetOwner()));
		}
		else
		{
			DrawFeedbackDebug(
				EFootstepDebugCategory::Decal,
				Event.WorldLocation,
				FColor::Orange,
				FString::Printf(
					TEXT("Decal: %s"),
					*GetNameSafe(Response.DecalMaterial)));
		}
	}
}

bool UFootstepComponent::ShouldSpawnAudioFeedback(
	const FFootstepEvent& Event,
	const FFootstepSurfaceResponse& Response) const
{
	return bEnableAudio
		&& Response.bSpawnAudio
		&& Response.Sound
		&& Event.NormalizedIntensity > 0.0f
		&& GetSafeNonNegative(Response.VolumeMultiplier) * Event.NormalizedIntensity
			> KINDA_SMALL_NUMBER;
}

bool UFootstepComponent::ShouldSpawnNiagaraFeedback(
	const FFootstepEvent& Event,
	const FFootstepSurfaceResponse& Response) const
{
	return bEnableNiagara
		&& Response.bSpawnNiagara
		&& Response.NiagaraSystem
		&& Event.NormalizedIntensity > 0.0f
		&& GetSafeNonNegative(Response.NiagaraScale) * Event.NormalizedIntensity
			> KINDA_SMALL_NUMBER;
}

bool UFootstepComponent::ShouldSpawnDecalFeedback(
	const FFootstepEvent& Event,
	const FFootstepSurfaceResponse& Response) const
{
	const FVector ScaledSize(
		GetSafeNonNegative(Response.DecalSize.X) * Event.NormalizedIntensity,
		GetSafeNonNegative(Response.DecalSize.Y) * Event.NormalizedIntensity,
		GetSafeNonNegative(Response.DecalSize.Z) * Event.NormalizedIntensity);
	return bEnableDecals
		&& Response.bSpawnDecal
		&& Response.DecalMaterial
		&& Event.NormalizedIntensity > 0.0f
		&& ScaledSize.GetMax() > KINDA_SMALL_NUMBER;
}

void UFootstepComponent::RecordNotifyTiming(const FFootstepRequest& Request)
{
	UWorld* World = GetWorld();
	const int32 FootIndex = static_cast<int32>(Request.Foot);
	if (!World || FootIndex < 0 || FootIndex >= UE_ARRAY_COUNT(LastNotifyTimes))
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	const double PreviousTime = LastNotifyTimes[FootIndex];
	const double Threshold = GetSafeNonNegative(RapidNotifyThreshold);
	if (HasDebugCategory(EFootstepDebugCategory::Diagnostics)
		&& PreviousTime > -TNumericLimits<double>::Max()
		&& CurrentTime - PreviousTime < Threshold)
	{
		FOOTSTEPSYSTEM_LOG_WARNING(
			TEXT("Rapid duplicate footstep notify on '%s': foot=%s interval=%.4fs threshold=%.4fs. The request is not suppressed."),
			*GetNameSafe(GetOwner()),
			GetFootLabel(Request.Foot),
			CurrentTime - PreviousTime,
			Threshold);
	}
	LastNotifyTimes[FootIndex] = CurrentTime;
}

void UFootstepComponent::ReportMissingSocket(const FName SocketName)
{
	if (ReportedMissingSockets.Contains(SocketName))
	{
		return;
	}

	FOOTSTEPSYSTEM_LOG_WARNING(
		TEXT("Footstep component '%s' on '%s' cannot resolve configured socket '%s'."),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		SocketName.IsNone() ? TEXT("<None>") : *SocketName.ToString());
	ReportedMissingSockets.Add(SocketName);
}

bool UFootstepComponent::HasDebugCategory(const EFootstepDebugCategory Category) const
{
	return IsDebugEnabled()
		&& (DebugCategories & GetFootstepDebugCategoryMask(Category)) != 0;
}

void UFootstepComponent::DrawNotifyDebug(
	const FFootstepRequest& Request,
	const FVector& Location) const
{
#if ENABLE_DRAW_DEBUG
	if (HasDebugCategory(EFootstepDebugCategory::Notify) && GetWorld())
	{
		DrawDebugString(
			GetWorld(),
			Location + FVector(0.0f, 0.0f, 20.0f),
			FString::Printf(
				TEXT("Notify: %s intensity=%.2f"),
				GetFootLabel(Request.Foot),
				Request.NormalizedIntensity),
			nullptr,
			FColor::White,
			GetSafeNonNegative(DebugDrawDuration),
			false);
	}
#endif
}

void UFootstepComponent::DrawSocketDebug(
	const FName SocketName,
	const FVector& Location) const
{
#if ENABLE_DRAW_DEBUG
	if (HasDebugCategory(EFootstepDebugCategory::Socket) && GetWorld())
	{
		DrawDebugSphere(
			GetWorld(),
			Location,
			4.0f,
			8,
			FColor::Yellow,
			false,
			GetSafeNonNegative(DebugDrawDuration),
			0,
			1.5f);
		DrawDebugString(
			GetWorld(),
			Location + FVector(0.0f, 0.0f, 8.0f),
			SocketName.ToString(),
			nullptr,
			FColor::Yellow,
			GetSafeNonNegative(DebugDrawDuration),
			false);
	}
#endif
}

void UFootstepComponent::DrawTraceDebug(
	const FVector& TraceStart,
	const FVector& TraceEnd,
	const bool bHadValidHit) const
{
#if ENABLE_DRAW_DEBUG
	if (!HasDebugCategory(EFootstepDebugCategory::Trace) || !GetWorld())
	{
		return;
	}

	const FColor Color = bHadValidHit ? FColor::Green : FColor::Red;
	const float Duration = GetSafeNonNegative(DebugDrawDuration);
	DrawDebugLine(GetWorld(), TraceStart, TraceEnd, Color, false, Duration, 0, 1.5f);
	if (TraceShape == EFootstepTraceShape::Sphere)
	{
		const float Radius = FMath::Max(0.1f, SphereTraceRadius);
		DrawDebugSphere(GetWorld(), TraceStart, Radius, 12, Color, false, Duration);
		DrawDebugSphere(GetWorld(), TraceEnd, Radius, 12, Color, false, Duration);
	}
#endif
}

void UFootstepComponent::DrawResolvedDebug(
	const FFootstepEvent& Event,
	const FFootstepSurfaceResponse* Response,
	const bool bUsedFallback) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Duration = GetSafeNonNegative(DebugDrawDuration);
	if (HasDebugCategory(EFootstepDebugCategory::Hit))
	{
		const FColor HitColor =
			Event.bHadValidFloorHit ? FColor::Cyan : FColor::Red;
		DrawDebugPoint(
			World,
			Event.WorldLocation,
			12.0f,
			HitColor,
			false,
			Duration);
		if (Event.bHadValidFloorHit)
		{
			DrawDebugDirectionalArrow(
				World,
				Event.WorldLocation,
				Event.WorldLocation + Event.SurfaceNormal * 30.0f,
				8.0f,
				FColor::Cyan,
				false,
				Duration,
				0,
				1.5f);
		}
	}

	const bool bShowSurface =
		HasDebugCategory(EFootstepDebugCategory::Surface);
	const bool bShowResponse =
		HasDebugCategory(EFootstepDebugCategory::Response);
	if (!bShowSurface && !bShowResponse)
	{
		return;
	}

	FString Label = FString::Printf(
		TEXT("Foot=%s Intensity=%.2f Speed=%.1f"),
		GetFootLabel(Event.Foot),
		Event.NormalizedIntensity,
		Event.OwnerSpeed);
	if (bShowSurface)
	{
		Label += FString::Printf(
			TEXT("\nSurface=%s PhysMat=%s"),
			*GetSurfaceLabel(Event.SurfaceType),
			*GetNameSafe(Event.PhysicalMaterial));
	}
	if (bShowResponse)
	{
		Label += Response
			? FString::Printf(
				TEXT("\nResponse=%s Sound=%s Niagara=%s Decal=%s"),
				bUsedFallback ? TEXT("Fallback") : TEXT("Surface"),
				*GetNameSafe(Response->Sound),
				*GetNameSafe(Response->NiagaraSystem),
				*GetNameSafe(Response->DecalMaterial))
			: TEXT("\nResponse=None");
	}

	DrawDebugString(
		World,
		Event.WorldLocation + FVector(0.0f, 0.0f, 15.0f),
		Label,
		nullptr,
		Event.bHadValidFloorHit ? FColor::White : FColor::Red,
		Duration,
		false);
#endif
}

void UFootstepComponent::DrawFeedbackDebug(
	const EFootstepDebugCategory Category,
	const FVector& Location,
	const FColor& Color,
	const FString& Label) const
{
#if ENABLE_DRAW_DEBUG
	if (HasDebugCategory(Category) && GetWorld())
	{
		const float Duration = GetSafeNonNegative(DebugDrawDuration);
		DrawDebugPoint(GetWorld(), Location, 10.0f, Color, false, Duration);
		DrawDebugString(
			GetWorld(),
			Location + FVector(0.0f, 0.0f, 30.0f),
			Label,
			nullptr,
			Color,
			Duration,
			false);
	}
#endif
}

#if WITH_EDITOR
EDataValidationResult UFootstepComponent::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	const bool bInvalidTrace =
		!FMath::IsFinite(TraceStartOffset)
		|| !FMath::IsFinite(TraceLength)
		|| TraceLength <= 0.0f
		|| (TraceShape == EFootstepTraceShape::Sphere
			&& (!FMath::IsFinite(SphereTraceRadius) || SphereTraceRadius <= 0.0f));
	if (bInvalidTrace)
	{
		Context.AddError(LOCTEXT(
			"InvalidTraceConfiguration",
			"Footstep trace offsets, length, or sphere radius are invalid."));
		Result = EDataValidationResult::Invalid;
	}

	if (!FMath::IsFinite(DebugDrawDuration)
		|| DebugDrawDuration < 0.0f
		|| !FMath::IsFinite(RapidNotifyThreshold)
		|| RapidNotifyThreshold < 0.0f)
	{
		Context.AddError(LOCTEXT(
			"InvalidDebugConfiguration",
			"Footstep debug duration or rapid-notify threshold is invalid."));
		Result = EDataValidationResult::Invalid;
	}

	if (!FootstepProfile)
	{
		Context.AddWarning(LOCTEXT(
			"MissingProfile",
			"No Footstep Profile is assigned. Events work, but default feedback is disabled."));
	}
	if (LeftFootSocket.IsNone() && RightFootSocket.IsNone() && DefaultFootSocket.IsNone())
	{
		Context.AddWarning(LOCTEXT(
			"MissingSockets",
			"No foot sockets are configured. Every request must provide a socket override."));
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
