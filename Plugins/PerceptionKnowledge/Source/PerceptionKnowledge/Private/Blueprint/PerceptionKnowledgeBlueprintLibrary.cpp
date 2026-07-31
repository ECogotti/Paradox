#include "Blueprint/PerceptionKnowledgeBlueprintLibrary.h"

FPerceptionKnowledgeValue UPerceptionKnowledgeBlueprintLibrary::MakePerceptionKnowledgeBool(const bool Value)
{
	return FPerceptionKnowledgeValue::MakeBool(Value);
}

FPerceptionKnowledgeValue UPerceptionKnowledgeBlueprintLibrary::MakePerceptionKnowledgeInteger(const int64 Value)
{
	return FPerceptionKnowledgeValue::MakeInteger(Value);
}

FPerceptionKnowledgeValue UPerceptionKnowledgeBlueprintLibrary::MakePerceptionKnowledgeFloat(const double Value)
{
	return FPerceptionKnowledgeValue::MakeFloat(Value);
}

FPerceptionKnowledgeValue UPerceptionKnowledgeBlueprintLibrary::MakePerceptionKnowledgeName(const FName Value)
{
	return FPerceptionKnowledgeValue::MakeName(Value);
}

FPerceptionKnowledgeValue UPerceptionKnowledgeBlueprintLibrary::MakePerceptionKnowledgeGameplayTag(const FGameplayTag Value)
{
	return FPerceptionKnowledgeValue::MakeGameplayTag(Value);
}

FPerceptionKnowledgeValue UPerceptionKnowledgeBlueprintLibrary::MakePerceptionKnowledgeEntityId(
	const FPerceptionKnowledgeEntityId Value)
{
	return FPerceptionKnowledgeValue::MakeEntityId(Value);
}

FPerceptionKnowledgeValue UPerceptionKnowledgeBlueprintLibrary::MakePerceptionKnowledgeVector(const FVector Value)
{
	return FPerceptionKnowledgeValue::MakeVector(Value);
}

FString UPerceptionKnowledgeBlueprintLibrary::PerceptionKnowledgeValueToString(
	const FPerceptionKnowledgeValue& Value)
{
	return Value.ToString();
}

FString UPerceptionKnowledgeBlueprintLibrary::PerceptionKnowledgeEntityIdToString(
	const FPerceptionKnowledgeEntityId EntityId)
{
	return EntityId.ToString();
}
