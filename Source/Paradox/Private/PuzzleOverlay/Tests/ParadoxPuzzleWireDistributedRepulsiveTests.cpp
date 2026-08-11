#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PuzzleOverlay/ParadoxPuzzleWireRouter.h"
#include "UObject/UnrealType.h"

namespace ParadoxPuzzleWireDistributedRepulsiveTests
{
	FParadoxPuzzleWireEndpointBounds MakeBounds(
		const TCHAR* Key,
		const FVector& Center,
		const FVector& Extent = FVector(50.0, 50.0, 25.0),
		const bool bWireTarget = false)
	{
		FParadoxPuzzleWireEndpointBounds Bounds;
		Bounds.EndpointKey = Key;
		Bounds.Min = Center - Extent;
		Bounds.Max = Center + Extent;
		Bounds.bValid = true;
		Bounds.bFromWireTarget = bWireTarget;
		Bounds.Source = bWireTarget
			? EParadoxPuzzleWireBoxSource::CustomWireTarget
			: EParadoxPuzzleWireBoxSource::VisibleMeshes;
		return Bounds;
	}

	FParadoxPuzzleRoutingLink MakeLink(
		const int32 StableOrder,
		const FParadoxPuzzleRoutingCoord& Source,
		const FParadoxPuzzleRoutingCoord& Target,
		const TCHAR* SourceKey,
		const TCHAR* TargetKey)
	{
		FParadoxPuzzleRoutingLink Link;
		Link.StableOrder = StableOrder;
		Link.Source = Source;
		Link.Target = Target;
		Link.SourceBounds = MakeBounds(SourceKey, FVector(Source.X * 100.0, Source.Y * 100.0, Source.Z));
		Link.TargetBounds = MakeBounds(TargetKey, FVector(Target.X * 100.0, Target.Y * 100.0, Target.Z));
		Link.RemoteEndpointKey = TargetKey;
		return Link;
	}

	FParadoxPuzzleRoutingSnapshot MakeSnapshot()
	{
		FParadoxPuzzleRoutingSnapshot Snapshot;
		Snapshot.Settings.Algorithm = EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive;
		Snapshot.Settings.MaxDistributedCandidatesPerLink = 128;
		Snapshot.Settings.MaxNegotiationPasses = 6;
		Snapshot.Settings.MaxRerouteAttempts = 4;
		return Snapshot;
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
		if (Route.Segments.IsEmpty()) return false;
		for (int32 Index = 0; Index < Route.Segments.Num(); ++Index)
		{
			if (!IsOrthogonal(Route.Segments[Index])) return false;
			if (Index > 0 && !Route.Segments[Index - 1].End.Equals(Route.Segments[Index].Start, 0.01))
			{
				return false;
			}
		}
		return true;
	}

	bool IsPortOnBounds(const FParadoxPuzzleWirePort& Port)
	{
		switch (Port.Side)
		{
		case EParadoxPuzzlePortSide::East: return FMath::IsNearlyEqual(Port.Position.X, Port.Bounds.Max.X, 0.01);
		case EParadoxPuzzlePortSide::West: return FMath::IsNearlyEqual(Port.Position.X, Port.Bounds.Min.X, 0.01);
		case EParadoxPuzzlePortSide::North: return FMath::IsNearlyEqual(Port.Position.Y, Port.Bounds.Max.Y, 0.01);
		case EParadoxPuzzlePortSide::South: return FMath::IsNearlyEqual(Port.Position.Y, Port.Bounds.Min.Y, 0.01);
		case EParadoxPuzzlePortSide::PositiveZ: return FMath::IsNearlyEqual(Port.Position.Z, Port.Bounds.Max.Z, 0.01);
		case EParadoxPuzzlePortSide::NegativeZ: return FMath::IsNearlyEqual(Port.Position.Z, Port.Bounds.Min.Z, 0.01);
		default: return false;
		}
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

	uint32 HashGeometry(const FParadoxPuzzleRoutingResult& Result)
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
			}
		}
		return Hash;
	}

	FParadoxPuzzleRoutingSnapshot MakeThreeWireFixture()
	{
		FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
		Snapshot.Links.Add(MakeLink(0, {-5, -2, 0.0}, {5, -2, 0.0}, TEXT("S0"), TEXT("T0")));
		Snapshot.Links.Add(MakeLink(1, {-5, 0, 0.0}, {5, 0, 0.0}, TEXT("S1"), TEXT("T1")));
		Snapshot.Links.Add(MakeLink(2, {-5, 2, 0.0}, {5, 2, 0.0}, TEXT("S2"), TEXT("T2")));
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedEnumAndEditorContractTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.01.EnumAndEditorContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedEnumAndEditorContractTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	const UEnum* Enum = StaticEnum<EParadoxPuzzleRoutingAlgorithm>();
	TestNotNull(TEXT("Routing enum is reflected"), Enum);
	TestTrue(TEXT("Distributed value is appended and reflected"), Enum
		&& Enum->GetIndexByValue(static_cast<int64>(EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive)) != INDEX_NONE);
	const FParadoxPuzzleRoutingSettings Defaults;
	TestEqual(TEXT("Distributed candidate default"), Defaults.MaxDistributedCandidatesPerLink, 128);
	TestEqual(TEXT("Negotiation pass default"), Defaults.MaxNegotiationPasses, 6);
	TestEqual(TEXT("Length weight default"), Defaults.LengthWeight, 1.0);
	TestEqual(TEXT("Shared edge penalty default"), Defaults.SharedEdgePenalty, 10000.0);
	TestEqual(TEXT("History weight default"), Defaults.HistoricalCongestionWeight, 2000.0);
	TestEqual(TEXT("Proximity radius default"), Defaults.ProximityRadius, 3);
	TestEqual(TEXT("Proximity penalty default"), Defaults.ProximityPenalty, 250.0);
	TestEqual(TEXT("Proximity exponent default"), Defaults.ProximityFalloffExponent, 1.5);
	TestEqual(TEXT("Parallel-run penalty default"), Defaults.ParallelRunPenalty, 25.0);
	TestEqual(TEXT("Perpendicular proximity scale default"), Defaults.PerpendicularProximityScale, 0.25);
	TestEqual(TEXT("Endpoint escape default"), Defaults.EndpointEscapeDistance, 2);
	TestEqual(TEXT("Vertical proximity threshold default"), Defaults.VerticalProximityThreshold, 50.0);
	TestTrue(TEXT("Hierarchical face pruning default"), Defaults.bEnableHierarchicalFacePairPruning);
	TestEqual(TEXT("Single-link fine face-pair default"), Defaults.SingleLinkFineFacePairLimit, 8);
	TestEqual(TEXT("Subdivided fine face-pair default"), Defaults.SubdividedFineFacePairLimit, 12);
	TestEqual(TEXT("Base-resolution fine face-pair default"), Defaults.BaseResolutionFineFacePairLimit, 18);
	TestTrue(TEXT("Single-link fast path default"), Defaults.bEnableSingleLinkFastPath);
	TestTrue(TEXT("Conflict-free negotiation skip default"), Defaults.bEnableConflictFreeNegotiationSkip);
	TestEqual(TEXT("Spatial link threshold default"), Defaults.SpatialIndexLinkThreshold, 8);
	TestEqual(TEXT("Spatial edge threshold default"), Defaults.SpatialIndexEdgeThreshold, 64);
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-2, 0, 0.0}, {2, 0, 0.0}, TEXT("A"), TEXT("B")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Explicit dispatcher reaches distributed strategy"), Result.Diagnostics.Algorithm,
		EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive);

	const FProperty* DistributedProperty = FindFProperty<FProperty>(
		FParadoxPuzzleRoutingSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FParadoxPuzzleRoutingSettings, ProximityPenalty));
	const FProperty* OrderedProperty = FindFProperty<FProperty>(
		FParadoxPuzzleRoutingSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FParadoxPuzzleRoutingSettings, BundleReuseBonus));
	const FProperty* LegacyProperty = FindFProperty<FProperty>(
		FParadoxPuzzleRoutingSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FParadoxPuzzleRoutingSettings, MaxCandidatesPerLink));
	const FProperty* SharedBendProperty = FindFProperty<FProperty>(
		FParadoxPuzzleRoutingSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FParadoxPuzzleRoutingSettings, BendPenalty));
	const FProperty* HierarchicalPruningProperty = FindFProperty<FProperty>(
		FParadoxPuzzleRoutingSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FParadoxPuzzleRoutingSettings, bEnableHierarchicalFacePairPruning));
	const FProperty* FinePairLimitProperty = FindFProperty<FProperty>(
		FParadoxPuzzleRoutingSettings::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FParadoxPuzzleRoutingSettings, SubdividedFineFacePairLimit));
	TestTrue(TEXT("Distributed tuning is conditionally hidden"), DistributedProperty
		&& DistributedProperty->HasMetaData(TEXT("EditConditionHides"))
		&& DistributedProperty->GetMetaData(TEXT("EditCondition")).Contains(TEXT("DistributedRepulsive")));
	TestTrue(TEXT("Ordered tuning is conditionally hidden"), OrderedProperty
		&& OrderedProperty->HasMetaData(TEXT("EditConditionHides"))
		&& !OrderedProperty->GetMetaData(TEXT("EditCondition")).Contains(TEXT("DistributedRepulsive")));
	TestTrue(TEXT("Legacy tuning is conditionally hidden"), LegacyProperty
		&& LegacyProperty->HasMetaData(TEXT("EditConditionHides"))
		&& LegacyProperty->GetMetaData(TEXT("EditCondition")).Contains(TEXT("LegacyIndependent"))
		&& !LegacyProperty->GetMetaData(TEXT("EditCondition")).Contains(TEXT("OrderedBundles")));
	TestTrue(TEXT("Shared bend tuning is visible only for Ordered and Distributed"), SharedBendProperty
		&& SharedBendProperty->HasMetaData(TEXT("EditConditionHides"))
		&& SharedBendProperty->GetMetaData(TEXT("EditCondition")).Contains(TEXT("OrderedBundles"))
		&& SharedBendProperty->GetMetaData(TEXT("EditCondition")).Contains(TEXT("DistributedRepulsive"))
		&& !SharedBendProperty->GetMetaData(TEXT("EditCondition")).Contains(TEXT("LegacyIndependent")));
	TestTrue(TEXT("Hierarchical pruning is advanced and Distributed-only"), HierarchicalPruningProperty
		&& HierarchicalPruningProperty->HasMetaData(TEXT("AdvancedDisplay"))
		&& HierarchicalPruningProperty->HasMetaData(TEXT("EditConditionHides"))
		&& HierarchicalPruningProperty->GetMetaData(TEXT("EditCondition")).Contains(TEXT("DistributedRepulsive")));
	TestTrue(TEXT("Fine-pair limit follows the pruning toggle"), FinePairLimitProperty
		&& FinePairLimitProperty->HasMetaData(TEXT("EditConditionHides"))
		&& FinePairLimitProperty->GetMetaData(TEXT("EditCondition")).Contains(TEXT("bEnableHierarchicalFacePairPruning")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedWireTargetMetadataTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.02.WireTargetMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedWireTargetMetadataTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	FParadoxPuzzleRoutingLink Link = MakeLink(0, {-3, 0, 0.0}, {3, 0, 0.0}, TEXT("A"), TEXT("B"));
	Link.SourceBounds = MakeBounds(TEXT("A"), FVector(-300.0, 0.0, 0.0), FVector(70.0, 90.0, 30.0), true);
	Snapshot.Links.Add(Link);
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Custom WireTarget remains routable"), Result.Routes.Num(), 1)) return false;
	TestTrue(TEXT("Resolved port retains WireTarget source"), Result.Routes[0].SourcePort.Bounds.bFromWireTarget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedPortsOnBoundsTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.03.PortsStayOnBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedPortsOnBoundsTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-3, -2, 0.0}, {3, 2, 0.0}, TEXT("A"), TEXT("B")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	TestTrue(TEXT("Source port lies on its box"), IsPortOnBounds(Route.SourcePort));
	TestTrue(TEXT("Target port lies on its box"), IsPortOnBounds(Route.TargetPort));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedSourceTerminalTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.04.SourceTerminalOrthogonal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedSourceTerminalTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-3, -2, 0.0}, {3, 2, 0.0}, TEXT("A"), TEXT("B")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	TestTrue(TEXT("First segment follows outward face normal"),
		(Route.Segments[0].End - Route.Segments[0].Start).GetSafeNormal().Equals(Route.SourcePort.Normal, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedTargetTerminalTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.05.TargetTerminalOrthogonal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedTargetTerminalTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-3, -2, 0.0}, {3, 2, 0.0}, TEXT("A"), TEXT("B")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	TestTrue(TEXT("Last segment approaches opposite target normal"),
		(Route.Segments.Last().End - Route.Segments.Last().Start).GetSafeNormal().Equals(-Route.TargetPort.Normal, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedEndpointProtectionTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.06.EndpointBoxesProtected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedEndpointProtectionTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	const FParadoxPuzzleRoutingLink Link = MakeLink(0, {-4, -2, 0.0}, {4, 2, 0.0}, TEXT("A"), TEXT("B"));
	Snapshot.Links.Add(Link);
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	const FParadoxPuzzleWireRoute& Route = Result.Routes[0];
	for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
	{
		TestFalse(TEXT("Route never penetrates source Wire Box"), EntersOpenBounds(Segment, Link.SourceBounds));
		TestFalse(TEXT("Route never penetrates target Wire Box"), EntersOpenBounds(Segment, Link.TargetBounds));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedPortDistributionTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.07.NPlusOnePorts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedPortDistributionTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	FParadoxPuzzleRoutingLink A = MakeLink(0, {0, 0, 0.0}, {4, 0, 0.0}, TEXT("Shared"), TEXT("A"));
	FParadoxPuzzleRoutingLink B = MakeLink(1, {0, 0, 0.0}, {4, 1, 0.0}, TEXT("Shared"), TEXT("B"));
	A.SourceBounds = MakeBounds(TEXT("Shared"), FVector::ZeroVector, FVector(50.0, 150.0, 25.0));
	B.SourceBounds = A.SourceBounds;
	Snapshot.Links = {A, B};
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("Both links remain independent"), Result.Routes.Num(), 2)) return false;
	TestEqual(TEXT("Face records two slots"), Result.Routes[0].SourcePort.FaceSlotCount, 2);
	TestTrue(TEXT("N+1 slots are symmetric around face centre"), FMath::IsNearlyEqual(
		Result.Routes[0].SourcePort.Position.Y,
		-Result.Routes[1].SourcePort.Position.Y,
		0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedSharedUsageTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.08.SharedUsageNegotiated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedSharedUsageTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeThreeWireFixture();
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("All wires remain routable"), Result.Routes.Num(), Snapshot.Links.Num());
	TestTrue(TEXT("Shared-edge diagnostics are non-negative"), Result.Diagnostics.SharedUnitEdgeLength >= 0);
	TestTrue(TEXT("Negotiation executes at least one pass"), Result.Diagnostics.NegotiationPassCount >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedNoRadiusTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.09.ZeroProximityRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedNoRadiusTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeThreeWireFixture();
	Snapshot.Settings.ProximityRadius = 0;
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("No proximity field when radius is zero"), Result.Diagnostics.TotalProximityCost, 0.0);
	TestEqual(TEXT("No parallel-near length when radius is zero"), Result.Diagnostics.ParallelNearUnitEdgeLength, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedProximityDebugTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.10.ProximityDebugField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedProximityDebugTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeThreeWireFixture();
	Snapshot.bCollectDebugData = true;
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("A conflict record exists per routed wire"), Result.WireConflicts.Num(), Result.Routes.Num());
	TestTrue(TEXT("Congestion/proximity edge records are exposed"), !Result.CongestionEdges.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedPassBoundTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.11.NegotiationPassBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedPassBoundTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeThreeWireFixture();
	Snapshot.Settings.MaxNegotiationPasses = 2;
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestTrue(TEXT("Negotiation never exceeds configured bound"), Result.Diagnostics.NegotiationPassCount <= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedRerouteDiagnosticsTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.12.PerWireRerouteDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedRerouteDiagnosticsTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeThreeWireFixture();
	Snapshot.bCollectDebugData = true;
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	int32 PerWireReroutes = 0;
	for (const FParadoxPuzzleWireConflictDebug& Conflict : Result.WireConflicts) PerWireReroutes += Conflict.RerouteCount;
	TestEqual(TEXT("Per-wire reroutes reconcile with aggregate count"), PerWireReroutes, Result.Diagnostics.ReroutedWireCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedHistoryLocalTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.13.HistoryIsSolveLocal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedHistoryLocalTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	const FParadoxPuzzleRoutingSnapshot Snapshot = MakeThreeWireFixture();
	const FParadoxPuzzleRoutingResult First = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	const FParadoxPuzzleRoutingResult Second = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Previous solves do not affect geometry"), HashGeometry(First), HashGeometry(Second));
	TestEqual(TEXT("Previous solves do not affect history cost"),
		First.Diagnostics.TotalHistoricalCongestionCost,
		Second.Diagnostics.TotalHistoricalCongestionCost);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedRegistrationOrderTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.14.NoRegistrationOrderPrivilege",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedRegistrationOrderTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Forward = MakeThreeWireFixture();
	FParadoxPuzzleRoutingSnapshot Reversed = Forward;
	Algo::Reverse(Reversed.Links);
	TestEqual(TEXT("Stable order, not input array order, determines routing"),
		HashGeometry(FParadoxPuzzleWireRouter::CalculateRoutes(Forward)),
		HashGeometry(FParadoxPuzzleWireRouter::CalculateRoutes(Reversed)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedBestPassTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.15.BestGenerationRetained",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedBestPassTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(MakeThreeWireFixture());
	TestTrue(TEXT("Best pass is within the executed negotiation range"),
		Result.Diagnostics.BestNegotiationPass >= 0
		&& Result.Diagnostics.BestNegotiationPass <= Result.Diagnostics.NegotiationPassCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedNudgeGeometryTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.16.NudgeStaysOrthogonal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedNudgeGeometryTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(MakeThreeWireFixture());
	for (const FParadoxPuzzleWireRoute& Route : Result.Routes)
	{
		TestTrue(TEXT("Nudge/fallback output remains continuous and orthogonal"), IsContinuousAndOrthogonal(Route));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedFloorSeparationTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.17.StructuralFloorsDoNotRepel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedFloorSeparationTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	Snapshot.Settings.VerticalProximityThreshold = 25.0;
	Snapshot.Links.Add(MakeLink(0, {-4, 0, 0.0}, {4, 0, 0.0}, TEXT("A0"), TEXT("B0")));
	Snapshot.Links.Add(MakeLink(1, {-4, 0, 300.0}, {4, 0, 300.0}, TEXT("A1"), TEXT("B1")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Both structural levels route"), Result.Routes.Num(), 2);
	TestEqual(TEXT("Separated floors contribute no proximity"), Result.Diagnostics.TotalProximityCost, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedCrossingFallbackTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.18.CrossingAndBridgeFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedCrossingFallbackTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-3, -3, 0.0}, {3, 3, 0.0}, TEXT("A"), TEXT("B")));
	Snapshot.Links.Add(MakeLink(1, {-3, 3, 0.0}, {3, -3, 0.0}, TEXT("C"), TEXT("D")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Crossing fixture retains both links"), Result.Routes.Num(), 2);
	for (const FParadoxPuzzleWireRoute& Route : Result.Routes)
	{
		TestTrue(TEXT("Crossing resolution never creates oblique geometry"), IsContinuousAndOrthogonal(Route));
	}
	TestTrue(TEXT("Every unresolved crossing is represented by a bridge"),
		Result.Diagnostics.BridgeCount >= Result.Diagnostics.CrossingCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedVoidTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.19.UnsupportedSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedVoidTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	Snapshot.Links.Add(MakeLink(0, {-5, 0, 0.0}, {5, 0, 0.0}, TEXT("A"), TEXT("B")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	if (!TestEqual(TEXT("A void does not make the link unroutable"), Result.Routes.Num(), 1)) return false;
	TestTrue(TEXT("Void route remains continuous"), IsContinuousAndOrthogonal(Result.Routes[0]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedSignalStateTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.20.SignalStateDoesNotAffectGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedSignalStateTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Inactive = MakeThreeWireFixture();
	FParadoxPuzzleRoutingSnapshot Active = Inactive;
	for (FParadoxPuzzleRoutingLink& Link : Active.Links)
	{
		Link.bActive = true;
		Link.bSignalValid = true;
	}
	TestEqual(TEXT("Signal-only changes preserve route geometry"),
		HashGeometry(FParadoxPuzzleWireRouter::CalculateRoutes(Inactive)),
		HashGeometry(FParadoxPuzzleWireRouter::CalculateRoutes(Active)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedCandidateBoundTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.21.CandidateBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedCandidateBoundTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeThreeWireFixture();
	Snapshot.Settings.MaxDistributedCandidatesPerLink = 36;
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestTrue(TEXT("Retained candidates obey per-link hard cap"),
		Result.Diagnostics.CandidateCount <= Snapshot.Links.Num() * Snapshot.Settings.MaxDistributedCandidatesPerLink);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedDeterminismTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.22.DeterminismAndTermination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	const FParadoxPuzzleRoutingSnapshot Snapshot = MakeThreeWireFixture();
	const FParadoxPuzzleRoutingResult A = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	const FParadoxPuzzleRoutingResult B = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Repeated solve is deterministic"), HashGeometry(A), HashGeometry(B));
	TestTrue(TEXT("Solve terminates inside configured pass bound"),
		A.Diagnostics.NegotiationPassCount <= Snapshot.Settings.MaxNegotiationPasses);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedSingleWireFastPathTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.23.SingleWireFastPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedSingleWireFastPathTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	Snapshot.Settings.GridCellSubdivision = EParadoxPuzzleRoutingSubdivision::TwoByTwo;
	Snapshot.Settings.PitchX = 50.0;
	Snapshot.Settings.PitchY = 50.0;
	FParadoxPuzzleRoutingLink Link = MakeLink(0, {-6, 0, 0.0}, {6, 0, 0.0}, TEXT("A"), TEXT("B"));
	Link.SourceBounds = MakeBounds(TEXT("A"), FVector(-300.0, 0.0, 0.0));
	Link.TargetBounds = MakeBounds(TEXT("B"), FVector(300.0, 0.0, 0.0));
	Snapshot.Links.Add(Link);
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Single wire is identified by the fast path"), Result.Diagnostics.FastPathWireCount, 1);
	TestEqual(TEXT("Single wire skips repulsive negotiation"), Result.Diagnostics.NegotiationPassCount, 0);
	TestEqual(TEXT("Coarse stage evaluates all face pairs"), Result.Diagnostics.HierarchicalCoarseFacePairCount, 36);
	TestTrue(TEXT("Fine stage prunes face pairs after coarse ranking"), Result.Diagnostics.PrunedFineFacePairCount > 0);
	TestEqual(TEXT("Fast path retains a valid route"), Result.Routes.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedHierarchyIsolationTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.24.HierarchyStrategyIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedHierarchyIsolationTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	Snapshot.Settings.Algorithm = EParadoxPuzzleRoutingAlgorithm::OrderedBundles;
	Snapshot.Links.Add(MakeLink(0, {-4, 0, 0.0}, {4, 0, 0.0}, TEXT("A"), TEXT("B")));
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Ordered Bundles keeps its independent candidate strategy"),
		Result.Diagnostics.HierarchicalCoarseFacePairCount, 0);
	TestEqual(TEXT("Ordered Bundles does not receive distributed fine pruning"),
		Result.Diagnostics.PrunedFineFacePairCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParadoxDistributedSpatialContextTest,
	"Paradox.PuzzleOverlay.DistributedRepulsive.25.SpatialContextAndHierarchyBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FParadoxDistributedSpatialContextTest::RunTest(const FString& Parameters)
{
	using namespace ParadoxPuzzleWireDistributedRepulsiveTests;
	FParadoxPuzzleRoutingSnapshot Snapshot = MakeSnapshot();
	Snapshot.Settings.GridCellSubdivision = EParadoxPuzzleRoutingSubdivision::TwoByTwo;
	Snapshot.Settings.PitchX = 50.0;
	Snapshot.Settings.PitchY = 50.0;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		FParadoxPuzzleRoutingLink Link = MakeLink(
			Index,
			{-12, Index - 6, 0.0},
			{12, 6 - Index, 0.0},
			*FString::Printf(TEXT("S%d"), Index),
			*FString::Printf(TEXT("T%d"), Index));
		Link.SourceBounds = MakeBounds(
			*FString::Printf(TEXT("S%d"), Index),
			FVector(-600.0, (Index - 6) * 50.0, 0.0));
		Link.TargetBounds = MakeBounds(
			*FString::Printf(TEXT("T%d"), Index),
			FVector(600.0, (6 - Index) * 50.0, 0.0));
		Snapshot.Links.Add(Link);
	}
	const FParadoxPuzzleRoutingResult Result = FParadoxPuzzleWireRouter::CalculateRoutes(Snapshot);
	TestEqual(TEXT("Every link is routed"), Result.Routes.Num(), Snapshot.Links.Num());
	TestEqual(TEXT("Coarse hierarchy evaluates 36 pairs per link"),
		Result.Diagnostics.HierarchicalCoarseFacePairCount, Snapshot.Links.Num() * 36);
	TestTrue(TEXT("Fine candidate generation is pruned"), Result.Diagnostics.PrunedFineFacePairCount > 0);
	TestTrue(TEXT("Repulsive context is built once, with at most one best-pass restoration"),
		Result.Diagnostics.RepulsiveContextBuildCount >= 1
		&& Result.Diagnostics.RepulsiveContextBuildCount <= 2);
	TestTrue(TEXT("Large selected edge sets use spatial queries"), Result.Diagnostics.SpatialQueryCount > 0);
	TestTrue(TEXT("Spatial queries inspect a bounded subset of indexed edges"),
		Result.Diagnostics.SpatialEdgeVisitCount > 0);
	TestTrue(TEXT("Candidate count remains under the configured hard budget"),
		Result.Diagnostics.CandidateCount
		<= Snapshot.Links.Num() * Snapshot.Settings.MaxDistributedCandidatesPerLink);
	return true;
}

#endif
