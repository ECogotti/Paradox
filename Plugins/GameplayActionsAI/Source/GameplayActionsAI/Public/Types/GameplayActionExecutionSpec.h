#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "StructUtils/PropertyBag.h"
#include "Types/GameplayActionTypes.h"
#include "GameplayActionExecutionSpec.generated.h"

class UBlackboardComponent;
class UGameplayActionComponent;
class UGameplayActionDefinition;
class AAIController;

/**
 * Data-authored equivalent of the Blueprint Create Request -> configure -> Submit workflow.
 *
 * Parameters is a full, isolated value bag rather than a mutable schema. Editor tooling keeps its
 * layout synchronized with Definition and preserves values whose name and reflected type still
 * match. Runtime request construction rejects stale layouts instead of silently adding fields.
 */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONSAI_API FGameplayActionExecutionSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	TObjectPtr<UGameplayActionDefinition> Definition;

	UPROPERTY(EditAnywhere, Category = "Action", meta = (FixedLayout))
	FInstancedPropertyBag Parameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scheduling")
	bool bOverridePriority = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scheduling", meta = (EditCondition = "bOverridePriority"))
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scheduling")
	bool bOverrideBlockedPolicy = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scheduling", meta = (EditCondition = "bOverrideBlockedPolicy"))
	EGameplayActionBlockedPolicy BlockedPolicy = EGameplayActionBlockedPolicy::Queue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context")
	FGameplayTag OriginTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context")
	TObjectPtr<UObject> Requester;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context")
	FGameplayActionCorrelationData Correlation;

	/** Replaces Parameters with a deep copy of Definition defaults while migrating compatible values. */
	bool SynchronizeParameters();

	/** True only when the authored bag has the exact Definition schema expected by SubmitAction. */
	bool IsSchemaSynchronized() const;
};

/** One type-safe override from a Behavior Tree Blackboard key into an existing Definition field. */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONSAI_API FGameplayActionBlackboardParameterBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Action")
	FName ParameterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameplay Action")
	FBlackboardKeySelector BlackboardKey;
};

/**
 * Result of constructing a request from an execution spec and optional Blackboard overrides.
 *
 * Request is initialized only when bSucceeded is true. Callers must submit it through the owning
 * GameplayActionComponent; this bridge never bypasses the core factory or scheduler.
 */
USTRUCT(BlueprintType)
struct GAMEPLAYACTIONSAI_API FGameplayActionRequestBuildResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Action")
	bool bSucceeded = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Action")
	FGameplayActionRequest Request;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay Action")
	FString DiagnosticMessage;
};

namespace GameplayActionsAI
{
	/** Resolves exactly one component on explicit Actor, controlled Pawn, then AIController. */
	GAMEPLAYACTIONSAI_API UGameplayActionComponent* ResolveActionComponent(
		AActor* ExplicitActor,
		AAIController* AIController,
		FString& OutDiagnostic);

	/**
	 * Builds an isolated request through UGameplayActionBlueprintLibrary and applies authored values.
	 * Blackboard bindings are optional and may overwrite only existing fields with compatible types.
	 */
	GAMEPLAYACTIONSAI_API FGameplayActionRequestBuildResult BuildRequest(
		const FGameplayActionExecutionSpec& Spec,
		const UBlackboardComponent* Blackboard,
		const TArray<FGameplayActionBlackboardParameterBinding>& BlackboardBindings);

	/** Writes supported structured output types to an optional Blackboard Struct key. */
	GAMEPLAYACTIONSAI_API bool WriteBlackboardStruct(
		UBlackboardComponent* Blackboard,
		const FBlackboardKeySelector& Key,
		FConstStructView Value,
		FString& OutDiagnostic);
}
