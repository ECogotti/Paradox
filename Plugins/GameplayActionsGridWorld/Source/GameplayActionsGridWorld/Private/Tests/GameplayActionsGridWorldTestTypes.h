#pragma once

#include "CoreMinimal.h"
#include "Actions/GameplayActionInstance.h"
#include "GameplayActionsGridWorldTestTypes.generated.h"

/** Lock holder used to verify that Grid movement tasks are never created during queue residence. */
UCLASS()
class UGameplayActionsGridWorldTestLockAction : public UGameplayActionInstance
{
	GENERATED_BODY()

public:
	void CompleteForTest();
};
