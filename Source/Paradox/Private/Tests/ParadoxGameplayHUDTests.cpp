#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Blueprint/UserWidget.h"
#include "CommonButtonBase.h"
#include "CommonTextBlock.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Controllers/ParadoxPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HUD/ParadoxGameplayHUDComponent.h"
#include "HUD/ParadoxGameplayHUDWidget.h"
#include "Inventory/ParadoxInventoryActionButtonWidget.h"
#include "Inventory/ParadoxInventoryWidget.h"
#include "UObject/UnrealType.h"
#include "Widgets/TacticalPauseControlsWidget.h"

struct FParadoxGameplayHUDTestAccessor
{
	static UWidgetSwitcher* GetModeSwitcher(UParadoxGameplayHUDWidget& Widget)
	{
		return Widget.HUDModeSwitcher.Get();
	}

	static UPanelWidget* GetSection(
		UParadoxGameplayHUDWidget& Widget,
		const EParadoxGameplayHUDSection Section)
	{
		return Widget.GetSectionContainer(Section);
	}

	static UParadoxInventoryWidget* GetEmbeddedEquipmentWidget(
		UParadoxGameplayHUDWidget& Widget)
	{
		return Widget.FindEmbeddedEquipmentWidget();
	}

	static UTacticalPauseControlsWidget* GetEmbeddedTacticalPauseWidget(
		UParadoxGameplayHUDWidget& Widget)
	{
		return Widget.FindEmbeddedTacticalPauseWidget();
	}
};

namespace UE::Paradox::GameplayHUD::Tests
{
	void TestRequiredWidgetBinding(
		FAutomationTestBase& Test,
		UClass* WidgetClass,
		const FName PropertyName,
		UClass* RequiredWidgetClass)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(
			WidgetClass, PropertyName);
		if (!Test.TestNotNull(
			*FString::Printf(TEXT("%s declares %s"), *GetNameSafe(WidgetClass), *PropertyName.ToString()),
			Property))
		{
			return;
		}
		Test.TestTrue(
			*FString::Printf(TEXT("%s uses the required widget type"), *PropertyName.ToString()),
			Property->PropertyClass->IsChildOf(RequiredWidgetClass));
		Test.TestTrue(
			*FString::Printf(TEXT("%s is a required BindWidget"), *PropertyName.ToString()),
			Property->HasMetaData(TEXT("BindWidget")));
		Test.TestFalse(
			*FString::Printf(TEXT("%s is not optional"), *PropertyName.ToString()),
			Property->HasMetaData(TEXT("BindWidgetOptional")));
	}

	void TestOptionalWidgetBinding(
		FAutomationTestBase& Test,
		UClass* WidgetClass,
		const FName PropertyName,
		UClass* RequiredWidgetClass)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(
			WidgetClass, PropertyName);
		if (!Test.TestNotNull(
			*FString::Printf(TEXT("%s declares %s"), *GetNameSafe(WidgetClass), *PropertyName.ToString()),
			Property))
		{
			return;
		}
		Test.TestTrue(
			*FString::Printf(TEXT("%s uses the expected widget type"), *PropertyName.ToString()),
			Property->PropertyClass->IsChildOf(RequiredWidgetClass));
		Test.TestTrue(
			*FString::Printf(TEXT("%s is an optional BindWidget"), *PropertyName.ToString()),
			Property->HasMetaData(TEXT("BindWidgetOptional")));
		Test.TestFalse(
			*FString::Printf(TEXT("%s is not required"), *PropertyName.ToString()),
			Property->HasMetaData(TEXT("BindWidget")));
	}

	struct FScopedTestWorld
	{
		explicit FScopedTestWorld(const TCHAR* Name)
		{
			Context = GEngine
				? &GEngine->CreateNewWorldContext(EWorldType::Game)
				: nullptr;
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			if (World)
			{
				World->AddToRoot();
			}
			if (Context)
			{
				Context->SetCurrentWorld(World);
			}
		}

		~FScopedTestWorld()
		{
			if (!World)
			{
				return;
			}
			World->DestroyWorld(true);
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World->RemoveFromRoot();
		}

		FWorldContext* Context = nullptr;
		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxGameplayHUDArchitectureTest,
	"Paradox.GameplayHUD.Architecture.DefaultsAndReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxGameplayHUDArchitectureTest::RunTest(const FString& Parameters)
{
	const AParadoxPlayerController* ControllerCDO = GetDefault<AParadoxPlayerController>();
	const UParadoxGameplayHUDComponent* HUD = ControllerCDO
		? ControllerCDO->GetGameplayHUDComponent()
		: nullptr;
	if (!TestNotNull(TEXT("Player Controller owns the native Gameplay HUD component"), HUD))
	{
		return false;
	}
	TestEqual(TEXT("HUD defaults to Normal mode"), HUD->InitialHUDMode, EParadoxGameplayHUDMode::Normal);
	TestEqual(TEXT("visibility policy defaults Automatic"), HUD->GetVisibilityOverride(), EParadoxGameplayHUDVisibilityOverride::Automatic);
	TestEqual(TEXT("Status section is reserved and collapsed"), HUD->StatusSectionVisibility, ESlateVisibility::Collapsed);
	TestTrue(TEXT("native root class is configured"), HUD->GameplayHUDWidgetClass.Get() == UParadoxGameplayHUDWidget::StaticClass());
	TestNull(
		TEXT("Tactical Pause section class is no longer exposed by the coordinator"),
		FindFProperty<FProperty>(UParadoxGameplayHUDComponent::StaticClass(), TEXT("TacticalPauseWidgetClass")));
	TestNull(
		TEXT("Equipment section class is no longer exposed by the coordinator"),
		FindFProperty<FProperty>(UParadoxGameplayHUDComponent::StaticClass(), TEXT("EquipmentWidgetClass")));
	TestTrue(TEXT("screen-space HUD input defaults to Game and UI routing"), HUD->bConfigureGameAndUIInputMode);
	TestFalse(TEXT("inventory widget is concrete"), UParadoxInventoryWidget::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
	TestFalse(TEXT("action entry widget is concrete"), UParadoxInventoryActionButtonWidget::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
	TestTrue(TEXT("Tactical Pause controls are ordinary User Widgets"), UTacticalPauseControlsWidget::StaticClass()->IsChildOf(UUserWidget::StaticClass()));

	using namespace UE::Paradox::GameplayHUD::Tests;
	TestRequiredWidgetBinding(*this, UParadoxInventoryActionButtonWidget::StaticClass(),
		TEXT("ActionButton"), UCommonButtonBase::StaticClass());
	TestRequiredWidgetBinding(*this, UParadoxInventoryActionButtonWidget::StaticClass(),
		TEXT("ActionIcon"), UImage::StaticClass());
	TestRequiredWidgetBinding(*this, UParadoxInventoryActionButtonWidget::StaticClass(),
		TEXT("ActionLabel"), UCommonTextBlock::StaticClass());
	TestRequiredWidgetBinding(*this, UParadoxInventoryWidget::StaticClass(),
		TEXT("EquipmentStateSwitcher"), UWidgetSwitcher::StaticClass());
	TestRequiredWidgetBinding(*this, UParadoxInventoryWidget::StaticClass(),
		TEXT("EmptySlotIcon"), UImage::StaticClass());
	TestRequiredWidgetBinding(*this, UParadoxInventoryWidget::StaticClass(),
		TEXT("EquippedItemIcon"), UImage::StaticClass());
	TestOptionalWidgetBinding(*this, UParadoxInventoryWidget::StaticClass(),
		TEXT("EquippedItemName"), UCommonTextBlock::StaticClass());
	TestRequiredWidgetBinding(*this, UParadoxInventoryWidget::StaticClass(),
		TEXT("DropButton"), UCommonButtonBase::StaticClass());
	TestRequiredWidgetBinding(*this, UParadoxInventoryWidget::StaticClass(),
		TEXT("SpecialActionsContainer"), UVerticalBox::StaticClass());

	const FStructProperty* ToggleKeyProperty = FindFProperty<FStructProperty>(
		AParadoxPlayerController::StaticClass(), TEXT("ToggleHUDModeKey"));
	const FKey* ToggleKey = ToggleKeyProperty && ControllerCDO
		? ToggleKeyProperty->ContainerPtrToValuePtr<FKey>(ControllerCDO)
		: nullptr;
	TestTrue(TEXT("HUD toggle defaults to Tab"), ToggleKey && *ToggleKey == EKeys::Tab);

	TestNotNull(TEXT("Set HUD Mode is Blueprint-visible"), UParadoxGameplayHUDComponent::StaticClass()->FindFunctionByName(TEXT("SetHUDMode")));
	TestNotNull(TEXT("Toggle HUD Mode is Blueprint-visible"), UParadoxGameplayHUDComponent::StaticClass()->FindFunctionByName(TEXT("ToggleHUDMode")));
	TestNotNull(TEXT("mode hook is reflected"), UParadoxGameplayHUDWidget::StaticClass()->FindFunctionByName(TEXT("OnModeChanged")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxGameplayHUDAuthoredCompositionTest,
	"Paradox.GameplayHUD.Assets.EmbeddedInventoryComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxGameplayHUDAuthoredCompositionTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::GameplayHUD::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxAuthoredGameplayHUDWorld"));
	UClass* AuthoredHUDClass = LoadClass<UParadoxGameplayHUDWidget>(
		nullptr,
		TEXT("/Game/UI/Widgets/GameplayHud/WBP_ParadoxHudWidget.WBP_ParadoxHudWidget_C"));
	if (!TestNotNull(TEXT("authored Gameplay HUD class resolves"), AuthoredHUDClass))
	{
		return false;
	}
	UParadoxGameplayHUDWidget* Widget = CreateWidget<UParadoxGameplayHUDWidget>(
		Scope.World,
		AuthoredHUDClass);
	if (!TestNotNull(TEXT("authored Gameplay HUD can be instantiated"), Widget))
	{
		return false;
	}
	Widget->TakeWidget();
	UParadoxInventoryWidget* EmbeddedInventory =
		FParadoxGameplayHUDTestAccessor::GetEmbeddedEquipmentWidget(*Widget);
	UTacticalPauseControlsWidget* EmbeddedTacticalPause =
		FParadoxGameplayHUDTestAccessor::GetEmbeddedTacticalPauseWidget(*Widget);
	TestNotNull(
		TEXT("authored root owns its Tactical Pause controls"),
		EmbeddedTacticalPause);
	TestNotNull(
		TEXT("authored root owns and exposes its embedded inventory to the native coordinator"),
		EmbeddedInventory);
	if (EmbeddedInventory)
	{
		TestTrue(
			TEXT("embedded inventory remains hit-testable"),
			EmbeddedInventory->GetVisibility() == ESlateVisibility::Visible
				|| EmbeddedInventory->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);
		Widget->SetSectionVisibility(
			EParadoxGameplayHUDSection::Equipment,
			ESlateVisibility::Hidden);
		TestEqual(
			TEXT("embedded inventory participates in authoritative Equipment visibility"),
			EmbeddedInventory->GetVisibility(),
			ESlateVisibility::Hidden);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxGameplayHUDInventoryNativeFallbackTest,
	"Paradox.GameplayHUD.InventoryWidget.NativeCommonUIFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxGameplayHUDInventoryNativeFallbackTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::GameplayHUD::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxInventoryWidgetWorld"));
	UParadoxInventoryWidget* InventoryWidget = Scope.World
		? CreateWidget<UParadoxInventoryWidget>(Scope.World, UParadoxInventoryWidget::StaticClass())
		: nullptr;
	UParadoxInventoryActionButtonWidget* ActionWidget = Scope.World
		? CreateWidget<UParadoxInventoryActionButtonWidget>(
			Scope.World, UParadoxInventoryActionButtonWidget::StaticClass())
		: nullptr;
	if (!TestNotNull(TEXT("native inventory widget can be created"), InventoryWidget)
		|| !TestNotNull(TEXT("native action entry can be created"), ActionWidget))
	{
		return false;
	}

	InventoryWidget->TakeWidget();
	ActionWidget->TakeWidget();
	const FObjectPropertyBase* DropButtonProperty = FindFProperty<FObjectPropertyBase>(
		UParadoxInventoryWidget::StaticClass(), TEXT("DropButton"));
	const FObjectPropertyBase* ActionButtonProperty = FindFProperty<FObjectPropertyBase>(
		UParadoxInventoryActionButtonWidget::StaticClass(), TEXT("ActionButton"));
	TestTrue(TEXT("native Drop is backed by a Common Button"),
		DropButtonProperty
		&& Cast<UCommonButtonBase>(DropButtonProperty->GetObjectPropertyValue_InContainer(InventoryWidget)));
	TestTrue(TEXT("native action entry is backed by a Common Button"),
		ActionButtonProperty
		&& Cast<UCommonButtonBase>(ActionButtonProperty->GetObjectPropertyValue_InContainer(ActionWidget)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxGameplayHUDModeTest,
	"Paradox.GameplayHUD.Widget.NativeSwitcherAndModeState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxGameplayHUDModeTest::RunTest(const FString& Parameters)
{
	using namespace UE::Paradox::GameplayHUD::Tests;
	FScopedTestWorld Scope(TEXT("ParadoxGameplayHUDWidgetWorld"));
	UParadoxGameplayHUDWidget* Widget = Scope.World
		? CreateWidget<UParadoxGameplayHUDWidget>(Scope.World, UParadoxGameplayHUDWidget::StaticClass())
		: nullptr;
	if (!TestNotNull(TEXT("native Gameplay HUD widget can be created"), Widget))
	{
		return false;
	}
	Widget->TakeWidget();
	UWidgetSwitcher* Switcher = FParadoxGameplayHUDTestAccessor::GetModeSwitcher(*Widget);
	if (!TestNotNull(TEXT("native HUD builds its mode switcher"), Switcher))
	{
		return false;
	}
	TestEqual(TEXT("switcher owns exactly Normal and Collapsed pages"), Switcher->GetNumWidgets(), 2);
	TestNotNull(
		TEXT("native root fallback owns Tactical Pause controls"),
		FParadoxGameplayHUDTestAccessor::GetEmbeddedTacticalPauseWidget(*Widget));
	TestNotNull(
		TEXT("native root fallback owns an Inventory widget"),
		FParadoxGameplayHUDTestAccessor::GetEmbeddedEquipmentWidget(*Widget));
	TestEqual(TEXT("initial page is Normal"), Switcher->GetActiveWidgetIndex(), UParadoxGameplayHUDWidget::GetNormalModePageIndex());
	TestTrue(TEXT("Collapsed presentation applies"), Widget->ApplyHUDMode(EParadoxGameplayHUDMode::Collapsed));
	TestEqual(TEXT("collapsed page index is stable"), Switcher->GetActiveWidgetIndex(), UParadoxGameplayHUDWidget::GetCollapsedModePageIndex());
	TestEqual(TEXT("widget reports Collapsed"), Widget->GetHUDMode(), EParadoxGameplayHUDMode::Collapsed);
	TestTrue(TEXT("designer collapsed container exists"), Widget->GetCollapsedModeContainer() != nullptr);
	Widget->SetSectionVisibility(EParadoxGameplayHUDSection::Equipment, ESlateVisibility::Hidden);
	UPanelWidget* Equipment = FParadoxGameplayHUDTestAccessor::GetSection(
		*Widget, EParadoxGameplayHUDSection::Equipment);
	TestTrue(TEXT("normal section visibility remains independently writable"), Equipment && Equipment->GetVisibility() == ESlateVisibility::Hidden);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxGameplayHUDComponentStateTest,
	"Paradox.GameplayHUD.Component.ModeAndSectionState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxGameplayHUDComponentStateTest::RunTest(const FString& Parameters)
{
	UParadoxGameplayHUDComponent* HUD = NewObject<UParadoxGameplayHUDComponent>();
	if (!TestNotNull(TEXT("standalone component state can be inspected"), HUD))
	{
		return false;
	}
	TestTrue(TEXT("first toggle changes mode"), HUD->ToggleHUDMode());
	TestTrue(TEXT("component reports Collapsed"), HUD->IsHUDCollapsed());
	TestTrue(TEXT("second toggle returns Normal"), HUD->ToggleHUDMode());
	TestEqual(TEXT("component reports Normal"), HUD->GetHUDMode(), EParadoxGameplayHUDMode::Normal);
	TestFalse(TEXT("same mode is idempotent"), HUD->SetHUDMode(EParadoxGameplayHUDMode::Normal));
	HUD->SetSectionVisibility(EParadoxGameplayHUDSection::Equipment, ESlateVisibility::Collapsed);
	TestEqual(TEXT("section state persists without a widget"), HUD->GetSectionVisibility(EParadoxGameplayHUDSection::Equipment), ESlateVisibility::Collapsed);
	TestFalse(TEXT("hidden/uninitialized component rejects input toggle"), HUD->CanToggleHUDModeFromInput());
	return true;
}

#endif
