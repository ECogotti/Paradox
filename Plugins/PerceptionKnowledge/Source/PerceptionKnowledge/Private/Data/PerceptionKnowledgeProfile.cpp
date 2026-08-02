#include "Data/PerceptionKnowledgeProfile.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

UPerceptionKnowledgeProfile::UPerceptionKnowledgeProfile()
{
	SightAffiliation.bDetectEnemies = true;
	SightAffiliation.bDetectFriendlies = true;
	SightAffiliation.bDetectNeutrals = true;
	HearingAffiliation.bDetectEnemies = true;
	HearingAffiliation.bDetectFriendlies = true;
	HearingAffiliation.bDetectNeutrals = true;
}

bool UPerceptionKnowledgeProfile::SetSightRanges(
	const float InSightRadius,
	const float InLoseSightRadius,
	FString& OutError)
{
	if (InSightRadius < 0.0f || InLoseSightRadius < InSightRadius)
	{
		OutError =
			TEXT("Sight Radius must be non-negative and Lose Sight Radius must be greater than or equal to Sight Radius.");
		return false;
	}

	const float PreviousSightRadius = SightRadius;
	const float PreviousLoseSightRadius = LoseSightRadius;
	SightRadius = InSightRadius;
	LoseSightRadius = InLoseSightRadius;
	if (!IsConfigurationValid(OutError))
	{
		SightRadius = PreviousSightRadius;
		LoseSightRadius = PreviousLoseSightRadius;
		return false;
	}

	ProfileChangedNative.Broadcast();
	return true;
}

bool UPerceptionKnowledgeProfile::SetHearingRange(
	const float InHearingRange,
	FString& OutError)
{
	if (InHearingRange < 0.0f)
	{
		OutError = TEXT("Hearing Range must be non-negative.");
		return false;
	}

	const float PreviousHearingRange = HearingRange;
	HearingRange = InHearingRange;
	if (!IsConfigurationValid(OutError))
	{
		HearingRange = PreviousHearingRange;
		return false;
	}

	ProfileChangedNative.Broadcast();
	return true;
}

bool UPerceptionKnowledgeProfile::NotifyRuntimeConfigurationChanged(
	FString& OutError)
{
	const bool bIsValid = IsConfigurationValid(OutError);
	ProfileChangedNative.Broadcast();
	return bIsValid;
}

bool UPerceptionKnowledgeProfile::IsConfigurationValid(FString& OutError) const
{
	if (!bEnableSight && !bEnableHearing)
	{
		OutError = TEXT("At least Sight or Hearing must be enabled.");
		return false;
	}
	if (bEnableSight && (SightRadius < 0.0f || LoseSightRadius < SightRadius
		|| PeripheralVisionHalfAngle < 0.0f || PeripheralVisionHalfAngle > 180.0f || SightMaxAge < 0.0f))
	{
		OutError = TEXT("Sight ranges, max age, or peripheral angle are inconsistent.");
		return false;
	}
	if (bEnableHearing && (HearingRange < 0.0f || HearingMaxAge < 0.1f))
	{
		OutError = TEXT("Hearing range or max age is invalid.");
		return false;
	}
	if (RecentEventLifetime < 0.1f || MaxRecentEvents < 1)
	{
		OutError = TEXT("Recent Event Memory requires a positive lifetime and capacity.");
		return false;
	}
	if (VisibleStateValidationInterval > 0.0f && VisibleStateValidationInterval < 0.1f)
	{
		OutError = TEXT("Visible-state validation interval must be zero or at least 0.1 seconds.");
		return false;
	}
	OutError.Reset();
	return true;
}

#if WITH_EDITOR
void UPerceptionKnowledgeProfile::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FString Error;
	NotifyRuntimeConfigurationChanged(Error);
}

void UPerceptionKnowledgeProfile::PostEditUndo()
{
	Super::PostEditUndo();

	FString Error;
	NotifyRuntimeConfigurationChanged(Error);
}
#endif
