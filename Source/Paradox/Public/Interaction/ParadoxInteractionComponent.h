#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Interaction/ParadoxInteractionTypes.h"
#include "ParadoxInteractionComponent.generated.h"

class USceneComponent;
class USmartObjectComponent;
class UPuzzleEmitterComponent;
class UPuzzleReceiverComponent;
class APuzzleController;
enum class EPuzzleGraphTopologyChangeKind : uint8;
struct FPuzzleGraphLinkHandle;
struct FPuzzleGraphLinkState;
struct FPuzzleSignalState;
struct FGameplayActionRequest;
struct FSmartObjectEventData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxInteractionAffordanceChanged,
	UParadoxInteractionComponent*, InteractionComponent);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FParadoxInteractionAffordanceChangedNative,
	UParadoxInteractionComponent*);

/** Multi-interaction catalog and read-only Smart Object/GridWorld affordance query. */
UCLASS(ClassGroup = (Paradox), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxInteractionComponent();

	/** Every matching definition produces an option for every matching slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	TArray<FParadoxInteractionDefinition> InteractionDefinitions;

	/** Projection box used to map current Smart Object slot transforms onto GridWorld. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction|Grid World", meta = (ClampMin = "0.0", Units = "cm"))
	FVector GridProjectionExtent = FVector(50.0, 50.0, 200.0);

	/** Extra clearance applied when checking existing GridWorld traffic reservations. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction|Grid World", meta = (ClampMin = "0.0", Units = "cm"))
	float TrafficAdditionalSeparation = 5.0f;

	/** Local half of the Paradox.Interaction.Debug gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction|Debug")
	bool bEnableDebug = false;

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Interaction")
	FParadoxInteractionAffordanceChanged OnInteractionAffordanceChanged;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction")
	FParadoxInteractionQueryResult QueryInteractionOptions(AActor* Requester) const;

	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction")
	FParadoxInteractionQueryResult QueryInteractionOptionsByTag(
		AActor* Requester,
		FGameplayTag InteractionTag) const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Interaction")
	bool HasFreeInteractionOption(AActor* Requester, FGameplayTag InteractionTag) const;

	/** Evaluates effect preconditions, exact-cell reachability and the current scheduler decision. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction")
	FParadoxInteractionAvailabilityResult EvaluateInteractionAvailability(
		AActor* Requester,
		FGameplayTag InteractionTag) const;

	/** Pure validation of the shared player/AI execution path. It never acquires a Smart Object claim. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction")
	bool CanRequestInteraction(AActor* Requester, FGameplayTag InteractionTag) const;

	/**
	 * Creates and submits a replay-safe Gameplay Action request for the exact Interaction Tag.
	 * Requester must own the Gameplay Action Component; Request Source is diagnostic request context.
	 */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction")
	FParadoxInteractionRequestResult RequestInteraction(
		AActor* Requester,
		FGameplayTag InteractionTag,
		FGameplayTag OriginTag,
		UObject* RequestSource) const;

	/** Reconciles direct Smart Object Components and broadcasts when the source set changes. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Interaction")
	void RefreshInteractionSources();

	FParadoxInteractionAffordanceChangedNative& OnInteractionAffordanceChangedNative()
	{
		return InteractionAffordanceChangedNative;
	}

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	FParadoxInteractionQueryResult QueryInteractionOptionsInternal(
		AActor* Requester,
		const FGameplayTag* InteractionTag) const;
	bool ResolveCurrentExecutionOption(
		AActor* Requester,
		FGameplayTag InteractionTag,
		FParadoxInteractionOption& OutOption,
		EParadoxInteractionRequestStatus& OutStatus,
		EParadoxInteractionQueryStatus& OutQueryStatus,
		FString& OutDiagnostic) const;
	bool ResolveExactFreeCatalogOption(
		AActor* Requester,
		FGameplayTag InteractionTag,
		FParadoxInteractionOption& OutOption,
		EParadoxInteractionRequestStatus& OutStatus,
		EParadoxInteractionQueryStatus& OutQueryStatus,
		FString& OutDiagnostic) const;
	bool ResolveBestReachableExecutionOption(
		AActor* Requester,
		FGameplayTag InteractionTag,
		FParadoxInteractionOption& OutOption,
		double& OutPathCost,
		bool& bOutAlreadyInPlace,
		EParadoxInteractionRequestStatus& OutStatus,
		EParadoxInteractionQueryStatus& OutQueryStatus,
		FString& OutDiagnostic,
		const FParadoxInteractionMovementParameters* MovementParameters = nullptr) const;
	bool BuildGameplayActionRequest(
		AActor* Requester,
		FGameplayTag InteractionTag,
		FGameplayTag OriginTag,
		UObject* RequestSource,
		FGameplayActionRequest& OutRequest,
		FParadoxInteractionRequestResult& OutResult) const;
	void BroadcastInteractionAffordanceChanged();
	void UnbindInteractionSources();
	void BindPuzzleAffordanceSources();
	void UnbindPuzzleAffordanceSources();
	void HandleSmartObjectEvent(const FSmartObjectEventData& EventData, const AActor* Interactor);
	void HandleSmartObjectTransformUpdated(
		USceneComponent* UpdatedComponent,
		EUpdateTransformFlags UpdateTransformFlags,
		ETeleportType Teleport);
	void HandleReceiverStateChanged(UPuzzleReceiverComponent* Receiver, bool bReceiverActive);
	void HandleReceiverPrerequisitesChanged(UPuzzleReceiverComponent* Receiver, bool bPrerequisitesSatisfied);
	void HandleEmitterSignalChanged(UPuzzleEmitterComponent* Emitter, FGameplayTag SignalTag, FPuzzleSignalState SignalState);
	void HandlePuzzleGraphTopologyChanged(int64 Revision, APuzzleController* Controller, EPuzzleGraphTopologyChangeKind ChangeKind);
	void HandlePuzzleGraphLinkStateChanged(const FPuzzleGraphLinkHandle& LinkHandle, const FPuzzleGraphLinkState& PreviousState, const FPuzzleGraphLinkState& NewState);

	TArray<TWeakObjectPtr<USmartObjectComponent>> InteractionSources;
	TArray<TWeakObjectPtr<UPuzzleReceiverComponent>> ReceiverAffordanceSources;
	TArray<TWeakObjectPtr<UPuzzleEmitterComponent>> EmitterAffordanceSources;
	FParadoxInteractionAffordanceChangedNative InteractionAffordanceChangedNative;

	friend class UParadoxInteractionActionBase;
};
