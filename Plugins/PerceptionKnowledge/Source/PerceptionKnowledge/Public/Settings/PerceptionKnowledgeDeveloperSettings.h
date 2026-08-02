#pragma once

#include "Engine/DeveloperSettings.h"
#include "PerceptionKnowledgeDeveloperSettings.generated.h"

/** Project-wide bounds for temporary correlation and low-frequency diagnostics. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Perception Knowledge"))
class PERCEPTIONKNOWLEDGE_API UPerceptionKnowledgeDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(config, EditAnywhere, Category = "Hearing", meta = (ClampMin = "0.1", Units = "Seconds"))
	float SemanticNoiseCorrelationLifetime = 10.0f;

	UPROPERTY(config, EditAnywhere, Category = "Hearing", meta = (ClampMin = "1"))
	int32 MaxPendingSemanticNoises = 256;

	UPROPERTY(config, EditAnywhere, Category = "Hearing", meta = (ClampMin = "0.1", Units = "Seconds"))
	float SemanticNoiseCleanupInterval = 1.0f;

	UPROPERTY(config, EditAnywhere, Category = "Debug", meta = (ClampMin = "0.05", Units = "Seconds"))
	float DebugDrawInterval = 0.1f;
};
