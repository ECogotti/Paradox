// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Presentation/GridCellOverlayPresentationTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GridCellOverlayPresentationSubsystem.generated.h"

class UGridRuntimeVisualizationSubsystem;
class UGridWorldSubsystem;

/** Owns generic, local, non-authoritative cell-overlay sessions for one render-capable World. */
UCLASS()
class GRIDWORLD_API UGridCellOverlayPresentationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Overlay")
	bool CreateCellOverlayPresentation(
		const FGridCellOverlayPresentationRequest& Request,
		FGridCellOverlayPresentationHandle& OutHandle);

	/** Atomically replaces every entry retained by the session. Empty is valid and clears it. */
	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Overlay")
	bool UpdateCellOverlayPresentation(
		const FGridCellOverlayPresentationHandle& Handle,
		const TArray<FGridCellOverlayEntry>& Entries);

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Overlay")
	bool SetCellOverlayPresentationVisible(
		const FGridCellOverlayPresentationHandle& Handle,
		bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Overlay")
	bool SetCellOverlayPresentationPriority(
		const FGridCellOverlayPresentationHandle& Handle,
		int32 Priority);

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Overlay")
	bool ClearCellOverlayPresentation(const FGridCellOverlayPresentationHandle& Handle);

	UFUNCTION(BlueprintCallable, Category = "Grid World|Presentation|Overlay")
	bool ReleaseCellOverlayPresentation(const FGridCellOverlayPresentationHandle& Handle);

	UFUNCTION(BlueprintPure, Category = "Grid World|Presentation|Overlay")
	bool IsCellOverlayPresentationValid(const FGridCellOverlayPresentationHandle& Handle) const;

	UFUNCTION(BlueprintPure, Category = "Grid World|Presentation|Overlay")
	bool GetCellOverlayPresentation(
		const FGridCellOverlayPresentationHandle& Handle,
		FGridCellOverlayPresentationSnapshot& OutPresentation) const;

	int32 GetActiveSessionCount() const { return Sessions.Num(); }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FGridCellOverlayPresentationSession
	{
		FGridCellOverlayPresentationHandle Handle;
		TArray<FGridCellOverlayEntry> Entries;
		TWeakObjectPtr<UObject> Owner;
		uint64 CreationSequence = 0;
		int32 Priority = 0;
		bool bVisible = true;
	};

	TMap<FGuid, FGridCellOverlayPresentationSession> Sessions;
	uint64 NextCreationSequence = 1;
	FDelegateHandle PostGarbageCollectHandle;

	UFUNCTION()
	void HandleGridWorldChanged(const FGridChangeSet& ChangeSet);

	void HandlePostGarbageCollect();
	UGridWorldSubsystem* GetGridWorldSubsystem() const;
	UGridRuntimeVisualizationSubsystem* GetVisualizationSubsystem() const;
	bool ValidateEntries(TConstArrayView<FGridCellOverlayEntry> Entries, bool bAllowEmpty) const;
	bool PruneExpiredOwnerSessions();
	void RebuildPresentationOutput();
};
