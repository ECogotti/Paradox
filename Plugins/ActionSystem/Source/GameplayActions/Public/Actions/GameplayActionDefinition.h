#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/GameplayActionTypes.h"
#include "GameplayActionDefinition.generated.h"

class UGameplayActionInstance;

/**
 * Shared authoring data for one kind of gameplay action.
 *
 * A Definition never owns execution state. The component copies every scheduling field and the
 * Property Bag into a transient instance at acceptance time, so editing an asset cannot change an
 * action that is already queued or running.
 */
UCLASS(BlueprintType)
class GAMEPLAYACTIONS_API UGameplayActionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UGameplayActionDefinition();

	/** Concrete transient UObject created once for every accepted request. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Action")
	TSubclassOf<UGameplayActionInstance> InstanceClass;

	/** Required semantic identity used by gameplay, diagnostics, journal consumers, and replay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Action")
	FGameplayTag ActionTag;

	/** Scheduler priority used unless the request supplies an explicit override. Higher values win. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scheduling")
	int32 DefaultPriority = 0;

	/**
	 * Authoritative parameter schema and default values.
	 *
	 * The request factory deep-copies this bag. Runtime setters may change existing values but are
	 * never allowed to add fields or alter the schema after authoring.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Parameters")
	FInstancedPropertyBag DefaultParameters;

	/** Exact-match resources acquired atomically before Action Start and held until terminal cleanup. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scheduling", meta = (Categories = "GameplayAction.Lock"))
	FGameplayTagContainer ExecutionLocks;

	/** Whether a strictly higher-priority incoming action may interrupt this instance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scheduling")
	bool bInterruptible = true;

	/** Behavior when the complete lock set cannot be acquired and preemption is unavailable. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scheduling")
	EGameplayActionBlockedPolicy BlockedPolicy = EGameplayActionBlockedPolicy::Queue;

	/** Copied into runtime snapshots; automatic execution-time enforcement is not currently applied. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scheduling")
	FGameplayActionTimeout OptionalTimeout;

	/**
	 * Maximum gameplay-scaled time an accepted action may remain in the queue.
	 *
	 * The counter advances only while the runtime instance is actually Queued. It freezes during
	 * world pause and Pause Actions. A value of zero keeps the action queued indefinitely.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Scheduling", meta = (ClampMin = "0.0", Units = "s"))
	double MaxQueueTimeSeconds = 0.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Journaling")
	EGameplayActionJournalRequirement JournalRequirement = EGameplayActionJournalRequirement::Disabled;

	/** Non-authoritative description shown only in tooling and diagnostics. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Debug")
	FString DebugDescription;

	/** Optional authoring color for external debug UIs; the runtime plugin performs no spatial drawing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Debug")
	FLinearColor DebugColor = FLinearColor::White;

	/** Read-only native access used by factories and validation; callers must never mutate the asset bag. */
	const FInstancedPropertyBag& GetDefaultParameters() const { return DefaultParameters; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
