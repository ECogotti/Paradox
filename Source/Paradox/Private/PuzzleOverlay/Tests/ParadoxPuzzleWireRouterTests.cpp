#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PuzzleOverlay/ParadoxPuzzleWireRouter.h"
#include "Tasks/Task.h"

namespace ParadoxPuzzleWireRouterTests
{
	FParadoxPuzzleRoutingSnapshot MakeLegacySnapshot()
	{
		FParadoxPuzzleRoutingSnapshot Snapshot;
		Snapshot.Settings.Algorithm = EParadoxPuzzleRoutingAlgorithm::LegacyIndependent;
		return Snapshot;
	}

	FParadoxPuzzleRoutingLink MakeLink(
		const int32 StableOrder,
		const EParadoxPuzzleWireDirection Direction,
		const FParadoxPuzzleRoutingCoord& Source,
		const FParadoxPuzzleRoutingCoord& Target)
	{
		FParadoxPuzzleRoutingLink Link;
		Link.Direction = Direction;
		Link.Source = Source;
		Link.Target = Target;
		Link.RemoteEndpointKey = FString::Printf(TEXT("Endpoint_%03d"), StableOrder);
		Link.StableOrder = StableOrder;
		return Link;
	}

	FParadoxPuzzleWireEndpointBounds MakeBounds(
		const TCHAR* EndpointKey,
		const FVector& Min,
		const FVector& Max)
	{
		FParadoxPuzzleWireEndpointBounds Bounds;
		Bounds.EndpointKey = EndpointKey;
		Bounds.Min = Min;
		Bounds.Max = Max;
		Bounds.bValid = true;
		return Bounds;
	}

	bool IsOrthogonal(const FParadoxPuzzleWireSegment& Segment)
	{
		const FVector Delta = Segment.End - Segment.Start;
		const int32 ChangedAxes = static_cast<int32>(!FMath::IsNearlyZero(Delta.X))
			+ static_cast<int32>(!FMath::IsNearlyZero(Delta.Y))
			+ static_cast<int32>(!FMath::IsNearlyZero(Delta.Z));
		return ChangedAxes == 1;
	}

	bool SegmentEntersOpenBounds(
		const FParadoxPuzzleWireSegment& Segment,
		const FParadoxPuzzleWireEndpointBounds& Bounds)
	{
		if (!Bounds.bValid)
		{
			return false;
		}
		const FVector Delta = Segment.End - Segment.Start;
		if (!FMath::IsNearlyZero(Delta.X))
		{
			return Segment.Start.Y > Bounds.Min.Y && Segment.Start.Y < Bounds.Max.Y
				&& Segment.Start.Z > Bounds.Min.Z && Segment.Start.Z < Bounds.Max.Z
				&& FMath::Max(FMath::Min(Segment.Start.X, Segment.End.X), Bounds.Min.X)
					< FMath::Min(FMath::Max(Segment.Start.X, Segment.End.X), Bounds.Max.X);
		}
		if (!FMath::IsNearlyZero(Delta.Y))
		{
			return Segment.Start.X > Bounds.Min.X && Segment.Start.X < Bounds.Max.X
				&& Segment.Start.Z > Bounds.Min.Z && Segment.Start.Z < Bounds.Max.Z
				&& FMath::Max(FMath::Min(Segment.Start.Y, Segment.End.Y), Bounds.Min.Y)
					< FMath::Min(FMath::Max(Segment.Start.Y, Segment.End.Y), Bounds.Max.Y);
		}
		return Segment.Start.X > Bounds.Min.X && Segment.Start.X < Bounds.Max.X
			&& Segment.Start.Y > Bounds.Min.Y && Segment.Start.Y < Bounds.Max.Y
			&& FMath::Max(FMath::Min(Segment.Start.Z, Segment.End.Z), Bounds.Min.Z)
				< FMath::Min(FMath::Max(Segment.Start.Z, Segment.End.Z), Bounds.Max.Z);
	}

	bool HasLane(const FParadoxPuzzleWireRoute& Route, const int32 Lane)
	{
		return Route.Segments.ContainsByPredicate([Lane](const FParadoxPuzzleWireSegment& Segment)
		{
			return Segment.Lane == Lane;
		});
	}

	bool IsContinuous(const FParadoxPuzzleWireRoute& Route)
	{
		for (int32 Index = 1; Index < Route.Segments.Num(); ++Index)
		{
			if (!Route.Segments[Index - 1].End.Equals(Route.Segments[Index].Start, KINDA_SMALL_NUMBER))
			{
				return false;
			}
		}
		return true;
	}

	int32 CountMainCorners(const FParadoxPuzzleWireRoute& Route)
	{
		TOptional<EParadoxPuzzleWireAxis> PreviousAxis;
		int32 Count = 0;
		for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
		{
			if (Segment.Kind == EParadoxPuzzleWireSegmentKind::EndpointTerminal
				|| Segment.Kind == EParadoxPuzzleWireSegmentKind::BridgeHorizontal
				|| Segment.Kind == EParadoxPuzzleWireSegmentKind::BridgeVertical)
			{
				continue;
			}
			if (PreviousAxis.IsSet() && PreviousAxis.GetValue() != Segment.Axis)
			{
				++Count;
			}
			PreviousAxis = Segment.Axis;
		}
		return Count;
	}

	uint32 HashResult(const FParadoxPuzzleRoutingResult& Result)
	{
		uint32 Hash = GetTypeHash(Result.Routes.Num());
		for (const FParadoxPuzzleWireRoute& Route : Result.Routes)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Route.StableOrder));
			for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
			{
				Hash = HashCombineFast(Hash, GetTypeHash(Segment.Start));
				Hash = HashCombineFast(Hash, GetTypeHash(Segment.End));
				Hash = HashCombineFast(Hash, GetTypeHash(Segment.Lane));
				Hash = HashCombineFast(Hash, static_cast<uint32>(Segment.Kind));
			}
		}
		return Hash;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterBoundsPortsTest,
	"Paradox.PuzzleOverlay.Router.BoundsPortsAndTerminalOrthogonality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterBoundsPortsTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
	FParadoxPuzzleRoutingLink Link = MakeLink(
		0,
		EParadoxPuzzleWireDirection::Input,
		{-3, 0, 0.0},
		{0, 0, 0.0});
	Link.SourceBounds = MakeBounds(TEXT("Source"), FVector(-350.0, -50.0, -20.0), FVector(-250.0, 50.0, 20.0));
	Link.TargetBounds = MakeBounds(TEXT("Target"), FVector(-50.0, -60.0, -20.0), FVector(50.0, 60.0, 20.0));
	Snapshot.Links.Add(Link);

	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Bounded endpoints produce one route"), Result.Routes.Num(), 1))
	{
		return false;
	}
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	TestTrue(TEXT("Source port is valid"), Route.SourcePort.bValid);
	TestTrue(TEXT("Target port is valid"), Route.TargetPort.bValid);
	TestEqual(TEXT("Source uses the face toward the target"), Route.SourcePort.Side, EParadoxPuzzlePortSide::East);
	TestEqual(TEXT("Target uses the face toward the source"), Route.TargetPort.Side, EParadoxPuzzlePortSide::West);
	TestTrue(TEXT("Source begins exactly on its bounds"), Route.RoutePoints[0].Equals(Route.SourcePort.Position));
	TestTrue(TEXT("Target ends exactly on its bounds"), Route.RoutePoints.Last().Equals(Route.TargetPort.Position));
	TestTrue(TEXT("First segment follows the source outward normal"),
		(Route.Segments[0].End - Route.Segments[0].Start).GetSafeNormal().Equals(Route.SourcePort.Normal));
	TestTrue(TEXT("Last segment approaches opposite the target outward normal"),
		(Route.Segments.Last().End - Route.Segments.Last().Start).GetSafeNormal().Equals(-Route.TargetPort.Normal));
	TestEqual(TEXT("Source terminal is not lane-offset"), Route.Segments[0].Lane, 0);
	TestEqual(TEXT("Target terminal is not lane-offset"), Route.Segments.Last().Lane, 0);
	for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
	{
		TestFalse(TEXT("No main segment enters the source volume"), SegmentEntersOpenBounds(Segment, Link.SourceBounds));
		TestFalse(TEXT("No main segment enters the target volume"), SegmentEntersOpenBounds(Segment, Link.TargetBounds));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterSingleConnectionCenteredPortsTest,
	"Paradox.PuzzleOverlay.Router.SingleConnectionUsesCenteredPorts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterSingleConnectionCenteredPortsTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
	FParadoxPuzzleRoutingLink Link = MakeLink(
		0,
		EParadoxPuzzleWireDirection::Output,
		{-3, 1, 0.0},
		{3, 1, 0.0});
	Link.SourceBounds = MakeBounds(
		TEXT("SingleSource"),
		FVector(-350.0, 23.0, -10.0),
		FVector(-250.0, 83.0, 10.0));
	Link.TargetBounds = MakeBounds(
		TEXT("SingleTarget"),
		FVector(250.0, 13.0, -10.0),
		FVector(350.0, 93.0, 10.0));
	Snapshot.Links.Add(Link);

	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Single connection routes"), Result.Routes.Num(), 1))
	{
		return false;
	}
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	TestTrue(TEXT("Source port uses the exact face centre"),
		FMath::IsNearlyEqual(Route.SourcePort.Position.Y, Link.SourceBounds.GetCenter().Y));
	TestTrue(TEXT("Target port uses the exact face centre"),
		FMath::IsNearlyEqual(Route.TargetPort.Position.Y, Link.TargetBounds.GetCenter().Y));
	TestEqual(TEXT("Single source face reports one slot"), Route.SourcePort.FaceSlotCount, 1);
	TestEqual(TEXT("Single target face reports one slot"), Route.TargetPort.FaceSlotCount, 1);
	TestTrue(TEXT("Single source port has no centre displacement"),
		FMath::IsNearlyZero(Route.SourcePort.NormalizedDistanceFromFaceCenter));
	TestTrue(TEXT("Single target port has no centre displacement"),
		FMath::IsNearlyZero(Route.TargetPort.NormalizedDistanceFromFaceCenter));
	TestTrue(TEXT("Route starts on the source box"), Route.RoutePoints[0].Equals(Route.SourcePort.Position));
	TestTrue(TEXT("Route ends on the target box"), Route.RoutePoints.Last().Equals(Route.TargetPort.Position));
	TestTrue(TEXT("Single-link source uses only base clearance"),
		FMath::IsNearlyEqual(
			FVector::Distance(Route.SourcePort.Position, Route.SourcePort.ClearancePoint),
			Snapshot.Settings.EndpointClearance));
	TestTrue(TEXT("Single-link target uses only base clearance"),
		FMath::IsNearlyEqual(
			FVector::Distance(Route.TargetPort.Position, Route.TargetPort.ClearancePoint),
			Snapshot.Settings.EndpointClearance));
	for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
	{
		TestTrue(TEXT("Single connection remains X/Y/Z orthogonal"), IsOrthogonal(Segment));
		TestFalse(TEXT("Single connection never enters source bounds"),
			SegmentEntersOpenBounds(Segment, Link.SourceBounds));
		TestFalse(TEXT("Single connection never enters target bounds"),
			SegmentEntersOpenBounds(Segment, Link.TargetBounds));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterDirectionalFacesTest,
	"Paradox.PuzzleOverlay.Router.DirectionalFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterDirectionalFacesTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	struct FCase
	{
		FVector TargetCenter;
		EParadoxPuzzlePortSide SourceSide;
		EParadoxPuzzlePortSide TargetSide;
	};
	const TArray<FCase> Cases{
		{FVector(300.0, 0.0, 0.0), EParadoxPuzzlePortSide::East, EParadoxPuzzlePortSide::West},
		{FVector(-300.0, 0.0, 0.0), EParadoxPuzzlePortSide::West, EParadoxPuzzlePortSide::East},
		{FVector(0.0, 300.0, 0.0), EParadoxPuzzlePortSide::North, EParadoxPuzzlePortSide::South},
		{FVector(0.0, -300.0, 0.0), EParadoxPuzzlePortSide::South, EParadoxPuzzlePortSide::North}};
	for (int32 Index = 0; Index < Cases.Num(); ++Index)
	{
		const FCase& Case = Cases[Index];
		FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
		FParadoxPuzzleRoutingLink Link = MakeLink(
			Index,
			EParadoxPuzzleWireDirection::Output,
			{0, 0, 0.0},
			{FMath::RoundToInt32(Case.TargetCenter.X / 100.0), FMath::RoundToInt32(Case.TargetCenter.Y / 100.0), 0.0});
		Link.LinkKind = Index == 0 ? EPuzzleGraphLinkKind::GateInfluence : EPuzzleGraphLinkKind::PrimarySignal;
		Link.SourceBounds = MakeBounds(TEXT("DirectionalSource"), FVector(-40.0, -30.0, -10.0), FVector(40.0, 30.0, 10.0));
		Link.TargetBounds = MakeBounds(
			*FString::Printf(TEXT("DirectionalTarget_%d"), Index),
			Case.TargetCenter - FVector(50.0, 40.0, 10.0),
			Case.TargetCenter + FVector(50.0, 40.0, 10.0));
		Snapshot.Links.Add(Link);
		const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
		if (!TestEqual(TEXT("Directional case is routable"), Result.Routes.Num(), 1))
		{
			continue;
		}
		TestEqual(TEXT("Source directional face"), Result.Routes[0].SourcePort.Side, Case.SourceSide);
		TestEqual(TEXT("Target directional face"), Result.Routes[0].TargetPort.Side, Case.TargetSide);
		TestEqual(TEXT("Link kind is preserved"), Result.Routes[0].LinkKind, Link.LinkKind);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterNetworkBoundsFaceSelectionTest,
	"Paradox.PuzzleOverlay.Router.NetworkBoundsBeforeShortestFacePair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterNetworkBoundsFaceSelectionTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
	Snapshot.Settings.MaxRerouteAttempts = 0;
	FParadoxPuzzleRoutingLink PrimaryLink = MakeLink(
		0,
		EParadoxPuzzleWireDirection::Input,
		{-3, 0, 0.0},
		{3, 0, 0.0});
	PrimaryLink.SourceBounds = MakeBounds(
		TEXT("Source"),
		FVector(-350.0, -50.0, -20.0),
		FVector(-250.0, 50.0, 20.0));
	PrimaryLink.TargetBounds = MakeBounds(
		TEXT("Target"),
		FVector(250.0, -50.0, -20.0),
		FVector(350.0, 50.0, 20.0));
	Snapshot.Links.Add(PrimaryLink);

	const FParadoxPuzzleWireEndpointBounds ObstacleBounds = MakeBounds(
		TEXT("IntermediateNetworkActor"),
		FVector(-50.0, -50.0, -20.0),
		FVector(50.0, 50.0, 20.0));
	FParadoxPuzzleRoutingLink ObstacleLink = MakeLink(
		1,
		EParadoxPuzzleWireDirection::Output,
		{0, 0, 0.0},
		{0, 5, 0.0});
	ObstacleLink.SourceBounds = ObstacleBounds;
	ObstacleLink.TargetBounds = MakeBounds(
		TEXT("ObstacleConsumer"),
		FVector(-50.0, 450.0, -20.0),
		FVector(50.0, 550.0, 20.0));
	Snapshot.Links.Add(ObstacleLink);

	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	const FParadoxPuzzleWireRoute* PrimaryRoute = Result.Routes.FindByPredicate(
		[](const FParadoxPuzzleWireRoute& Route)
		{
			return Route.StableOrder == 0;
		});
	if (!TestNotNull(TEXT("The primary relationship remains routable"), PrimaryRoute))
	{
		return false;
	}
	TestFalse(TEXT("The blocked shortest facing pair is not selected"),
		PrimaryRoute->SourcePort.Side == EParadoxPuzzlePortSide::East
		&& PrimaryRoute->TargetPort.Side == EParadoxPuzzlePortSide::West);
	const double ExpectedShortestRemainingDistance = FMath::Sqrt(305000.0);
	TestTrue(TEXT("The shortest obstacle-free face pair is selected"),
		FMath::IsNearlyEqual(
			FVector::Distance(PrimaryRoute->SourcePort.Position, PrimaryRoute->TargetPort.Position),
			ExpectedShortestRemainingDistance));
	for (const FParadoxPuzzleWireSegment& Segment : PrimaryRoute->Segments)
	{
		TestFalse(TEXT("No final segment enters another network Actor bounds"),
			SegmentEntersOpenBounds(Segment, ObstacleBounds));
	}
	TestEqual(TEXT("An obstacle-free face pair avoids network fallback"),
		Result.Diagnostics.NetworkBoundsFallbackCount,
		0);

	FParadoxPuzzleRoutingSnapshot UnobstructedSnapshot = MakeLegacySnapshot();
	UnobstructedSnapshot.Settings.MaxRerouteAttempts = 0;
	UnobstructedSnapshot.Links.Add(PrimaryLink);
	const FParadoxPuzzleRoutingResult UnobstructedResult = FParadoxPuzzleWireRouter::CalculateRoutes(
		UnobstructedSnapshot);
	if (TestEqual(TEXT("The unobstructed relationship routes"), UnobstructedResult.Routes.Num(), 1))
	{
		TestEqual(TEXT("Without an obstacle the closest source face is selected"),
			UnobstructedResult.Routes[0].SourcePort.Side,
			EParadoxPuzzlePortSide::East);
		TestEqual(TEXT("Without an obstacle the closest target face is selected"),
			UnobstructedResult.Routes[0].TargetPort.Side,
			EParadoxPuzzlePortSide::West);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterPortDistributionTest,
	"Paradox.PuzzleOverlay.Router.PortDistributionCompressionAndSelfLink",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterPortDistributionTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const double SourceY = (Index - 1) * 200.0;
		FParadoxPuzzleRoutingLink Link = MakeLink(
			Index,
			EParadoxPuzzleWireDirection::Input,
			{-4, Index - 1, 0.0},
			{0, 0, 0.0});
		Link.SourceBounds = MakeBounds(
			*FString::Printf(TEXT("Source_%d"), Index),
			FVector(-450.0, SourceY - 30.0, -10.0),
			FVector(-350.0, SourceY + 30.0, 10.0));
		Link.TargetBounds = MakeBounds(TEXT("SharedTarget"), FVector(-50.0, -30.0, -10.0), FVector(50.0, 30.0, 10.0));
		Snapshot.Links.Add(Link);
	}
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("All incident links route"), Result.Routes.Num(), 3))
	{
		return false;
	}
	TArray<double> TargetPortY;
	for (const FParadoxPuzzleWireRoute& Route : Result.Routes)
	{
		const FParadoxPuzzleRoutingLink& AuthoredLink = Snapshot.Links[Route.StableOrder];
		TestEqual(TEXT("Every incoming link uses the target west face"), Route.TargetPort.Side, EParadoxPuzzlePortSide::West);
		TestEqual(TEXT("Spatial ordering assigns a stable face slot"), Route.TargetPort.FaceSlotIndex, Route.StableOrder);
		TestEqual(TEXT("Each target port reports the full incident face group"), Route.TargetPort.FaceSlotCount, 3);
		TestTrue(TEXT("Multi-port fan-out remains straight beyond base clearance"),
			FMath::IsNearlyEqual(
				FVector::Distance(Route.TargetPort.Position, Route.TargetPort.ClearancePoint),
				Snapshot.Settings.EndpointClearance + Snapshot.Settings.MultiPortFanoutLength));
		TestTrue(TEXT("Multi-port route starts exactly on its source bounds"),
			Route.RoutePoints[0].Equals(Route.SourcePort.Position));
		TestTrue(TEXT("Multi-port route ends exactly on its target bounds"),
			Route.RoutePoints.Last().Equals(Route.TargetPort.Position));
		for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
		{
			TestTrue(TEXT("Multi-port fan-out remains X/Y/Z orthogonal"), IsOrthogonal(Segment));
			TestFalse(TEXT("Final multi-port geometry never enters source bounds"),
				SegmentEntersOpenBounds(Segment, AuthoredLink.SourceBounds));
			TestFalse(TEXT("Final multi-port geometry never enters target bounds"),
				SegmentEntersOpenBounds(Segment, AuthoredLink.TargetBounds));
		}
		TargetPortY.Add(Route.TargetPort.Position.Y);
	}
	TargetPortY.Sort();
	const double ThreePortSpacing = 15.0;
	TestTrue(
		TEXT("Dynamically spaced ports remain distinct"),
		TargetPortY[0] < TargetPortY[1] && TargetPortY[1] < TargetPortY[2]);
	TestTrue(
		TEXT("Dynamically spaced ports remain inside the edge inset"),
		TargetPortY[0] >= -22.0 && TargetPortY[2] <= 22.0);
	TestTrue(TEXT("Odd port groups divide the full face into equal edge and internal gaps"),
		FMath::IsNearlyEqual(TargetPortY[0], -ThreePortSpacing)
		&& FMath::IsNearlyZero(TargetPortY[1])
		&& FMath::IsNearlyEqual(TargetPortY[2], ThreePortSpacing));
	TestTrue(TEXT("Centre slot reports zero normalized distance"),
		FMath::IsNearlyZero(Result.Routes[1].TargetPort.NormalizedDistanceFromFaceCenter));

	FParadoxPuzzleRoutingSnapshot EvenSnapshot = MakeLegacySnapshot();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FParadoxPuzzleRoutingLink Link = MakeLink(
			Index,
			EParadoxPuzzleWireDirection::Input,
			{-4, Index == 0 ? -1 : 1, 0.0},
			{0, 0, 0.0});
		Link.SourceBounds = MakeBounds(
			*FString::Printf(TEXT("EvenSource_%d"), Index),
			FVector(-450.0, Index == 0 ? -130.0 : 70.0, -10.0),
			FVector(-350.0, Index == 0 ? -70.0 : 130.0, 10.0));
		Link.TargetBounds = MakeBounds(TEXT("EvenTarget"), FVector(-50.0, -50.0, -10.0), FVector(50.0, 50.0, 10.0));
		EvenSnapshot.Links.Add(Link);
	}
	const FParadoxPuzzleRoutingResult EvenResult = FParadoxPuzzleWireRouter::CalculateRoutes(EvenSnapshot);
	if (TestEqual(TEXT("Even incident group routes"), EvenResult.Routes.Num(), 2))
	{
		TArray<double> EvenPortY{
			EvenResult.Routes[0].TargetPort.Position.Y,
			EvenResult.Routes[1].TargetPort.Position.Y};
		EvenPortY.Sort();
		const double TwoPortOffset = 100.0 / 6.0;
		TestTrue(TEXT("Two ports divide the face into three equal symmetric gaps"),
			FMath::IsNearlyEqual(EvenPortY[0], -TwoPortOffset)
			&& FMath::IsNearlyEqual(EvenPortY[1], TwoPortOffset));
	}
	FParadoxPuzzleRoutingSnapshot InsetSnapshot = EvenSnapshot;
	InsetSnapshot.Settings.PortEdgeInset = 40.0;
	const FParadoxPuzzleRoutingResult InsetResult = FParadoxPuzzleWireRouter::CalculateRoutes(InsetSnapshot);
	if (TestEqual(TEXT("Minimum edge inset group routes"), InsetResult.Routes.Num(), 2))
	{
		TArray<double> InsetPortY{
			InsetResult.Routes[0].TargetPort.Position.Y,
			InsetResult.Routes[1].TargetPort.Position.Y};
		InsetPortY.Sort();
		TestTrue(TEXT("PortEdgeInset clamps edge gaps only when larger than the equal N+1 gap"),
			FMath::IsNearlyEqual(InsetPortY[0], -10.0)
			&& FMath::IsNearlyEqual(InsetPortY[1], 10.0));
	}

	FParadoxPuzzleRoutingSnapshot OutputSnapshot = MakeLegacySnapshot();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const double TargetY = (Index * 200.0) - 300.0;
		FParadoxPuzzleRoutingLink Link = MakeLink(
			Index,
			EParadoxPuzzleWireDirection::Output,
			{0, 0, 0.0},
			{4, Index - 2, 0.0});
		Link.SourceBounds = MakeBounds(
			TEXT("SharedOutputSource"),
			FVector(-50.0, -60.0, -10.0),
			FVector(50.0, 60.0, 10.0));
		Link.TargetBounds = MakeBounds(
			*FString::Printf(TEXT("OutputTarget_%d"), Index),
			FVector(350.0, TargetY - 30.0, -10.0),
			FVector(450.0, TargetY + 30.0, 10.0));
		OutputSnapshot.Links.Add(Link);
	}
	const FParadoxPuzzleRoutingResult OutputResult = FParadoxPuzzleWireRouter::CalculateRoutes(OutputSnapshot);
	if (TestEqual(TEXT("All outgoing links route"), OutputResult.Routes.Num(), 4))
	{
		TArray<double> SourcePortY;
		for (const FParadoxPuzzleWireRoute& Route : OutputResult.Routes)
		{
			TestEqual(
				TEXT("Every outgoing link uses the source east face"),
				Route.SourcePort.Side,
				EParadoxPuzzlePortSide::East);
			TestEqual(
				TEXT("Each source port reports the full outgoing face group"),
				Route.SourcePort.FaceSlotCount,
				4);
			SourcePortY.Add(Route.SourcePort.Position.Y);
		}
		SourcePortY.Sort();
		const double FourPortSpacing = 24.0;
		TestTrue(TEXT("Four-wire output group remains symmetric around the source face centre"),
			FMath::IsNearlyEqual(SourcePortY[0], -36.0)
			&& FMath::IsNearlyEqual(SourcePortY[1], -36.0 + FourPortSpacing)
			&& FMath::IsNearlyEqual(SourcePortY[2], -36.0 + 2.0 * FourPortSpacing)
			&& FMath::IsNearlyEqual(SourcePortY[3], 36.0));
	}

	FParadoxPuzzleRoutingSnapshot SelfSnapshot = MakeLegacySnapshot();
	FParadoxPuzzleRoutingLink SelfLink = MakeLink(0, EParadoxPuzzleWireDirection::Output, {0, 0, 0.0}, {0, 0, 0.0});
	SelfLink.SourceBounds = MakeBounds(TEXT("Self"), FVector(-50.0, -50.0, -10.0), FVector(50.0, 50.0, 10.0));
	SelfLink.TargetBounds = SelfLink.SourceBounds;
	SelfSnapshot.Links.Add(SelfLink);
	const FParadoxPuzzleRoutingResult SelfResult = FParadoxPuzzleWireRouter::CalculateRoutes(SelfSnapshot);
	if (!TestEqual(TEXT("Self-link remains routable"), SelfResult.Routes.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Self-link endpoints use opposite faces"),
		SelfResult.Routes[0].TargetPort.Side,
		EParadoxPuzzlePortSide::West);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterOrthogonalTest,
	"Paradox.PuzzleOverlay.Router.OrthogonalAndDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterOrthogonalTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
	Snapshot.RoutingGeneration = 7;
	Snapshot.SelectedAnchor = {0, 0, 0.0};
	Snapshot.Links.Add(MakeLink(2, EParadoxPuzzleWireDirection::Input, {-4, 3, 0.0}, {0, 0, 0.0}));
	Snapshot.Links.Add(MakeLink(1, EParadoxPuzzleWireDirection::Output, {0, 0, 0.0}, {5, -2, 0.0}));

	const FParadoxPuzzleRoutingResult First = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	Algo::Reverse(Snapshot.Links);
	const FParadoxPuzzleRoutingResult Second = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Both authored links produce routes"), First.Routes.Num(), 2);
	TestEqual(TEXT("Routing ignores registration order"), HashResult(First), HashResult(Second));
	TestEqual(TEXT("Generation is retained"), First.RoutingGeneration, int64(7));
	for (const FParadoxPuzzleWireRoute& Route : First.Routes)
	{
		for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
		{
			TestTrue(TEXT("Every segment changes exactly one axis"), IsOrthogonal(Segment));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterMinimumCornersTest,
	"Paradox.PuzzleOverlay.Router.MinimumCornersAndFinalContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterMinimumCornersTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot StraightSnapshot = MakeLegacySnapshot();
	StraightSnapshot.Settings.EndpointClearance = 0.0;
	StraightSnapshot.Links.Add(MakeLink(0, EParadoxPuzzleWireDirection::Input, {0, 0, 0.0}, {5, 0, 0.0}));
	const FParadoxPuzzleRoutingResult StraightResult = FParadoxPuzzleWireRouter::CalculateRoutes(StraightSnapshot);
	if (!TestEqual(TEXT("Straight link routes"), StraightResult.Routes.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Straight link has no topology corner"), StraightResult.Routes[0].TopologyCornerCount, 0);
	TestEqual(TEXT("Straight link has two normalized route points"), StraightResult.Routes[0].RoutePoints.Num(), 2);
	TestTrue(TEXT("Straight route is continuous"), IsContinuous(StraightResult.Routes[0]));

	FParadoxPuzzleRoutingSnapshot LSnapshot = MakeLegacySnapshot();
	LSnapshot.Settings.EndpointClearance = 0.0;
	LSnapshot.Links.Add(MakeLink(0, EParadoxPuzzleWireDirection::Input, {0, 0, 0.0}, {5, 4, 0.0}));
	const FParadoxPuzzleRoutingResult LResult = FParadoxPuzzleWireRouter::CalculateRoutes(LSnapshot);
	if (!TestEqual(TEXT("Diagonal lattice link routes"), LResult.Routes.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Diagonal lattice link uses one minimum Manhattan corner"), LResult.Routes[0].TopologyCornerCount, 1);
	TestEqual(TEXT("L route has exactly three normalized route points"), LResult.Routes[0].RoutePoints.Num(), 3);
	TestEqual(TEXT("Rendered and topology corner counts agree without a bridge"),
		LResult.Routes[0].RenderedCornerCount,
		LResult.Routes[0].TopologyCornerCount);
	TestTrue(TEXT("L route is continuous"), IsContinuous(LResult.Routes[0]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterDeprecatedCompactSettingsTest,
	"Paradox.PuzzleOverlay.Router.DeprecatedCompactSettingsCannotAddCorners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterDeprecatedCompactSettingsTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
	Snapshot.Settings.EndpointClearance = 0.0;
	// Simulates a Blueprint serialized while the compact-route experiment was enabled.
	Snapshot.Settings.MaxCompactRouteExtraCorners = 1;
	Snapshot.Settings.CompactRouteMinDeviation = 0.0;
	Snapshot.Settings.CompactRouteMinImprovementRatio = 0.0;
	Snapshot.Links.Add(MakeLink(0, EParadoxPuzzleWireDirection::Input, {0, 0, 0.0}, {8, 8, 0.0}));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Legacy compact settings still produce a route"), Result.Routes.Num(), 1))
	{
		return false;
	}
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	TestEqual(TEXT("Deprecated compact settings cannot promote an extra corner"), Route.TopologyCornerCount, 1);
	TestEqual(TEXT("Strict L route remains normalized"), Route.RoutePoints.Num(), 3);
	for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
	{
		TestTrue(TEXT("Strict route remains X/Y/Z orthogonal"), IsOrthogonal(Segment));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterCornerPriorityTest,
	"Paradox.PuzzleOverlay.Router.CornerTierDominatesCrossingCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterCornerPriorityTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
	Snapshot.Settings.EndpointClearance = 0.0;
	Snapshot.Settings.CrossingPenalty = 1000000.0;
	Snapshot.Settings.MaxCandidatesPerLink = 24;
	Snapshot.Settings.MaxRerouteAttempts = 8;
	FParadoxPuzzleWireRoute Preserved;
	Preserved.StableOrder = 0;
	Preserved.Segments.Add({FVector(-200, 0, 0), FVector(200, 0, 0), EParadoxPuzzleWireAxis::X, EParadoxPuzzleWireSegmentKind::GroundSupported, 0});
	Snapshot.PreservedRoutes.Add(Preserved);
	Snapshot.Links.Add(MakeLink(1, EParadoxPuzzleWireDirection::Input, {0, -2, 0.0}, {0, 2, 0.0}));

	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Preserved and crossing route are returned"), Result.Routes.Num(), 2))
	{
		return false;
	}
	const FParadoxPuzzleWireRoute& Route = Result.Routes[1];
	TestEqual(TEXT("Crossing does not promote a dogleg with more topology corners"), Route.TopologyCornerCount, 0);
	TestEqual(TEXT("Unavoidable minimum-corner crossing uses one bridge"), Result.Diagnostics.BridgeCount, 1);
	TestEqual(TEXT("Bridge contributes four visible direction changes"), Route.BridgeCornerCount, 4);
	TestEqual(TEXT("Rendered count includes bridge corners"), Route.RenderedCornerCount, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterMultiLinkSimplicityTest,
	"Paradox.PuzzleOverlay.Router.MultiLinkMinimumBendFixture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterMultiLinkSimplicityTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
	const FParadoxPuzzleWireEndpointBounds SharedTarget = MakeBounds(
		TEXT("SharedPlate"),
		FVector(-60.0, -80.0, -10.0),
		FVector(60.0, 80.0, 10.0));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const double SourceY = (Index - 1) * 250.0;
		FParadoxPuzzleRoutingLink Link = MakeLink(
			Index,
			EParadoxPuzzleWireDirection::Input,
			{-6, (Index - 1) * 3, 0.0},
			{0, 0, 0.0});
		Link.SourceBounds = MakeBounds(
			*FString::Printf(TEXT("InputPlate_%d"), Index),
			FVector(-660.0, SourceY - 50.0, -10.0),
			FVector(-540.0, SourceY + 50.0, 10.0));
		Link.TargetBounds = SharedTarget;
		Snapshot.Links.Add(Link);
	}
	FParadoxPuzzleRoutingLink Output = MakeLink(
		3,
		EParadoxPuzzleWireDirection::Output,
		{0, 0, 0.0},
		{5, 4, 0.0});
	Output.SourceBounds = SharedTarget;
	Output.TargetBounds = MakeBounds(
		TEXT("Door"),
		FVector(440.0, 340.0, -10.0),
		FVector(560.0, 460.0, 10.0));
	Snapshot.Links.Add(Output);

	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Three inputs and one output route"), Result.Routes.Num(), 4))
	{
		return false;
	}
	int32 SummedRenderedCorners = 0;
	for (const FParadoxPuzzleWireRoute& Route : Result.Routes)
	{
		TestTrue(TEXT("Each final route is continuous"), IsContinuous(Route));
		TestTrue(TEXT("A same-level main route has no unnecessary dogleg"), CountMainCorners(Route) <= 1);
		SummedRenderedCorners += Route.RenderedCornerCount;
	}
	TestEqual(TEXT("Diagnostics aggregate final rendered corners"),
		Result.Diagnostics.TotalRenderedCornerCount,
		SummedRenderedCorners);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterVerticalSurfaceTest,
	"Paradox.PuzzleOverlay.Router.VerticalAndSurfaceFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterVerticalSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
	Snapshot.SelectedAnchor = {0, 0, 100.0};
	Snapshot.Links.Add(MakeLink(0, EParadoxPuzzleWireDirection::Input, {-3, 2, 0.0}, {0, 0, 100.0}));
	Snapshot.SurfaceSamples.Add({-3, 2, 0}, {true, 0.0, FVector::UpVector, true});

	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Multilevel link produces one route"), Result.Routes.Num(), 1);
	bool bFoundStructuralVertical = false;
	bool bFoundUnsupported = false;
	for (const FParadoxPuzzleWireSegment& Segment : Result.Routes[0].Segments)
	{
		bFoundStructuralVertical |= Segment.Kind == EParadoxPuzzleWireSegmentKind::StructuralVertical;
		bFoundUnsupported |= Segment.Kind == EParadoxPuzzleWireSegmentKind::GroundUnsupported;
	}
	TestTrue(TEXT("Real Z difference remains an explicit structural vertical"), bFoundStructuralVertical);
	TestTrue(TEXT("Absent sampled support remains visible and marked unsupported"), bFoundUnsupported);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterLaneBridgeTest,
	"Paradox.PuzzleOverlay.Router.LanesAndLocalBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterLaneBridgeTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot LaneSnapshot = MakeLegacySnapshot();
	LaneSnapshot.Settings.EndpointClearance = 0.0;
	LaneSnapshot.Settings.MaxCandidatesPerLink = 1;
	FParadoxPuzzleWireRoute PreservedLane;
	PreservedLane.StableOrder = 0;
	PreservedLane.Segments.Add({FVector(0, 0, 0), FVector(300, 0, 0), EParadoxPuzzleWireAxis::X, EParadoxPuzzleWireSegmentKind::GroundSupported, 0});
	LaneSnapshot.PreservedRoutes.Add(PreservedLane);
	LaneSnapshot.Links.Add(MakeLink(1, EParadoxPuzzleWireDirection::Output, {0, 0, 0}, {3, 0, 0}));
	const FParadoxPuzzleRoutingResult LaneResult = FParadoxPuzzleWireRouter::CalculateRoutes(LaneSnapshot);
	TestEqual(TEXT("Preserved and new parallel routes are returned"), LaneResult.Routes.Num(), 2);
	TestTrue(TEXT("Shared edge receives a distinct parallel lane"), HasLane(LaneResult.Routes[1], 1));
	TestTrue(TEXT("Lane geometry is part of a connected final route"), IsContinuous(LaneResult.Routes[1]));

	FParadoxPuzzleRoutingSnapshot BridgeSnapshot = MakeLegacySnapshot();
	BridgeSnapshot.Settings.EndpointClearance = 0.0;
	BridgeSnapshot.Settings.MaxCandidatesPerLink = 1;
	BridgeSnapshot.Settings.MaxRerouteAttempts = 0;
	FParadoxPuzzleWireRoute PreservedCrossing;
	PreservedCrossing.StableOrder = 0;
	PreservedCrossing.Segments.Add({FVector(-200, 0, 0), FVector(200, 0, 0), EParadoxPuzzleWireAxis::X, EParadoxPuzzleWireSegmentKind::GroundSupported, 0});
	BridgeSnapshot.PreservedRoutes.Add(PreservedCrossing);
	BridgeSnapshot.Links.Add(MakeLink(1, EParadoxPuzzleWireDirection::Input, {0, -2, 0}, {0, 2, 0}));
	const FParadoxPuzzleRoutingResult BridgeResult = FParadoxPuzzleWireRouter::CalculateRoutes(BridgeSnapshot);
	TestEqual(TEXT("Unavoidable crossing creates one local bridge"), BridgeResult.Diagnostics.BridgeCount, 1);
	bool bHasBridgeHorizontal = false;
	int32 BridgeVerticals = 0;
	for (const FParadoxPuzzleWireSegment& Segment : BridgeResult.Routes[1].Segments)
	{
		bHasBridgeHorizontal |= Segment.Kind == EParadoxPuzzleWireSegmentKind::BridgeHorizontal;
		BridgeVerticals += Segment.Kind == EParadoxPuzzleWireSegmentKind::BridgeVertical ? 1 : 0;
	}
	TestTrue(TEXT("Bridge contains a raised horizontal"), bHasBridgeHorizontal);
	TestEqual(TEXT("Bridge contains distinct rise and fall segments"), BridgeVerticals, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterBoundedBenchmarkTest,
	"Paradox.PuzzleOverlay.Router.BoundedThirtyTwoLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterBoundedBenchmarkTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
	Snapshot.SelectedAnchor = {0, 0, 0.0};
	for (int32 Index = 0; Index < 32; ++Index)
	{
		const int32 Sign = Index % 2 == 0 ? 1 : -1;
		Snapshot.Links.Add(MakeLink(
			Index,
			Index % 3 == 0 ? EParadoxPuzzleWireDirection::Output : EParadoxPuzzleWireDirection::Input,
			{Sign * (8 + Index), -12 + Index, static_cast<double>((Index % 3) * 50)},
			{0, 0, 0.0}));
	}

	for (int32 Warmup = 0; Warmup < 4; ++Warmup)
	{
		FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	}
	TArray<double> Timings;
	for (int32 Iteration = 0; Iteration < 20; ++Iteration)
	{
		Timings.Add(FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot).Diagnostics.RoutingMilliseconds);
	}
	Timings.Sort();
	const double P95 = Timings[FMath::Clamp(FMath::CeilToInt(Timings.Num() * 0.95) - 1, 0, Timings.Num() - 1)];
	AddInfo(FString::Printf(TEXT("32-link synchronous router P95 after warm-up: %.3f ms"), P95));
	TestTrue(TEXT("Candidate count stays bounded"),
		FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot).Diagnostics.CandidateCount
		<= Snapshot.Links.Num() * Snapshot.Settings.MaxCandidatesPerLink);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterWorkerEquivalenceTest,
	"Paradox.PuzzleOverlay.Router.StandardAndWorkerEquivalenceAllAlgorithms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterWorkerEquivalenceTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireRouterTests;
	for (const EParadoxPuzzleRoutingAlgorithm Algorithm : {
		EParadoxPuzzleRoutingAlgorithm::LegacyIndependent,
		EParadoxPuzzleRoutingAlgorithm::OrderedBundles,
		EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive})
	{
		FParadoxPuzzleRoutingSnapshot Snapshot = MakeLegacySnapshot();
		Snapshot.Settings.Algorithm = Algorithm;
		Snapshot.Links.Add(MakeLink(
			0,
			EParadoxPuzzleWireDirection::Input,
			{-3, 0, 0.0},
			{3, 2, 0.0}));
		const FParadoxPuzzleRoutingResult StandardResult =
			FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
		UE::Tasks::TTask<FParadoxPuzzleRoutingResult> WorkerTask = UE::Tasks::Launch(
			UE_SOURCE_LOCATION,
			[Snapshot]()
			{
				return FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
			});
		const FParadoxPuzzleRoutingResult WorkerResult = WorkerTask.GetResult();
		TestFalse(TEXT("Worker solve is not cancelled"), WorkerResult.bCancelled);
		TestEqual(TEXT("Standard and worker geometry are identical"),
			HashResult(StandardResult), HashResult(WorkerResult));
		TestEqual(TEXT("Standard and worker strategy diagnostics match"),
			WorkerResult.Diagnostics.Algorithm, StandardResult.Diagnostics.Algorithm);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParadoxPuzzleWireRouterCancellationContractTest,
	"Paradox.PuzzleOverlay.Router.CooperativeCancellationAllAlgorithms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParadoxPuzzleWireRouterCancellationContractTest::RunTest(const FString& Parameters)
{
	UE::Tasks::FCancellationToken CancellationToken;
	CancellationToken.Cancel();
	UE::Tasks::FCancellationTokenScope CancellationScope(CancellationToken);
	for (const EParadoxPuzzleRoutingAlgorithm Algorithm : {
		EParadoxPuzzleRoutingAlgorithm::LegacyIndependent,
		EParadoxPuzzleRoutingAlgorithm::OrderedBundles,
		EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive})
	{
		FParadoxPuzzleRoutingSnapshot Snapshot;
		Snapshot.RoutingGeneration = 42;
		Snapshot.Settings.Algorithm = Algorithm;
		const FParadoxPuzzleRoutingResult Result =
			FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
		TestTrue(TEXT("Pre-cancelled strategy exits cooperatively"), Result.bCancelled);
		TestEqual(TEXT("Cancellation preserves generation identity"),
			Result.RoutingGeneration, Snapshot.RoutingGeneration);
		TestEqual(TEXT("Cancellation preserves algorithm diagnostics"),
			Result.Diagnostics.Algorithm, Algorithm);
	}
	return true;
}

#endif
