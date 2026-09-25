#pragma once

#include "SceneViewExtension.h"

/**
 * Keeps temporal rendering coherent for a movable camera while Unreal gameplay remains paused.
 * The owning world subsystem controls activation according to Tactical Pause ownership.
 */
class FTacticalPauseSceneViewExtension final : public FWorldSceneViewExtension
{
public:
	FTacticalPauseSceneViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld);

	void SetTemporalRenderingOverrideActive(bool bActive);
	bool IsTemporalRenderingOverrideActive() const;

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
	TAtomic<bool> bTemporalRenderingOverrideActive = false;
};
