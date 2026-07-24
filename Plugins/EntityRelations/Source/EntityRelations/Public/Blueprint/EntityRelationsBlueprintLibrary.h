#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/EntityRelationTypes.h"
#include "EntityRelationsBlueprintLibrary.generated.h"

class AActor;
class UEntityIdentityComponent;

/** Stateless Blueprint entry points; authoritative evaluation remains in the World Subsystem. */
UCLASS()
class ENTITYRELATIONS_API UEntityRelationsBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	static UEntityIdentityComponent* GetEntityIdentityComponent(AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	static FEntityRelationId MakeEntityRelationId(FGuid Guid);

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	static FEntityRelationId GenerateEntityRelationId();

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Identity")
	static FGuid GetEntityRelationGuid(FEntityRelationId EntityId);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Query", meta = (WorldContext = "WorldContextObject"))
	static FEntityRelationResult EvaluateRelation(
		UObject* WorldContextObject,
		AActor* Source,
		AActor* Target,
		const FEntityRelationQueryContext& Context,
		bool& bSuccess);

	UFUNCTION(BlueprintCallable, Category = "Entity Relations|Query", meta = (WorldContext = "WorldContextObject"))
	static TArray<FEntityRelationResult> EvaluateRelationsFromSource(
		UObject* WorldContextObject,
		AActor* Source,
		const TArray<AActor*>& Targets,
		const FEntityRelationQueryContext& Context);

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Result")
	static bool HasClassificationTag(const FEntityRelationResult& Result, FGameplayTag Tag, bool bExactMatch = true);

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Result")
	static bool HasOutcomeTag(const FEntityRelationResult& Result, FGameplayTag Tag, bool bExactMatch = true);

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Debug")
	static FText QueryStatusToText(EEntityRelationQueryStatus Status);

	UFUNCTION(BlueprintPure, Category = "Entity Relations|Debug")
	static FText DecisionToText(EEntityRelationDecision Decision);
};
