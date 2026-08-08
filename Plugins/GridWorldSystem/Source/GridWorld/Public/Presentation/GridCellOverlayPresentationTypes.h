// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GridWorldTypes.h"
#include "Presentation/GridPresentationTypes.h"
#include "GridCellOverlayPresentationTypes.generated.h"

/** One persistent GridWorld cell and its generic non-authoritative overlay state. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridCellOverlayEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation|Overlay")
	FGridCellId CellId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation|Overlay")
	EGridCellOverlayVisualState State = EGridCellOverlayVisualState::None;

	bool operator==(const FGridCellOverlayEntry& Other) const
	{
		return CellId == Other.CellId && State == Other.State;
	}
};

/** Opaque world-local identity for one owner-scoped cell-overlay session. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridCellOverlayPresentationHandle
{
	GENERATED_BODY()

	bool IsSet() const { return SessionId.IsValid(); }
	bool operator==(const FGridCellOverlayPresentationHandle& Other) const { return SessionId == Other.SessionId; }
	bool operator!=(const FGridCellOverlayPresentationHandle& Other) const { return !(*this == Other); }

private:
	friend class UGridCellOverlayPresentationSubsystem;

	UPROPERTY(VisibleAnywhere, Category = "Grid World|Presentation|Overlay")
	FGuid SessionId;
};

/** Blueprint-safe request for one weak-owner-lifetime cell-overlay session. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridCellOverlayPresentationRequest
{
	GENERATED_BODY()

	/** Entries are copied into the session and never become navigation authority. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation|Overlay")
	TArray<FGridCellOverlayEntry> Entries;

	/** Required weak owner. Collection or explicit release removes the session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation|Overlay")
	TObjectPtr<UObject> Owner = nullptr;

	/** Larger values win deterministic overlap resolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation|Overlay")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid World|Presentation|Overlay")
	bool bVisible = true;
};

/** Read-only snapshot of one live cell-overlay session. */
USTRUCT(BlueprintType)
struct GRIDWORLD_API FGridCellOverlayPresentationSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Overlay")
	FGridCellOverlayPresentationHandle Handle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Overlay")
	TArray<FGridCellOverlayEntry> Entries;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Overlay")
	int32 Priority = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grid World|Presentation|Overlay")
	bool bVisible = false;
};
