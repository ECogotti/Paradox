#pragma once

#include "Interaction/ParadoxInteractionActionBase.h"
#include "NativeGameplayTags.h"
#include "SmartObjectDefinition.h"
#include "ParadoxInteractionTestTypes.generated.h"

namespace ParadoxInteractionTestTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Primary);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Secondary);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Rejected);
}

/** Test-only inert definition used solely to satisfy the engine Smart Object asset invariant. */
UCLASS()
class UParadoxInteractionTestBehaviorDefinition final
	: public USmartObjectBehaviorDefinition
{
	GENERATED_BODY()
};

/** Test action that retains its claim until the test drives a terminal lifecycle path. */
UCLASS()
class UParadoxInteractionTestHoldAction final : public UParadoxInteractionActionBase
{
	GENERATED_BODY()

public:
	void CompleteSuccessForTest();
	void CompleteFailureForTest();

protected:
	virtual void ExecuteInteraction_Implementation() override;
};

/** Immediate test action used to verify semantic recording and fresh replay execution. */
UCLASS()
class UParadoxInteractionTestSuccessAction final : public UParadoxInteractionActionBase
{
	GENERATED_BODY()

public:
	static void ResetObservations();
	static int32 ExecutionCount;
	static int32 ClaimedExecutionCount;

protected:
	virtual void ExecuteInteraction_Implementation() override;
};

/** Concrete class that deliberately inherits the base NotImplemented execution fallback. */
UCLASS()
class UParadoxInteractionTestDefaultAction final : public UParadoxInteractionActionBase
{
	GENERATED_BODY()
};
