#pragma once

#include "CoreMinimal.h"
#include "GridWorldTypes.h"
#include "Navigation/GridTrafficReservation.h"
#include "Puzzles/ParadoxPairedTransferEndpoint.h"
#include "ParadoxTeleportGate.generated.h"

class AGridNavigationData;
class AParadoxCharacter;
class UGameplayActionInstance;
class UGridNavigationModifierComponent;
class UGridNavigationOccupancyComponent;
class UParadoxInteractionComponent;
class UParadoxSelectableComponent;
class USmartObjectComponent;
class FDataValidationContext;
struct FParadoxInteractionOption;

/** Explicit semantic paired-transfer endpoint for player, clone, and compatible AI Characters. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxTeleportGate : public AParadoxPairedTransferEndpoint
{
	GENERATED_BODY()

public:
	AParadoxTeleportGate();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	/** Side-effect-free Enter validation. The supplied semantic action is mandatory for this endpoint. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Teleport Gate")
	FParadoxTransferOperationResult EvaluateEnter(
		AParadoxCharacter* Character,
		UGameplayActionInstance* RequestingAction) const;

	/** Acquires and starts the paired Character transfer through the running Enter action. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Teleport Gate")
	FParadoxTransferOperationResult TryEnter(
		AParadoxCharacter* Character,
		UGameplayActionInstance* RequestingAction);

	UFUNCTION(BlueprintPure, Category = "Paradox|Teleport Gate|Components")
	UParadoxSelectableComponent* GetSelectableComponent() const { return SelectableComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Teleport Gate|Components")
	USmartObjectComponent* GetSmartObjectComponent() const { return SmartObjectComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Teleport Gate|Components")
	UParadoxInteractionComponent* GetInteractionComponent() const { return InteractionComponent.Get(); }

	/** Authored footprint that remains blocked for the entire Gate lifetime. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Teleport Gate|Components")
	UGridNavigationModifierComponent* GetPermanentNavigationBlocker() const
	{
		return PermanentNavigationBlocker.Get();
	}

	/** Authored footprint that blocks only while this Gate is being physically traversed. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Teleport Gate|Components")
	UGridNavigationModifierComponent* GetTransitNavigationBlocker() const
	{
		return TransitNavigationBlocker.Get();
	}

	/** Planar acceptance radius used by both pathfinding-free tunnel traversal segments. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Teleport Gate|Movement", meta = (ClampMin = "0.1", Units = "cm"))
	float TunnelTraversalAcceptanceRadius = 10.0f;

protected:
	virtual bool CanRequestTransferWithAction_Implementation(
		AActor* TransferSubject,
		AActor* Requester,
		UGameplayActionInstance* RequestingAction,
		FString& OutDiagnostic) const override;
	virtual bool CanTransferSubject_Implementation(
		AActor* TransferSubject,
		AActor* Requester,
		FString& OutDiagnostic) const override;
	virtual bool CanSourceStartTransfer_Implementation(
		AActor* TransferSubject,
		AActor* Requester,
		AParadoxPairedTransferEndpoint* Destination,
		FString& OutDiagnostic) const override;
	virtual bool CanDestinationReceiveTransfer_Implementation(
		AActor* TransferSubject,
		AActor* Requester,
		AParadoxPairedTransferEndpoint* Source,
		FString& OutDiagnostic) const override;
	virtual bool PrepareSubjectForTransfer_Implementation(
		const FParadoxTransferOperationContext& Context,
		FString& OutDiagnostic) override;
	virtual bool PerformTransferCommit_Implementation(
		const FParadoxTransferOperationContext& Context,
		FString& OutDiagnostic) override;
	virtual bool FinalizeTransferredSubject_Implementation(
		const FParadoxTransferOperationContext& Context,
		FString& OutDiagnostic) override;
	virtual void HandleTransferCancelled_Implementation(
		const FParadoxTransferOperationContext& Context,
		EParadoxTransferCancellationReason Reason,
		bool bWasCommitted) override;
	virtual bool IsLinkedEndpointCompatible(
		const AParadoxPairedTransferEndpoint* Candidate,
		FString& OutDiagnostic) const override;

private:
	bool ResolveUniqueEnterOption(
		AParadoxCharacter& Character,
		const AParadoxTeleportGate& Gate,
		FParadoxInteractionOption& OutOption,
		FString& OutDiagnostic) const;
	bool ResolveSafeTunnelAnchorPlacement(
		AParadoxCharacter& Character,
		const AParadoxTeleportGate& Gate,
		FTransform& OutTransform,
		FString& OutDiagnostic) const;
	bool ResolveInteractionCell(
		const FParadoxInteractionOption& Option,
		FGridCellQueryResult& OutCell,
		FString& OutDiagnostic) const;
	bool BuildExitClaim(
		AParadoxCharacter& Character,
		const FGridCellQueryResult& ExitCell,
		bool bCreateOccupancy,
		FGridTrafficGoalClaimRequest& OutClaim,
		AGridNavigationData*& OutNavigationData,
		UGridNavigationOccupancyComponent*& OutOccupancy,
		FString& OutDiagnostic) const;
	void StopCharacterMovement(AParadoxCharacter& Character) const;
	bool RecoverCharacterToSlot(
		AParadoxCharacter& Character,
		const FTransform& SlotTransform,
		const FGridCellId& SlotCell,
		bool bCommitParking,
		FString& OutDiagnostic);
	bool IsCharacterAtSlot(
		const AParadoxCharacter& Character,
		const FTransform& SlotTransform,
		const FGridCellId& SlotCell) const;
	bool GetActiveTunnelTarget(
		const FGuid& OperationId,
		bool bDestinationEgress,
		FVector& OutLocation) const;
	void SetTransitNavigationBlocking(bool bBlocking);
	void ClearPairTransitNavigationBlocking(const FParadoxTransferOperationContext& Context);
	bool IsActiveExitClaimStillOwned() const;
	void ReleaseExitReservation();
	void ClearPlacementState();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxSelectableComponent> SelectableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USmartObjectComponent> SmartObjectComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxInteractionComponent> InteractionComponent;

	/** GridWorld footprint that always blocks navigation and should cover the solid Gate body. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGridNavigationModifierComponent> PermanentNavigationBlocker;

	/** GridWorld footprint that blocks both paired entrances from forced ingress through egress. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGridNavigationModifierComponent> TransitNavigationBlocker;

	TWeakObjectPtr<AGridNavigationData> ActiveExitNavigationData;
	FGridTrafficGoalClaimRequest ActiveExitClaim;
	FGridCellId ActiveSourceCell;
	FGridCellId ActiveExitCell;
	FTransform ActiveSourceSlotTransform = FTransform::Identity;
	FTransform ActiveDestinationSlotTransform = FTransform::Identity;
	FTransform ActiveDestinationTeleportTransform = FTransform::Identity;
	bool bHasActiveExitClaim = false;
	bool bHasCommittedPlacement = false;

	friend class UParadoxEnterTeleportGateInteractionAction;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxTeleportGateTestAccessor;
#endif
};
