#include "EntityRelations.h"

#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Settings/EntityRelationsDeveloperSettings.h"
#include "Subsystems/EntityRelationsWorldSubsystem.h"

DEFINE_LOG_CATEGORY(LogEntityRelations);

namespace
{
	TAutoConsoleVariable<int32> CVarEntityRelationsDebug(
		TEXT("EntityRelations.Debug"),
		-1,
		TEXT("Entity Relations global debug override. -1 uses project settings, 0 disables, 1 enables."),
		ECVF_Default);

	UEntityRelationsWorldSubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UEntityRelationsWorldSubsystem>() : nullptr;
	}

	bool ParseEntityId(const FString& Text, FEntityRelationId& OutId)
	{
		FGuid Guid;
		if (!FGuid::Parse(Text, Guid))
		{
			return false;
		}
		OutId = FEntityRelationId(Guid);
		return OutId.IsValid();
	}

	void ListEntities(UWorld* World)
	{
		if (UEntityRelationsWorldSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->DumpRegistryToLog();
		}
	}

	void DumpEntity(const TArray<FString>& Args, UWorld* World)
	{
		FEntityRelationId EntityId;
		if (Args.Num() != 1 || !ParseEntityId(Args[0], EntityId))
		{
			ENTITYRELATIONS_LOG_WARNING("Usage: EntityRelations.Dump <EntityId>");
			return;
		}
		if (UEntityRelationsWorldSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->DumpEntityToLog(EntityId);
		}
	}

	void ExplainRelation(const TArray<FString>& Args, UWorld* World)
	{
		FEntityRelationId SourceId;
		FEntityRelationId TargetId;
		if (Args.Num() != 3 || !ParseEntityId(Args[0], SourceId) || !ParseEntityId(Args[1], TargetId))
		{
			ENTITYRELATIONS_LOG_WARNING("Usage: EntityRelations.Explain <SourceId> <TargetId> <Domain>");
			return;
		}
		const FGameplayTag Domain = FGameplayTag::RequestGameplayTag(FName(*Args[2]), false);
		if (!Domain.IsValid())
		{
			ENTITYRELATIONS_LOG_WARNING("EntityRelations.Explain received unknown domain '%s'.", *Args[2]);
			return;
		}
		if (UEntityRelationsWorldSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->ExplainRelationToLog(SourceId, TargetId, Domain);
		}
	}

	void ClearCache(UWorld* World)
	{
		if (UEntityRelationsWorldSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->ClearCache();
			ENTITYRELATIONS_LOG_INFO("Entity Relations cache cleared for World %s.", *GetNameSafe(World));
		}
	}

	void DumpCacheStats(UWorld* World)
	{
		if (const UEntityRelationsWorldSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->DumpCacheStatsToLog();
		}
	}
}

bool IsEntityRelationsGlobalDebugEnabled()
{
	const int32 Override = CVarEntityRelationsDebug.GetValueOnGameThread();
	if (Override >= 0)
	{
		return Override != 0;
	}
	const UEntityRelationsDeveloperSettings* Settings = GetDefault<UEntityRelationsDeveloperSettings>();
	return Settings && Settings->bEnableGlobalDebug;
}

void FEntityRelationsModule::StartupModule()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("EntityRelations.List"),
		TEXT("Lists registered Entity Relations identities in the current World."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&ListEntities)));
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("EntityRelations.Dump"),
		TEXT("EntityRelations.Dump <EntityId>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpEntity)));
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("EntityRelations.Explain"),
		TEXT("EntityRelations.Explain <SourceId> <TargetId> <Domain>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExplainRelation)));
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("EntityRelations.ClearCache"),
		TEXT("Clears the current World's Entity Relations query cache."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&ClearCache)));
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("EntityRelations.CacheStats"),
		TEXT("Logs current Entity Relations cache and query counters."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&DumpCacheStats)));
}

void FEntityRelationsModule::ShutdownModule()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	for (IConsoleObject* ConsoleCommand : ConsoleCommands)
	{
		if (ConsoleCommand)
		{
			ConsoleManager.UnregisterConsoleObject(ConsoleCommand, false);
		}
	}
	ConsoleCommands.Reset();
}

IMPLEMENT_MODULE(FEntityRelationsModule, EntityRelations)
