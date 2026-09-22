#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
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

/** Structured outcome of evaluating or executing the generic Use operation for one equipped item. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxPickupableUseResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Use")
	bool bSucceeded = false;

	/** Requested by the item effect; the Inventory remains the only authority that may commit it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Use")
	bool bConsumeItemOnSuccess = false;

	/** True only after the Inventory has removed the item from its authoritative slot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Use")
	bool bItemConsumed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Use")
	FGameplayTag ReasonTag;

	/** Informative only; bSucceeded and ReasonTag remain authoritative. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Use")
	FString DiagnosticMessage;

	bool IsSuccess() const { return bSucceeded; }
};

/** Authoritative gameplay state of one pickupable Actor. */
UENUM(BlueprintType)
enum class EParadoxPickupableState : uint8
{
	World,
	Held,
	Inserted,
	RestorePending,
	/** Hidden, unavailable run-local state restored by the ordinary World State lifecycle. */
	Consumed
};
