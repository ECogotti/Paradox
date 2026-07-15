// Copyright Epic Games, Inc. All Rights Reserved.

#include "GridWorldEditorModule.h"

#include "AI/Navigation/NavigationDirtyArea.h"
#include "Components/PrimitiveComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GridWorldModule.h"
#include "Navigation/GridNavigationBoundsVolume.h"
#include "Navigation/GridNavigationData.h"
#include "NavigationSystem.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "GridWorldEditor"

namespace UE::GridWorld::Editor::Private
{
	UWorld* GetEditorWorld()
	{
		return GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	AGridNavigationData* GetGridNavData(bool bCreateIfMissing)
	{
		UWorld* World = GetEditorWorld();
		if (World == nullptr)
		{
			return nullptr;
		}
		for (TActorIterator<AGridNavigationData> It(World); It; ++It)
		{
			return *It;
		}
		UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		return NavigationSystem != nullptr
			? Cast<AGridNavigationData>(NavigationSystem->GetDefaultNavDataInstance(
				bCreateIfMissing ? FNavigationSystem::Create : FNavigationSystem::DontCreate))
			: nullptr;
	}

	void RefreshDrawing(AGridNavigationData& NavData)
	{
		if (NavData.RenderingComp != nullptr)
		{
			NavData.RenderingComp->MarkRenderStateDirty();
		}
	}
}

IMPLEMENT_MODULE(FGridWorldEditorModule, GridWorldEditor)

void FGridWorldEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGridWorldEditorModule::RegisterMenus));
}

void FGridWorldEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FGridWorldEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* BuildMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Build"));
	if (BuildMenu == nullptr)
	{
		return;
	}
	FToolMenuSection& Section = BuildMenu->FindOrAddSection(TEXT("GridWorld"), LOCTEXT("GridWorldSection", "Grid World"));
	auto AddAction = [&Section](FName Name, const FText& Label, const FText& Tooltip, FExecuteAction Execute)
	{
		Section.AddMenuEntry(Name, Label, Tooltip, FSlateIcon(), FUIAction(MoveTemp(Execute)));
	};
	AddAction(TEXT("GridWorldBuildAll"), LOCTEXT("BuildAll", "Build Grid World"), LOCTEXT("BuildAllTip", "Build all GridNavigationBoundsVolume regions."), FExecuteAction::CreateRaw(this, &FGridWorldEditorModule::BuildAll));
	AddAction(TEXT("GridWorldRebuildSelected"), LOCTEXT("RebuildSelected", "Rebuild Grid World for Selection"), LOCTEXT("RebuildSelectedTip", "Rebuild chunks touched by selected actors, including a one-chunk halo."), FExecuteAction::CreateRaw(this, &FGridWorldEditorModule::RebuildSelected));
	AddAction(TEXT("GridWorldClear"), LOCTEXT("Clear", "Clear Grid World"), LOCTEXT("ClearTip", "Clear generated Grid World navigation data."), FExecuteAction::CreateRaw(this, &FGridWorldEditorModule::ClearAll));
	AddAction(TEXT("GridWorldValidate"), LOCTEXT("Validate", "Validate Grid World"), LOCTEXT("ValidateTip", "Validate bounds, transforms, GUIDs and overlaps without replacing valid data."), FExecuteAction::CreateRaw(this, &FGridWorldEditorModule::ValidateAll));
	AddAction(TEXT("GridWorldInspect"), LOCTEXT("Inspect", "Inspect Grid Cell at Selection"), LOCTEXT("InspectTip", "Log the projected cell under the first selected actor."), FExecuteAction::CreateRaw(this, &FGridWorldEditorModule::InspectSelectedCell));
	AddAction(TEXT("GridWorldAgent"), LOCTEXT("Agent", "Use GridWorld Supported Agent"), LOCTEXT("AgentTip", "Select the configured 42 cm radius / 192 cm height GridWorld agent as default."), FExecuteAction::CreateRaw(this, &FGridWorldEditorModule::UseGridWorldAgent));

	Section.AddSeparator(TEXT("GridWorldDebugSeparator"));
	auto AddToggle = [this, &Section](FName Name, const FText& Label, bool AGridNavigationData::* Flag)
	{
		Section.AddMenuEntry(
			Name,
			Label,
			LOCTEXT("DebugToggleTip", "Toggle this GridWorld Show Navigation layer."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FGridWorldEditorModule::ToggleDebugFlag, Flag),
				FCanExecuteAction(),
				FIsActionChecked::CreateRaw(this, &FGridWorldEditorModule::IsDebugFlagEnabled, Flag)),
			EUserInterfaceActionType::ToggleButton);
	};
	AddToggle(TEXT("GridWorldDrawCells"), LOCTEXT("DrawCells", "Show Grid Cells"), &AGridNavigationData::bDrawCells);
	AddToggle(TEXT("GridWorldDrawCosts"), LOCTEXT("DrawCosts", "Show Grid Costs"), &AGridNavigationData::bDrawCosts);
	AddToggle(TEXT("GridWorldDrawOccupancy"), LOCTEXT("DrawOccupancy", "Show Grid Occupancy"), &AGridNavigationData::bDrawOccupancy);
	AddToggle(TEXT("GridWorldDrawLinks"), LOCTEXT("DrawLinks", "Show Grid Links"), &AGridNavigationData::bDrawLinks);
	AddToggle(TEXT("GridWorldDrawChunks"), LOCTEXT("DrawChunks", "Show Grid Chunks"), &AGridNavigationData::bDrawChunks);
	AddToggle(TEXT("GridWorldDrawDirty"), LOCTEXT("DrawDirty", "Show Dirty Grid Regions"), &AGridNavigationData::bDrawDirtyRegions);
	AddToggle(TEXT("GridWorldDrawErrors"), LOCTEXT("DrawErrors", "Show Grid Errors"), &AGridNavigationData::bDrawErrors);
	AddToggle(TEXT("GridWorldDrawPaths"), LOCTEXT("DrawPaths", "Show Last Grid Path"), &AGridNavigationData::bDrawPaths);
	AddToggle(TEXT("GridWorldDrawReachability"), LOCTEXT("DrawReachability", "Show Last Reachability Query"), &AGridNavigationData::bDrawReachability);
}

void FGridWorldEditorModule::BuildAll()
{
	if (AGridNavigationData* NavData = UE::GridWorld::Editor::Private::GetGridNavData(true))
	{
		NavData->BuildFromWorld();
	}
}

void FGridWorldEditorModule::RebuildSelected()
{
	AGridNavigationData* NavData = UE::GridWorld::Editor::Private::GetGridNavData(true);
	if (NavData == nullptr || GEditor == nullptr)
	{
		return;
	}
	TArray<FNavigationDirtyArea> DirtyAreas;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
	{
		if (const AActor* Actor = Cast<AActor>(*It))
		{
			DirtyAreas.Emplace(Actor->GetComponentsBoundingBox(true), ENavigationDirtyFlag::All);
		}
	}
	if (DirtyAreas.IsEmpty())
	{
		GRIDWORLD_LOG_WARNING("Rebuild selection requested with no selected actors.");
		return;
	}
	NavData->BuildDirtyAreas(DirtyAreas);
}

void FGridWorldEditorModule::ClearAll()
{
	if (AGridNavigationData* NavData = UE::GridWorld::Editor::Private::GetGridNavData(false))
	{
		NavData->ClearGridWorld();
		UE::GridWorld::Editor::Private::RefreshDrawing(*NavData);
	}
}

void FGridWorldEditorModule::ValidateAll()
{
	UWorld* World = UE::GridWorld::Editor::Private::GetEditorWorld();
	if (World == nullptr)
	{
		return;
	}
	TSet<FGuid> SeenIds;
	TArray<const AGridNavigationBoundsVolume*> ValidVolumes;
	int32 ErrorCount = 0;
	for (TActorIterator<AGridNavigationBoundsVolume> It(World); It; ++It)
	{
		FString Error;
		if (!It->ValidateGridBounds(Error))
		{
			GRIDWORLD_LOG_ERROR("%s: %s", *GetNameSafe(*It), *Error);
			++ErrorCount;
		}
		if (SeenIds.Contains(It->GridId))
		{
			GRIDWORLD_LOG_ERROR("%s has duplicate GridId %s.", *GetNameSafe(*It), *It->GridId.ToString());
			++ErrorCount;
		}
		SeenIds.Add(It->GridId);
		for (const AGridNavigationBoundsVolume* Other : ValidVolumes)
		{
			if (It->GetGridWorldBounds().Intersect(Other->GetGridWorldBounds()))
			{
				GRIDWORLD_LOG_ERROR("%s ambiguously overlaps %s.", *GetNameSafe(*It), *GetNameSafe(Other));
				++ErrorCount;
			}
		}
		ValidVolumes.Add(*It);
	}
	if (ErrorCount == 0)
	{
		GRIDWORLD_LOG_INFO("GridWorld validation succeeded for %d bounds volume(s).", ValidVolumes.Num());
	}
}

void FGridWorldEditorModule::InspectSelectedCell()
{
	AGridNavigationData* NavData = UE::GridWorld::Editor::Private::GetGridNavData(false);
	AActor* SelectedActor = GEditor != nullptr ? Cast<AActor>(GEditor->GetSelectedActors()->GetTop(AActor::StaticClass())) : nullptr;
	if (NavData == nullptr || SelectedActor == nullptr)
	{
		GRIDWORLD_LOG_WARNING("Cell inspection requires generated GridWorld data and a selected actor.");
		return;
	}
	FNavLocation Projected;
	if (NavData->ProjectPoint(SelectedActor->GetActorLocation(), Projected, FVector(50.0, 50.0, 200.0)))
	{
		const FGridWorldSnapshotPtr Snapshot = NavData->GetSnapshot();
		const int32 CellIndex = Snapshot.IsValid() ? Snapshot->ResolveNodeRef(Projected.NodeRef) : INDEX_NONE;
		if (Snapshot.IsValid() && Snapshot->Cells.IsValidIndex(CellIndex))
		{
			const FGridCellData& Cell = Snapshot->Cells[CellIndex];
			GRIDWORLD_LOG_INFO("Cell %s [%d,%d,%d]: center=%s cost=%d walkable=%s occupied=%s.",
				*Cell.Id.GridId.ToString(), Cell.Id.Coord.X, Cell.Id.Coord.Y, Cell.Id.Coord.Layer,
				*Cell.WorldCenter.ToCompactString(), Cell.TraversalCost,
				Cell.bWalkable ? TEXT("true") : TEXT("false"), Cell.bOccupied ? TEXT("true") : TEXT("false"));
		}
	}
}

void FGridWorldEditorModule::UseGridWorldAgent()
{
	if (AGridNavigationData* NavData = UE::GridWorld::Editor::Private::GetGridNavData(true))
	{
		NavData->SetSupportsDefaultAgent(true);
		GRIDWORLD_LOG_INFO("GridWorld Supported Agent selected: radius 42 cm, height 192 cm.");
	}
}

void FGridWorldEditorModule::ToggleDebugFlag(bool AGridNavigationData::* Flag)
{
	if (AGridNavigationData* NavData = UE::GridWorld::Editor::Private::GetGridNavData(false))
	{
		NavData->*Flag = !(NavData->*Flag);
		UE::GridWorld::Editor::Private::RefreshDrawing(*NavData);
	}
}

bool FGridWorldEditorModule::IsDebugFlagEnabled(bool AGridNavigationData::* Flag) const
{
	const AGridNavigationData* NavData = UE::GridWorld::Editor::Private::GetGridNavData(false);
	return NavData != nullptr && NavData->*Flag;
}

#undef LOCTEXT_NAMESPACE
