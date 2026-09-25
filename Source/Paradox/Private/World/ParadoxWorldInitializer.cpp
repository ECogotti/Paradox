#include "World/ParadoxWorldInitializer.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ParadoxWorldInitializer"

AParadoxWorldInitializer::AParadoxWorldInitializer()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);
	bReplicates = false;
}

#if WITH_EDITOR
EDataValidationResult AParadoxWorldInitializer::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	if (OxygenConfiguration.Mode == EParadoxOxygenMode::SharedGlobal
		&& (!FMath::IsFinite(OxygenConfiguration.SharedDurationSeconds)
			|| OxygenConfiguration.SharedDurationSeconds <= 0.0f
			|| !FMath::IsFinite(OxygenConfiguration.SharedBaseConsumptionSpeed)
			|| OxygenConfiguration.SharedBaseConsumptionSpeed <= 0.0f))
	{
		Context.AddError(LOCTEXT(
			"InvalidSharedOxygenConfiguration",
			"Shared Oxygen duration and base consumption speed must be finite and greater than zero."));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
