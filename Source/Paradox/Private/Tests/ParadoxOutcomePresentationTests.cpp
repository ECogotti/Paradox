#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Controllers/ParadoxPlayerController.h"
#include "Presentation/ParadoxOutcomePresentationComponent.h"
#include "Presentation/ParadoxOutcomeWidget.h"

struct FParadoxOutcomePresentationTestAccessor
{
	static FParadoxOutcomePresentationData MakeParadoxData(
		const FParadoxContext& Context)
	{
		return UParadoxOutcomePresentationComponent::
			MakeParadoxPresentationData(Context);
	}

	static FParadoxOutcomePresentationData MakeGameOverData(
		const FParadoxGameOverContext& Context)
	{
		return UParadoxOutcomePresentationComponent::
			MakeGameOverPresentationData(Context);
	}

	static FParadoxOutcomePresentationData MakeLevelCompleteData(
		const FParadoxLevelCompleteContext& Context)
	{
		return UParadoxOutcomePresentationComponent::
			MakeLevelCompletePresentationData(Context);
	}

	static UClass* GetWidgetClass(
		const UParadoxOutcomePresentationComponent& Presentation)
	{
		return Presentation.OutcomeWidgetClass.Get();
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxOutcomePresentationDefaultsTest,
	"Paradox.Presentation.NativeFallbackContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxOutcomePresentationDefaultsTest::RunTest(
	const FString& Parameters)
{
	const AParadoxPlayerController* Controller =
		GetDefault<AParadoxPlayerController>();
	const UParadoxOutcomePresentationComponent* Presentation =
		Controller
			? Controller->GetOutcomePresentationComponent()
			: nullptr;
	if (!TestNotNull(
		TEXT("Player controller owns an outcome presenter"),
		Presentation))
	{
		return false;
	}
	TestTrue(
		TEXT("Outcome fades continue while gameplay is paused"),
		Presentation->PrimaryComponentTick.bTickEvenWhenPaused);
	UClass* WidgetClass =
		FParadoxOutcomePresentationTestAccessor::GetWidgetClass(
			*Presentation);
	TestTrue(
		TEXT("Native fallback widget is configured"),
		WidgetClass
			&& WidgetClass->IsChildOf(UParadoxOutcomeWidget::StaticClass()));

	FParadoxContext ParadoxContext;
	ParadoxContext.ObserverTemporalIndex = 0;
	ParadoxContext.TargetTemporalIndex = 2;
	const FParadoxOutcomePresentationData ParadoxData =
		FParadoxOutcomePresentationTestAccessor::MakeParadoxData(
			ParadoxContext);
	TestEqual(
		TEXT("Collapse title is native and deterministic"),
		ParadoxData.Title.ToString(),
		FString(TEXT("TIMELINE COLLAPSE")));
	TestEqual(
		TEXT("Collapse message contains temporal indices"),
		ParadoxData.Message.ToString(),
		FString(TEXT("T0 witnessed T2.\nThe past saw the future.")));
	TestFalse(
		TEXT("Collapse fallback does not expose restart"),
		ParadoxData.bShowRestart);

	FParadoxGameOverContext GameOverContext;
	GameOverContext.EventId = FGuid::NewGuid();
	const FParadoxOutcomePresentationData GameOverData =
		FParadoxOutcomePresentationTestAccessor::MakeGameOverData(
			GameOverContext);
	TestEqual(
		TEXT("Game Over title is native and deterministic"),
		GameOverData.Title.ToString(),
		FString(TEXT("NO TIMELINES REMAIN")));
	TestEqual(
		TEXT("Game Over message is native and deterministic"),
		GameOverData.Message.ToString(),
		FString(TEXT("The loop has no future left.")));
	TestTrue(
		TEXT("Game Over fallback exposes restart"),
		GameOverData.bShowRestart);

	FParadoxLevelCompleteContext LevelCompleteContext;
	LevelCompleteContext.EventId = FGuid::NewGuid();
	const FParadoxOutcomePresentationData LevelCompleteData =
		FParadoxOutcomePresentationTestAccessor::MakeLevelCompleteData(
			LevelCompleteContext);
	TestEqual(
		TEXT("Level Complete title is native and deterministic"),
		LevelCompleteData.Title.ToString(),
		FString(TEXT("LEVEL COMPLETE")));
	TestTrue(
		TEXT("Level Complete fallback exposes restart"),
		LevelCompleteData.bShowRestart);

	const UFunction* PresenterHook =
		UParadoxOutcomePresentationComponent::StaticClass()->FindFunctionByName(
			TEXT("ReceiveOutcomePresentationStarted"));
	const UFunction* WidgetHook =
		UParadoxOutcomeWidget::StaticClass()->FindFunctionByName(
			TEXT("ReceiveOutcomeDataChanged"));
	TestTrue(
		TEXT("Presenter customization hook is a native Blueprint event"),
		PresenterHook
			&& PresenterHook->HasAllFunctionFlags(FUNC_Event | FUNC_Native));
	TestTrue(
		TEXT("Widget customization hook is a native Blueprint event"),
		WidgetHook
			&& WidgetHook->HasAllFunctionFlags(FUNC_Event | FUNC_Native));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
