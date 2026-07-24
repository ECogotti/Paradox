#pragma once

#include "Spawning/WorldStateSpawnStrategy.h"

/** Conservative native strategy that only reconstructs compatible runtime-spawned Actors. */
class FWorldStateDefaultSpawnStrategy final : public IWorldStateSpawnStrategy
{
public:
	/** Rejects level-authored Actors, unloaded classes/levels and incomplete deterministic identity. */
	virtual bool CanSpawn(const UWorld& World, const FWorldStateSpawnDescriptor& Descriptor, FString& OutError) const override;
	/** Uses deferred spawning so the subsystem can stage identity before construction and BeginPlay. */
	virtual AActor* Spawn(UWorld& World, const FWorldStateSpawnDescriptor& Descriptor, FString& OutError) const override;
};
