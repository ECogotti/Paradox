#include "Health/ParadoxHealthComponent.h"

#include "GameFramework/Actor.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Paradox.h"

UParadoxHealthComponent::UParadoxHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

float UParadoxHealthComponent::GetNormalizedHealth() const
{
	return MaxHealth > UE_SMALL_NUMBER
		? FMath::Clamp(CurrentHealth / MaxHealth, 0.0f, 1.0f)
		: 0.0f;
}

float UParadoxHealthComponent::Heal(const float Amount)
{
	if (Amount <= 0.0f || bIsDead)
	{
		return 0.0f;
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth + Amount, 0.0f, MaxHealth);
	const float HealthRestored = CurrentHealth - OldHealth;
	if (HealthRestored <= 0.0f)
	{
		return 0.0f;
	}

	OnHealed.Broadcast(HealthRestored, CurrentHealth);
	OnHealthChanged.Broadcast(
		OldHealth,
		CurrentHealth,
		MaxHealth,
		GetNormalizedHealth());
	return HealthRestored;
}

float UParadoxHealthComponent::Kill(
	AController* EventInstigator,
	AActor* DamageCauser,
	TSubclassOf<UDamageType> DamageTypeClass)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || bIsDead || CurrentHealth <= 0.0f)
	{
		return 0.0f;
	}

	if (!DamageTypeClass)
	{
		DamageTypeClass = UDamageType::StaticClass();
	}
	return UGameplayStatics::ApplyDamage(
		Owner,
		CurrentHealth,
		EventInstigator,
		DamageCauser ? DamageCauser : Owner,
		DamageTypeClass);
}

void UParadoxHealthComponent::ResetHealth()
{
	SanitizeConfiguration();
	const float OldHealth = CurrentHealth;
	const bool bWasDead = bIsDead;
	CurrentHealth = MaxHealth;
	bIsDead = false;

	if (!FMath::IsNearlyEqual(OldHealth, CurrentHealth))
	{
		OnHealthChanged.Broadcast(
			OldHealth,
			CurrentHealth,
			MaxHealth,
			GetNormalizedHealth());
	}
	OnHealthReset.Broadcast();
	if (bWasDead)
	{
		OnRevived.Broadcast();
	}
}

void UParadoxHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	SanitizeConfiguration();
	CurrentHealth = MaxHealth;
	bIsDead = false;
	if (AActor* Owner = GetOwner())
	{
		Owner->OnTakeAnyDamage.AddUniqueDynamic(
			this,
			&ThisClass::HandleOwnerTakeAnyDamage);
	}
}

void UParadoxHealthComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* Owner = GetOwner())
	{
		Owner->OnTakeAnyDamage.RemoveDynamic(
			this,
			&ThisClass::HandleOwnerTakeAnyDamage);
	}
	Super::EndPlay(EndPlayReason);
}

void UParadoxHealthComponent::PostLoad()
{
	Super::PostLoad();
	SanitizeConfiguration();
}

#if WITH_EDITOR
void UParadoxHealthComponent::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SanitizeConfiguration();
	if (!HasBegunPlay())
	{
		CurrentHealth = MaxHealth;
	}
}
#endif

void UParadoxHealthComponent::SanitizeConfiguration()
{
	if (MaxHealth <= 0.0f)
	{
		PARADOX_LOG_WARNING(
			TEXT("Health component '%s' on '%s' had invalid MaxHealth %.3f; clamping to 1."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			MaxHealth);
		MaxHealth = 1.0f;
	}
	CurrentHealth = FMath::Clamp(CurrentHealth, 0.0f, MaxHealth);
}

void UParadoxHealthComponent::HandleOwnerTakeAnyDamage(
	AActor* DamagedActor,
	const float Damage,
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	if (DamagedActor != GetOwner() || Damage <= 0.0f || bIsDead)
	{
		return;
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth - Damage, 0.0f, MaxHealth);
	const float DamageApplied = OldHealth - CurrentHealth;
	if (DamageApplied <= 0.0f)
	{
		return;
	}

	const bool bDiedFromDamage = CurrentHealth <= 0.0f;
	if (bDiedFromDamage)
	{
		bIsDead = true;
	}
	OnDamageTaken.Broadcast(
		DamageApplied,
		DamageType,
		InstigatedBy,
		DamageCauser);
	OnHealthChanged.Broadcast(
		OldHealth,
		CurrentHealth,
		MaxHealth,
		GetNormalizedHealth());
	if (bDiedFromDamage)
	{
		OnDeath.Broadcast(DamageType, InstigatedBy, DamageCauser);
	}
}
