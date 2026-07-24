#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

class UWorld;

/** Internal boundary around Unreal's world pause and global time-dilation APIs. */
class ITacticalPauseTemporalDriver
{
public:
	virtual ~ITacticalPauseTemporalDriver() = default;
	/** Whether the bound world and world settings can currently accept requests. */
	virtual bool IsAvailable() const = 0;
	/** Reads Unreal's live gameplay-pause state. */
	virtual bool IsPaused() const = 0;
	/** Reads the live global time-dilation value. */
	virtual float GetGlobalTimeDilation() const = 0;
	/** Reads the world-configured engine ceiling for global time dilation. */
	virtual float GetMaximumGlobalTimeDilation() const = 0;
	/** Contributes a pause owner guarded by the supplied release delegate. */
	virtual bool AcquirePause(const FCanUnpause& CanUnpauseDelegate) = 0;
	/** Attempts to remove the pause contribution acquired by this driver. */
	virtual bool ReleasePause() = 0;
	/** Writes dilation and returns the value Unreal actually applied. */
	virtual float SetGlobalTimeDilation(float InMultiplier) = 0;
};

/** Creates the production driver bound weakly to one gameplay world. */
TUniquePtr<ITacticalPauseTemporalDriver> CreateTacticalPauseTemporalDriver(UWorld& World);
