#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GridWorldTypes.h"
#include "Prediction/GridPathPreviewComponent.h"
#include "Types/GameplayActionTypes.h"
#include "ParadoxDropTargetingComponent.generated.h"

class AParadoxCharacter;
class AParadoxPlayerController;
class AParadoxPickupableActor;
class UParadoxInventoryComponent;
class UParadoxDropActionDefinition;
class UMaterialInterface;
class UStaticMeshComponent;
class AActor;
struct FHitResult;
struct FWorldStateRestoreLifecycleContext;

UENUM(BlueprintType)
enum class EParadoxDropTargetingStatus : uint8
{
	Succeeded,
	AlreadyActive,
	NotActive,
	InvalidController,
	InvalidCharacter,
	SlotEmpty,
	InvalidDefinition,
	InvalidTarget,
	SubmissionRejected,
	Cancelled,
	SourceInvalidated
};

USTRUCT(BlueprintType)
struct PARADOX_API FParadoxDropTargetingResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Drop Targeting")
	EParadoxDropTargetingStatus Status = EParadoxDropTargetingStatus::InvalidController;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Drop Targeting")
	FGameplayActionSubmissionResult SubmissionResult;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Drop Targeting")
	FString DiagnosticMessage;

	bool IsSuccess() const { return Status == EParadoxDropTargetingStatus::Succeeded; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FParadoxDropTargetingChanged,
	bool, bIsTargeting);

/** Player-only input/presentation adapter; the Drop action itself remains player/clone neutral. */
UCLASS(ClassGroup = (Paradox), BlueprintType, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxDropTargetingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxDropTargetingComponent();

	/** Starts a session for the supplied Character; the controller Pawn is never used as an implicit source. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Drop Targeting")
	FParadoxDropTargetingResult BeginDropTargeting(AParadoxCharacter* SourceCharacter);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Drop Targeting")
	FParadoxDropTargetingResult CancelDropTargeting();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Inventory|Drop Targeting")
	FParadoxDropTargetingResult ConfirmDropTarget();

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Drop Targeting")
	bool IsDropTargetingActive() const { return bTargetingActive; }

	/** Character whose inventory is targeted by the active session; independent from the input-owning controller's Pawn. */
	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Drop Targeting")
	AParadoxCharacter* GetDropSourceCharacter() const { return DropSourceCharacter.Get(); }

	UFUNCTION(BlueprintPure, Category = "Paradox|Inventory|Drop Targeting")
	FGridCellId GetHoveredDropCell() const { return HoveredCell; }

	/** Uses the controller's shared pointer hit; no independent trace or Tick is introduced. */
	void UpdateTargetFromHit(const FHitResult& HitResult, bool bHitSuccessful);

	UPROPERTY(BlueprintAssignable, Category = "Paradox|Inventory|Drop Targeting")
	FParadoxDropTargetingChanged OnDropTargetingChanged;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Inventory|Drop Targeting")
	TObjectPtr<UParadoxDropActionDefinition> DropActionDefinition;

	/** Optional local ghost material applied to every slot of the held pickupable mesh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Inventory|Drop Targeting|Presentation")
	TObjectPtr<UMaterialInterface> DropPreviewMaterial;

	/** Local half of the Paradox.Inventory.Debug gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Inventory|Debug")
	bool bEnableDebug = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FParadoxDropTargetingResult MakeResult(
		EParadoxDropTargetingStatus Status,
		FString Diagnostic) const;
	AParadoxPlayerController* GetParadoxController() const;
	bool ValidateDropSource(FString& OutDiagnostic) const;
	void RefreshTargetPreview();
	void UpdateDropPreview(const FGridPathPreviewResult& Preview);
	void SetDropPreviewVisible(bool bVisible);
	void HideDropPreview();
	void DestroyDropPreview();
	UStaticMeshComponent* EnsureDropPreview();
	void BindRuntimeSources();
	void UnbindRuntimeSources();
	void EndTargeting();
	bool BuildDropRequest(
		const FGridCellId& TargetCell,
		const FGridInjectedPath& InjectedPath,
		FGameplayActionRequest& OutRequest,
		FParadoxDropTargetingResult& OutFailure) const;
	void LogDebugState(const TCHAR* EventName, const FString& Diagnostic = FString()) const;

	UFUNCTION()
	void HandlePathPreviewChanged(const FGridPathPreviewResult& Preview);
	UFUNCTION()
	void HandleDropSourceInventoryChanged(
		AParadoxPickupableActor* PreviousItem,
		AParadoxPickupableActor* NewItem);
	UFUNCTION()
	void HandleDropSourceDestroyed(AActor* DestroyedActor);
	void HandleWorldStateRestoreStarted(const FWorldStateRestoreLifecycleContext& Context);

	UPROPERTY(Transient)
	FGridCellId HoveredCell;

	UPROPERTY(Transient)
	TWeakObjectPtr<AParadoxCharacter> DropSourceCharacter;

	UPROPERTY(Transient)
	TWeakObjectPtr<AParadoxPickupableActor> DropSourceItem;

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxInventoryComponent> BoundInventory;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DropPreviewComponent;

	/** Visible transient presentation owner; Player Controllers are hidden Actors. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> DropPreviewActor;

	bool bTargetingActive = false;

#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxInventoryTestAccessor;
#endif
};
