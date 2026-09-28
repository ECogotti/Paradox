#pragma once

#include "Puzzles/ParadoxTeleportGate.h"
#include "SmartObjectDefinition.h"
#include "ParadoxTeleportGateTestTypes.generated.h"

/** Test-only inert behavior used to make runtime Smart Object definitions valid. */
UCLASS()
class UParadoxTeleportGateTestBehaviorDefinition final
	: public USmartObjectBehaviorDefinition
{
	GENERATED_BODY()
};

/** Concrete Gate exposing deterministic phase completion to automation only. */
UCLASS()
class AParadoxTeleportGateTestActor final : public AParadoxTeleportGate
{
	GENERATED_BODY()

public:
	bool CompleteTransferOutForTest(const FGuid& OperationId)
	{
		return CompleteTransferOut(OperationId);
	}

	bool CompleteTransferInForTest(const FGuid& OperationId)
	{
		return CompleteTransferIn(OperationId);
	}
};
