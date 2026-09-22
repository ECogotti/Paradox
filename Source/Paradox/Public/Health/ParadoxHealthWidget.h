#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "ParadoxHealthWidget.generated.h"

class AActor;
class AController;
class UDamageType;
class UParadoxHealthComponent;

UENUM(BlueprintType)
enum class EParadoxHealthDisplayState : uint8
{
	Fine,
	Caution,
	Danger,
	Dead
};

/** Presentation-only observer for one explicitly supplied Health component. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API UParadoxHealthWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Rebinds to an explicit source. The owning Player/Controller is never used for discovery. */
	UFUNCTION(BlueprintCallable, Category = "Paradox|Health|Widget")
	void SetObservedHealthComponent(UParadoxHealthComponent* InHealthComponent);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Health|Widget")
	void ClearObservedHealthComponent();

	UFUNCTION(BlueprintPure, Category = "Paradox|Health|Widget")
	UParadoxHealthComponent* GetObservedHealthComponent() const
	{
		return ObservedHealthComponent.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Health|Widget")
	float GetDisplayedCurrentHealth() const { return DisplayedCurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Health|Widget")
	float GetDisplayedMaxHealth() const { return DisplayedMaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Health|Widget")
	float GetDisplayedNormalizedHealth() const { return DisplayedNormalizedHealth; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Health|Widget")
	bool IsDisplayedDead() const { return bDisplayedDead; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Health|Widget")
	EParadoxHealthDisplayState GetHealthDisplayState() const { return DisplayState; }

	UFUNCTION(BlueprintCallable, Category = "Paradox|Health|Widget")
	void RefreshHealthPresentation();

	/** Normalized threshold at or above which the display is Fine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Health|Widget|Presentation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FineThreshold = 0.60f;

	/** Normalized threshold at or above which the display is Caution. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Health|Widget|Presentation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CautionThreshold = 0.30f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Health|Widget", meta = (DisplayName = "On Observed Health Component Changed"))
	void ReceiveObservedHealthComponentChanged(UParadoxHealthComponent* NewHealthComponent);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Health|Widget", meta = (DisplayName = "On Health Display Updated"))
	void ReceiveHealthDisplayUpdated(
		float CurrentHealth,
		float MaxHealth,
		float NormalizedHealth,
		bool bIsDead);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Health|Widget", meta = (DisplayName = "On Health Display State Changed"))
	void ReceiveHealthDisplayStateChanged(
		EParadoxHealthDisplayState PreviousState,
		EParadoxHealthDisplayState NewState);

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Health|Widget", meta = (DisplayName = "On Death Feedback Requested"))
	void ReceiveDeathFeedbackRequested();

	UFUNCTION(BlueprintImplementableEvent, Category = "Paradox|Health|Widget", meta = (DisplayName = "On Health Reset Feedback Requested"))
	void ReceiveHealthResetFeedbackRequested();

private:
	void BindObservedHealth();
	void UnbindObservedHealth();
	EParadoxHealthDisplayState CalculateDisplayState() const;

	UFUNCTION()
	void HandleHealthChanged(
		float OldHealth,
		float NewHealth,
		float MaxHealth,
		float NormalizedHealth);

	UFUNCTION()
	void HandleDeath(
		const UDamageType* DamageType,
		AController* InstigatedBy,
		AActor* DamageCauser);

	UFUNCTION()
	void HandleHealthReset();

	UFUNCTION()
	void HandleObservedOwnerDestroyed(AActor* DestroyedActor);

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxHealthComponent> ObservedHealthComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<UParadoxHealthComponent> BoundHealthComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> BoundHealthOwner;

	UPROPERTY(Transient)
	float DisplayedCurrentHealth = 0.0f;

	UPROPERTY(Transient)
	float DisplayedMaxHealth = 0.0f;

	UPROPERTY(Transient)
	float DisplayedNormalizedHealth = 0.0f;

	UPROPERTY(Transient)
	bool bDisplayedDead = false;

	UPROPERTY(Transient)
	EParadoxHealthDisplayState DisplayState = EParadoxHealthDisplayState::Dead;
};
