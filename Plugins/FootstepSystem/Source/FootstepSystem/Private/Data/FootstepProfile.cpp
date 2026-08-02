#include "Data/FootstepProfile.h"

#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "FootstepProfile"

namespace
{
	bool ValidateResponse(
		const FFootstepSurfaceResponse& Response,
		const FText& ResponseName,
		FDataValidationContext& Context)
	{
		bool bIsValid = true;

		const bool bInvalidAudioValues =
			!FMath::IsFinite(Response.VolumeMultiplier)
			|| !FMath::IsFinite(Response.PitchMin)
			|| !FMath::IsFinite(Response.PitchMax)
			|| Response.VolumeMultiplier < 0.0f
			|| Response.PitchMin <= 0.0f
			|| Response.PitchMax <= 0.0f
			|| Response.PitchMin > Response.PitchMax;
		if (bInvalidAudioValues)
		{
			Context.AddError(FText::Format(
				LOCTEXT("InvalidAudioValues", "{0} has invalid volume or pitch values."),
				ResponseName));
			bIsValid = false;
		}

		if (!FMath::IsFinite(Response.NiagaraScale) || Response.NiagaraScale < 0.0f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("InvalidNiagaraScale", "{0} has an invalid Niagara scale."),
				ResponseName));
			bIsValid = false;
		}

		if (Response.DecalSize.ContainsNaN()
			|| Response.DecalSize.GetMin() < 0.0f
			|| !FMath::IsFinite(Response.DecalLifeSpan)
			|| Response.DecalLifeSpan < 0.0f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("InvalidDecalValues", "{0} has an invalid decal size or lifetime."),
				ResponseName));
			bIsValid = false;
		}

		if (Response.bSpawnAudio && !Response.Sound)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("MissingSound", "{0} enables audio but has no sound."),
				ResponseName));
		}
		if (Response.bSpawnNiagara && !Response.NiagaraSystem)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("MissingNiagara", "{0} enables Niagara but has no system."),
				ResponseName));
		}
		if (Response.bSpawnDecal && !Response.DecalMaterial)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("MissingDecal", "{0} enables decals but has no material."),
				ResponseName));
		}

		return bIsValid;
	}
}

bool UFootstepProfile::ResolveResponse(
	const TEnumAsByte<EPhysicalSurface> SurfaceType,
	FFootstepSurfaceResponse& OutResponse,
	bool& bOutUsedFallback) const
{
	bOutUsedFallback = false;
	if (const FFootstepSurfaceResponse* SurfaceResponse = SurfaceResponses.Find(SurfaceType))
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

	OutResponse = FFootstepSurfaceResponse();
	return false;
}

#if WITH_EDITOR
EDataValidationResult UFootstepProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	for (const TPair<TEnumAsByte<EPhysicalSurface>, FFootstepSurfaceResponse>& Pair : SurfaceResponses)
	{
		const FText ResponseName = FText::Format(
			LOCTEXT("SurfaceResponseName", "Surface response {0}"),
			FText::AsNumber(static_cast<int32>(Pair.Key.GetValue())));
		if (!ValidateResponse(Pair.Value, ResponseName, Context))
		{
			Result = EDataValidationResult::Invalid;
		}
	}

	if (bUseDefaultResponse
		&& !ValidateResponse(DefaultResponse, LOCTEXT("DefaultResponseName", "Default response"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
