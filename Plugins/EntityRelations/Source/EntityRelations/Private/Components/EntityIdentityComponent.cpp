#include "Components/EntityIdentityComponent.h"

#include "Engine/World.h"
#include "Subsystems/EntityRelationsWorldSubsystem.h"

UEntityIdentityComponent::UEntityIdentityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FEntityRelationId UEntityIdentityComponent::GetEntityId() const
{
	return EntityIdMode == EEntityRelationIdMode::Explicit ? ExplicitEntityId : RuntimeEntityId;
}

bool UEntityIdentityComponent::IsRegistered() const
{
	const UWorld* World = GetWorld();
	const UEntityRelationsWorldSubsystem* Subsystem = World ? World->GetSubsystem<UEntityRelationsWorldSubsystem>() : nullptr;
	return Subsystem && Subsystem->IsEntityRegistered(this);
}

bool UEntityIdentityComponent::SetExplicitEntityId(FEntityRelationId NewEntityId)
{
	if (!NewEntityId.IsValid() || IsRegistered())
	{
		return false;
	}
	if (EntityIdMode == EEntityRelationIdMode::Explicit && ExplicitEntityId == NewEntityId)
	{
		return false;
	}
	EntityIdMode = EEntityRelationIdMode::Explicit;
	ExplicitEntityId = NewEntityId;
	RuntimeEntityId.Reset();
	PublishIdentityChange();
	return true;
}

bool UEntityIdentityComponent::SetDebugName(FName NewDebugName)
{
	if (DebugName == NewDebugName)
	{
		return false;
	}
	DebugName = NewDebugName;
	PublishIdentityChange();
	return true;
}

bool UEntityIdentityComponent::SetIdentityTags(const FGameplayTagContainer& NewTags)
{
	if (IdentityTags == NewTags)
	{
		return false;
	}
	IdentityTags = NewTags;
	PublishIdentityChange();
	return true;
}

bool UEntityIdentityComponent::AddIdentityTag(FGameplayTag Tag)
{
	if (!Tag.IsValid() || IdentityTags.HasTagExact(Tag))
	{
		return false;
	}
	IdentityTags.AddTag(Tag);
	PublishIdentityChange();
	return true;
}

bool UEntityIdentityComponent::RemoveIdentityTag(FGameplayTag Tag)
{
	if (!Tag.IsValid() || !IdentityTags.HasTagExact(Tag))
	{
		return false;
	}
	IdentityTags.RemoveTag(Tag);
	PublishIdentityChange();
	return true;
}

bool UEntityIdentityComponent::SetAffiliationTags(const FGameplayTagContainer& NewTags)
{
	if (AffiliationTags == NewTags)
	{
		return false;
	}
	AffiliationTags = NewTags;
	PublishIdentityChange();
	return true;
}

bool UEntityIdentityComponent::AddAffiliationTag(FGameplayTag Tag)
{
	if (!Tag.IsValid() || AffiliationTags.HasTagExact(Tag))
	{
		return false;
	}
	AffiliationTags.AddTag(Tag);
	PublishIdentityChange();
	return true;
}

bool UEntityIdentityComponent::RemoveAffiliationTag(FGameplayTag Tag)
{
	if (!Tag.IsValid() || !AffiliationTags.HasTagExact(Tag))
	{
		return false;
	}
	AffiliationTags.RemoveTag(Tag);
	PublishIdentityChange();
	return true;
}

void UEntityIdentityComponent::PublishIdentityChange()
{
	++IdentityRevision;
	if (UWorld* World = GetWorld())
	{
		if (UEntityRelationsWorldSubsystem* Subsystem = World->GetSubsystem<UEntityRelationsWorldSubsystem>(); Subsystem && Subsystem->IsEntityRegistered(this))
		{
			Subsystem->NotifyIdentityChanged(this);
		}
	}
	OnEntityIdentityChanged.Broadcast(GetEntityId(), IdentityRevision);
}

void UEntityIdentityComponent::BeginPlay()
{
	Super::BeginPlay();
	if (EntityIdMode == EEntityRelationIdMode::RuntimeGenerated)
	{
		RuntimeEntityId = FEntityRelationId::NewId();
	}
	IdentityRevision = FMath::Max<int64>(IdentityRevision, 1);
	if (UWorld* World = GetWorld())
	{
		if (UEntityRelationsWorldSubsystem* Subsystem = World->GetSubsystem<UEntityRelationsWorldSubsystem>())
		{
			LastRegistrationResult = Subsystem->RegisterIdentity(this);
		}
	}
}

void UEntityIdentityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UEntityRelationsWorldSubsystem* Subsystem = World->GetSubsystem<UEntityRelationsWorldSubsystem>())
		{
			Subsystem->UnregisterIdentity(this);
		}
	}
	if (EntityIdMode == EEntityRelationIdMode::RuntimeGenerated)
	{
		RuntimeEntityId.Reset();
	}
	Super::EndPlay(EndPlayReason);
}
