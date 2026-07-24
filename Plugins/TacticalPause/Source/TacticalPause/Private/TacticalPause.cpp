#include "TacticalPause.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/TacticalPauseWorldSubsystem.h"

DEFINE_LOG_CATEGORY(LogTacticalPause);

namespace
{
	UTacticalPauseWorldSubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UTacticalPauseWorldSubsystem>() : nullptr;
	}

	void LogStatus(UWorld* World)
	{
		const UTacticalPauseWorldSubsystem* Subsystem = GetSubsystem(World);
		if (!Subsystem)
		{
			TACTICALPAUSE_LOG_WARNING("TacticalPause.Status could not find a subsystem for World %s.", *GetNameSafe(World));
			return;
		}
		TACTICALPAUSE_LOG_INFO("Status World=%s State=%d Selected=%.3f Applied=%.3f SelectedPreset=%s Presets=%d.",
			*GetNameSafe(World),
			static_cast<int32>(Subsystem->GetPlaybackState()),
			Subsystem->GetSelectedPlaybackSpeed(),
			Subsystem->GetAppliedPlaybackSpeed(),
			*Subsystem->GetSelectedPresetId().ToString(),
			Subsystem->GetAvailablePresets().Num());
	}

	void Pause(UWorld* World)
	{
		if (UTacticalPauseWorldSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->RequestPause();
		}
	}

	void Play(UWorld* World)
	{
		if (UTacticalPauseWorldSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->RequestPlay();
		}
	}

	void SetSpeed(const TArray<FString>& Args, UWorld* World)
	{
		float Multiplier = 0.0f;
		if (Args.Num() != 1 || !Args[0].IsNumeric())
		{
			TACTICALPAUSE_LOG_WARNING("Usage: TacticalPause.SetSpeed <Multiplier>");
			return;
		}
		Multiplier = FCString::Atof(*Args[0]);
		if (UTacticalPauseWorldSubsystem* Subsystem = GetSubsystem(World))
		{
			Subsystem->SetPlaybackSpeed(Multiplier);
		}
	}
}

void FTacticalPauseModule::StartupModule()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("TacticalPause.Status"),
		TEXT("Logs the authoritative Tactical Pause state for the current World."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&LogStatus)));
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("TacticalPause.Pause"),
		TEXT("Requests Tactical Pause for the current World."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Pause)));
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("TacticalPause.Play"),
		TEXT("Requests Tactical Play for the current World."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Play)));
	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("TacticalPause.SetSpeed"),
		TEXT("TacticalPause.SetSpeed <Multiplier>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetSpeed)));
}

void FTacticalPauseModule::ShutdownModule()
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

IMPLEMENT_MODULE(FTacticalPauseModule, TacticalPause)
