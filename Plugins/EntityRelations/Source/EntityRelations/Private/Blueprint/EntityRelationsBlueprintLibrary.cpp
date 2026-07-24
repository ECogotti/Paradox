#include "Blueprint/EntityRelationsBlueprintLibrary.h"

#include "Components/EntityIdentityComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EntityRelationsWorldSubsystem.h"

UEntityIdentityComponent* UEntityRelationsBlueprintLibrary::GetEntityIdentityComponent(AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UEntityIdentityComponent>() : nullptr;
}

FEntityRelationId UEntityRelationsBlueprintLibrary::MakeEntityRelationId(FGuid Guid)
{
	return FEntityRelationId(Guid);
}

FEntityRelationId UEntityRelationsBlueprintLibrary::GenerateEntityRelationId()
{
	return FEntityRelationId::NewId();
}

FGuid UEntityRelationsBlueprintLibrary::GetEntityRelationGuid(FEntityRelationId EntityId)
{
	return EntityId.GetGuid();
}

FEntityRelationResult UEntityRelationsBlueprintLibrary::EvaluateRelation(
	UObject* WorldContextObject,
	AActor* Source,
	AActor* Target,
	const FEntityRelationQueryContext& Context,
	bool& bSuccess)
{
	FEntityRelationResult Result;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	UEntityRelationsWorldSubsystem* Subsystem = World ? World->GetSubsystem<UEntityRelationsWorldSubsystem>() : nullptr;
	if (!Subsystem)
	{
		Result.Status = EEntityRelationQueryStatus::EvaluationFailed;
		bSuccess = false;
		return Result;
	}
	Result = Subsystem->EvaluateRelationByActor(Source, Target, Context);
	bSuccess = Result.IsSuccess();
	return Result;
}

TArray<FEntityRelationResult> UEntityRelationsBlueprintLibrary::EvaluateRelationsFromSource(
	UObject* WorldContextObject,
	AActor* Source,
	const TArray<AActor*>& Targets,
	const FEntityRelationQueryContext& Context)
{
	TArray<FEntityRelationResult> Results;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	UEntityRelationsWorldSubsystem* Subsystem = World ? World->GetSubsystem<UEntityRelationsWorldSubsystem>() : nullptr;
	UEntityIdentityComponent* SourceIdentity = GetEntityIdentityComponent(Source);
	if (!Subsystem || !SourceIdentity)
	{
		Results.SetNum(Targets.Num());
		for (FEntityRelationResult& Result : Results)
		{
			Result.Status = Subsystem ? EEntityRelationQueryStatus::InvalidSource : EEntityRelationQueryStatus::EvaluationFailed;
		}
		return Results;
	}

	TArray<FEntityRelationId> TargetIds;
	TargetIds.Reserve(Targets.Num());
	for (AActor* Target : Targets)
	{
		const UEntityIdentityComponent* Identity = GetEntityIdentityComponent(Target);
		TargetIds.Add(Identity ? Identity->GetEntityId() : FEntityRelationId());
	}
	return Subsystem->EvaluateRelationsFromSource(SourceIdentity->GetEntityId(), TargetIds, Context);
}

bool UEntityRelationsBlueprintLibrary::HasClassificationTag(
	const FEntityRelationResult& Result,
	FGameplayTag Tag,
	bool bExactMatch)
{
	return Tag.IsValid() && (bExactMatch ? Result.ClassificationTags.HasTagExact(Tag) : Result.ClassificationTags.HasTag(Tag));
}

bool UEntityRelationsBlueprintLibrary::HasOutcomeTag(
	const FEntityRelationResult& Result,
	FGameplayTag Tag,
	bool bExactMatch)
{
	return Tag.IsValid() && (bExactMatch ? Result.OutcomeTags.HasTagExact(Tag) : Result.OutcomeTags.HasTag(Tag));
}

FText UEntityRelationsBlueprintLibrary::QueryStatusToText(EEntityRelationQueryStatus Status)
{
	return StaticEnum<EEntityRelationQueryStatus>()->GetDisplayNameTextByValue(static_cast<int64>(Status));
}

FText UEntityRelationsBlueprintLibrary::DecisionToText(EEntityRelationDecision Decision)
{
	return StaticEnum<EEntityRelationDecision>()->GetDisplayNameTextByValue(static_cast<int64>(Decision));
}
