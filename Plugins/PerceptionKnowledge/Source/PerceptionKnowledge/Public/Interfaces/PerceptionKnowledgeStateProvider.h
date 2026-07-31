#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Types/PerceptionKnowledgeTypes.h"
#include "PerceptionKnowledgeStateProvider.generated.h"

class UPerceptionKnowledgeListenerComponent;

UINTERFACE(BlueprintType)
class PERCEPTIONKNOWLEDGE_API UPerceptionKnowledgeStateProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Optional semantic-state provider implemented by an Actor or one of its Components.
 * Providers return value copies and must explicitly report Unknown/Invalidated states.
 */
class PERCEPTIONKNOWLEDGE_API IPerceptionKnowledgeStateProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Perception Knowledge|Provider")
	void GatherObservableStates(
		UPerceptionKnowledgeListenerComponent* Observer,
		FGameplayTag SenseTag,
		TArray<FPerceptionKnowledgeExposedState>& OutStates) const;
};
