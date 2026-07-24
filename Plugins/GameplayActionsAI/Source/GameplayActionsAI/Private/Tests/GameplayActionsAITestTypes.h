#pragma once

#include "CoreMinimal.h"
#include "Actions/GameplayActionInstance.h"
#include "BehaviorTree/BTTask_ExecuteGameplayAction.h"
#include "GameplayActionsAITestTypes.generated.h"

class UBehaviorTreeComponent;

UENUM()
enum class EGameplayActionsAITestEnum : uint8
{
	First,
	Second
};

USTRUCT()
struct FGameplayActionsAITestStruct
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Value = 0;
};

/** Minimal action used to validate bridge request construction and synchronous completion. */
UCLASS()
class UGameplayActionsAITestAction : public UGameplayActionInstance
{
	GENERATED_BODY()

public:
	static bool bCompleteSynchronously;
	void CompleteForTest(bool bSuccess);

protected:
	virtual void OnActionStarted_Implementation() override;
};

/** Exposes protected BT task entry points without changing the production node API. */
UCLASS()
class UGameplayActionsAITestBTTask : public UBTTask_ExecuteGameplayAction
{
	GENERATED_BODY()

public:
	EBTNodeResult::Type ExecuteForTest(UBehaviorTreeComponent& OwnerComp);
	EBTNodeResult::Type AbortForTest(UBehaviorTreeComponent& OwnerComp);
};
