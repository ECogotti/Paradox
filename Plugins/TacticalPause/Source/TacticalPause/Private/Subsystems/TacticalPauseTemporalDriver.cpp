#include "Subsystems/TacticalPauseTemporalDriver.h"

#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

namespace
{
	/** Production adapter; weak references prevent the private driver from extending UObject lifetimes. */
	class FUnrealTacticalPauseTemporalDriver final : public ITacticalPauseTemporalDriver
	{
	public:
		explicit FUnrealTacticalPauseTemporalDriver(UWorld& InWorld)
			: World(&InWorld)
		{
		}

		virtual bool IsAvailable() const override
		{
			return World.IsValid() && World->GetWorldSettings() != nullptr;
		}

		virtual bool IsPaused() const override
		{
			return World.IsValid() && World->IsPaused();
		}

		virtual float GetGlobalTimeDilation() const override
		{
			const AWorldSettings* WorldSettings = World.IsValid() ? World->GetWorldSettings() : nullptr;
			return WorldSettings ? WorldSettings->TimeDilation : 1.0f;
		}

		virtual float GetMaximumGlobalTimeDilation() const override
		{
			const AWorldSettings* WorldSettings = World.IsValid() ? World->GetWorldSettings() : nullptr;
			return WorldSettings ? WorldSettings->MaxGlobalTimeDilation : 1.0f;
		}

		virtual bool AcquirePause(const FCanUnpause& CanUnpauseDelegate) override
		{
			if (!World.IsValid() || World->IsPaused())
			{
				return false;
			}
			APlayerController* PlayerController = World->GetFirstPlayerController();
			if (!PlayerController)
			{
				return false;
			}
			// Passing the subsystem's guard lets Unreal preserve stacked external pause owners.
			const bool bApplied = PlayerController->SetPause(true, CanUnpauseDelegate);
			if (bApplied && World->IsPaused())
			{
				PauseController = PlayerController;
				return true;
			}
			return false;
		}

		virtual bool ReleasePause() override
		{
			if (!World.IsValid())
			{
				return false;
			}
			// Prefer the controller that acquired pause, but tolerate controller replacement.
			APlayerController* PlayerController = PauseController.Get();
			if (!PlayerController)
			{
				PlayerController = World->GetFirstPlayerController();
			}
			return PlayerController && PlayerController->SetPause(false);
		}

		virtual float SetGlobalTimeDilation(float InMultiplier) override
		{
			AWorldSettings* WorldSettings = World.IsValid() ? World->GetWorldSettings() : nullptr;
			return WorldSettings ? WorldSettings->SetTimeDilation(InMultiplier) : 1.0f;
		}

	private:
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<APlayerController> PauseController;
	};
}

TUniquePtr<ITacticalPauseTemporalDriver> CreateTacticalPauseTemporalDriver(UWorld& World)
{
	return MakeUnique<FUnrealTacticalPauseTemporalDriver>(World);
}
