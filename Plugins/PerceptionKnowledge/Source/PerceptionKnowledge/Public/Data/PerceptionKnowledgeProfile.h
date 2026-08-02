#pragma once

#include "Engine/DataAsset.h"
#include "Perception/AIPerceptionTypes.h"
#include "PerceptionKnowledgeProfile.generated.h"

#if WITH_EDITOR
struct FPropertyChangedEvent;
#endif

UENUM(BlueprintType)
enum class EPerceptionKnowledgeRepeatedObservationPolicy : uint8
{
	Always,
	AcquisitionsAndChanges,
	ChangesOnly
};

DECLARE_MULTICAST_DELEGATE(FPerceptionKnowledgeProfileChangedNativeEvent);

/** Shared listener configuration applied to native Sight and Hearing sense configs. */
UCLASS(BlueprintType)
class PERCEPTIONKNOWLEDGE_API UPerceptionKnowledgeProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPerceptionKnowledgeProfile();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Sight")
	bool bEnableSight = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Sight", meta = (ClampMin = "0.0", Units = "Centimeters", EditCondition = "bEnableSight"))
	float SightRadius = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Sight", meta = (ClampMin = "0.0", Units = "Centimeters", EditCondition = "bEnableSight"))
	float LoseSightRadius = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Sight", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "Degrees", EditCondition = "bEnableSight"))
	float PeripheralVisionHalfAngle = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Sight", meta = (ClampMin = "0.0", Units = "Seconds", EditCondition = "bEnableSight"))
	float SightMaxAge = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Sight", meta = (EditCondition = "bEnableSight"))
	FAISenseAffiliationFilter SightAffiliation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Hearing")
	bool bEnableHearing = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Hearing", meta = (ClampMin = "0.0", Units = "Centimeters", EditCondition = "bEnableHearing"))
	float HearingRange = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Hearing", meta = (ClampMin = "0.1", Units = "Seconds", EditCondition = "bEnableHearing"))
	float HearingMaxAge = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Hearing", meta = (EditCondition = "bEnableHearing"))
	FAISenseAffiliationFilter HearingAffiliation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Memory", meta = (ClampMin = "0.1", Units = "Seconds"))
	float RecentEventLifetime = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Memory", meta = (ClampMin = "1"))
	int32 MaxRecentEvents = 128;

	/** Zero disables fallback rescans. Positive values must be at least 0.1 seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Memory", meta = (ClampMin = "0.0", Units = "Seconds"))
	float VisibleStateValidationInterval = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Memory")
	EPerceptionKnowledgeRepeatedObservationPolicy RepeatedObservationPolicy =
		EPerceptionKnowledgeRepeatedObservationPolicy::AcquisitionsAndChanges;

	/**
	 * Atomically changes the two coupled Sight ranges and refreshes every listener using this
	 * Profile. The new configuration is also applied to native AI Perception at runtime.
	 */
	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Runtime Configuration")
	bool SetSightRanges(
		float InSightRadius,
		float InLoseSightRadius,
		FString& OutError);

	/** Changes Hearing Range and refreshes every listener and Hearing renderer using this Profile. */
	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Runtime Configuration")
	bool SetHearingRange(float InHearingRange, FString& OutError);

	/**
	 * Validates and publishes changes made directly by C++ to this Data Asset.
	 *
	 * Blueprint callers should prefer the validated setters. Editor property changes publish
	 * automatically. Invalid configurations are still published so active listeners suspend
	 * instead of silently retaining stale native sense settings.
	 */
	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Runtime Configuration")
	bool NotifyRuntimeConfigurationChanged(FString& OutError);

	bool IsConfigurationValid(FString& OutError) const;

	FPerceptionKnowledgeProfileChangedNativeEvent& OnProfileChangedNative()
	{
		return ProfileChangedNative;
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

private:
	FPerceptionKnowledgeProfileChangedNativeEvent ProfileChangedNative;
};
