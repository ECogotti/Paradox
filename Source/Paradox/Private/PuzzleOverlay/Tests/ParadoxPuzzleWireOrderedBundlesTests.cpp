#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PuzzleOverlay/ParadoxPuzzleWireRouter.h"

namespace ParadoxPuzzleWireOrderedBundlesTests
{
	FParadoxPuzzleWireEndpointBounds MakeBounds(
		const TCHAR* Key,
		const FVector& Center,
		const FVector& Extent = FVector(50.0, 50.0, 25.0))
	{
		FParadoxPuzzleWireEndpointBounds Bounds;
		Bounds.EndpointKey = Key;
		Bounds.Min = Center - Extent;
		Bounds.Max = Center + Extent;
		Bounds.bValid = true;
		Bounds.bPointFallback = false;
		Bounds.Source = EParadoxPuzzleWireBoxSource::VisibleMeshes;
		return Bounds;
	}

	FParadoxPuzzleRoutingLink MakeLink(
		const int32 StableOrder,
		const FParadoxPuzzleRoutingCoord& Source,
		const FParadoxPuzzleRoutingCoord& Target,
		const TCHAR* SourceKey,
		const TCHAR* TargetKey,
		const EParadoxPuzzleWireDirection Direction = EParadoxPuzzleWireDirection::Input)
	{
		FParadoxPuzzleRoutingLink Link;
		Link.StableOrder = StableOrder;
		Link.Direction = Direction;
		Link.Source = Source;
		Link.Target = Target;
		Link.SourceBounds = MakeBounds(
			SourceKey,
			FVector(Source.X * 100.0, Source.Y * 100.0, Source.Z));
		Link.TargetBounds = MakeBounds(
			TargetKey,
			FVector(Target.X * 100.0, Target.Y * 100.0, Target.Z));
		Link.RemoteEndpointKey = TargetKey;
		return Link;
	}

	bool IsOrthogonal(const FParadoxPuzzleWireSegment& Segment)
	{
		const FVector Delta = Segment.End - Segment.Start;
		return static_cast<int32>(!FMath::IsNearlyZero(Delta.X))
			+ static_cast<int32>(!FMath::IsNearlyZero(Delta.Y))
			+ static_cast<int32>(!FMath::IsNearlyZero(Delta.Z)) == 1;
	}

	bool IsContinuousAndOrthogonal(const FParadoxPuzzleWireRoute& Route)
	{
		for (int32 Index = 0; Index < Route.Segments.Num(); ++Index)
		{
			if (!IsOrthogonal(Route.Segments[Index])) return false;
			if (Index > 0 && !Route.Segments[Index - 1].End.Equals(Route.Segments[Index].Start, 0.01))
			{
				return false;
			}
		}
		return !Route.Segments.IsEmpty();
	}

	bool EntersOpenBounds(
		const FParadoxPuzzleWireSegment& Segment,
		const FParadoxPuzzleWireEndpointBounds& Bounds)
	{
		const FVector Midpoint = (Segment.Start + Segment.End) * 0.5;
		return Midpoint.X > Bounds.Min.X && Midpoint.X < Bounds.Max.X
			&& Midpoint.Y > Bounds.Min.Y && Midpoint.Y < Bounds.Max.Y
			&& Midpoint.Z > Bounds.Min.Z && Midpoint.Z < Bounds.Max.Z;
	}

	uint32 HashResult(const FParadoxPuzzleRoutingResult& Result)
	{
		uint32 Hash = GetTypeHash(Result.Routes.Num());
		for (const FParadoxPuzzleWireRoute& Route : Result.Routes)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Route.StableOrder));
			Hash = HashCombineFast(Hash, static_cast<uint32>(Route.SourcePort.Side));
			Hash = HashCombineFast(Hash, static_cast<uint32>(Route.TargetPort.Side));
			for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
			{
				Hash = HashCombineFast(Hash, GetTypeHash(Segment.Start));
				Hash = HashCombineFast(Hash, GetTypeHash(Segment.End));
				Hash = HashCombineFast(Hash, GetTypeHash(Segment.BundleId));
				Hash = HashCombineFast(Hash, GetTypeHash(Segment.Lane));
			}
		}
		return Hash;
	}

	FParadoxPuzzleRoutingSnapshot MakeOrderedSnapshot()
	{
		FParadoxPuzzleRoutingSnapshot Snapshot;
		Snapshot.Settings.Algorithm = EParadoxPuzzleRoutingAlgorithm::OrderedBundles;
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesDefaultsTest,
	"Paradox.PuzzleOverlay.OrderedBundles.DefaultsAndLegacyDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	const FParadoxPuzzleRoutingSettings Defaults;
	TestEqual(TEXT("Distributed Repulsive is the default"), Defaults.Algorithm, EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive);
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-2, 0, 0.0}, {2, 0, 0.0}, TEXT("A"), TEXT("B")));
	TestEqual(TEXT("Ordered dispatch returns one route"), FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot).Routes.Num(), 1);
	Snapshot.Settings.Algorithm = EParadoxPuzzleRoutingAlgorithm::LegacyIndependent;
	TestEqual(TEXT("Deprecated legacy dispatch remains available"), FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot).Routes.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesHorizontalFacesTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Faces.Horizontal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesHorizontalFacesTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-3, 0, 0.0}, {3, 0, 0.0}, TEXT("Left"), TEXT("Right")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Route exists"), Result.Routes.Num(), 1)) return false;
	TestEqual(TEXT("Source chooses +X face"), Result.Routes[0].SourcePort.Side, EParadoxPuzzlePortSide::East);
	TestEqual(TEXT("Target chooses -X face"), Result.Routes[0].TargetPort.Side, EParadoxPuzzlePortSide::West);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesVerticalPlanarFacesTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Faces.PlanarY",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesVerticalPlanarFacesTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Links.Add(MakeLink(0, {0, -3, 0.0}, {0, 3, 0.0}, TEXT("South"), TEXT("North")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Route exists"), Result.Routes.Num(), 1)) return false;
	TestTrue(TEXT("Opposite Y faces are selected"), Result.Routes[0].SourcePort.Normal.Y > 0.0 && Result.Routes[0].TargetPort.Normal.Y < 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesZFacesTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Faces.PositiveAndNegativeZ",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesZFacesTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Links.Add(MakeLink(0, {0, 0, -300.0}, {0, 0, 300.0}, TEXT("Below"), TEXT("Above")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Route exists"), Result.Routes.Num(), 1)) return false;
	TestEqual(TEXT("Source chooses +Z"), Result.Routes[0].SourcePort.Side, EParadoxPuzzlePortSide::PositiveZ);
	TestEqual(TEXT("Target chooses -Z"), Result.Routes[0].TargetPort.Side, EParadoxPuzzlePortSide::NegativeZ);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesTerminalTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Terminals.OrthogonalAndContinuous",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesTerminalTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-3, -2, 0.0}, {3, 2, 0.0}, TEXT("A"), TEXT("B")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Route exists"), Result.Routes.Num(), 1)) return false;
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	TestTrue(TEXT("Final polyline is continuous and orthogonal"), IsContinuousAndOrthogonal(Route));
	TestTrue(TEXT("First terminal follows source normal"), (Route.Segments[0].End - Route.Segments[0].Start).GetSafeNormal().Equals(Route.SourcePort.Normal));
	TestTrue(TEXT("Last terminal approaches target"), (Route.Segments.Last().End - Route.Segments.Last().Start).GetSafeNormal().Equals(-Route.TargetPort.Normal));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesEndpointProtectionTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Terminals.EndpointBoxesProtected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesEndpointProtectionTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	const FParadoxPuzzleRoutingLink Link = MakeLink(0, {-3, -2, 0.0}, {3, 2, 0.0}, TEXT("A"), TEXT("B"));
	Snapshot.Links.Add(Link);
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Route exists"), Result.Routes.Num(), 1)) return false;
	for (const FParadoxPuzzleWireSegment& Segment : Result.Routes[0].Segments)
	{
		TestFalse(TEXT("Source interior is protected"), EntersOpenBounds(Segment, Link.SourceBounds));
		TestFalse(TEXT("Target interior is protected"), EntersOpenBounds(Segment, Link.TargetBounds));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesPointFallbackTest,
	"Paradox.PuzzleOverlay.OrderedBundles.WireBox.PointFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesPointFallbackTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	FParadoxPuzzleRoutingLink Link;
	Link.StableOrder = 0;
	Link.Source = {-2, 0, 0.0};
	Link.Target = {2, 0, 0.0};
	Snapshot.Links.Add(Link);
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Point endpoints remain routable"), Result.Routes.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesSinglePortTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Ports.SingleCentered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesSinglePortTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-3, 0, 0.0}, {3, 0, 0.0}, TEXT("A"), TEXT("B")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	TestEqual(TEXT("One port has one slot"), Route.SourcePort.FaceSlotCount, 1);
	TestTrue(TEXT("One port is centered"), FMath::IsNearlyZero(Route.SourcePort.NormalizedDistanceFromFaceCenter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesNPlusOnePortsTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Ports.NPlusOneDistribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesNPlusOnePortsTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	FParadoxPuzzleRoutingLink A = MakeLink(0, {0, 0, 0.0}, {4, 0, 0.0}, TEXT("Shared"), TEXT("A"));
	FParadoxPuzzleRoutingLink B = MakeLink(1, {0, 0, 0.0}, {4, 0, 0.0}, TEXT("Shared"), TEXT("B"));
	A.SourceBounds = MakeBounds(TEXT("Shared"), FVector::ZeroVector, FVector(50.0, 150.0, 25.0));
	B.SourceBounds = A.SourceBounds;
	Snapshot.Links = {A, B};
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Both routes exist"), Result.Routes.Num(), 2)) return false;
	TestEqual(TEXT("Both ports share the face group"), Result.Routes[0].SourcePort.FaceSlotCount, 2);
	TestTrue(TEXT("N+1 slots are symmetric"), FMath::IsNearlyEqual(
		Result.Routes[0].SourcePort.Position.Y,
		-Result.Routes[1].SourcePort.Position.Y,
		0.01));
	TestTrue(TEXT("Slots are separated from centre and edges"), FMath::Abs(Result.Routes[0].SourcePort.Position.Y) > 40.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesIdentityTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Bundle.LinkIdentityPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesIdentityTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Links.Add(MakeLink(7, {-4, -1, 0.0}, {4, -1, 0.0}, TEXT("A0"), TEXT("B0")));
	Snapshot.Links.Add(MakeLink(3, {-4, 1, 0.0}, {4, 1, 0.0}, TEXT("A1"), TEXT("B1")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Each link remains a route"), Result.Routes.Num(), Snapshot.Links.Num());
	TestTrue(TEXT("Routes retain independent stable identity"), Result.Routes[0].StableOrder != Result.Routes[1].StableOrder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesAttractionTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Bundle.Attraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesAttractionTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Settings.BundleReuseBonus = 500.0;
	Snapshot.Links.Add(MakeLink(0, {-5, -1, 0.0}, {5, -1, 0.0}, TEXT("A0"), TEXT("B0")));
	Snapshot.Links.Add(MakeLink(1, {-5, 1, 0.0}, {5, 1, 0.0}, TEXT("A1"), TEXT("B1")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestTrue(TEXT("Compatible routes create shared bundle data"), Result.Diagnostics.BundleCount > 0);
	TestTrue(TEXT("Shared unit edges are reported"), Result.Diagnostics.BundledUnitEdgeCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesLaneSymmetryTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Bundle.SymmetricLaneNudging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesLaneSymmetryTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Settings.BundleReuseBonus = 500.0;
	Snapshot.Links.Add(MakeLink(0, {-5, -1, 0.0}, {5, -1, 0.0}, TEXT("A0"), TEXT("B0")));
	Snapshot.Links.Add(MakeLink(1, {-5, 1, 0.0}, {5, 1, 0.0}, TEXT("A1"), TEXT("B1")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TArray<double> Offsets;
	for (const FParadoxPuzzleWireRoute& Route : Result.Routes)
	{
		for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
		{
			if (Segment.BundleId != INDEX_NONE) Offsets.AddUnique(Segment.NudgeOffset);
		}
	}
	Offsets.Sort();
	TestTrue(TEXT("Bundled lanes receive distinct offsets"), Offsets.Num() >= 2);
	if (Offsets.Num() >= 2) TestTrue(TEXT("Offsets are centered"), FMath::IsNearlyEqual(Offsets[0], -Offsets.Last(), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesOrderingTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Bundle.MetroOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesOrderingTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Settings.BundleReuseBonus = 500.0;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Snapshot.Links.Add(MakeLink(Index, {-6, Index - 2, 0.0}, {6, 2 - Index, 0.0},
			*FString::Printf(TEXT("A%d"), Index), *FString::Printf(TEXT("B%d"), Index)));
	}
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestTrue(TEXT("Ordering never increases inversions"), Result.Diagnostics.InversionsAfterOrdering <= Result.Diagnostics.InversionsBeforeOrdering);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesBendTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Cost.Bend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesBendTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Settings.BendPenalty = 10000.0;
	Snapshot.Links.Add(MakeLink(0, {-4, 0, 0.0}, {4, 0, 0.0}, TEXT("A"), TEXT("B")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	TestTrue(TEXT("Aligned endpoints do not acquire a large dogleg"), Route.RenderedCornerCount <= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesUnsupportedTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Surface.Void",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesUnsupportedTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-4, 0, 0.0}, {4, 0, 0.0}, TEXT("A"), TEXT("B")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	TestTrue(TEXT("Missing samples keep geometry and mark unsupported segments"), Route.Segments.ContainsByPredicate([](const FParadoxPuzzleWireSegment& Segment)
	{
		return Segment.Kind == EParadoxPuzzleWireSegmentKind::GroundUnsupported;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesMultiLevelTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Surface.MultiLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesMultiLevelTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-3, 0, 0.0}, {3, 0, 200.0}, TEXT("A"), TEXT("B")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	TestTrue(TEXT("Different elevations use a structural vertical"), Route.Segments.ContainsByPredicate([](const FParadoxPuzzleWireSegment& Segment)
	{
		return Segment.Axis == EParadoxPuzzleWireAxis::Z;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesCrossingTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Crossing.BoundedResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesCrossingTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-4, 0, 0.0}, {4, 0, 0.0}, TEXT("West"), TEXT("East")));
	Snapshot.Links.Add(MakeLink(1, {0, -4, 0.0}, {0, 4, 0.0}, TEXT("South"), TEXT("North")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestTrue(TEXT("Reroutes are bounded"), Result.Diagnostics.RerouteAttempts <= Snapshot.Settings.MaxRerouteAttempts * Snapshot.Links.Num());
	TestTrue(TEXT("Bridge fallback is bounded"), Result.Diagnostics.BridgeCount <= Result.Routes.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesNoNetworkObstacleTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Obstacles.OnlyEndpointsProtected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesNoNetworkObstacleTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Baseline = MakeOrderedSnapshot();
	Baseline.Links.Add(MakeLink(0, {-4, 0, 0.0}, {4, 0, 0.0}, TEXT("A"), TEXT("B")));
	FParadoxPuzzleRoutingSnapshot WithUnrelated = Baseline;
	FParadoxPuzzleRoutingLink Unrelated = MakeLink(1, {0, 5, 0.0}, {0, 6, 0.0}, TEXT("C"), TEXT("D"));
	Unrelated.SourceBounds = MakeBounds(TEXT("C"), FVector::ZeroVector, FVector(150.0, 150.0, 25.0));
	WithUnrelated.Links.Add(Unrelated);
	const FParadoxPuzzleRoutingResult A = FParadoxPuzzleWireRouter::CalculateRoutes(Baseline);
	const FParadoxPuzzleRoutingResult B = FParadoxPuzzleWireRouter::CalculateRoutes(WithUnrelated);
	TestEqual(TEXT("Unrelated endpoint boxes are not network obstacles"), A.Routes[0].SourcePort.Side, B.Routes[0].SourcePort.Side);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesDebugDataTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Debug.OptInCandidateData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesDebugDataTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-2, 0, 0.0}, {2, 0, 0.0}, TEXT("A"), TEXT("B")));
	TestTrue(TEXT("Candidate debug is absent by default"), FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot).FaceCandidates.IsEmpty());
	Snapshot.bCollectDebugData = true;
	TestTrue(TEXT("Candidate debug is collected on explicit opt-in"), !FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot).FaceCandidates.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesDeterminismTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Determinism.RegistrationOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot A = MakeOrderedSnapshot();
	for (int32 Index = 0; Index < 6; ++Index)
	{
		A.Links.Add(MakeLink(Index, {-5, Index - 3, 0.0}, {5, Index - 3, 0.0},
			*FString::Printf(TEXT("A%d"), Index), *FString::Printf(TEXT("B%d"), Index)));
	}
	FParadoxPuzzleRoutingSnapshot B = A;
	Algo::Reverse(B.Links);
	TestEqual(TEXT("Stable authored order makes results registration-order independent"),
		HashResult(FParadoxPuzzleWireRouter::CalculateRoutes(A)),
		HashResult(FParadoxPuzzleWireRouter::CalculateRoutes(B)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesBudgetTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Bounds.CandidatesAndPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesBudgetTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	Snapshot.Settings.MaxOrderedBundleCandidatesPerLink = 64;
	Snapshot.Settings.MaxBundleOptimizationPasses = 2;
	Snapshot.Settings.MaxMetroOrderingPasses = 3;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		Snapshot.Links.Add(MakeLink(Index, {-8, Index - 6, 0.0}, {8, 6 - Index, 0.0},
			*FString::Printf(TEXT("A%d"), Index), *FString::Printf(TEXT("B%d"), Index)));
	}
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestTrue(TEXT("Candidate budget is hard bounded"), Result.Diagnostics.CandidateCount <= Snapshot.Links.Num() * Snapshot.Settings.MaxOrderedBundleCandidatesPerLink);
	TestEqual(TEXT("All links terminate with a route"), Result.Routes.Num(), Snapshot.Links.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxOrderedBundlesBenchmarkTest,
	"Paradox.PuzzleOverlay.OrderedBundles.Benchmark.ThirtyTwoLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxOrderedBundlesBenchmarkTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireOrderedBundlesTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeOrderedSnapshot();
	for (int32 Index = 0; Index < 32; ++Index)
	{
		Snapshot.Links.Add(MakeLink(Index, {-12, Index - 16, static_cast<double>((Index % 3) * 50)},
			{12, 16 - Index, static_cast<double>((Index % 3) * 50)},
			*FString::Printf(TEXT("A%d"), Index), *FString::Printf(TEXT("B%d"), Index)));
	}
	FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TArray<double> Timings;
	for (int32 Iteration = 0; Iteration < 8; ++Iteration)
	{
		Timings.Add(FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot).Diagnostics.RoutingMilliseconds);
	}
	Timings.Sort();
	const double P95 = Timings[FMath::Clamp(FMath::CeilToInt(Timings.Num() * 0.95) - 1, 0, Timings.Num() - 1)];
	AddInfo(FString::Printf(TEXT("Ordered Bundles 32-link synchronous P95 after warm-up: %.3f ms"), P95));
	TestTrue(TEXT("Benchmark completed with finite timing"), FMath::IsFinite(P95) && P95 >= 0.0);
	return true;
}

#endif
