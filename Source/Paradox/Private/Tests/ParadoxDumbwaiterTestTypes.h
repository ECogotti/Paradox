#pragma once

#include "Inventory/ParadoxInsertablePickupableActor.h"
#include "Puzzles/ParadoxDumbwaiter.h"
#include "ParadoxDumbwaiterTestTypes.generated.h"

UCLASS()
class AParadoxDumbwaiterTestCargo : public AParadoxInsertablePickupableActor
{
	GENERATED_BODY()

public:
	void SetTraits(const FGameplayTagContainer& InTraits) { InsertableTraits = InTraits; }
	void ConfigureInsertedPresence(const bool bCollision, const bool bNavigation)
	{
		bUseAuthoredInsertedCollision = bCollision;
		bUseAuthoredInsertedNavigationInfluence = bNavigation;
	}
};

UCLASS()
class AParadoxDumbwaiterTestActor : public AParadoxDumbwaiter
{
	GENERATED_BODY()

public:
	bool CompleteTransferOutForTest(const FGuid OperationId)
	{
		return CompleteTransferOut(OperationId);
	}

	bool CompleteTransferInForTest(const FGuid OperationId)
	{
		return CompleteTransferIn(OperationId);
	}

	void SetAcceptedQuery(const FGameplayTagQuery& Query) { AcceptedCargoQuery = Query; }
	void SetLocked(const bool bLocked) { bLockStoredPickupable = bLocked; }
	void SetAdditionalAcceptance(const bool bAccepted) { bTestAdditionalAcceptance = bAccepted; }

protected:
	virtual bool CanAcceptCargoAdditional_Implementation(
		AParadoxInsertablePickupableActor* Cargo,
		AActor* Requester,
		FString& OutDiagnostic) const override
	{
		(void)Cargo;
		(void)Requester;
		OutDiagnostic = bTestAdditionalAcceptance
			? FString()
			: TEXT("Test Dumbwaiter rejected the cargo.");
		return bTestAdditionalAcceptance;
	}

private:
	bool bTestAdditionalAcceptance = true;
};

UCLASS()
class UParadoxDumbwaiterPairOccupancyTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandlePairOccupancyChanged(
		AParadoxDumbwaiter* Dumbwaiter,
		const bool bSelfOccupied,
		const bool bLinkedOccupied)
	{
		LastDumbwaiter = Dumbwaiter;
		bLastSelfOccupied = bSelfOccupied;
		bLastLinkedOccupied = bLinkedOccupied;
		++NotificationCount;
	}

	UPROPERTY()
	TObjectPtr<AParadoxDumbwaiter> LastDumbwaiter;

	int32 NotificationCount = 0;
	bool bLastSelfOccupied = false;
	bool bLastLinkedOccupied = false;
};
