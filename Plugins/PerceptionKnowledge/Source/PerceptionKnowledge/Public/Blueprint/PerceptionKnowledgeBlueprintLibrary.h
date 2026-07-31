#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/PerceptionKnowledgeTypes.h"
#include "PerceptionKnowledgeBlueprintLibrary.generated.h"

/** Safe Blueprint factories and accessors for the closed semantic value type. */
UCLASS()
class PERCEPTIONKNOWLEDGE_API UPerceptionKnowledgeBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Value")
	static FPerceptionKnowledgeValue MakePerceptionKnowledgeBool(bool Value);

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Value")
	static FPerceptionKnowledgeValue MakePerceptionKnowledgeInteger(int64 Value);

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Value")
	static FPerceptionKnowledgeValue MakePerceptionKnowledgeFloat(double Value);

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Value")
	static FPerceptionKnowledgeValue MakePerceptionKnowledgeName(FName Value);

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Value")
	static FPerceptionKnowledgeValue MakePerceptionKnowledgeGameplayTag(FGameplayTag Value);

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Value")
	static FPerceptionKnowledgeValue MakePerceptionKnowledgeEntityId(FPerceptionKnowledgeEntityId Value);

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Value")
	static FPerceptionKnowledgeValue MakePerceptionKnowledgeVector(FVector Value);

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Value")
	static FString PerceptionKnowledgeValueToString(const FPerceptionKnowledgeValue& Value);

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Identity")
	static FString PerceptionKnowledgeEntityIdToString(FPerceptionKnowledgeEntityId EntityId);
};
