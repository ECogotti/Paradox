#pragma once

#include "CoreMinimal.h"
#include "ParadoxItemSlotTypes.generated.h"

/** Observable outcome of one authoritative Item Slot transition or validation. */
UENUM(BlueprintType)
enum class EParadoxItemSlotOperationStatus : uint8
{
	Succeeded,
	InvalidRequester,
	MissingInventory,
	InvalidItem,
	NotInsertable,
	SlotInactive,
	SlotOccupied,
	SlotEmpty,
	ItemLocked,
	InventoryOccupied,
	RequesterDoesNotOwnItem,
	IncompatibleTraits,
	AdditionalValidationFailed,
	OwnershipConflict,
	InvalidPlacement,
	OperationInProgress,
	ResetInProgress
};

/** Structured diagnostic returned without exposing mutable slot ownership. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxItemSlotOperationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot")
	EParadoxItemSlotOperationStatus Status = EParadoxItemSlotOperationStatus::InvalidRequester;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Item Slot")
	FString DiagnosticMessage;

	bool IsSuccess() const
	{
		return Status == EParadoxItemSlotOperationStatus::Succeeded;
	}
};
