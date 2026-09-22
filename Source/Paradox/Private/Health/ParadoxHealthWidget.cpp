#include "Health/ParadoxHealthWidget.h"

#include "GameFramework/Actor.h"
#include "Health/ParadoxHealthComponent.h"

void UParadoxHealthWidget::SetObservedHealthComponent(
	UParadoxHealthComponent* InHealthComponent)
{
	if (ObservedHealthComponent.Get() == InHealthComponent)
	{
		BindObservedHealth();
		RefreshHealthPresentation();
		return;
	}

	UnbindObservedHealth();
	ObservedHealthComponent = InHealthComponent;
	BindObservedHealth();
	RefreshHealthPresentation();
	ReceiveObservedHealthComponentChanged(InHealthComponent);
}

void UParadoxHealthWidget::ClearObservedHealthComponent()
{
	if (!ObservedHealthComponent.IsValid() && !BoundHealthComponent.IsValid())
	{
		return;
	}
	UnbindObservedHealth();
	ObservedHealthComponent.Reset();
	RefreshHealthPresentation();
	ReceiveObservedHealthComponentChanged(nullptr);
}

void UParadoxHealthWidget::RefreshHealthPresentation()
{
	UParadoxHealthComponent* Health = ObservedHealthComponent.Get();
	DisplayedCurrentHealth = Health ? Health->GetCurrentHealth() : 0.0f;
	DisplayedMaxHealth = Health ? Health->GetMaxHealth() : 0.0f;
	DisplayedNormalizedHealth = Health ? Health->GetNormalizedHealth() : 0.0f;
	bDisplayedDead = !Health || Health->IsDead();

	const EParadoxHealthDisplayState PreviousState = DisplayState;
	DisplayState = CalculateDisplayState();
	ReceiveHealthDisplayUpdated(
		DisplayedCurrentHealth,
		DisplayedMaxHealth,
		DisplayedNormalizedHealth,
		bDisplayedDead);
	if (PreviousState != DisplayState)
	{
		ReceiveHealthDisplayStateChanged(PreviousState, DisplayState);
	}
}

void UParadoxHealthWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindObservedHealth();
	RefreshHealthPresentation();
}

void UParadoxHealthWidget::NativeDestruct()
{
	UnbindObservedHealth();
	Super::NativeDestruct();
}

void UParadoxHealthWidget::BindObservedHealth()
{
	UParadoxHealthComponent* Health = ObservedHealthComponent.Get();
	if (!Health || BoundHealthComponent.Get() == Health)
	{
		return;
	}
	UnbindObservedHealth();
	BoundHealthComponent = Health;
	Health->OnHealthChanged.AddUniqueDynamic(
		this,
		&ThisClass::HandleHealthChanged);
	Health->OnDeath.AddUniqueDynamic(this, &ThisClass::HandleDeath);
	Health->OnHealthReset.AddUniqueDynamic(
		this,
		&ThisClass::HandleHealthReset);
	BoundHealthOwner = Health->GetOwner();
	if (BoundHealthOwner.IsValid())
	{
		BoundHealthOwner->OnDestroyed.AddUniqueDynamic(
			this,
			&ThisClass::HandleObservedOwnerDestroyed);
	}
}

void UParadoxHealthWidget::UnbindObservedHealth()
{
	if (BoundHealthComponent.IsValid())
	{
		BoundHealthComponent->OnHealthChanged.RemoveDynamic(
			this,
			&ThisClass::HandleHealthChanged);
		BoundHealthComponent->OnDeath.RemoveDynamic(
			this,
			&ThisClass::HandleDeath);
		BoundHealthComponent->OnHealthReset.RemoveDynamic(
			this,
			&ThisClass::HandleHealthReset);
	}
	if (BoundHealthOwner.IsValid())
	{
		BoundHealthOwner->OnDestroyed.RemoveDynamic(
			this,
			&ThisClass::HandleObservedOwnerDestroyed);
	}
	BoundHealthComponent.Reset();
	BoundHealthOwner.Reset();
}

EParadoxHealthDisplayState UParadoxHealthWidget::CalculateDisplayState() const
{
	if (bDisplayedDead || DisplayedCurrentHealth <= 0.0f)
	{
		return EParadoxHealthDisplayState::Dead;
	}
	const float SafeFine = FMath::Clamp(FineThreshold, 0.0f, 1.0f);
	const float SafeCaution = FMath::Min(
		FMath::Clamp(CautionThreshold, 0.0f, 1.0f),
		SafeFine);
	if (DisplayedNormalizedHealth >= SafeFine)
	{
		return EParadoxHealthDisplayState::Fine;
	}
	return DisplayedNormalizedHealth >= SafeCaution
		? EParadoxHealthDisplayState::Caution
		: EParadoxHealthDisplayState::Danger;
}

void UParadoxHealthWidget::HandleHealthChanged(
	float OldHealth,
	float NewHealth,
	float MaxHealth,
	float NormalizedHealth)
{
	RefreshHealthPresentation();
}

void UParadoxHealthWidget::HandleDeath(
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	RefreshHealthPresentation();
	ReceiveDeathFeedbackRequested();
}

void UParadoxHealthWidget::HandleHealthReset()
{
	RefreshHealthPresentation();
	ReceiveHealthResetFeedbackRequested();
}

void UParadoxHealthWidget::HandleObservedOwnerDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == BoundHealthOwner.Get())
	{
		ClearObservedHealthComponent();
	}
}
