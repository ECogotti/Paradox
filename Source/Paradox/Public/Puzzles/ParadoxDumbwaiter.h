#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Inventory/ParadoxItemSlotTypes.h"
#include "Puzzles/ParadoxPairedTransferEndpoint.h"
#include "Types/WorldStateTypes.h"
#include "ParadoxDumbwaiter.generated.h"

class AParadoxCharacter;
class AParadoxDumbwaiter;
class AParadoxInsertablePickupableActor;
class UGameplayActionInstance;
class UParadoxInteractionComponent;
class UParadoxInventoryComponent;
class UParadoxSelectableComponent;
class USmartObjectComponent;
class UWorldStateParticipantComponent;
class FDataValidationContext;
struct FWorldStateRestoreLifecycleContext;
struct FWorldStateRestoreResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FParadoxDumbwaiterCargoChanged,
	AParadoxDumbwaiter*, Dumbwaiter,
	AParadoxInsertablePickupableActor*, PreviousCargo,
	AParadoxInsertablePickupableActor*, NewCargo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FParadoxDumbwaiterPairOccupancyChanged,
	AParadoxDumbwaiter*, Dumbwaiter,
	bool, bSelfOccupied,
	bool, bLinkedOccupied);

/** Paired transfer endpoint that stores and transports exactly one insertable pickupable. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxDumbwaiter : public AParadoxPairedTransferEndpoint
{
	GENERATED_BODY()

public:
	AParadoxDumbwaiter();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UFUNCTION(BlueprintPure, Category = "Paradox|Dumbwaiter")
	bool IsOccupied() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Dumbwaiter")
	bool IsLinkedDumbwaiterOccupied() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Dumbwaiter")
	AParadoxInsertablePickupableActor* GetStoredPickupable() const { return StoredPickupable.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Dumbwaiter")
	bool CanAcceptCargo(AParadoxInsertablePickupableActor* Cargo, AParadoxCharacter* Requester) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Dumbwaiter")
	FParadoxItemSlotOperationResult EvaluateAcceptCargo(
		AParadoxInsertablePickupableActor* Cargo,
		AParadoxCharacter* Requester) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Dumbwaiter")
	FParadoxItemSlotOperationResult EvaluatePickupCargo(AParadoxCharacter* Requester) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Dumbwaiter")
	FParadoxItemSlotOperationResult TryInsertCargo(AParadoxCharacter* Requester);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Dumbwaiter")
	FParadoxItemSlotOperationResult TryPickupCargo(AParadoxCharacter* Requester);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Dumbwaiter")
	FParadoxTransferOperationResult EvaluateSendCargo(
		AActor* Requester,
		UGameplayActionInstance* RequestingAction = nullptr) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Dumbwaiter")
	FParadoxTransferOperationResult TrySendCargo(
		AActor* Requester,
		UGameplayActionInstance* RequestingAction = nullptr);

	/** Re-evaluates interaction availability after a dynamic cargo condition changes. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Dumbwaiter")
	void NotifyStoredPickupableRelevantStateChanged(AParadoxInsertablePickupableActor* ChangedCargo);

	UFUNCTION(BlueprintPure, Category = "Paradox|Dumbwaiter|Components")
	UParadoxSelectableComponent* GetSelectableComponent() const { return SelectableComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Dumbwaiter|Components")
	USmartObjectComponent* GetSmartObjectComponent() const { return SmartObjectComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Dumbwaiter|Components")
	UParadoxInteractionComponent* GetInteractionComponent() const { return InteractionComponent.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Dumbwaiter|Components")
	UWorldStateParticipantComponent* GetWorldStateParticipantComponent() const { return WorldStateParticipant.Get(); }

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Dumbwaiter|Events")
	FParadoxDumbwaiterCargoChanged OnStoredPickupableChanged;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Dumbwaiter|Events")
	FParadoxDumbwaiterPairOccupancyChanged OnPairOccupancyChanged;

protected:
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

	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Dumbwaiter|Validation", meta = (BlueprintProtected = "true"))
	bool CanAcceptCargoAdditional(
		AParadoxInsertablePickupableActor* Cargo,
		AActor* Requester,
		FString& OutDiagnostic) const;
	virtual bool CanAcceptCargoAdditional_Implementation(
		AParadoxInsertablePickupableActor* Cargo,
		AActor* Requester,
		FString& OutDiagnostic) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Dumbwaiter|Presentation", meta = (DisplayName = "On Stored Pickupable Changed"))
	void ReceiveStoredPickupableChanged(
		AParadoxInsertablePickupableActor* PreviousCargo,
		AParadoxInsertablePickupableActor* NewCargo);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Dumbwaiter|Presentation", meta = (DisplayName = "On Pair Occupancy Changed"))
	void ReceivePairOccupancyChanged(bool bSelfOccupied, bool bLinkedOccupied);

	/** Hard cargo filter evaluated against the pickupable's InsertableTraits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Dumbwaiter|Compatibility")
	FGameplayTagQuery AcceptedCargoQuery;

	/** Prevents ordinary character pickup; paired Send remains available. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Dumbwaiter|Policy")
	bool bLockStoredPickupable = false;

private:
	FParadoxItemSlotOperationResult MakeCargoResult(
		EParadoxItemSlotOperationStatus Status,
		FString Diagnostic) const;
	bool MatchesAcceptedCargoQuery(const AParadoxInsertablePickupableActor* Cargo) const;
	void InitializeAuthoredCargo();
	void SetStoredPickupableCommitted(AParadoxInsertablePickupableActor* NewCargo);
	void ClearStoredPickupableCommitted(AParadoxInsertablePickupableActor* ExpectedCargo);
	void FinalizeCargoTransition(
		AParadoxInsertablePickupableActor* PreviousCargo,
		AParadoxInsertablePickupableActor* NewCargo,
		bool bNotifyPair = true);
	void NotifyPairOccupancyChanged();
	void DispatchPairOccupancyChanged();
	void RefreshPairInteractionAffordances();
	void BindStoredPickupable(AParadoxInsertablePickupableActor& Cargo);
	void UnbindStoredPickupable(AParadoxInsertablePickupableActor* Cargo);
	void PrepareForWorldStateRestore();
	void RestoreCapturedRelationship();
	void FinishWorldStateRestore(bool bSucceeded);
	void RefreshInteractionAffordances();
	void HandleWorldStateRestoreStarted(const FWorldStateRestoreLifecycleContext& Context);
	void HandleWorldStateRestoreFinished(const FWorldStateRestoreResult& Result);

	UFUNCTION()
	void HandleWorldStatePreCapture(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStatePreRestore(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStatePropertiesRestored(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStateParticipantRestored(FWorldStateParticipantId ParticipantId);
	UFUNCTION()
	void HandleWorldStateParticipantFailed(const FWorldStateParticipantResult& Result);
	UFUNCTION()
	void HandleStoredPickupableDestroyed(AActor* DestroyedActor);

	void HandleStoredPickupableInvalidated(AParadoxInsertablePickupableActor* Cargo);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxSelectableComponent> SelectableComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USmartObjectComponent> SmartObjectComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxInteractionComponent> InteractionComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWorldStateParticipantComponent> WorldStateParticipant;

	UPROPERTY(
		EditInstanceOnly,
		BlueprintReadOnly,
		Category = "Paradox|Dumbwaiter|State",
		meta = (
			AllowPrivateAccess = "true",
			DisplayName = "Initially Stored Pickupable",
			AllowedClasses = "/Script/Paradox.ParadoxInsertablePickupableActor"))
	TObjectPtr<AParadoxInsertablePickupableActor> StoredPickupable;

	UPROPERTY(Transient)
	TSoftObjectPtr<AParadoxInsertablePickupableActor> WorldStateStoredPickupable;

	bool bOperationInProgress = false;
	bool bResetInProgress = false;
	bool bInitialized = false;

	friend class AParadoxInsertablePickupableActor;
	friend class UParadoxInventoryComponent;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxDumbwaiterTestAccessor;
#endif
};
