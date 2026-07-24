#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/TacticalPauseTypes.h"
#include "TacticalPauseBlueprintLibrary.generated.h"

class UTacticalPauseWorldSubsystem;

/** Blueprint convenience nodes that forward exclusively to the authoritative world subsystem. */
UCLASS()
class TACTICALPAUSE_API UTacticalPauseBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Resolves the authoritative subsystem from a Game or PIE world context. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Tactical Pause Subsystem"))
	static UTacticalPauseWorldSubsystem* GetTacticalPauseSubsystem(const UObject* WorldContextObject);

	/** Requests gameplay pause and returns an explicit result instead of hiding failure. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause", meta = (WorldContext = "WorldContextObject"))
	static ETacticalPauseRequestResult PauseSimulation(const UObject* WorldContextObject);

	/** Requests resume at the selected speed when the plugin owns pause. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause", meta = (WorldContext = "WorldContextObject"))
	static ETacticalPauseRequestResult PlaySimulation(const UObject* WorldContextObject);

	/** Toggles through the same validated subsystem command path. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause", meta = (WorldContext = "WorldContextObject"))
	static ETacticalPauseRequestResult ToggleSimulationPause(const UObject* WorldContextObject);

	/** Selects a custom positive multiplier and applies it immediately when playing. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause", meta = (WorldContext = "WorldContextObject"))
	static ETacticalPauseRequestResult SetSimulationPlaybackSpeed(const UObject* WorldContextObject, float Multiplier);

	/** Selects a configured preset by stable ID. */
	UFUNCTION(BlueprintCallable, Category = "Tactical Pause", meta = (WorldContext = "WorldContextObject"))
	static ETacticalPauseRequestResult SetSimulationPlaybackPreset(const UObject* WorldContextObject, FName PresetId);

	/** Reads whether simulation is effectively paused from the authoritative subsystem. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause", meta = (WorldContext = "WorldContextObject"))
	static bool IsSimulationPaused(const UObject* WorldContextObject);

	/** Returns the multiplier used for play or the next resume. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause", meta = (WorldContext = "WorldContextObject"))
	static float GetSelectedPlaybackSpeed(const UObject* WorldContextObject);

	/** Returns the live effective multiplier, or zero while paused. */
	UFUNCTION(BlueprintPure, Category = "Tactical Pause", meta = (WorldContext = "WorldContextObject"))
	static float GetAppliedPlaybackSpeed(const UObject* WorldContextObject);
};
