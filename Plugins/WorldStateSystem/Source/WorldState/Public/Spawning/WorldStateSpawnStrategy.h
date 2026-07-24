#pragma once

#include "CoreMinimal.h"
#include "Types/WorldStateTypes.h"

class AActor;
class UWorld;

/** Data required to conservatively recreate one missing participant Actor. */
struct WORLDSTATE_API FWorldStateSpawnDescriptor
{
	/** Identity the spawned participant must adopt before its BeginPlay registration. */
	FWorldStateParticipantId ParticipantId;
	/** Captured Actor class; default spawning requires it to be already loaded. */
	TSoftClassPtr<AActor> ActorClass;
	/** Original Actor object path used to verify an exact reconstruction. */
	FSoftObjectPath CapturedObjectPath;
	/** Required UObject name for deterministic path reconstruction. */
	FName ActorName;
	/** Captured owning level package; strategies must not silently move Actors between levels. */
	FName LevelPackageName;
	/** Initial world transform used by deferred spawning. */
	FTransform Transform = FTransform::Identity;
	/** Native strategy registry key captured with the participant. */
	FName StrategyId = TEXT("WorldState.DefaultActor");
	/** True only for runtime-created Actors eligible for the conservative default strategy. */
	bool bWasRuntimeSpawned = false;
};

/** Replaceable C++ contract for participant recreation. Implementations must return a fully spawned Actor or null with a diagnostic. */
class WORLDSTATE_API IWorldStateSpawnStrategy
{
public:
	virtual ~IWorldStateSpawnStrategy() = default;

	/**
	 * Performs non-mutating preflight validation without loading classes or levels.
	 * @return True only when Spawn can reconstruct the descriptor in the current world.
	 */
	virtual bool CanSpawn(const UWorld& World, const FWorldStateSpawnDescriptor& Descriptor, FString& OutError) const = 0;
	/**
	 * Recreates the Actor synchronously on the Game Thread.
	 * @return A fully finished Actor with the exact captured path, or nullptr with OutError populated.
	 */
	virtual AActor* Spawn(UWorld& World, const FWorldStateSpawnDescriptor& Descriptor, FString& OutError) const = 0;
};
