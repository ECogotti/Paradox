#include "Footsteps/ParadoxFootstepNoiseProfile.h"

#include "Misc/DataValidation.h"
#include "Paradox.h"

#define LOCTEXT_NAMESPACE "ParadoxFootstepNoiseProfile"

namespace
{
	bool ValidateNoiseResponse(
		const FParadoxFootstepNoiseResponse& Response,
		const FText& ResponseName,
		FDataValidationContext& Context)
	{
		bool bIsValid = true;
		if (!FMath::IsFinite(Response.BaseLoudness) || Response.BaseLoudness < 0.0f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("InvalidBaseLoudness", "{0} has an invalid Base Loudness."),
				ResponseName));
			bIsValid = false;
		}
		if (!FMath::IsFinite(Response.MaxRange) || Response.MaxRange < 0.0f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("InvalidMaxRange", "{0} has an invalid Max Range."),
				ResponseName));
			bIsValid = false;
		}
		if (Response.bEmitNoise && !Response.EventTag.IsValid())
		{
			Context.AddError(FText::Format(
				LOCTEXT("MissingEventTag", "{0} emits noise but has no valid Event Tag."),
				ResponseName));
			bIsValid = false;
		}
		return bIsValid;
	}
}

UParadoxFootstepNoiseProfile::UParadoxFootstepNoiseProfile()
{
	DefaultResponse.EventTag =
		ParadoxGameplayTags::Event_Noise_Character_Footstep;
	DefaultResponse.CauseTag =
		ParadoxGameplayTags::Cause_CharacterMovement_Footstep;
}

void UParadoxFootstepNoiseProfile::PostLoad()
{
	Super::PostLoad();

	// Native Gameplay Tags are not guaranteed to be registered while the
	// profile CDO is constructed. Repair legacy/new assets once registration
	// is complete so the project-specific defaults remain usable.
	if (!DefaultResponse.EventTag.IsValid())
	{
		DefaultResponse.EventTag =
			ParadoxGameplayTags::Event_Noise_Character_Footstep.GetTag();
	}
	if (!DefaultResponse.CauseTag.IsValid())
	{
		DefaultResponse.CauseTag =
			ParadoxGameplayTags::Cause_CharacterMovement_Footstep.GetTag();
	}
}

bool UParadoxFootstepNoiseProfile::ResolveResponse(
	const TEnumAsByte<EPhysicalSurface> SurfaceType,
	FParadoxFootstepNoiseResponse& OutResponse,
	bool& bOutUsedFallback) const
{
	bOutUsedFallback = false;
	if (const FParadoxFootstepNoiseResponse* SurfaceResponse =
		SurfaceResponses.Find(SurfaceType))
	{
		OutResponse = *SurfaceResponse;
		return true;
	}

	if (bUseDefaultResponse)
	{
		OutResponse = DefaultResponse;
		bOutUsedFallback = true;
		return true;
	}

	OutResponse = FParadoxFootstepNoiseResponse();
	return false;
}

#if WITH_EDITOR
EDataValidationResult UParadoxFootstepNoiseProfile::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	for (const TPair<
		TEnumAsByte<EPhysicalSurface>,
		FParadoxFootstepNoiseResponse>& Pair : SurfaceResponses)
	{
		const FText ResponseName = FText::Format(
			LOCTEXT("SurfaceResponseName", "Surface response {0}"),
			FText::AsNumber(static_cast<int32>(Pair.Key.GetValue())));
		if (!ValidateNoiseResponse(Pair.Value, ResponseName, Context))
		{
			Result = EDataValidationResult::Invalid;
		}
	}

	if (bUseDefaultResponse
		&& !ValidateNoiseResponse(
			DefaultResponse,
			LOCTEXT("DefaultResponseName", "Default response"),
			Context))
	{
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
