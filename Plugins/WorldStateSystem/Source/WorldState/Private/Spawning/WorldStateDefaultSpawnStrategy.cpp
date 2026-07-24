#include "Spawning/WorldStateDefaultSpawnStrategy.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Finds an already loaded level by captured package identity; spawning never streams or loads a level. */
	ULevel* FindCapturedLevel(const UWorld& World, FName PackageName)
	{
		for (ULevel* Level : World.GetLevels())
		{
			if (Level && Level->GetPackage()->GetFName() == PackageName)
			{
				return Level;
			}
		}
		return nullptr;
	}
}

bool FWorldStateDefaultSpawnStrategy::CanSpawn(
	const UWorld& World,
	const FWorldStateSpawnDescriptor& Descriptor,
	FString& OutError) const
{
	if (!Descriptor.bWasRuntimeSpawned)
	{
		// Level-authored reconstruction needs project-specific streaming/content ownership knowledge.
		OutError = TEXT("The default strategy only recreates Actors that were runtime-spawned at capture time.");
		return false;
	}
	if (!Descriptor.ActorClass.Get())
	{
		OutError = TEXT("The captured Actor class is not currently loaded; World State does not load it synchronously.");
		return false;
	}
	if (Descriptor.ActorName.IsNone() || !FindCapturedLevel(World, Descriptor.LevelPackageName))
	{
		OutError = TEXT("The captured Actor name or level context is unavailable.");
		return false;
	}
	return true;
}

AActor* FWorldStateDefaultSpawnStrategy::Spawn(
	UWorld& World,
	const FWorldStateSpawnDescriptor& Descriptor,
	FString& OutError) const
{
	if (!CanSpawn(World, Descriptor, OutError))
	{
		return nullptr;
	}

	// Exact name and level are required so the resulting UObject path matches the captured identity.
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = Descriptor.ActorName;
	SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;
	SpawnParameters.OverrideLevel = FindCapturedLevel(World, Descriptor.LevelPackageName);
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.bDeferConstruction = true;

	// Deferred construction lets the subsystem expose the pending participant identity before BeginPlay registers it.
	AActor* Actor = World.SpawnActor<AActor>(Descriptor.ActorClass.Get(), Descriptor.Transform, SpawnParameters);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Failed to reserve the captured Actor path %s."), *Descriptor.CapturedObjectPath.ToString());
		return nullptr;
	}
	Actor->FinishSpawning(Descriptor.Transform);
	return Actor;
}
