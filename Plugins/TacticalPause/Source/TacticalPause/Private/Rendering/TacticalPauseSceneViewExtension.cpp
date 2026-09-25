#include "Rendering/TacticalPauseSceneViewExtension.h"

#include "SceneView.h"

FTacticalPauseSceneViewExtension::FTacticalPauseSceneViewExtension(
	const FAutoRegister& AutoRegister,
	UWorld* InWorld)
	: FWorldSceneViewExtension(AutoRegister, InWorld)
{
}

void FTacticalPauseSceneViewExtension::SetTemporalRenderingOverrideActive(const bool bActive)
{
	bTemporalRenderingOverrideActive.Store(bActive);
}

bool FTacticalPauseSceneViewExtension::IsTemporalRenderingOverrideActive() const
{
	return bTemporalRenderingOverrideActive.Load();
}

void FTacticalPauseSceneViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	if (bTemporalRenderingOverrideActive.Load() && InViewFamily.bWorldIsPaused)
	{
		// Only the renderer sees an unpaused view. The World, gameplay timers, physics,
		// animation, and AI continue to obey Unreal's authoritative pause state.
		InViewFamily.bWorldIsPaused = false;
	}
}

bool FTacticalPauseSceneViewExtension::IsActiveThisFrame_Internal(
	const FSceneViewExtensionContext& Context) const
{
	return bTemporalRenderingOverrideActive.Load()
		&& FWorldSceneViewExtension::IsActiveThisFrame_Internal(Context);
}
