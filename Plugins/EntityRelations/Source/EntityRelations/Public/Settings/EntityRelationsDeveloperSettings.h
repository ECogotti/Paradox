#pragma once

#include "Engine/DeveloperSettings.h"
#include "EntityRelationsDeveloperSettings.generated.h"

class UEntityRelationPolicySet;

/** Project-wide defaults for per-world relation evaluation and diagnostics. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Entity Relations"))
class ENTITYRELATIONS_API UEntityRelationsDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEntityRelationsDeveloperSettings();
	virtual FName GetCategoryName() const override;

	UPROPERTY(Config, EditAnywhere, Category = "Policy", meta = (AllowedClasses = "/Script/EntityRelations.EntityRelationPolicySet"))
	TSoftObjectPtr<UEntityRelationPolicySet> DefaultPolicySet;

	UPROPERTY(Config, EditAnywhere, Category = "Cache")
	bool bEnableQueryCache = true;

	UPROPERTY(Config, EditAnywhere, Category = "Cache", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxCacheEntries = 1024;

	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bEnableGlobalDebug = false;

	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bEnableVerboseExplanation = false;

	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bEnableDebugDraw = false;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DebugDrawDuration = 2.0f;
};
