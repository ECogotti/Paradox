#include "Footsteps/ParadoxFootstepNoiseComponent.h"

#include "Components/FootstepComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "DrawDebugHelpers.h"
#include "Footsteps/ParadoxFootstepNoiseProfile.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Paradox.h"
#include "Types/PerceptionKnowledgeTypes.h"

UParadoxFootstepNoiseComponent::UParadoxFootstepNoiseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UParadoxFootstepNoiseComponent::BeginPlay()
{
	Super::BeginPlay();
	bAcceptingEvents = true;
	ResolveDependencies();
	BindToFootstepComponent();
}

void UParadoxFootstepNoiseComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	bAcceptingEvents = false;
	UnbindFromFootstepComponent();
	PerceptionSource = nullptr;
	Super::EndPlay(EndPlayReason);
}

bool UParadoxFootstepNoiseComponent::IsDebugEnabled() const
{
	return bEnableDebug && IsParadoxFootstepDebugEnabled();
}

void UParadoxFootstepNoiseComponent::ResolveDependencies()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		BoundFootstepComponent = nullptr;
		PerceptionSource = nullptr;
		return;
	}

	BoundFootstepComponent = Cast<UFootstepComponent>(
		FootstepComponentOverride.GetComponent(Owner));
	if (!BoundFootstepComponent)
	{
		BoundFootstepComponent =
			Owner->FindComponentByClass<UFootstepComponent>();
	}

	PerceptionSource = Cast<UPerceptionKnowledgeSourceComponent>(
		PerceptionSourceOverride.GetComponent(Owner));
	if (!PerceptionSource)
	{
		PerceptionSource =
			Owner->FindComponentByClass<UPerceptionKnowledgeSourceComponent>();
	}
}

void UParadoxFootstepNoiseComponent::BindToFootstepComponent()
{
	if (!BoundFootstepComponent)
	{
		ReportResult(
			EParadoxFootstepNoiseResult::InvalidEvent,
			SurfaceType_Default,
			FString::Printf(
				TEXT("Footstep adapter '%s' found no UFootstepComponent on owner '%s'."),
				*GetNameSafe(this),
				*GetNameSafe(GetOwner())));
		return;
	}

	BoundFootstepComponent->OnFootstepGeneratedNative().RemoveAll(this);
	BoundFootstepComponent->OnFootstepGeneratedNative().AddUObject(
		this,
		&UParadoxFootstepNoiseComponent::HandleFootstepGenerated);
}

void UParadoxFootstepNoiseComponent::UnbindFromFootstepComponent()
{
	if (BoundFootstepComponent)
	{
		BoundFootstepComponent->OnFootstepGeneratedNative().RemoveAll(this);
	}
	BoundFootstepComponent = nullptr;
}

void UParadoxFootstepNoiseComponent::HandleFootstepGenerated(
	const FFootstepEvent& Event)
{
	if (!bAcceptingEvents)
	{
		return;
	}

	FParadoxFootstepNoiseResponse Response;
	bool bOwnerCrouched = false;
	float EffectiveLoudness = 0.0f;
	FString Diagnostic;
	const EParadoxFootstepNoiseResult Result = ProcessFootstepEvent(
		Event,
		Response,
		bOwnerCrouched,
		EffectiveLoudness,
		Diagnostic);
	const bool bHasResolvedResponse =
		Result != EParadoxFootstepNoiseResult::MissingNoiseProfile
		&& Result != EParadoxFootstepNoiseResult::MissingSurfaceResponse
		&& Result != EParadoxFootstepNoiseResult::InvalidEvent
		&& Result != EParadoxFootstepNoiseResult::InvalidOwner;
	CompleteProcessing(
		Event,
		Result,
		bHasResolvedResponse ? &Response : nullptr,
		bOwnerCrouched,
		EffectiveLoudness,
		Diagnostic);
}

EParadoxFootstepNoiseResult
UParadoxFootstepNoiseComponent::ProcessFootstepEvent(
	const FFootstepEvent& Event,
	FParadoxFootstepNoiseResponse& OutResponse,
	bool& bOutOwnerCrouched,
	float& OutEffectiveLoudness,
	FString& OutDiagnostic)
{
	bOutOwnerCrouched = false;
	OutEffectiveLoudness = 0.0f;
	OutResponse = FParadoxFootstepNoiseResponse();

	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		OutDiagnostic = TEXT("The footstep noise adapter has no valid owning Actor.");
		return EParadoxFootstepNoiseResult::InvalidOwner;
	}
	if (!Event.bHadValidFloorHit
		|| Event.InstigatorActor != Owner
		|| Event.WorldLocation.ContainsNaN()
		|| !FMath::IsFinite(Event.NormalizedIntensity))
	{
		OutDiagnostic = FString::Printf(
			TEXT("Rejected an invalid footstep event for owner '%s'."),
			*GetNameSafe(Owner));
		return EParadoxFootstepNoiseResult::InvalidEvent;
	}
	if (!NoiseProfile)
	{
		OutDiagnostic = FString::Printf(
			TEXT("Owner '%s' has no Paradox Footstep Noise Profile."),
			*GetNameSafe(Owner));
		return EParadoxFootstepNoiseResult::MissingNoiseProfile;
	}

	bool bUsedFallback = false;
	if (!NoiseProfile->ResolveResponse(
		Event.SurfaceType,
		OutResponse,
		bUsedFallback))
	{
		OutDiagnostic = FString::Printf(
			TEXT("Noise profile '%s' has no response for Physical Surface %d and no fallback."),
			*GetNameSafe(NoiseProfile),
			static_cast<int32>(Event.SurfaceType.GetValue()));
		return EParadoxFootstepNoiseResult::MissingSurfaceResponse;
	}
	if (!OutResponse.bEmitNoise)
	{
		OutDiagnostic = FString::Printf(
			TEXT("Physical Surface %d disables semantic footstep noise."),
			static_cast<int32>(Event.SurfaceType.GetValue()));
		return EParadoxFootstepNoiseResult::DisabledBySurface;
	}

	const ACharacter* CharacterOwner = Cast<ACharacter>(Owner);
	bOutOwnerCrouched =
		CharacterOwner && CharacterOwner->IsCrouched();
	if (bIgnoreNoiseDuringCrouch && bOutOwnerCrouched)
	{
		OutDiagnostic = FString::Printf(
			TEXT("Semantic footstep noise for '%s' was suppressed by crouch."),
			*GetNameSafe(Owner));
		return EParadoxFootstepNoiseResult::SuppressedByCrouch;
	}

	if (!IsValid(PerceptionSource))
	{
		ResolveDependencies();
	}
	if (!IsValid(PerceptionSource))
	{
		OutDiagnostic = FString::Printf(
			TEXT("Owner '%s' has no PerceptionKnowledge Source component."),
			*GetNameSafe(Owner));
		return EParadoxFootstepNoiseResult::EmissionFailed;
	}
	if (!OutResponse.EventTag.IsValid()
		|| !FMath::IsFinite(OutResponse.BaseLoudness)
		|| OutResponse.BaseLoudness < 0.0f
		|| !FMath::IsFinite(OutResponse.MaxRange)
		|| OutResponse.MaxRange < 0.0f)
	{
		OutDiagnostic = FString::Printf(
			TEXT("Noise response for Physical Surface %d is invalid."),
			static_cast<int32>(Event.SurfaceType.GetValue()));
		return EParadoxFootstepNoiseResult::EmissionFailed;
	}

	const float Intensity =
		FMath::Clamp(Event.NormalizedIntensity, 0.0f, 1.0f);
	OutEffectiveLoudness = OutResponse.BaseLoudness * Intensity;

	FPerceptionKnowledgeNoiseRequest Request;
	Request.EventTag = OutResponse.EventTag;
	Request.CauseTag = OutResponse.CauseTag;
	Request.Instigator = Owner;
	Request.WorldLocation = Event.WorldLocation;
	Request.bUseSourceLocation = false;
	Request.Loudness = OutEffectiveLoudness;
	Request.MaxRange = OutResponse.MaxRange;
	Request.Strength = OutEffectiveLoudness;

	const FPerceptionKnowledgeOperationResult EmissionResult =
		EmitNoise(*PerceptionSource, Request);
	if (!EmissionResult.IsSuccess())
	{
		OutDiagnostic = FString::Printf(
			TEXT("PerceptionKnowledge rejected footstep noise for '%s': %s"),
			*GetNameSafe(Owner),
			*EmissionResult.Message);
		return EParadoxFootstepNoiseResult::EmissionFailed;
	}

	OutDiagnostic = FString::Printf(
		TEXT("Emitted semantic footstep noise for '%s' (surface=%d fallback=%s)."),
		*GetNameSafe(Owner),
		static_cast<int32>(Event.SurfaceType.GetValue()),
		bUsedFallback ? TEXT("yes") : TEXT("no"));
	return EParadoxFootstepNoiseResult::Emitted;
}

FPerceptionKnowledgeOperationResult UParadoxFootstepNoiseComponent::EmitNoise(
	UPerceptionKnowledgeSourceComponent& Source,
	const FPerceptionKnowledgeNoiseRequest& Request)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestNoiseEmitter)
	{
		return TestNoiseEmitter(Source, Request);
	}
#endif
	return Source.EmitSemanticNoise(Request);
}

void UParadoxFootstepNoiseComponent::CompleteProcessing(
	const FFootstepEvent& Event,
	const EParadoxFootstepNoiseResult Result,
	const FParadoxFootstepNoiseResponse* Response,
	const bool bOwnerCrouched,
	const float EffectiveLoudness,
	const FString& Diagnostic)
{
	bHasProcessedFootstep = true;
	LastResult = Result;
	LastDiagnosticMessage = Diagnostic;
	ReportResult(Result, Event.SurfaceType, Diagnostic);
	DrawProcessingDebug(
		Event,
		Result,
		Response,
		bOwnerCrouched,
		EffectiveLoudness);
}

void UParadoxFootstepNoiseComponent::ReportResult(
	const EParadoxFootstepNoiseResult Result,
	const TEnumAsByte<EPhysicalSurface> SurfaceType,
	const FString& Diagnostic)
{
	const bool bIsFailure =
		Result == EParadoxFootstepNoiseResult::MissingNoiseProfile
		|| Result == EParadoxFootstepNoiseResult::MissingSurfaceResponse
		|| Result == EParadoxFootstepNoiseResult::InvalidEvent
		|| Result == EParadoxFootstepNoiseResult::InvalidOwner
		|| Result == EParadoxFootstepNoiseResult::EmissionFailed;
	if (bIsFailure)
	{
		if (Result == EParadoxFootstepNoiseResult::MissingSurfaceResponse)
		{
			if (ReportedMissingSurfaces.Contains(SurfaceType))
			{
				return;
			}
			ReportedMissingSurfaces.Add(SurfaceType);
		}
		else if (ReportedResults.Contains(Result))
		{
			return;
		}
		ReportedResults.Add(Result);
		PARADOX_LOG_WARNING(
			TEXT("Footstep perception adapter '%s': %s"),
			*GetNameSafe(this),
			*Diagnostic);
		return;
	}

	if (IsDebugEnabled())
	{
		PARADOX_LOG_INFO(
			TEXT("Footstep perception adapter '%s': %s"),
			*GetNameSafe(this),
			*Diagnostic);
	}
}

void UParadoxFootstepNoiseComponent::DrawProcessingDebug(
	const FFootstepEvent& Event,
	const EParadoxFootstepNoiseResult Result,
	const FParadoxFootstepNoiseResponse* Response,
	const bool bOwnerCrouched,
	const float EffectiveLoudness) const
{
#if ENABLE_DRAW_DEBUG
	if (!IsDebugEnabled() || !GetWorld())
	{
		return;
	}

	FColor Color = FColor::Red;
	switch (Result)
	{
	case EParadoxFootstepNoiseResult::Emitted:
		Color = FColor::Cyan;
		break;
	case EParadoxFootstepNoiseResult::SuppressedByCrouch:
		Color = FColor(120, 120, 120);
		break;
	case EParadoxFootstepNoiseResult::DisabledBySurface:
		Color = FColor::Yellow;
		break;
	default:
		break;
	}

	DrawDebugPoint(
		GetWorld(),
		Event.WorldLocation,
		14.0f,
		Color,
		false,
		DebugDrawDuration);
	if (const AActor* Owner = GetOwner())
	{
		DrawDebugLine(
			GetWorld(),
			Event.WorldLocation,
			Owner->GetActorLocation(),
			Color,
			false,
			DebugDrawDuration,
			0,
			1.5f);
	}
	if (Response && Response->MaxRange > 0.0f)
	{
		DrawDebugSphere(
			GetWorld(),
			Event.WorldLocation,
			Response->MaxRange,
			32,
			Color,
			false,
			DebugDrawDuration,
			0,
			1.0f);
	}

	const FString RangeText =
		Response && Response->MaxRange > 0.0f
			? FString::Printf(TEXT("%.0fcm"), Response->MaxRange)
			: TEXT("listener-controlled");
	const FString Label = FString::Printf(
		TEXT("Surface=%d Tag=%s Loudness=%.2f Range=%s Crouched=%s IgnoreCrouch=%s Result=%s"),
		static_cast<int32>(Event.SurfaceType.GetValue()),
		Response ? *Response->EventTag.ToString() : TEXT("None"),
		EffectiveLoudness,
		*RangeText,
		bOwnerCrouched ? TEXT("true") : TEXT("false"),
		bIgnoreNoiseDuringCrouch ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result));
	DrawDebugString(
		GetWorld(),
		Event.WorldLocation + FVector(0.0f, 0.0f, 35.0f),
		Label,
		nullptr,
		Color,
		DebugDrawDuration,
		false,
		1.0f);
#endif
}
