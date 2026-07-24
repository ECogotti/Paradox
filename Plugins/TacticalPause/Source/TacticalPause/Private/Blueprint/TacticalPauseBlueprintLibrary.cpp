#include "Blueprint/TacticalPauseBlueprintLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Subsystems/TacticalPauseWorldSubsystem.h"

UTacticalPauseWorldSubsystem* UTacticalPauseBlueprintLibrary::GetTacticalPauseSubsystem(const UObject* WorldContextObject)
{
	// ReturnNull keeps Blueprint convenience queries predictable for invalid contexts.
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UTacticalPauseWorldSubsystem>() : nullptr;
}

ETacticalPauseRequestResult UTacticalPauseBlueprintLibrary::PauseSimulation(const UObject* WorldContextObject)
{
	UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->RequestPause() : ETacticalPauseRequestResult::InvalidWorld;
}

ETacticalPauseRequestResult UTacticalPauseBlueprintLibrary::PlaySimulation(const UObject* WorldContextObject)
{
	UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->RequestPlay() : ETacticalPauseRequestResult::InvalidWorld;
}

ETacticalPauseRequestResult UTacticalPauseBlueprintLibrary::ToggleSimulationPause(const UObject* WorldContextObject)
{
	UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->TogglePause() : ETacticalPauseRequestResult::InvalidWorld;
}

ETacticalPauseRequestResult UTacticalPauseBlueprintLibrary::SetSimulationPlaybackSpeed(const UObject* WorldContextObject, float Multiplier)
{
	UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->SetPlaybackSpeed(Multiplier) : ETacticalPauseRequestResult::InvalidWorld;
}

ETacticalPauseRequestResult UTacticalPauseBlueprintLibrary::SetSimulationPlaybackPreset(const UObject* WorldContextObject, FName PresetId)
{
	UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->SetPlaybackPreset(PresetId) : ETacticalPauseRequestResult::InvalidWorld;
}

bool UTacticalPauseBlueprintLibrary::IsSimulationPaused(const UObject* WorldContextObject)
{
	const UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem(WorldContextObject);
	return Subsystem && Subsystem->IsPaused();
}

float UTacticalPauseBlueprintLibrary::GetSelectedPlaybackSpeed(const UObject* WorldContextObject)
{
	const UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->GetSelectedPlaybackSpeed() : 1.0f;
}

float UTacticalPauseBlueprintLibrary::GetAppliedPlaybackSpeed(const UObject* WorldContextObject)
{
	const UTacticalPauseWorldSubsystem* Subsystem = GetTacticalPauseSubsystem(WorldContextObject);
	return Subsystem ? Subsystem->GetAppliedPlaybackSpeed() : 1.0f;
}
