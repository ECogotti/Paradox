#pragma once

#include "Inventory/ParadoxInsertablePickupableActor.h"
#include "Inventory/ParadoxItemSlotActor.h"
#include "Inventory/ParadoxPickupablePassiveEffect.h"
#include "Inventory/ParadoxPuzzleItemSlotActor.h"
#include "ParadoxItemSlotTestTypes.generated.h"

UCLASS()
class UParadoxItemSlotTestPassiveEffect : public UParadoxPickupablePassiveEffect
{
	GENERATED_BODY()

public:
	virtual void Apply_Implementation(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable) override;
	virtual void Remove_Implementation(
		AParadoxCharacter* Character,
		AParadoxPickupableActor* Pickupable) override;

	UPROPERTY()
	int32 ApplyCount = 0;
	UPROPERTY()
	int32 RemoveCount = 0;
	UPROPERTY()
	bool bAttemptReentrantInsert = false;
	UPROPERTY()
	TObjectPtr<AParadoxItemSlotActor> ReentrantSlot;
	UPROPERTY()
	EParadoxItemSlotOperationStatus ReentrantStatus =
		EParadoxItemSlotOperationStatus::Succeeded;
};

UCLASS()
class AParadoxItemSlotTestInsertable : public AParadoxInsertablePickupableActor
{
	GENERATED_BODY()

public:
	AParadoxItemSlotTestInsertable();
	void SetTraits(const FGameplayTagContainer& InTraits) { InsertableTraits = InTraits; }
	void SetInsertedPresence(const bool bUseCollision, const bool bUseNavigation)
	{
		bUseAuthoredInsertedCollision = bUseCollision;
		bUseAuthoredInsertedNavigationInfluence = bUseNavigation;
	}
	UParadoxItemSlotTestPassiveEffect* GetTestEffect() const { return TestEffect; }

private:
	UPROPERTY()
	TObjectPtr<UParadoxItemSlotTestPassiveEffect> TestEffect;
};

UCLASS()
class AParadoxItemSlotTestActor : public AParadoxItemSlotActor
{
	GENERATED_BODY()

public:
	void SetAcceptedQuery(const FGameplayTagQuery& Query) { AcceptedItemQuery = Query; }
	void SetLocked(const bool bLocked) { bLockInsertedItem = bLocked; }
	void SetRequiredActive(const bool bActive) { bTestRequiredActive = bActive; }
	void SetAdditionalActive(const bool bActive) { bTestAdditionalActive = bActive; }
	void SetAdditionalAcceptance(const bool bAccepted) { bTestAdditionalAcceptance = bAccepted; }
	FParadoxItemSlotOperationResult ReleaseForTest(const FTransform& Transform)
	{
		return ReleaseInsertedItemToWorld(Transform);
	}

protected:
	virtual bool EvaluateRequiredSlotActive() const override;
	virtual bool EvaluateAdditionalSlotActive_Implementation() const override;
	virtual bool CanAcceptItemAdditional_Implementation(
		AParadoxInsertablePickupableActor* Item,
		AParadoxCharacter* Requester,
		FString& OutDiagnostic) const override;

private:
	bool bTestRequiredActive = true;
	bool bTestAdditionalActive = true;
	bool bTestAdditionalAcceptance = true;
};

UCLASS()
class AParadoxPuzzleItemSlotTestActor : public AParadoxPuzzleItemSlotActor
{
	GENERATED_BODY()

public:
	void SetRequireReceiver(const bool bRequired) { bRequirePuzzleReceiverForActivation = bRequired; }
	void SetAdditionalActive(const bool bActive) { bTestAdditionalActive = bActive; }
	void SetPuzzleRole(const EParadoxPuzzleItemSlotRole InRole)
	{
		PuzzleRole = InRole;
		ConfigurePuzzleRole();
		NotifyPuzzleRelevantItemStateChanged();
	}
	void SetRightItemTags(const FGameplayTagContainer& InTags)
	{
		RightItemTags = InTags;
		NotifyPuzzleRelevantItemStateChanged();
	}

protected:
	virtual bool EvaluateAdditionalSlotActive_Implementation() const override;

private:
	bool bTestAdditionalActive = true;
};
