#pragma once

#include "CoreMinimal.h"
#include "ParadoxInventoryTypes.generated.h"

/** Observable outcome of one authoritative single-slot inventory transition. */
UENUM(BlueprintType)
enum class EParadoxInventoryOperationStatus : uint8
{
	Succeeded,
	InvalidOwner,
	InvalidItem,
	SlotOccupied,
	SlotEmpty,
	ItemUnavailable,
	OwnershipConflict,
	InvalidPlacement,
	OperationInProgress,
	ResetInProgress
};

/** Structured result used by native actions and Blueprint-facing request adapters. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxInventoryOperationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory")
	EParadoxInventoryOperationStatus Status = EParadoxInventoryOperationStatus::InvalidOwner;

	/** Informative only; Status and Gameplay Tags remain authoritative. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory")
	FString DiagnosticMessage;

	bool IsSuccess() const
	{
		return Status == EParadoxInventoryOperationStatus::Succeeded;
	}
};

/** Authoritative gameplay state of one pickupable Actor. */
UENUM(BlueprintType)
enum class EParadoxPickupableState : uint8
{
	World,
	Held,
	Inserted,
	RestorePending
};
