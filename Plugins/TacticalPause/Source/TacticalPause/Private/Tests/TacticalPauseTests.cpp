#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "CommonActivatableWidget.h"
#include "CommonButtonBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameTime.h"
#include "Rendering/TacticalPauseSceneViewExtension.h"
#include "SceneView.h"
#include "Settings/TacticalPauseSettings.h"
#include "ShowFlags.h"
#include "Subsystems/TacticalPauseTemporalDriver.h"
#include "Subsystems/TacticalPauseWorldSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"
#include "Widgets/TacticalPauseControlsWidget.h"

#include <limits>

/** Deterministic driver used to test ownership and rollback without real player state. */
class FFakeTacticalPauseTemporalDriver final : public ITacticalPauseTemporalDriver
{
public:
	virtual bool IsAvailable() const override { return bAvailable; }
	virtual bool IsPaused() const override { return bPluginPause || bExternalPause; }
	virtual float GetGlobalTimeDilation() const override { return GlobalTimeDilation; }
	virtual float GetMaximumGlobalTimeDilation() const override { return MaximumGlobalTimeDilation; }

	virtual bool AcquirePause(const FCanUnpause& CanUnpauseDelegate) override
	{
		if (!bAvailable || bRejectPauseAcquisition || IsPaused())
		{
			return false;
		}
		bPluginPause = true;
		StoredCanUnpause = CanUnpauseDelegate;
		return true;
	}

	virtual bool ReleasePause() override
	{
		if (!bAvailable || !bPluginPause || !StoredCanUnpause.IsBound())
		{
			return false;
		}
		if (!StoredCanUnpause.Execute())
		{
			return false;
		}
		bPluginPause = false;
		StoredCanUnpause.Unbind();
		return true;
	}

	virtual float SetGlobalTimeDilation(float InMultiplier) override
	{
		if (!bAvailable || bRejectDilationWrites)
		{
			return GlobalTimeDilation;
		}
		GlobalTimeDilation = FMath::Clamp(InMultiplier, 0.0001f, MaximumGlobalTimeDilation);
		return GlobalTimeDilation;
	}

	void SetExternalPause(bool bPaused) { bExternalPause = bPaused; }
	void SetExternalDilation(float InMultiplier) { GlobalTimeDilation = InMultiplier; }

	bool bAvailable = true;
	bool bPluginPause = false;
	bool bExternalPause = false;
	bool bRejectPauseAcquisition = false;
	bool bRejectDilationWrites = false;
	float GlobalTimeDilation = 1.0f;
	float MaximumGlobalTimeDilation = 10.0f;
	FCanUnpause StoredCanUnpause;
};

/** Narrow friend accessor for injecting the driver and inspecting private test seams. */
struct FTacticalPauseTestAccessor
{
	static FFakeTacticalPauseTemporalDriver* InstallDriver(UTacticalPauseWorldSubsystem& Subsystem, TUniquePtr<FFakeTacticalPauseTemporalDriver> Driver)
	{
		delete Subsystem.TemporalDriver;
		FFakeTacticalPauseTemporalDriver* RawDriver = Driver.Release();
		Subsystem.TemporalDriver = RawDriver;
		Subsystem.PlaybackState = RawDriver->IsPaused() ? ETacticalPlaybackState::Paused : ETacticalPlaybackState::Playing;
		Subsystem.AppliedPlaybackMultiplier = RawDriver->IsPaused() ? 0.0f : RawDriver->GetGlobalTimeDilation();
		Subsystem.bShuttingDown = false;
		Subsystem.bTemporalStateRestored = false;
		Subsystem.bPauseOwnedByPlugin = false;
		Subsystem.bDilationOwnedByPlugin = false;
		Subsystem.bHasDilationSnapshot = false;
		Subsystem.SetTemporalRenderingOverrideActive(false);
		return RawDriver;
	}

	static void RebuildPresets(UTacticalPauseWorldSubsystem& Subsystem)
	{
		Subsystem.BuildValidatedPresets();
	}

	static void ForceState(UTacticalPauseWorldSubsystem& Subsystem, ETacticalPlaybackState State)
	{
		Subsystem.PlaybackState = State;
	}

	static void Restore(UTacticalPauseWorldSubsystem& Subsystem)
	{
		Subsystem.RestoreTemporalState();
	}

	static bool IsDilationOwned(const UTacticalPauseWorldSubsystem& Subsystem)
	{
		return Subsystem.bDilationOwnedByPlugin;
	}

	static bool HasSceneViewExtension(const UTacticalPauseWorldSubsystem& Subsystem)
	{
		return Subsystem.SceneViewExtension.IsValid();
	}

	static void ApplyTemporalRenderingOverride(
		UTacticalPauseWorldSubsystem& Subsystem,
		FSceneViewFamily& ViewFamily)
	{
		if (Subsystem.SceneViewExtension.IsValid())
		{
			Subsystem.SceneViewExtension->SetupViewFamily(ViewFamily);
		}
	}

	static ETacticalPauseRequestResult SelectPresetSlot(UTacticalPauseControlsWidget& Widget, int32 SlotIndex)
	{
		return Widget.SelectPlaybackPresetSlot(SlotIndex);
	}

	static bool HasCommonButtonBinding(const TCHAR* PropertyName)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UTacticalPauseControlsWidget::StaticClass(), PropertyName);
		const bool bIsCommonButton = Property
			&& Property->PropertyClass
			&& Property->PropertyClass->IsChildOf(UCommonButtonBase::StaticClass());
#if WITH_EDITOR
		return bIsCommonButton && Property->HasMetaData(TEXT("BindWidget"));
#else
		return bIsCommonButton;
#endif
	}
};

namespace UE::TacticalPause::Tests
{
	struct FScopedTestWorld
	{
		/** Creates an isolated Game world so each test receives a fresh world subsystem. */
		explicit FScopedTestWorld(const TCHAR* Name)
		{
			FWorldContext* Context = GEngine ? &GEngine->CreateNewWorldContext(EWorldType::Game) : nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(true);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World->RemoveFromRoot();
			}
		}

		UTacticalPauseWorldSubsystem* GetSubsystem() const
		{
			return World ? World->GetSubsystem<UTacticalPauseWorldSubsystem>() : nullptr;
		}

		UWorld* World = nullptr;
	};

	struct FScopedSettingsOverride
	{
		/** Preserves mutable CDO settings and restores them after each focused test. */
		FScopedSettingsOverride()
		{
			Settings = GetMutableDefault<UTacticalPauseSettings>();
			if (Settings)
			{
				OriginalPresets = Settings->SpeedPresets;
				OriginalDefaultPresetId = Settings->DefaultPresetId;
				OriginalMaximum = Settings->MaximumAllowedMultiplier;
				bOriginalAllowPausedSelection = Settings->bAllowSpeedSelectionWhilePaused;
				bOriginalKeepTemporalRenderingActive = Settings->bKeepTemporalRenderingActiveWhilePaused;
			}
		}

		~FScopedSettingsOverride()
		{
			if (Settings)
			{
				Settings->SpeedPresets = OriginalPresets;
				Settings->DefaultPresetId = OriginalDefaultPresetId;
				Settings->MaximumAllowedMultiplier = OriginalMaximum;
				Settings->bAllowSpeedSelectionWhilePaused = bOriginalAllowPausedSelection;
				Settings->bKeepTemporalRenderingActiveWhilePaused = bOriginalKeepTemporalRenderingActive;
			}
		}

		UTacticalPauseSettings* Settings = nullptr;
		TArray<FTacticalPlaybackSpeedPreset> OriginalPresets;
		FName OriginalDefaultPresetId;
		float OriginalMaximum = 3.0f;
		bool bOriginalAllowPausedSelection = true;
		bool bOriginalKeepTemporalRenderingActive = true;
	};

	FTacticalPlaybackSpeedPreset MakePreset(FName Id, float Multiplier, int32 SortOrder)
	{
		FTacticalPlaybackSpeedPreset Preset;
		Preset.Id = Id;
		Preset.DisplayName = FText::FromName(Id);
		Preset.Multiplier = Multiplier;
		Preset.SortOrder = SortOrder;
		return Preset;
	}

	FFakeTacticalPauseTemporalDriver* InstallDefaultDriver(UTacticalPauseWorldSubsystem& Subsystem)
	{
		return FTacticalPauseTestAccessor::InstallDriver(Subsystem, MakeUnique<FFakeTacticalPauseTemporalDriver>());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalPauseStateTransitionsTest,
	"TacticalPause.Runtime.State.TransitionsIdempotencyAndReentry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTacticalPauseStateTransitionsTest::RunTest(const FString& Parameters)
{
	using namespace UE::TacticalPause::Tests;
	FScopedTestWorld Scope(TEXT("TacticalPauseStateWorld"));
	UTacticalPauseWorldSubsystem* Subsystem = Scope.GetSubsystem();
	TestNotNull(TEXT("world subsystem exists"), Subsystem);
	if (!Subsystem)
	{
		return false;
	}
	FFakeTacticalPauseTemporalDriver* Driver = InstallDefaultDriver(*Subsystem);
	int32 PausedEvents = 0;
	int32 ResumedEvents = 0;
	Subsystem->OnPausedNative().AddLambda([&](const FTacticalPauseStateChange&) { ++PausedEvents; });
	Subsystem->OnResumedNative().AddLambda([&](const FTacticalPauseStateChange&) { ++ResumedEvents; });

	TestEqual(TEXT("playing transitions to paused"), Subsystem->RequestPause(), ETacticalPauseRequestResult::Succeeded);
	TestTrue(TEXT("driver is paused"), Driver->IsPaused());
	TestEqual(TEXT("applied speed is stopped while paused"), Subsystem->GetAppliedPlaybackSpeed(), 0.0f);
	TestEqual(TEXT("repeated pause is idempotent"), Subsystem->RequestPause(), ETacticalPauseRequestResult::AlreadyInRequestedState);
	TestEqual(TEXT("speed selection while paused succeeds"), Subsystem->SetPlaybackPreset(TEXT("Faster")), ETacticalPauseRequestResult::Succeeded);
	TestEqual(TEXT("paused selection does not alter world dilation"), Driver->GetGlobalTimeDilation(), 1.0f);
	TestEqual(TEXT("play resumes at selected speed"), Subsystem->RequestPlay(), ETacticalPauseRequestResult::Succeeded);
	TestFalse(TEXT("driver resumes"), Driver->IsPaused());
	TestEqual(TEXT("selected speed is applied"), Subsystem->GetAppliedPlaybackSpeed(), 2.0f);
	TestEqual(TEXT("repeated play is idempotent"), Subsystem->RequestPlay(), ETacticalPauseRequestResult::AlreadyInRequestedState);
	TestEqual(TEXT("toggle pauses"), Subsystem->TogglePause(), ETacticalPauseRequestResult::Succeeded);
	TestEqual(TEXT("toggle resumes"), Subsystem->TogglePause(), ETacticalPauseRequestResult::Succeeded);
	TestEqual(TEXT("pause events match successful pauses"), PausedEvents, 2);
	TestEqual(TEXT("resume events match successful resumes"), ResumedEvents, 2);

	FTacticalPauseTestAccessor::ForceState(*Subsystem, ETacticalPlaybackState::TransitioningToPause);
	TestEqual(TEXT("reentrant transition is rejected"), Subsystem->RequestPause(), ETacticalPauseRequestResult::TransitionInProgress);
	FTacticalPauseTestAccessor::ForceState(*Subsystem, ETacticalPlaybackState::Playing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalPauseSpeedValidationTest,
	"TacticalPause.Runtime.Speed.ValidationPresetsAndPausedSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTacticalPauseSpeedValidationTest::RunTest(const FString& Parameters)
{
	using namespace UE::TacticalPause::Tests;
	FScopedSettingsOverride SettingsOverride;
	FScopedTestWorld Scope(TEXT("TacticalPauseSpeedWorld"));
	UTacticalPauseWorldSubsystem* Subsystem = Scope.GetSubsystem();
	if (!TestNotNull(TEXT("world subsystem exists"), Subsystem))
	{
		return false;
	}
	InstallDefaultDriver(*Subsystem);

	TestEqual(TEXT("x1.5 applies"), Subsystem->SetPlaybackSpeed(1.5f), ETacticalPauseRequestResult::Succeeded);
	TestEqual(TEXT("x2 preset applies"), Subsystem->SetPlaybackPreset(TEXT("Faster")), ETacticalPauseRequestResult::Succeeded);
	TestEqual(TEXT("x3 preset applies"), Subsystem->SetPlaybackPreset(TEXT("Fastest")), ETacticalPauseRequestResult::Succeeded);
	TestEqual(TEXT("custom positive speed applies"), Subsystem->SetPlaybackSpeed(1.25f), ETacticalPauseRequestResult::Succeeded);
	TestEqual(TEXT("zero is rejected"), Subsystem->SetPlaybackSpeed(0.0f), ETacticalPauseRequestResult::InvalidPlaybackSpeed);
	TestEqual(TEXT("negative speed is rejected"), Subsystem->SetPlaybackSpeed(-1.0f), ETacticalPauseRequestResult::InvalidPlaybackSpeed);
	TestEqual(TEXT("NaN is rejected"), Subsystem->SetPlaybackSpeed(std::numeric_limits<float>::quiet_NaN()), ETacticalPauseRequestResult::InvalidPlaybackSpeed);
	TestEqual(TEXT("infinity is rejected"), Subsystem->SetPlaybackSpeed(std::numeric_limits<float>::infinity()), ETacticalPauseRequestResult::InvalidPlaybackSpeed);
	TestEqual(TEXT("configured maximum is enforced"), Subsystem->SetPlaybackSpeed(3.01f), ETacticalPauseRequestResult::InvalidPlaybackSpeed);
	TestEqual(TEXT("unknown preset is explicit"), Subsystem->SetPlaybackPreset(TEXT("Missing")), ETacticalPauseRequestResult::UnknownPreset);

	TestEqual(TEXT("pause succeeds"), Subsystem->RequestPause(), ETacticalPauseRequestResult::Succeeded);
	SettingsOverride.Settings->bAllowSpeedSelectionWhilePaused = false;
	TestEqual(TEXT("paused selection policy is enforced"), Subsystem->SetPlaybackSpeed(1.5f), ETacticalPauseRequestResult::SpeedSelectionWhilePausedDisabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalPausePresetValidationTest,
	"TacticalPause.Runtime.Presets.DeterministicValidationAndOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTacticalPausePresetValidationTest::RunTest(const FString& Parameters)
{
	using namespace UE::TacticalPause::Tests;
	FScopedSettingsOverride SettingsOverride;
	SettingsOverride.Settings->MaximumAllowedMultiplier = 3.0f;
	SettingsOverride.Settings->DefaultPresetId = TEXT("MissingDefault");
	SettingsOverride.Settings->SpeedPresets = {
		MakePreset(TEXT("Late"), 2.0f, 20),
		MakePreset(TEXT("First"), 1.0f, 0),
		MakePreset(TEXT("Late"), 2.5f, -10),
		MakePreset(NAME_None, 1.5f, 5),
		MakePreset(TEXT("TooFast"), 4.0f, 5),
		MakePreset(TEXT("Tie"), 1.5f, 20)
	};

	FScopedTestWorld Scope(TEXT("TacticalPausePresetWorld"));
	UTacticalPauseWorldSubsystem* Subsystem = Scope.GetSubsystem();
	if (!TestNotNull(TEXT("world subsystem exists"), Subsystem))
	{
		return false;
	}
	InstallDefaultDriver(*Subsystem);
	FTacticalPauseTestAccessor::RebuildPresets(*Subsystem);
	const TArray<FTacticalPlaybackSpeedPreset> Presets = Subsystem->GetAvailablePresets();
	TestEqual(TEXT("invalid and duplicate presets are removed"), Presets.Num(), 3);
	TestEqual(TEXT("sort order places First first"), Presets[0].Id, FName(TEXT("First")));
	TestEqual(TEXT("stable tie retains Late before Tie"), Presets[1].Id, FName(TEXT("Late")));
	TestEqual(TEXT("stable tie retains Tie last"), Presets[2].Id, FName(TEXT("Tie")));
	TestEqual(TEXT("missing default falls back deterministically"), Subsystem->GetSelectedPresetId(), FName(TEXT("First")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalPauseOwnershipTest,
	"TacticalPause.Runtime.Ownership.ExternalPauseDilationConflictAndRestoration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTacticalPauseOwnershipTest::RunTest(const FString& Parameters)
{
	using namespace UE::TacticalPause::Tests;
	FScopedTestWorld Scope(TEXT("TacticalPauseOwnershipWorld"));
	UTacticalPauseWorldSubsystem* Subsystem = Scope.GetSubsystem();
	if (!TestNotNull(TEXT("world subsystem exists"), Subsystem))
	{
		return false;
	}
	auto DriverOwner = MakeUnique<FFakeTacticalPauseTemporalDriver>();
	DriverOwner->GlobalTimeDilation = 0.75f;
	FFakeTacticalPauseTemporalDriver* Driver = FTacticalPauseTestAccessor::InstallDriver(*Subsystem, MoveTemp(DriverOwner));

	TestEqual(TEXT("plugin acquires dilation"), Subsystem->SetPlaybackSpeed(2.0f), ETacticalPauseRequestResult::Succeeded);
	TestTrue(TEXT("dilation ownership is tracked"), FTacticalPauseTestAccessor::IsDilationOwned(*Subsystem));
	Driver->SetExternalDilation(1.25f);
	TestEqual(TEXT("newer external dilation is not overwritten"), Subsystem->SetPlaybackSpeed(3.0f), ETacticalPauseRequestResult::ExternalStateConflict);
	TestEqual(TEXT("external dilation remains"), Driver->GetGlobalTimeDilation(), 1.25f);
	TestFalse(TEXT("plugin relinquishes conflicted ownership"), FTacticalPauseTestAccessor::IsDilationOwned(*Subsystem));
	TestEqual(TEXT("explicit retry acquires current external baseline"), Subsystem->SetPlaybackSpeed(3.0f), ETacticalPauseRequestResult::Succeeded);
	FTacticalPauseTestAccessor::Restore(*Subsystem);
	TestEqual(TEXT("teardown restores reacquired baseline"), Driver->GetGlobalTimeDilation(), 1.25f);

	FScopedTestWorld ExternalPauseScope(TEXT("TacticalPauseExternalPauseWorld"));
	UTacticalPauseWorldSubsystem* ExternalPauseSubsystem = ExternalPauseScope.GetSubsystem();
	auto ExternalDriverOwner = MakeUnique<FFakeTacticalPauseTemporalDriver>();
	ExternalDriverOwner->SetExternalPause(true);
	FFakeTacticalPauseTemporalDriver* ExternalDriver = FTacticalPauseTestAccessor::InstallDriver(*ExternalPauseSubsystem, MoveTemp(ExternalDriverOwner));
	TestEqual(TEXT("pre-existing external pause is idempotent"), ExternalPauseSubsystem->RequestPause(), ETacticalPauseRequestResult::AlreadyInRequestedState);
	TestEqual(TEXT("external pause cannot be removed"), ExternalPauseSubsystem->RequestPlay(), ETacticalPauseRequestResult::ExternalStateConflict);
	TestTrue(TEXT("external pause remains active"), ExternalDriver->IsPaused());

	FScopedTestWorld StackedPauseScope(TEXT("TacticalPauseStackedPauseWorld"));
	UTacticalPauseWorldSubsystem* StackedPauseSubsystem = StackedPauseScope.GetSubsystem();
	FFakeTacticalPauseTemporalDriver* StackedDriver = InstallDefaultDriver(*StackedPauseSubsystem);
	TestEqual(TEXT("plugin pause succeeds"), StackedPauseSubsystem->RequestPause(), ETacticalPauseRequestResult::Succeeded);
	StackedDriver->SetExternalPause(true);
	TestEqual(TEXT("play reports remaining external owner"), StackedPauseSubsystem->RequestPlay(), ETacticalPauseRequestResult::ExternalStateConflict);
	TestTrue(TEXT("stacked external owner keeps world paused"), StackedDriver->IsPaused());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalPauseTemporalRenderingTest,
	"TacticalPause.Runtime.Rendering.TemporalOverrideOwnershipConfigurationAndWorldIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTacticalPauseTemporalRenderingTest::RunTest(const FString& Parameters)
{
	using namespace UE::TacticalPause::Tests;
	FScopedSettingsOverride SettingsOverride;
	if (!TestNotNull(TEXT("Tactical Pause settings exist"), SettingsOverride.Settings))
	{
		return false;
	}
	TestTrue(TEXT("temporal rendering override is enabled by default"),
		SettingsOverride.bOriginalKeepTemporalRenderingActive);
	SettingsOverride.Settings->bKeepTemporalRenderingActiveWhilePaused = true;

	auto MakeViewFamily = []()
	{
		const FEngineShowFlags ShowFlags(ESFIM_Game);
		return MakeUnique<FSceneViewFamilyContext>(
			FSceneViewFamily::ConstructionValues(nullptr, nullptr, ShowFlags)
				.SetTime(FGameTime::CreateUndilated(0.0, 1.0f / 60.0f)));
	};

	FScopedTestWorld FirstScope(TEXT("TacticalPauseRenderingFirstWorld"));
	UTacticalPauseWorldSubsystem* FirstSubsystem = FirstScope.GetSubsystem();
	if (!TestNotNull(TEXT("first world subsystem exists"), FirstSubsystem))
	{
		return false;
	}
	InstallDefaultDriver(*FirstSubsystem);
	TestTrue(TEXT("enabled setting creates a per-world view extension"),
		FTacticalPauseTestAccessor::HasSceneViewExtension(*FirstSubsystem));
	TestFalse(TEXT("renderer override starts inactive"),
		FirstSubsystem->IsTemporalRenderingOverrideActive());

	FScopedTestWorld SecondScope(TEXT("TacticalPauseRenderingSecondWorld"));
	UTacticalPauseWorldSubsystem* SecondSubsystem = SecondScope.GetSubsystem();
	if (!TestNotNull(TEXT("second world subsystem exists"), SecondSubsystem))
	{
		return false;
	}
	InstallDefaultDriver(*SecondSubsystem);

	TestEqual(TEXT("successful Tactical Pause activates rendering override"),
		FirstSubsystem->RequestPause(), ETacticalPauseRequestResult::Succeeded);
	TestTrue(TEXT("first world renderer override is active"),
		FirstSubsystem->IsTemporalRenderingOverrideActive());
	TestFalse(TEXT("second world renderer override remains inactive"),
		SecondSubsystem->IsTemporalRenderingOverrideActive());

	TUniquePtr<FSceneViewFamilyContext> FirstPausedView = MakeViewFamily();
	FirstPausedView->bWorldIsPaused = true;
	FTacticalPauseTestAccessor::ApplyTemporalRenderingOverride(*FirstSubsystem, *FirstPausedView);
	TestFalse(TEXT("owned Tactical Pause clears only the renderer pause flag"),
		FirstPausedView->bWorldIsPaused);

	TUniquePtr<FSceneViewFamilyContext> AlreadyRealtimeView = MakeViewFamily();
	AlreadyRealtimeView->bWorldIsPaused = false;
	FTacticalPauseTestAccessor::ApplyTemporalRenderingOverride(*FirstSubsystem, *AlreadyRealtimeView);
	TestFalse(TEXT("override never converts a realtime view into a paused view"),
		AlreadyRealtimeView->bWorldIsPaused);

	TUniquePtr<FSceneViewFamilyContext> SecondPausedView = MakeViewFamily();
	SecondPausedView->bWorldIsPaused = true;
	FTacticalPauseTestAccessor::ApplyTemporalRenderingOverride(*SecondSubsystem, *SecondPausedView);
	TestTrue(TEXT("inactive world retains its renderer pause flag"),
		SecondPausedView->bWorldIsPaused);

	TestEqual(TEXT("Play deactivates rendering override"),
		FirstSubsystem->RequestPlay(), ETacticalPauseRequestResult::Succeeded);
	TestFalse(TEXT("renderer override is inactive after Play"),
		FirstSubsystem->IsTemporalRenderingOverrideActive());

	FScopedTestWorld ExternalPauseScope(TEXT("TacticalPauseRenderingExternalPauseWorld"));
	UTacticalPauseWorldSubsystem* ExternalPauseSubsystem = ExternalPauseScope.GetSubsystem();
	if (!TestNotNull(TEXT("external-pause world subsystem exists"), ExternalPauseSubsystem))
	{
		return false;
	}
	auto ExternalDriverOwner = MakeUnique<FFakeTacticalPauseTemporalDriver>();
	ExternalDriverOwner->SetExternalPause(true);
	FTacticalPauseTestAccessor::InstallDriver(*ExternalPauseSubsystem, MoveTemp(ExternalDriverOwner));
	TestEqual(TEXT("external pause remains an idempotent request"),
		ExternalPauseSubsystem->RequestPause(), ETacticalPauseRequestResult::AlreadyInRequestedState);
	TestFalse(TEXT("external pause does not activate renderer override"),
		ExternalPauseSubsystem->IsTemporalRenderingOverrideActive());

	FScopedTestWorld FailedPauseScope(TEXT("TacticalPauseRenderingFailedPauseWorld"));
	UTacticalPauseWorldSubsystem* FailedPauseSubsystem = FailedPauseScope.GetSubsystem();
	if (!TestNotNull(TEXT("failed-pause world subsystem exists"), FailedPauseSubsystem))
	{
		return false;
	}
	auto RejectingDriverOwner = MakeUnique<FFakeTacticalPauseTemporalDriver>();
	RejectingDriverOwner->bRejectPauseAcquisition = true;
	FTacticalPauseTestAccessor::InstallDriver(*FailedPauseSubsystem, MoveTemp(RejectingDriverOwner));
	TestEqual(TEXT("rejected pause reports apply failure"),
		FailedPauseSubsystem->RequestPause(), ETacticalPauseRequestResult::ApplyFailed);
	TestFalse(TEXT("failed pause does not activate renderer override"),
		FailedPauseSubsystem->IsTemporalRenderingOverrideActive());

	TestEqual(TEXT("first world can pause again before restoration"),
		FirstSubsystem->RequestPause(), ETacticalPauseRequestResult::Succeeded);
	TestTrue(TEXT("override reactivates for a later owned pause"),
		FirstSubsystem->IsTemporalRenderingOverrideActive());
	FTacticalPauseTestAccessor::Restore(*FirstSubsystem);
	TestFalse(TEXT("temporal restoration always deactivates renderer override"),
		FirstSubsystem->IsTemporalRenderingOverrideActive());

	SettingsOverride.Settings->bKeepTemporalRenderingActiveWhilePaused = false;
	FScopedTestWorld DisabledScope(TEXT("TacticalPauseRenderingDisabledWorld"));
	UTacticalPauseWorldSubsystem* DisabledSubsystem = DisabledScope.GetSubsystem();
	if (!TestNotNull(TEXT("disabled-setting world subsystem exists"), DisabledSubsystem))
	{
		return false;
	}
	InstallDefaultDriver(*DisabledSubsystem);
	TestFalse(TEXT("disabled setting does not create a view extension"),
		FTacticalPauseTestAccessor::HasSceneViewExtension(*DisabledSubsystem));
	TestEqual(TEXT("simulation pause still succeeds with renderer override disabled"),
		DisabledSubsystem->RequestPause(), ETacticalPauseRequestResult::Succeeded);
	TestFalse(TEXT("disabled setting keeps renderer override inactive"),
		DisabledSubsystem->IsTemporalRenderingOverrideActive());
	TUniquePtr<FSceneViewFamilyContext> DisabledPausedView = MakeViewFamily();
	DisabledPausedView->bWorldIsPaused = true;
	FTacticalPauseTestAccessor::ApplyTemporalRenderingOverride(*DisabledSubsystem, *DisabledPausedView);
	TestTrue(TEXT("disabled setting preserves the legacy renderer pause flag"),
		DisabledPausedView->bWorldIsPaused);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalPauseWidgetRoutingTest,
	"TacticalPause.Runtime.Widget.CommonUIBindingsPresetSlotsAndCommandRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTacticalPauseWidgetRoutingTest::RunTest(const FString& Parameters)
{
	using namespace UE::TacticalPause::Tests;
	TestTrue(TEXT("native controls derive from an ordinary User Widget"), UTacticalPauseControlsWidget::StaticClass()->IsChildOf(UUserWidget::StaticClass()));
	TestFalse(TEXT("native controls are not Common UI activatable widgets"), UTacticalPauseControlsWidget::StaticClass()->IsChildOf(UCommonActivatableWidget::StaticClass()));
	TestTrue(TEXT("PlayButton is a required Common UI binding"), FTacticalPauseTestAccessor::HasCommonButtonBinding(TEXT("PlayButton")));
	TestTrue(TEXT("PauseButton is a required Common UI binding"), FTacticalPauseTestAccessor::HasCommonButtonBinding(TEXT("PauseButton")));
	TestTrue(TEXT("SpeedButton1 is a required Common UI binding"), FTacticalPauseTestAccessor::HasCommonButtonBinding(TEXT("SpeedButton1")));
	TestTrue(TEXT("SpeedButton2 is a required Common UI binding"), FTacticalPauseTestAccessor::HasCommonButtonBinding(TEXT("SpeedButton2")));
	TestTrue(TEXT("SpeedButton3 is a required Common UI binding"), FTacticalPauseTestAccessor::HasCommonButtonBinding(TEXT("SpeedButton3")));
	TestTrue(TEXT("SpeedButton4 is a required Common UI binding"), FTacticalPauseTestAccessor::HasCommonButtonBinding(TEXT("SpeedButton4")));

	FScopedTestWorld Scope(TEXT("TacticalPauseWidgetWorld"));
	UTacticalPauseWorldSubsystem* Subsystem = Scope.GetSubsystem();
	if (!TestNotNull(TEXT("world subsystem exists"), Subsystem))
	{
		return false;
	}
	InstallDefaultDriver(*Subsystem);
	UTacticalPauseControlsWidget* Widget = CreateWidget<UTacticalPauseControlsWidget>(Scope.World, UTacticalPauseControlsWidget::StaticClass());
	if (!TestNotNull(TEXT("native Common UI controls logic can be created"), Widget))
	{
		return false;
	}
	TestEqual(TEXT("third Common UI speed slot routes to x2 preset"), FTacticalPauseTestAccessor::SelectPresetSlot(*Widget, 2), ETacticalPauseRequestResult::Succeeded);
	TestEqual(TEXT("preset slot updates authoritative selected speed"), Subsystem->GetSelectedPlaybackSpeed(), 2.0f);
	TestEqual(TEXT("widget pause command routes to subsystem"), Widget->RequestPauseFromWidget(), ETacticalPauseRequestResult::Succeeded);
	TestTrue(TEXT("authoritative subsystem is paused"), Subsystem->IsPaused());
	return true;
}

#endif
