#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimeLoop/ParadoxTimeLoopTypes.h"
#include "ParadoxChronoSpawn.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UParadoxSelectableComponent;

/** Designer-placeable entry point for one playable timeline. */
UCLASS(Blueprintable)
class PARADOX_API AParadoxChronoSpawn : public AActor
{
	GENERATED_BODY()

public:
	AParadoxChronoSpawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	EParadoxChronoSpawnState GetChronoSpawnState() const { return ChronoSpawnState; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	bool IsChronoSpawnEnabled() const { return bChronoSpawnEnabled; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	bool IsAvailableForSelection() const;

	UFUNCTION(BlueprintPure, Category = "Paradox|Chrono Spawn")
	UParadoxSelectableComponent* GetSelectableComponent() const { return SelectableComponent.Get(); }

	/** Runtime/design-time switch. Disabling immediately makes this spawn unselectable. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Chrono Spawn")
	void SetChronoSpawnEnabled(bool bEnabled);

	/**
	 * Replaceable presentation hook; authoritative state is already committed when this fires.
	 * Blueprint overrides may omit Call to Parent to replace the native fallback completely.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Paradox|Chrono Spawn", meta = (DisplayName = "On Chrono Spawn Visual State Changed"))
	void ReceiveVisualStateChanged(EParadoxChronoSpawnState NewState);
	virtual void ReceiveVisualStateChanged_Implementation(
		EParadoxChronoSpawnState NewState);

private:
	void SetRuntimeState(EParadoxChronoSpawnState NewState);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> SelectionMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTextRenderComponent> StateLabel;

	/** Project-level hover, selection, outline, and optional world-widget capability. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UParadoxSelectableComponent> SelectableComponent;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	bool bChronoSpawnEnabled = true;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Paradox|Chrono Spawn", meta = (AllowPrivateAccess = "true"))
	EParadoxChronoSpawnState ChronoSpawnState = EParadoxChronoSpawnState::Available;

	friend class UParadoxTimeLoopComponent;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FParadoxTimeLoopTestAccessor;
#endif
};
