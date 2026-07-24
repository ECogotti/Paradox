#include "Components/EntityRelationStateComponent.h"

#include "Components/EntityIdentityComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EntityRelationsWorldSubsystem.h"

UEntityRelationStateComponent::UEntityRelationStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UEntityRelationStateComponent::HasStateForTarget(FEntityRelationId TargetId) const
{
	return TargetId.IsValid() && DirectedStates.Contains(TargetId);
}

bool UEntityRelationStateComponent::GetStateForTarget(FEntityRelationId TargetId, FEntityDirectedRelationState& OutState) const
{
	if (const FEntityDirectedRelationState* State = DirectedStates.Find(TargetId))
	{
		OutState = *State;
		return true;
	}
	OutState = FEntityDirectedRelationState();
	return false;
}

int64 UEntityRelationStateComponent::GetRevisionForTarget(FEntityRelationId TargetId) const
{
	const FEntityDirectedRelationState* State = DirectedStates.Find(TargetId);
	return State ? State->Revision : 0;
}

FEntityRelationStateMutationResult UEntityRelationStateComponent::MakeFailure(
	EEntityRelationStateMutationStatus Status,
	FEntityRelationId TargetId,
	FString Message) const
{
	FEntityRelationStateMutationResult Result;
	Result.Status = Status;
	Result.TargetId = TargetId;
	Result.Message = MoveTemp(Message);
	if (const AActor* Owner = GetOwner())
	{
		if (const UEntityIdentityComponent* Identity = Owner->FindComponentByClass<UEntityIdentityComponent>())
		{
			Result.SourceId = Identity->GetEntityId();
		}
	}
	return Result;
}

bool UEntityRelationStateComponent::ResolveMutationAuthority(
	FEntityRelationId TargetId,
	UEntityIdentityComponent*& OutIdentity,
	UEntityRelationsWorldSubsystem*& OutSubsystem,
	FEntityRelationStateMutationResult& OutFailure) const
{
	OutIdentity = nullptr;
	OutSubsystem = nullptr;
	if (!IsInGameThread())
	{
		OutFailure = MakeFailure(EEntityRelationStateMutationStatus::WrongThread, TargetId, TEXT("Directed state may only be changed on the Game Thread."));
		return false;
	}
	if (!TargetId.IsValid())
	{
		OutFailure = MakeFailure(EEntityRelationStateMutationStatus::InvalidTarget, TargetId, TEXT("TargetId is invalid."));
		return false;
	}
	AActor* Owner = GetOwner();
	OutIdentity = Owner ? Owner->FindComponentByClass<UEntityIdentityComponent>() : nullptr;
	if (!OutIdentity || !OutIdentity->GetEntityId().IsValid())
	{
		OutFailure = MakeFailure(EEntityRelationStateMutationStatus::InvalidSource, TargetId, TEXT("The owning Actor has no valid Entity Identity Component."));
		return false;
	}
	UWorld* World = GetWorld();
	OutSubsystem = World ? World->GetSubsystem<UEntityRelationsWorldSubsystem>() : nullptr;
	if (!OutSubsystem || !OutSubsystem->IsEntityRegistered(OutIdentity))
	{
		OutFailure = MakeFailure(EEntityRelationStateMutationStatus::SourceNotRegistered, TargetId, TEXT("The Source identity is not registered."));
		return false;
	}
	return true;
}

FEntityRelationStateMutationResult UEntityRelationStateComponent::CommitChange(FEntityRelationId TargetId, bool bEntryRemoved)
{
	FEntityDirectedRelationState* State = DirectedStates.Find(TargetId);
	int64 NewRevision = 1;
	if (State)
	{
		NewRevision = ++State->Revision;
		if (State->IsEmpty())
		{
			DirectedStates.Remove(TargetId);
			bEntryRemoved = true;
		}
	}

	UEntityIdentityComponent* Identity = GetOwner() ? GetOwner()->FindComponentByClass<UEntityIdentityComponent>() : nullptr;
	UEntityRelationsWorldSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UEntityRelationsWorldSubsystem>() : nullptr;
	FEntityRelationStateMutationResult Result;
	Result.Status = EEntityRelationStateMutationStatus::Changed;
	Result.SourceId = Identity ? Identity->GetEntityId() : FEntityRelationId();
	Result.TargetId = TargetId;
	Result.Revision = NewRevision;
	if (Subsystem)
	{
		Subsystem->NotifyDirectedStateChanged(this, TargetId, bEntryRemoved);
	}
	OnDirectedRelationStateChanged.Broadcast(Result);
	return Result;
}

FEntityRelationStateMutationResult UEntityRelationStateComponent::SetStateTagsForTarget(
	FEntityRelationId TargetId,
	const FGameplayTagContainer& NewTags)
{
	UEntityIdentityComponent* Identity;
	UEntityRelationsWorldSubsystem* Subsystem;
	FEntityRelationStateMutationResult Failure;
	if (!ResolveMutationAuthority(TargetId, Identity, Subsystem, Failure))
	{
		return Failure;
	}
	if (const FEntityDirectedRelationState* Existing = DirectedStates.Find(TargetId); Existing && Existing->StateTags == NewTags)
	{
		return MakeFailure(EEntityRelationStateMutationStatus::Unchanged, TargetId, TEXT("State tags are already equal."));
	}
	if (!DirectedStates.Contains(TargetId) && NewTags.IsEmpty())
	{
		return MakeFailure(EEntityRelationStateMutationStatus::Unchanged, TargetId, TEXT("No directed state exists and the requested tags are empty."));
	}
	FEntityDirectedRelationState& State = DirectedStates.FindOrAdd(TargetId);
	State.StateTags = NewTags;
	return CommitChange(TargetId, NewTags.IsEmpty() && State.NumericValues.IsEmpty());
}

FEntityRelationStateMutationResult UEntityRelationStateComponent::AddStateTagForTarget(FEntityRelationId TargetId, FGameplayTag Tag)
{
	if (!Tag.IsValid())
	{
		return MakeFailure(EEntityRelationStateMutationStatus::InvalidTag, TargetId, TEXT("State tag is invalid."));
	}
	UEntityIdentityComponent* Identity;
	UEntityRelationsWorldSubsystem* Subsystem;
	FEntityRelationStateMutationResult Failure;
	if (!ResolveMutationAuthority(TargetId, Identity, Subsystem, Failure))
	{
		return Failure;
	}
	FEntityDirectedRelationState& State = DirectedStates.FindOrAdd(TargetId);
	if (State.StateTags.HasTagExact(Tag))
	{
		return MakeFailure(EEntityRelationStateMutationStatus::Unchanged, TargetId, TEXT("State tag is already present."));
	}
	State.StateTags.AddTag(Tag);
	return CommitChange(TargetId, false);
}

FEntityRelationStateMutationResult UEntityRelationStateComponent::RemoveStateTagForTarget(FEntityRelationId TargetId, FGameplayTag Tag)
{
	if (!Tag.IsValid())
	{
		return MakeFailure(EEntityRelationStateMutationStatus::InvalidTag, TargetId, TEXT("State tag is invalid."));
	}
	UEntityIdentityComponent* Identity;
	UEntityRelationsWorldSubsystem* Subsystem;
	FEntityRelationStateMutationResult Failure;
	if (!ResolveMutationAuthority(TargetId, Identity, Subsystem, Failure))
	{
		return Failure;
	}
	FEntityDirectedRelationState* State = DirectedStates.Find(TargetId);
	if (!State)
	{
		return MakeFailure(EEntityRelationStateMutationStatus::StateNotFound, TargetId, TEXT("No directed state exists for TargetId."));
	}
	if (!State->StateTags.HasTagExact(Tag))
	{
		return MakeFailure(EEntityRelationStateMutationStatus::Unchanged, TargetId, TEXT("State tag is not present."));
	}
	State->StateTags.RemoveTag(Tag);
	return CommitChange(TargetId, State->IsEmpty());
}

FEntityRelationStateMutationResult UEntityRelationStateComponent::SetNumericValueForTarget(
	FEntityRelationId TargetId,
	FGameplayTag ValueTag,
	float Value)
{
	if (!ValueTag.IsValid())
	{
		return MakeFailure(EEntityRelationStateMutationStatus::InvalidTag, TargetId, TEXT("Numeric value tag is invalid."));
	}
	if (!FMath::IsFinite(Value))
	{
		return MakeFailure(EEntityRelationStateMutationStatus::InvalidValue, TargetId, TEXT("Numeric value must be finite."));
	}
	UEntityIdentityComponent* Identity;
	UEntityRelationsWorldSubsystem* Subsystem;
	FEntityRelationStateMutationResult Failure;
	if (!ResolveMutationAuthority(TargetId, Identity, Subsystem, Failure))
	{
		return Failure;
	}
	FEntityDirectedRelationState& State = DirectedStates.FindOrAdd(TargetId);
	if (const float* Existing = State.NumericValues.Find(ValueTag); Existing && *Existing == Value)
	{
		return MakeFailure(EEntityRelationStateMutationStatus::Unchanged, TargetId, TEXT("Numeric value is already equal."));
	}
	State.NumericValues.Add(ValueTag, Value);
	return CommitChange(TargetId, false);
}

FEntityRelationStateMutationResult UEntityRelationStateComponent::RemoveNumericValueForTarget(
	FEntityRelationId TargetId,
	FGameplayTag ValueTag)
{
	if (!ValueTag.IsValid())
	{
		return MakeFailure(EEntityRelationStateMutationStatus::InvalidTag, TargetId, TEXT("Numeric value tag is invalid."));
	}
	UEntityIdentityComponent* Identity;
	UEntityRelationsWorldSubsystem* Subsystem;
	FEntityRelationStateMutationResult Failure;
	if (!ResolveMutationAuthority(TargetId, Identity, Subsystem, Failure))
	{
		return Failure;
	}
	FEntityDirectedRelationState* State = DirectedStates.Find(TargetId);
	if (!State)
	{
		return MakeFailure(EEntityRelationStateMutationStatus::StateNotFound, TargetId, TEXT("No directed state exists for TargetId."));
	}
	if (State->NumericValues.Remove(ValueTag) == 0)
	{
		return MakeFailure(EEntityRelationStateMutationStatus::Unchanged, TargetId, TEXT("Numeric value is not present."));
	}
	return CommitChange(TargetId, State->IsEmpty());
}

FEntityRelationStateMutationResult UEntityRelationStateComponent::ClearStateForTarget(FEntityRelationId TargetId)
{
	UEntityIdentityComponent* Identity;
	UEntityRelationsWorldSubsystem* Subsystem;
	FEntityRelationStateMutationResult Failure;
	if (!ResolveMutationAuthority(TargetId, Identity, Subsystem, Failure))
	{
		return Failure;
	}
	FEntityDirectedRelationState* State = DirectedStates.Find(TargetId);
	if (!State)
	{
		return MakeFailure(EEntityRelationStateMutationStatus::StateNotFound, TargetId, TEXT("No directed state exists for TargetId."));
	}
	State->StateTags.Reset();
	State->NumericValues.Reset();
	return CommitChange(TargetId, true);
}

FEntityRelationStateMutationResult UEntityRelationStateComponent::ClearAllDirectedState()
{
	if (DirectedStates.IsEmpty())
	{
		return MakeFailure(EEntityRelationStateMutationStatus::Unchanged, FEntityRelationId(), TEXT("No directed state exists."));
	}
	const TArray<FEntityRelationId> Targets = [&]()
	{
		TArray<FEntityRelationId> Result;
		DirectedStates.GetKeys(Result);
		return Result;
	}();
	FEntityRelationStateMutationResult LastResult;
	for (const FEntityRelationId& TargetId : Targets)
	{
		LastResult = ClearStateForTarget(TargetId);
	}
	return LastResult;
}

void UEntityRelationStateComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UEntityRelationsWorldSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UEntityRelationsWorldSubsystem>() : nullptr)
	{
		Subsystem->RefreshStateComponent(this);
	}
}

void UEntityRelationStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UEntityRelationsWorldSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UEntityRelationsWorldSubsystem>() : nullptr)
	{
		Subsystem->UnregisterStateComponent(this);
	}
	Super::EndPlay(EndPlayReason);
}
