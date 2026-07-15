// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debug/GridNavigationRenderingComponent.h"

#include "Debug/GridCellSurfaceProjection.h"
#include "DebugRenderSceneProxy.h"
#include "DynamicMeshBuilder.h"
#include "GridWorldModule.h"
#include "Navigation/GridNavigationData.h"
#include "SceneView.h"

namespace UE::GridWorld::Private
{
	class FGridNavigationSceneProxy final : public FDebugRenderSceneProxy
	{
	public:
		explicit FGridNavigationSceneProxy(const UPrimitiveComponent* InComponent)
			: FDebugRenderSceneProxy(InComponent)
		{
			DrawType = EDrawType::WireMesh;
			const AGridNavigationData* NavData = Cast<AGridNavigationData>(InComponent->GetOwner());
			const FGridWorldSnapshotPtr Snapshot = NavData != nullptr ? NavData->GetSnapshot() : nullptr;
			if (!Snapshot.IsValid() || NavData == nullptr)
			{
				return;
			}

			if (NavData->bDrawCells)
			{
				static constexpr FColor SurfaceColor(90, 230, 115, 72);
				static constexpr FColor BorderColor(15, 95, 35, 180);
				static constexpr FColor ValidCellColor(35, 180, 70, 150);
				FMesh SurfaceMesh;
				SurfaceMesh.Color = SurfaceColor;
				for (const FGridCellData& Cell : Snapshot->Cells)
				{
					const FGridRegionData* Region = Snapshot->FindRegion(Cell.Id.GridId);
					if (Region == nullptr || !Cell.bWalkable)
					{
						continue;
					}

					FVector SurfaceVertices[4];
					if (UE::GridWorld::Debug::BuildProjectedCellQuad(*Region, Cell, SurfaceVertices))
					{
						const uint32 FirstVertex = SurfaceMesh.Vertices.Num();
						SurfaceMesh.Vertices.Emplace(FVector3f(SurfaceVertices[0]), FVector2f(0.0f, 0.0f), SurfaceColor);
						SurfaceMesh.Vertices.Emplace(FVector3f(SurfaceVertices[1]), FVector2f(1.0f, 0.0f), SurfaceColor);
						SurfaceMesh.Vertices.Emplace(FVector3f(SurfaceVertices[2]), FVector2f(1.0f, 1.0f), SurfaceColor);
						SurfaceMesh.Vertices.Emplace(FVector3f(SurfaceVertices[3]), FVector2f(0.0f, 1.0f), SurfaceColor);
						SurfaceMesh.Indices.Append({
							FirstVertex,
							FirstVertex + 1,
							FirstVertex + 2,
							FirstVertex,
							FirstVertex + 2,
							FirstVertex + 3});
						for (int32 CornerIndex = 0; CornerIndex < UE_ARRAY_COUNT(SurfaceVertices); ++CornerIndex)
						{
							SurfaceMesh.Box += SurfaceVertices[CornerIndex];
							Lines.Emplace(
								SurfaceVertices[CornerIndex],
								SurfaceVertices[(CornerIndex + 1) % UE_ARRAY_COUNT(SurfaceVertices)],
								BorderColor,
								1.5f);
						}
					}

					FColor CellColor = ValidCellColor;
					if (NavData->bDrawOccupancy && Cell.bOccupied)
					{
						CellColor = Cell.bOccupancyBlocks ? FColor::Orange : FColor::Yellow;
					}
					else if (NavData->bDrawCosts && Cell.TraversalCost != 1000)
					{
						const float CostAlpha = FMath::Clamp((Cell.TraversalCost - 1000) / 4000.0f, 0.0f, 1.0f);
						CellColor = FLinearColor::LerpUsingHSV(FLinearColor::Green, FLinearColor::Red, CostAlpha).ToFColor(true);
					}
					const FVector HalfExtent(
						Region->GridTransform.CellSize.X * 0.5,
						Region->GridTransform.CellSize.Y * 0.5,
						Region->GridTransform.CellSize.Z * 0.5);
					const FTransform CellTransform(Region->GridTransform.Rotation, Cell.WorldCenter);
					Boxes.Emplace(FBox(-HalfExtent, HalfExtent), CellColor, CellTransform, EDrawType::WireMesh, 1.25f);
					if (NavData->bDrawCosts)
					{
						Texts.Emplace(FString::Printf(TEXT("%d"), Cell.TraversalCost), Cell.WorldCenter + FVector(0.0, 0.0, 8.0), FLinearColor::White);
					}
				}
				if (!SurfaceMesh.Indices.IsEmpty())
				{
					Meshes.Add(SurfaceMesh);
				}
			}

			if (NavData->bDrawTrafficReservations)
			{
				static constexpr FColor ReservedFillColor(40, 210, 230, 96);
				static constexpr FColor ReservedBorderColor(5, 115, 145, 220);
				static constexpr FColor WaitingFillColor(255, 145, 35, 82);
				static constexpr FColor WaitingBorderColor(190, 75, 5, 230);
				static constexpr FColor ConflictColor(240, 35, 35, 230);
				FMesh ReservedMesh;
				ReservedMesh.Color = ReservedFillColor;
				FMesh WaitingMesh;
				WaitingMesh.Color = WaitingFillColor;

				const auto AddReservationCell = [
					this,
					&Snapshot,
					&ReservedMesh,
					&WaitingMesh](
					const FGridTrafficCellLocation& CellLocation,
					const FVector& OwnerLocation,
					const FGuid& OwnerId,
					bool bWaiting)
				{
					const FGridCellData* Cell = Snapshot->FindCell(CellLocation.CellId);
					const FGridRegionData* Region = Cell != nullptr ? Snapshot->FindRegion(Cell->Id.GridId) : nullptr;
					if (Cell == nullptr || Region == nullptr)
					{
						return;
					}

					FVector Vertices[4];
					if (!UE::GridWorld::Debug::BuildProjectedCellQuad(*Region, *Cell, Vertices))
					{
						return;
					}
					FMesh& Mesh = bWaiting ? WaitingMesh : ReservedMesh;
					const FColor FillColor = bWaiting ? WaitingFillColor : ReservedFillColor;
					const FColor BorderColor = bWaiting ? WaitingBorderColor : ReservedBorderColor;
					const uint32 FirstVertex = Mesh.Vertices.Num();
					Mesh.Vertices.Emplace(FVector3f(Vertices[0]), FVector2f(0.0f, 0.0f), FillColor);
					Mesh.Vertices.Emplace(FVector3f(Vertices[1]), FVector2f(1.0f, 0.0f), FillColor);
					Mesh.Vertices.Emplace(FVector3f(Vertices[2]), FVector2f(1.0f, 1.0f), FillColor);
					Mesh.Vertices.Emplace(FVector3f(Vertices[3]), FVector2f(0.0f, 1.0f), FillColor);
					Mesh.Indices.Append({
						FirstVertex,
						FirstVertex + 1,
						FirstVertex + 2,
						FirstVertex,
						FirstVertex + 2,
						FirstVertex + 3});
					for (int32 CornerIndex = 0; CornerIndex < UE_ARRAY_COUNT(Vertices); ++CornerIndex)
					{
						Mesh.Box += Vertices[CornerIndex];
						Lines.Emplace(
							Vertices[CornerIndex],
							Vertices[(CornerIndex + 1) % UE_ARRAY_COUNT(Vertices)],
							BorderColor,
							2.5f);
					}

					const FVector HalfExtent(
						Region->GridTransform.CellSize.X * 0.5,
						Region->GridTransform.CellSize.Y * 0.5,
						Region->GridTransform.CellSize.Z * 0.5);
					Boxes.Emplace(
						FBox(-HalfExtent, HalfExtent),
						BorderColor,
						FTransform(Region->GridTransform.Rotation, Cell->WorldCenter),
						EDrawType::WireMesh,
						2.0f);

					const FVector CellAnchor = Cell->WorldCenter + FVector(0.0, 0.0, 28.0);
					const FVector OwnerAnchor = OwnerLocation + FVector(0.0, 0.0, 28.0);
					Lines.Emplace(CellAnchor, OwnerAnchor, BorderColor, 1.75f);
					Texts.Emplace(
						FString::Printf(TEXT("RES %s"), *OwnerId.ToString(EGuidFormats::Short)),
						CellAnchor + FVector(0.0, 0.0, 12.0),
						FLinearColor(BorderColor));
				};

				const FGridTrafficReservationSnapshotPtr TrafficSnapshot = NavData->GetTrafficReservationSnapshot();
				if (TrafficSnapshot.IsValid())
				{
					for (const FGridTrafficReservationDebugData& Entry : TrafficSnapshot->DebugEntries)
					{
						for (const FGridTrafficCellLocation& ReservedCell : Entry.ReservedFutureCells)
						{
							AddReservationCell(ReservedCell, Entry.OwnerLocation, Entry.OwnerId, false);
						}
						for (const FGridTrafficCellLocation& WaitingCell : Entry.WaitingFutureCells)
						{
							const bool bAlreadyGranted = Entry.ReservedFutureCells.ContainsByPredicate(
								[&WaitingCell](const FGridTrafficCellLocation& ReservedCell)
								{
									return ReservedCell.CellId == WaitingCell.CellId;
								});
							if (!bAlreadyGranted)
							{
								AddReservationCell(WaitingCell, Entry.OwnerLocation, Entry.OwnerId, true);
							}
						}
						if (Entry.bWaiting && !Entry.BlockingCellCenter.IsNearlyZero())
						{
							const FVector ConflictCenter = Entry.BlockingCellCenter + FVector(0.0, 0.0, 32.0);
							Boxes.Emplace(
								FBox(ConflictCenter - FVector(12.0, 12.0, 6.0), ConflictCenter + FVector(12.0, 12.0, 6.0)),
								ConflictColor,
								EDrawType::WireMesh,
								3.0f);
						}
					}
				}
				if (!ReservedMesh.Indices.IsEmpty())
				{
					Meshes.Add(MoveTemp(ReservedMesh));
				}
				if (!WaitingMesh.Indices.IsEmpty())
				{
					Meshes.Add(MoveTemp(WaitingMesh));
				}
			}

			if (NavData->bDrawLinks)
			{
				for (const FGridLinkData& Link : Snapshot->Links)
				{
					if (Snapshot->Cells.IsValidIndex(Link.FromCellIndex) && Snapshot->Cells.IsValidIndex(Link.ToCellIndex))
					{
						ArrowLines.Emplace(
							Snapshot->Cells[Link.FromCellIndex].WorldCenter + FVector(0.0, 0.0, 12.0),
							Snapshot->Cells[Link.ToCellIndex].WorldCenter + FVector(0.0, 0.0, 12.0),
							FColor::Cyan,
							12.0f);
					}
				}
			}

			if (NavData->bDrawChunks)
			{
				for (const TPair<FGridChunkCoord, FGridChunkData>& Pair : Snapshot->Chunks)
				{
					Boxes.Emplace(Pair.Value.WorldBounds, FColor::Blue, EDrawType::WireMesh, 2.0f);
				}
			}

			if (NavData->bDrawDirtyRegions)
			{
				for (const FGridChunkCoord& DirtyChunk : NavData->GetLastDirtyChunks())
				{
					if (const FGridChunkData* Chunk = Snapshot->Chunks.Find(DirtyChunk))
					{
						Boxes.Emplace(Chunk->WorldBounds.ExpandBy(3.0), FColor::Magenta, EDrawType::WireMesh, 3.0f);
					}
				}
			}

			if (NavData->bDrawErrors)
			{
				int32 ErrorIndex = 0;
				for (const FString& Error : NavData->GetLastValidationErrors())
				{
					Texts.Emplace(Error, NavData->GetActorLocation() + FVector(0.0, 0.0, 30.0 + ErrorIndex * 16.0), FLinearColor::Red);
					++ErrorIndex;
				}
			}

			TArray<TArray<FVector>> PathPointSets;
			TArray<FVector> ReachablePoints;
			TArray<FVector> RequiredStopPoints;
			TArray<FGridCenterGateDebugData> CenterGates;
			TArray<FGridPathDriveDebugData> DriveData;
			TArray<FGridAgentAvoidanceDebugData> AgentAvoidanceData;
			NavData->GetDebugQueryData(PathPointSets, ReachablePoints, RequiredStopPoints, CenterGates, DriveData, AgentAvoidanceData);
			if (NavData->bDrawPaths)
			{
				for (const TArray<FVector>& PathPoints : PathPointSets)
				{
					for (int32 PointIndex = 1; PointIndex < PathPoints.Num(); ++PointIndex)
					{
						Lines.Emplace(PathPoints[PointIndex - 1] + FVector(0.0, 0.0, 18.0), PathPoints[PointIndex] + FVector(0.0, 0.0, 18.0), FColor::Blue, 3.0f);
					}
				}
				for (const FVector& StopPoint : RequiredStopPoints)
				{
					const FVector MarkerCenter = StopPoint + FVector(0.0, 0.0, 20.0);
					Boxes.Emplace(
						FBox(MarkerCenter - FVector(6.0, 6.0, 3.0), MarkerCenter + FVector(6.0, 6.0, 3.0)),
						FColor::Yellow,
						EDrawType::WireMesh,
						2.0f);
				}
				for (const FGridCenterGateDebugData& Gate : CenterGates)
				{
					const FColor GateColor = Gate.Style == EGridPathFollowingStyle::CellByCell
						? FColor(205, 80, 255)
						: FColor(255, 145, 35);
					const FVector GateBase = Gate.Center + Gate.Up * 3.0f;
					const FVector GateTopOffset = Gate.Up * 30.0f;
					const FVector LeftBase = GateBase - Gate.Tangent * Gate.HalfWidth;
					const FVector RightBase = GateBase + Gate.Tangent * Gate.HalfWidth;
					Lines.Emplace(LeftBase, RightBase, GateColor, 2.5f);
					Lines.Emplace(LeftBase, LeftBase + GateTopOffset, GateColor, 2.5f);
					Lines.Emplace(RightBase, RightBase + GateTopOffset, GateColor, 2.5f);
					Lines.Emplace(LeftBase + GateTopOffset, RightBase + GateTopOffset, GateColor, 2.5f);
					ArrowLines.Emplace(
						GateBase + GateTopOffset * 0.5f,
						GateBase + GateTopOffset * 0.5f + Gate.Forward * 20.0f,
						GateColor,
						7.0f);
				}
				for (const FGridPathDriveDebugData& Drive : DriveData)
				{
					FString DriveLabel = Drive.DriveMode == EGridPathDriveMode::Accelerated
						? TEXT("ACCELERATED")
						: TEXT("DIRECT VELOCITY");
					if (Drive.DriveMode == EGridPathDriveMode::DirectVelocity && Drive.bUseAcceleratedFinalApproach)
					{
						DriveLabel += TEXT(" / ACCELERATED FINAL");
					}
					Texts.Emplace(
						MoveTemp(DriveLabel),
						Drive.Location + FVector(0.0, 0.0, 42.0),
						Drive.DriveMode == EGridPathDriveMode::DirectVelocity ? FLinearColor(0.0f, 1.0f, 1.0f) : FLinearColor::Blue);
				}
				for (const FGridAgentAvoidanceDebugData& Avoidance : AgentAvoidanceData)
				{
					static constexpr FColor LookAheadColor(65, 220, 255, 190);
					for (int32 CellIndex = 0; CellIndex < Avoidance.LookAheadCellCenters.Num(); ++CellIndex)
					{
						const FVector Center = Avoidance.LookAheadCellCenters[CellIndex] + FVector(0.0, 0.0, 24.0);
						Boxes.Emplace(
							FBox(Center - FVector(4.0, 4.0, 2.0), Center + FVector(4.0, 4.0, 2.0)),
							LookAheadColor,
							EDrawType::WireMesh,
							1.5f);
						if (CellIndex > 0)
						{
							Lines.Emplace(
								Avoidance.LookAheadCellCenters[CellIndex - 1] + FVector(0.0, 0.0, 24.0),
								Center,
								LookAheadColor,
								2.0f);
						}
					}

					FString AvoidanceLabel = TEXT("AGENT LOOKAHEAD");
					FLinearColor AvoidanceColor = FLinearColor(0.25f, 0.85f, 1.0f);
					if (Avoidance.State == EGridAgentAvoidanceDebugState::Yielding)
					{
						AvoidanceLabel = TEXT("YIELDING");
						AvoidanceColor = FLinearColor(1.0f, 0.55f, 0.05f);
					}
					else if (Avoidance.State == EGridAgentAvoidanceDebugState::WaitingReservation)
					{
						AvoidanceLabel = TEXT("WAITING RESERVATION");
						AvoidanceColor = FLinearColor(1.0f, 0.45f, 0.05f);
					}
					else if (Avoidance.State == EGridAgentAvoidanceDebugState::Repathing)
					{
						AvoidanceLabel = TEXT("REPATHING");
						AvoidanceColor = FLinearColor::Red;
					}
					Texts.Emplace(MoveTemp(AvoidanceLabel), Avoidance.LabelLocation + FVector(0.0, 0.0, 58.0), AvoidanceColor);
					if (Avoidance.BlockingOccupantId.IsValid())
					{
						const FVector BlockedCenter = Avoidance.BlockingCellCenter + FVector(0.0, 0.0, 28.0);
						Boxes.Emplace(
							FBox(BlockedCenter - FVector(10.0, 10.0, 5.0), BlockedCenter + FVector(10.0, 10.0, 5.0)),
							FColor::Red,
							EDrawType::WireMesh,
							3.0f);
						Texts.Emplace(
							FString::Printf(TEXT("BLOCKER %s"), *Avoidance.BlockingOccupantId.ToString(EGuidFormats::Short)),
							BlockedCenter + FVector(0.0, 0.0, 18.0),
							FLinearColor::Red);
					}
				}
			}
			if (NavData->bDrawReachability)
			{
				for (const FVector& ReachablePoint : ReachablePoints)
				{
					Boxes.Emplace(FBox(ReachablePoint - FVector(5.0), ReachablePoint + FVector(5.0)), FColor::Cyan, EDrawType::WireMesh, 1.5f);
				}
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			const bool bVisible = IsGridWorldVisualDebugEnabled()
				&& View != nullptr
				&& View->Family != nullptr
				&& View->Family->EngineShowFlags.Navigation
				&& IsShown(View);

			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = bVisible;
			Result.bDynamicRelevance = true;
			Result.bSeparateTranslucency = Result.bNormalTranslucency = bVisible;
			return Result;
		}
	};
}

UGridNavigationRenderingComponent::UGridNavigationRenderingComponent()
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	bSelectable = false;
}

#if UE_ENABLE_DEBUG_DRAWING
FDebugRenderSceneProxy* UGridNavigationRenderingComponent::CreateDebugSceneProxy()
{
	return IsGridWorldVisualDebugEnabled()
		? new UE::GridWorld::Private::FGridNavigationSceneProxy(this)
		: nullptr;
}
#endif

FBoxSphereBounds UGridNavigationRenderingComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const AGridNavigationData* NavData = Cast<AGridNavigationData>(GetOwner());
	const FBox LocalBounds = NavData != nullptr ? NavData->GetBounds() : FBox(FVector(-25.0, -25.0, -100.0), FVector(25.0, 25.0, 100.0));
	return FBoxSphereBounds(LocalBounds);
}
