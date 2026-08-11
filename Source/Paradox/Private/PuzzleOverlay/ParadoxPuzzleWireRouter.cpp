#include "PuzzleOverlay/ParadoxPuzzleWireRouter.h"

#include "PuzzleOverlay/ParadoxPuzzleWireRouterInternal.h"

#include "HAL/PlatformTime.h"

namespace ParadoxPuzzleWireRouter
{
	constexpr double HeightBucketSize = 10.0;
	constexpr double CoordinateTolerance = KINDA_SMALL_NUMBER;

	struct FUnitEdge
	{
		int32 AX = 0;
		int32 AY = 0;
		int32 BX = 0;
		int32 BY = 0;
		int32 HeightBucket = 0;

		bool operator==(const FUnitEdge& Other) const
		{
			return AX == Other.AX && AY == Other.AY && BX == Other.BX && BY == Other.BY
				&& HeightBucket == Other.HeightBucket;
		}
	};

	uint32 GetTypeHash(const FUnitEdge& Edge)
	{
		uint32 Hash = HashCombineFast(::GetTypeHash(Edge.AX), ::GetTypeHash(Edge.AY));
		Hash = HashCombineFast(Hash, ::GetTypeHash(Edge.BX));
		Hash = HashCombineFast(Hash, ::GetTypeHash(Edge.BY));
		return HashCombineFast(Hash, ::GetTypeHash(Edge.HeightBucket));
	}

	struct FReservedHorizontalSegment
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		EParadoxPuzzleWireAxis Axis = EParadoxPuzzleWireAxis::X;
	};

	struct FCandidateEvaluation
	{
		TArray<FParadoxPuzzleWireSegment> Segments;
		FParadoxPuzzleWirePort SourcePort;
		FParadoxPuzzleWirePort TargetPort;
		double ConflictScore = TNumericLimits<double>::Max();
		double VisualScore = TNumericLimits<double>::Max();
		int32 TopologyCornerCount = MAX_int32;
		int32 TerminalCornerCount = 0;
		int32 Crossings = 0;
		int32 LaneConflicts = 0;
		int32 SurfaceHits = 0;
		int32 SurfaceMisses = 0;
		TOptional<FVector> FirstCrossingPoint;
		bool bRejectedEndpointInterior = false;
		bool bRejectedNetworkBounds = false;
		bool bValid = false;
	};

	struct FCandidateSpec
	{
		FParadoxPuzzleWirePort SourcePort;
		FParadoxPuzzleWirePort TargetPort;
		TArray<FParadoxPuzzleRoutingCoord> Points;
		int32 StableOrder = INDEX_NONE;
	};

	TArray<FCandidateSpec> BuildCandidateSpecs(
		const FParadoxPuzzleRoutingLink& Link,
		const FParadoxPuzzleWirePort& SourcePort,
		const FParadoxPuzzleWirePort& TargetPort,
		const FParadoxPuzzleRoutingSettings& Settings,
		const TArray<FParadoxPuzzleWireEndpointBounds>& NetworkObstacles,
		bool bRejectNetworkObstacles);

	struct FRouteCornerCounts
	{
		int32 Topology = 0;
		int32 Terminal = 0;
		int32 Bridge = 0;
		int32 Rendered = 0;
	};

	int32 ToHeightBucket(const double Height)
	{
		return FMath::RoundToInt(Height / HeightBucketSize);
	}

	FVector ToLocalPoint(const FParadoxPuzzleRoutingCoord& Coord, const FParadoxPuzzleRoutingSettings& Settings)
	{
		return FVector(Coord.X * Settings.PitchX, Coord.Y * Settings.PitchY, Coord.Z);
	}

	bool IsSameCoord(const FParadoxPuzzleRoutingCoord& A, const FParadoxPuzzleRoutingCoord& B)
	{
		return A.X == B.X && A.Y == B.Y && FMath::IsNearlyEqual(A.Z, B.Z, CoordinateTolerance);
	}

	EParadoxPuzzleWireAxis GetAxis(const FParadoxPuzzleRoutingCoord& A, const FParadoxPuzzleRoutingCoord& B)
	{
		if (A.X != B.X)
		{
			return EParadoxPuzzleWireAxis::X;
		}
		if (A.Y != B.Y)
		{
			return EParadoxPuzzleWireAxis::Y;
		}
		return EParadoxPuzzleWireAxis::Z;
	}

	void CompactCandidate(TArray<FParadoxPuzzleRoutingCoord>& Points)
	{
		for (int32 Index = Points.Num() - 1; Index > 0; --Index)
		{
			if (IsSameCoord(Points[Index], Points[Index - 1]))
			{
				Points.RemoveAt(Index);
			}
		}

		for (int32 Index = Points.Num() - 2; Index > 0; --Index)
		{
			if (GetAxis(Points[Index - 1], Points[Index]) == GetAxis(Points[Index], Points[Index + 1]))
			{
				Points.RemoveAt(Index);
			}
		}
	}

	void AddCandidate(
		TArray<TArray<FParadoxPuzzleRoutingCoord>>& OutCandidates,
		TArray<FParadoxPuzzleRoutingCoord> Candidate,
		const int32 MaxCandidates)
	{
		if (OutCandidates.Num() >= MaxCandidates)
		{
			return;
		}

		CompactCandidate(Candidate);
		if (Candidate.IsEmpty())
		{
			return;
		}

		for (const TArray<FParadoxPuzzleRoutingCoord>& Existing : OutCandidates)
		{
			if (Existing.Num() != Candidate.Num())
			{
				continue;
			}

			bool bIdentical = true;
			for (int32 Index = 0; Index < Existing.Num(); ++Index)
			{
				if (!IsSameCoord(Existing[Index], Candidate[Index]))
				{
					bIdentical = false;
					break;
				}
			}

			if (bIdentical)
			{
				return;
			}
		}

		OutCandidates.Add(MoveTemp(Candidate));
	}

	TArray<TArray<FParadoxPuzzleRoutingCoord>> BuildCandidates(
		const FParadoxPuzzleRoutingCoord& Start,
		const FParadoxPuzzleRoutingCoord& End,
		const FParadoxPuzzleRoutingSettings& Settings,
		const bool bDetoursOnly)
	{
		TArray<TArray<FParadoxPuzzleRoutingCoord>> Candidates;
		const int32 MaxCandidates = FMath::Max(1, Settings.MaxCandidatesPerLink);
		if (IsSameCoord(Start, End) && !bDetoursOnly)
		{
			AddCandidate(Candidates, {Start}, MaxCandidates);
			return Candidates;
		}

		const auto AddPermutation = [&](const int32 FirstAxis, const int32 SecondAxis, const int32 ThirdAxis)
		{
			TArray<FParadoxPuzzleRoutingCoord> Candidate;
			Candidate.Add(Start);
			FParadoxPuzzleRoutingCoord Cursor = Start;
			for (const int32 Axis : {FirstAxis, SecondAxis, ThirdAxis})
			{
				switch (Axis)
				{
				case 0: Cursor.X = End.X; break;
				case 1: Cursor.Y = End.Y; break;
				default: Cursor.Z = End.Z; break;
				}
				Candidate.Add(Cursor);
			}
			AddCandidate(Candidates, MoveTemp(Candidate), MaxCandidates);
		};

		if (!bDetoursOnly)
		{
			AddPermutation(0, 1, 2);
			AddPermutation(1, 0, 2);
			AddPermutation(2, 0, 1);
			AddPermutation(2, 1, 0);
			AddPermutation(0, 2, 1);
			AddPermutation(1, 2, 0);
			return Candidates;
		}

		const int32 RerouteLimit = FMath::Min(FMath::Max(0, Settings.MaxRerouteAttempts), 16);
		for (int32 Offset = 1; Offset <= RerouteLimit && Candidates.Num() < MaxCandidates; ++Offset)
		{
			for (const int32 Sign : {-1, 1})
			{
				const int32 XDetour = Start.X + (Offset * Sign);
				AddCandidate(Candidates,
					{Start, {XDetour, Start.Y, Start.Z}, {XDetour, End.Y, Start.Z}, {End.X, End.Y, Start.Z}, End},
					MaxCandidates);

				const int32 YDetour = Start.Y + (Offset * Sign);
				AddCandidate(Candidates,
					{Start, {Start.X, YDetour, Start.Z}, {End.X, YDetour, Start.Z}, {End.X, End.Y, Start.Z}, End},
					MaxCandidates);
			}
		}

		return Candidates;
	}

	bool DoSegmentsCross(
		const FVector& AStart,
		const FVector& AEnd,
		const EParadoxPuzzleWireAxis AAxis,
		const FReservedHorizontalSegment& B,
		FVector* OutCrossingPoint)
	{
		if (AAxis == B.Axis || AAxis == EParadoxPuzzleWireAxis::Z || B.Axis == EParadoxPuzzleWireAxis::Z
			|| !FMath::IsNearlyEqual(AStart.Z, B.Start.Z, HeightBucketSize * 0.5))
		{
			return false;
		}

		const FVector& XStart = AAxis == EParadoxPuzzleWireAxis::X ? AStart : B.Start;
		const FVector& XEnd = AAxis == EParadoxPuzzleWireAxis::X ? AEnd : B.End;
		const FVector& YStart = AAxis == EParadoxPuzzleWireAxis::Y ? AStart : B.Start;
		const FVector& YEnd = AAxis == EParadoxPuzzleWireAxis::Y ? AEnd : B.End;

		const double MinX = FMath::Min(XStart.X, XEnd.X);
		const double MaxX = FMath::Max(XStart.X, XEnd.X);
		const double MinY = FMath::Min(YStart.Y, YEnd.Y);
		const double MaxY = FMath::Max(YStart.Y, YEnd.Y);
		const double CrossingX = YStart.X;
		const double CrossingY = XStart.Y;

		// Shared endpoints are legitimate ports/junctions rather than visual crossings.
		const bool bCrosses = CrossingX > MinX + CoordinateTolerance && CrossingX < MaxX - CoordinateTolerance
			&& CrossingY > MinY + CoordinateTolerance && CrossingY < MaxY - CoordinateTolerance;
		if (bCrosses && OutCrossingPoint)
		{
			*OutCrossingPoint = FVector(CrossingX, CrossingY, AStart.Z);
		}
		return bCrosses;
	}

	void AppendUnitEdges(
		const FParadoxPuzzleRoutingCoord& Start,
		const FParadoxPuzzleRoutingCoord& End,
		TArray<FUnitEdge>& OutEdges)
	{
		if (Start.X != End.X && Start.Y == End.Y)
		{
			const int32 Step = End.X > Start.X ? 1 : -1;
			for (int32 X = Start.X; X != End.X; X += Step)
			{
				const int32 NextX = X + Step;
				OutEdges.Add({FMath::Min(X, NextX), Start.Y, FMath::Max(X, NextX), Start.Y, ToHeightBucket(Start.Z)});
			}
		}
		else if (Start.Y != End.Y && Start.X == End.X)
		{
			const int32 Step = End.Y > Start.Y ? 1 : -1;
			for (int32 Y = Start.Y; Y != End.Y; Y += Step)
			{
				const int32 NextY = Y + Step;
				OutEdges.Add({Start.X, FMath::Min(Y, NextY), Start.X, FMath::Max(Y, NextY), ToHeightBucket(Start.Z)});
			}
		}
	}

	struct FLaneChoice
	{
		int32 Lane = 0;
		int32 ConflictingEdges = 0;
	};

	FLaneChoice FindRouteLane(
		const TArray<FUnitEdge>& Edges,
		const TMap<FUnitEdge, uint32>& OccupiedLanes,
		const int32 MaxLanes)
	{
		FLaneChoice Best;
		Best.ConflictingEdges = MAX_int32;
		const int32 LaneCount = FMath::Clamp(MaxLanes, 1, 31);
		for (int32 Lane = 0; Lane < LaneCount; ++Lane)
		{
			int32 Conflicts = 0;
			for (const FUnitEdge& Edge : Edges)
			{
				if (const uint32* EdgeMask = OccupiedLanes.Find(Edge))
				{
					Conflicts += ((*EdgeMask & (1u << Lane)) != 0) ? 1 : 0;
				}
			}
			if (Conflicts < Best.ConflictingEdges)
			{
				Best.Lane = Lane;
				Best.ConflictingEdges = Conflicts;
				if (Conflicts == 0)
				{
					break;
				}
			}
		}
		if (Best.ConflictingEdges == MAX_int32)
		{
			Best.ConflictingEdges = 0;
		}
		return Best;
	}

	bool IsSurfaceSupported(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const FParadoxPuzzleRoutingCoord& Coord,
		int32& OutHits,
		int32& OutMisses)
	{
		const FParadoxPuzzleSurfaceKey Key{Coord.X, Coord.Y, ToHeightBucket(Coord.Z)};
		if (const FParadoxPuzzleSurfaceSample* Sample = Snapshot.SurfaceSamples.Find(Key))
		{
			++OutHits;
			return Sample->bHasSurface;
		}
		++OutMisses;
		return false;
	}

	double LaneOffsetForIndex(const int32 Lane, const double Spacing)
	{
		if (Lane <= 0)
		{
			return 0.0;
		}
		const int32 Magnitude = (Lane + 1) / 2;
		return (Lane % 2 == 1 ? 1.0 : -1.0) * Magnitude * Spacing;
	}

	double GetAxisValue(const FVector& Point, const EParadoxPuzzleWireAxis Axis)
	{
		switch (Axis)
		{
		case EParadoxPuzzleWireAxis::X: return Point.X;
		case EParadoxPuzzleWireAxis::Y: return Point.Y;
		default: return Point.Z;
		}
	}

	void SetAxisValue(FVector& Point, const EParadoxPuzzleWireAxis Axis, const double Value)
	{
		switch (Axis)
		{
		case EParadoxPuzzleWireAxis::X: Point.X = Value; break;
		case EParadoxPuzzleWireAxis::Y: Point.Y = Value; break;
		default: Point.Z = Value; break;
		}
	}

	bool ApplyLaneGeometry(
		TArray<FParadoxPuzzleWireSegment>& InOutSegments,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		for (FParadoxPuzzleWireSegment& Segment : InOutSegments)
		{
			const double Offset = LaneOffsetForIndex(Segment.Lane, Settings.LaneSpacing);
			if (Segment.Axis == EParadoxPuzzleWireAxis::X)
			{
				Segment.Start.Y += Offset;
				Segment.End.Y += Offset;
			}
			else if (Segment.Axis == EParadoxPuzzleWireAxis::Y)
			{
				Segment.Start.X += Offset;
				Segment.End.X += Offset;
			}
		}

		for (int32 Index = 1; Index < InOutSegments.Num(); ++Index)
		{
			FParadoxPuzzleWireSegment& Previous = InOutSegments[Index - 1];
			FParadoxPuzzleWireSegment& Current = InOutSegments[Index];
			if (Previous.End.Equals(Current.Start, CoordinateTolerance))
			{
				continue;
			}
			if (Previous.Axis == Current.Axis
				|| Previous.Axis == EParadoxPuzzleWireAxis::Z
				|| Current.Axis == EParadoxPuzzleWireAxis::Z)
			{
				return false;
			}

			FVector Join = Previous.End;
			SetAxisValue(Join, Previous.Axis, GetAxisValue(Current.Start, Previous.Axis));
			SetAxisValue(Join, Current.Axis, GetAxisValue(Previous.End, Current.Axis));
			Previous.End = Join;
			Current.Start = Join;
		}
		return true;
	}

	void BuildSourceTerminal(
		const FParadoxPuzzleWirePort& Port,
		const FVector& RouteStart,
		TArray<FParadoxPuzzleWireSegment>& OutSegments);

	void BuildTargetTerminal(
		const FParadoxPuzzleWirePort& Port,
		const FVector& RouteEnd,
		TArray<FParadoxPuzzleWireSegment>& OutSegments);

	void NormalizeSegments(TArray<FParadoxPuzzleWireSegment>& InOutSegments);
	FRouteCornerCounts CountRouteCorners(const TArray<FParadoxPuzzleWireSegment>& Segments);
	bool SegmentEntersBounds(
		const FVector& Start,
		const FVector& End,
		const FParadoxPuzzleWireEndpointBounds& Bounds);

	FCandidateEvaluation EvaluateCandidate(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const TArray<FParadoxPuzzleRoutingCoord>& Points,
		const FParadoxPuzzleWirePort& SourcePort,
		const FParadoxPuzzleWirePort& TargetPort,
		const TMap<FUnitEdge, uint32>& OccupiedLanes,
		const TArray<FReservedHorizontalSegment>& ReservedSegments,
		const TArray<FParadoxPuzzleWireEndpointBounds>& NetworkObstacles,
		const bool bRejectNetworkObstacles)
	{
		FCandidateEvaluation Evaluation;
		Evaluation.SourcePort = SourcePort;
		Evaluation.TargetPort = TargetPort;
		Evaluation.ConflictScore = 0.0;
		Evaluation.VisualScore = 0.0;
		TArray<FParadoxPuzzleWireSegment> MainSegments;
		TArray<FUnitEdge> RouteEdges;
		bool bHasStructuralVertical = false;

		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FParadoxPuzzleRoutingCoord& CoordStart = Points[Index - 1];
			const FParadoxPuzzleRoutingCoord& CoordEnd = Points[Index];
			const EParadoxPuzzleWireAxis Axis = GetAxis(CoordStart, CoordEnd);
			const FVector Start = ToLocalPoint(CoordStart, Snapshot.Settings);
			const FVector End = ToLocalPoint(CoordEnd, Snapshot.Settings);
			const double Length = FVector::Distance(Start, End);

			FParadoxPuzzleWireSegment& Segment = MainSegments.AddDefaulted_GetRef();
			Segment.Start = Start;
			Segment.End = End;
			Segment.Axis = Axis;
			Segment.Kind = Axis == EParadoxPuzzleWireAxis::Z
				? EParadoxPuzzleWireSegmentKind::StructuralVertical
				: EParadoxPuzzleWireSegmentKind::GroundSupported;

			Evaluation.VisualScore += Length;
			if (Axis == EParadoxPuzzleWireAxis::Z)
			{
				bHasStructuralVertical = true;
				Evaluation.VisualScore += Length * Snapshot.Settings.VerticalPenalty;
				continue;
			}

			TArray<FUnitEdge> Edges;
			AppendUnitEdges(CoordStart, CoordEnd, Edges);
			RouteEdges.Append(Edges);

			const int32 StepCount = FMath::Max(FMath::Abs(CoordEnd.X - CoordStart.X), FMath::Abs(CoordEnd.Y - CoordStart.Y));
			bool bAllSupported = true;
			for (int32 Step = 0; Step <= StepCount; ++Step)
			{
				FParadoxPuzzleRoutingCoord SampleCoord = CoordStart;
				if (StepCount > 0)
				{
					SampleCoord.X = FMath::RoundToInt(FMath::Lerp(static_cast<double>(CoordStart.X), static_cast<double>(CoordEnd.X), static_cast<double>(Step) / StepCount));
					SampleCoord.Y = FMath::RoundToInt(FMath::Lerp(static_cast<double>(CoordStart.Y), static_cast<double>(CoordEnd.Y), static_cast<double>(Step) / StepCount));
				}
				bAllSupported &= IsSurfaceSupported(Snapshot, SampleCoord, Evaluation.SurfaceHits, Evaluation.SurfaceMisses);
			}
			if (!bAllSupported)
			{
				Segment.Kind = EParadoxPuzzleWireSegmentKind::GroundUnsupported;
				Evaluation.VisualScore += Length * Snapshot.Settings.UnsupportedPenalty;
			}
		}

		const FLaneChoice LaneChoice = bHasStructuralVertical
			? FLaneChoice{}
			: FindRouteLane(RouteEdges, OccupiedLanes, Snapshot.Settings.MaxLanesPerEdge);
		Evaluation.LaneConflicts = LaneChoice.ConflictingEdges;
		Evaluation.ConflictScore += LaneChoice.Lane * Snapshot.Settings.LanePenalty;
		Evaluation.ConflictScore += LaneChoice.ConflictingEdges * Snapshot.Settings.LanePenalty;
		for (FParadoxPuzzleWireSegment& Segment : MainSegments)
		{
			Segment.Lane = Segment.Axis == EParadoxPuzzleWireAxis::Z ? 0 : LaneChoice.Lane;
		}
		if (!ApplyLaneGeometry(MainSegments, Snapshot.Settings))
		{
			Evaluation.TopologyCornerCount = MAX_int32;
			return Evaluation;
		}

		for (const FParadoxPuzzleWireSegment& Segment : MainSegments)
		{
			if (Segment.Axis == EParadoxPuzzleWireAxis::Z)
			{
				continue;
			}
			for (const FReservedHorizontalSegment& Reserved : ReservedSegments)
			{
				FVector CrossingPoint;
				if (DoSegmentsCross(Segment.Start, Segment.End, Segment.Axis, Reserved, &CrossingPoint))
				{
					++Evaluation.Crossings;
					if (!Evaluation.FirstCrossingPoint.IsSet())
					{
						Evaluation.FirstCrossingPoint = CrossingPoint;
					}
				}
			}
		}
		Evaluation.ConflictScore += Evaluation.Crossings * Snapshot.Settings.CrossingPenalty;

		const FVector RouteStart = MainSegments.IsEmpty()
			? ToLocalPoint(Points[0], Snapshot.Settings)
			: MainSegments[0].Start;
		const FVector RouteEnd = MainSegments.IsEmpty()
			? ToLocalPoint(Points.Last(), Snapshot.Settings)
			: MainSegments.Last().End;
		BuildSourceTerminal(SourcePort, RouteStart, Evaluation.Segments);
		Evaluation.Segments.Append(MainSegments);
		BuildTargetTerminal(TargetPort, RouteEnd, Evaluation.Segments);
		NormalizeSegments(Evaluation.Segments);
		for (const FParadoxPuzzleWireSegment& Segment : Evaluation.Segments)
		{
			if (SegmentEntersBounds(Segment.Start, Segment.End, SourcePort.Bounds)
				|| SegmentEntersBounds(Segment.Start, Segment.End, TargetPort.Bounds))
			{
				Evaluation.bRejectedEndpointInterior = true;
				return Evaluation;
			}
			if (bRejectNetworkObstacles)
			{
				for (const FParadoxPuzzleWireEndpointBounds& Obstacle : NetworkObstacles)
				{
					if (SegmentEntersBounds(Segment.Start, Segment.End, Obstacle))
					{
						Evaluation.bRejectedNetworkBounds = true;
						return Evaluation;
					}
				}
			}
		}
		const FRouteCornerCounts Corners = CountRouteCorners(Evaluation.Segments);
		Evaluation.TopologyCornerCount = Corners.Topology;
		Evaluation.TerminalCornerCount = Corners.Terminal;
		for (const FParadoxPuzzleWireSegment& Segment : Evaluation.Segments)
		{
			if (Segment.Kind == EParadoxPuzzleWireSegmentKind::EndpointTerminal)
			{
				Evaluation.VisualScore += FVector::Distance(Segment.Start, Segment.End);
			}
		}
		Evaluation.bValid = true;
		return Evaluation;
	}

	bool IsBetterEvaluation(const FCandidateEvaluation& Candidate, const FCandidateEvaluation& Current)
	{
		if (Candidate.TopologyCornerCount != Current.TopologyCornerCount)
		{
			return Candidate.TopologyCornerCount < Current.TopologyCornerCount;
		}
		if (!FMath::IsNearlyEqual(Candidate.ConflictScore, Current.ConflictScore))
		{
			return Candidate.ConflictScore < Current.ConflictScore;
		}
		if (!FMath::IsNearlyEqual(Candidate.VisualScore, Current.VisualScore))
		{
			return Candidate.VisualScore < Current.VisualScore;
		}
		return false;
	}

	void ReserveRoute(
		const TArray<FParadoxPuzzleWireSegment>& Segments,
		const FParadoxPuzzleRoutingSettings& Settings,
		TMap<FUnitEdge, uint32>& InOutOccupiedLanes,
		TArray<FReservedHorizontalSegment>& InOutReservedSegments)
	{
		for (const FParadoxPuzzleWireSegment& Segment : Segments)
		{
			if (Segment.Axis == EParadoxPuzzleWireAxis::Z
				|| Segment.Kind == EParadoxPuzzleWireSegmentKind::EndpointTerminal)
			{
				continue;
			}

			FParadoxPuzzleRoutingCoord Start{
				FMath::RoundToInt(Segment.Start.X / Settings.PitchX),
				FMath::RoundToInt(Segment.Start.Y / Settings.PitchY),
				Segment.Start.Z};
			FParadoxPuzzleRoutingCoord End{
				FMath::RoundToInt(Segment.End.X / Settings.PitchX),
				FMath::RoundToInt(Segment.End.Y / Settings.PitchY),
				Segment.End.Z};
			TArray<FUnitEdge> Edges;
			AppendUnitEdges(Start, End, Edges);
			for (const FUnitEdge& Edge : Edges)
			{
				InOutOccupiedLanes.FindOrAdd(Edge) |= (1u << FMath::Clamp(Segment.Lane, 0, 30));
			}
			InOutReservedSegments.Add({Segment.Start, Segment.End, Segment.Axis});
		}
	}

	void AddBridgeFallback(
		TArray<FParadoxPuzzleWireSegment>& InOutSegments,
		const FParadoxPuzzleRoutingSettings& Settings,
		const TOptional<FVector>& FirstCrossingPoint,
		int32& OutBridgeCount)
	{
		if (!FirstCrossingPoint.IsSet())
		{
			return;
		}
		for (int32 Index = 0; Index < InOutSegments.Num(); ++Index)
		{
			const FParadoxPuzzleWireSegment Original = InOutSegments[Index];
			const FVector CrossingPoint = FirstCrossingPoint.GetValue();
			const bool bContainsCrossing = Original.Axis != EParadoxPuzzleWireAxis::Z
				&& FMath::IsNearlyEqual(Original.Start.Z, CrossingPoint.Z, HeightBucketSize * 0.5)
				&& (Original.Axis == EParadoxPuzzleWireAxis::X
					? FMath::IsNearlyEqual(Original.Start.Y, CrossingPoint.Y, CoordinateTolerance)
						&& CrossingPoint.X > FMath::Min(Original.Start.X, Original.End.X) + CoordinateTolerance
						&& CrossingPoint.X < FMath::Max(Original.Start.X, Original.End.X) - CoordinateTolerance
					: FMath::IsNearlyEqual(Original.Start.X, CrossingPoint.X, CoordinateTolerance)
						&& CrossingPoint.Y > FMath::Min(Original.Start.Y, Original.End.Y) + CoordinateTolerance
						&& CrossingPoint.Y < FMath::Max(Original.Start.Y, Original.End.Y) - CoordinateTolerance);
			if (!bContainsCrossing)
			{
				continue;
			}

			const FVector Direction = (Original.End - Original.Start).GetSafeNormal();
			const double SegmentLength = FVector::Distance(Original.Start, Original.End);
			const double Pitch = Original.Axis == EParadoxPuzzleWireAxis::X ? Settings.PitchX : Settings.PitchY;
			const double HalfSpan = FMath::Min(Pitch * 0.35, SegmentLength * 0.25);
			const FVector BridgeStart = CrossingPoint - Direction * HalfSpan;
			const FVector BridgeEnd = CrossingPoint + Direction * HalfSpan;
			const FVector RaisedStart = BridgeStart + FVector(0.0, 0.0, Settings.BridgeHeightOffset);
			const FVector RaisedEnd = BridgeEnd + FVector(0.0, 0.0, Settings.BridgeHeightOffset);
			TArray<FParadoxPuzzleWireSegment> Replacement;
			if (!Original.Start.Equals(BridgeStart, CoordinateTolerance))
			{
				Replacement.Add({Original.Start, BridgeStart, Original.Axis, Original.Kind, Original.Lane});
			}
			Replacement.Add({BridgeStart, RaisedStart, EParadoxPuzzleWireAxis::Z, EParadoxPuzzleWireSegmentKind::BridgeVertical, Original.Lane});
			Replacement.Add({RaisedStart, RaisedEnd, Original.Axis, EParadoxPuzzleWireSegmentKind::BridgeHorizontal, Original.Lane});
			Replacement.Add({RaisedEnd, BridgeEnd, EParadoxPuzzleWireAxis::Z, EParadoxPuzzleWireSegmentKind::BridgeVertical, Original.Lane});
			if (!BridgeEnd.Equals(Original.End, CoordinateTolerance))
			{
				Replacement.Add({BridgeEnd, Original.End, Original.Axis, Original.Kind, Original.Lane});
			}
			InOutSegments.RemoveAt(Index);
			for (int32 ReplacementIndex = Replacement.Num() - 1; ReplacementIndex >= 0; --ReplacementIndex)
			{
				InOutSegments.Insert(Replacement[ReplacementIndex], Index);
			}
			++OutBridgeCount;
			return;
		}
	}

	FVector GetPortNormal(const EParadoxPuzzlePortSide Side)
	{
		switch (Side)
		{
		case EParadoxPuzzlePortSide::North: return FVector(0.0, 1.0, 0.0);
		case EParadoxPuzzlePortSide::South: return FVector(0.0, -1.0, 0.0);
		case EParadoxPuzzlePortSide::West: return FVector(-1.0, 0.0, 0.0);
		default: return FVector(1.0, 0.0, 0.0);
		}
	}

	EParadoxPuzzlePortSide GetOppositeSide(const EParadoxPuzzlePortSide Side)
	{
		switch (Side)
		{
		case EParadoxPuzzlePortSide::North: return EParadoxPuzzlePortSide::South;
		case EParadoxPuzzlePortSide::South: return EParadoxPuzzlePortSide::North;
		case EParadoxPuzzlePortSide::East: return EParadoxPuzzlePortSide::West;
		default: return EParadoxPuzzlePortSide::East;
		}
	}

	FVector GetEndpointCenter(
		const FParadoxPuzzleWireEndpointBounds& Bounds,
		const FParadoxPuzzleRoutingCoord& Fallback,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		return Bounds.bValid ? Bounds.GetCenter() : ToLocalPoint(Fallback, Settings);
	}

	EParadoxPuzzlePortSide DeterminePortSide(
		const FParadoxPuzzleWireEndpointBounds& OwnBounds,
		const FParadoxPuzzleRoutingCoord& OwnFallback,
		const FParadoxPuzzleWireEndpointBounds& RemoteBounds,
		const FParadoxPuzzleRoutingCoord& RemoteFallback,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		const FVector OwnCenter = GetEndpointCenter(OwnBounds, OwnFallback, Settings);
		const FVector RemoteCenter = GetEndpointCenter(RemoteBounds, RemoteFallback, Settings);
		const FVector OwnExtent = OwnBounds.bValid ? OwnBounds.GetExtent() : FVector::ZeroVector;
		const FVector RemoteExtent = RemoteBounds.bValid ? RemoteBounds.GetExtent() : FVector::ZeroVector;
		const double DeltaX = RemoteCenter.X - OwnCenter.X;
		const double DeltaY = RemoteCenter.Y - OwnCenter.Y;
		const double NormalizedX = FMath::Abs(DeltaX) / FMath::Max(1.0, OwnExtent.X + RemoteExtent.X);
		const double NormalizedY = FMath::Abs(DeltaY) / FMath::Max(1.0, OwnExtent.Y + RemoteExtent.Y);
		// X wins exact ties so face selection remains deterministic.
		if (NormalizedX >= NormalizedY)
		{
			return DeltaX >= 0.0 ? EParadoxPuzzlePortSide::East : EParadoxPuzzlePortSide::West;
		}
		return DeltaY >= 0.0 ? EParadoxPuzzlePortSide::North : EParadoxPuzzlePortSide::South;
	}

	FParadoxPuzzleWirePort MakePort(
		const FParadoxPuzzleWireEndpointBounds& Bounds,
		const FParadoxPuzzleRoutingCoord& Fallback,
		const FVector& RemoteCenter,
		const EParadoxPuzzlePortSide Side,
		const FParadoxPuzzleRoutingSettings& Settings,
		const TOptional<double> TangentialPosition = TOptional<double>(),
		const int32 FaceSlotIndex = INDEX_NONE,
		const int32 FaceSlotCount = 1)
	{
		FParadoxPuzzleWirePort Port;
		Port.Bounds = Bounds;
		Port.Side = Side;
		Port.Normal = GetPortNormal(Side);
		Port.FaceSlotIndex = FaceSlotIndex;
		Port.FaceSlotCount = FMath::Max(1, FaceSlotCount);
		const FVector FallbackPoint = ToLocalPoint(Fallback, Settings);
		if (!Bounds.bValid)
		{
			Port.Position = FallbackPoint;
			Port.ClearancePoint = Port.Position + Port.Normal * Settings.EndpointClearance;
			return Port;
		}

		Port.bValid = true;
		Port.Position = Bounds.GetCenter();
		Port.Position.Z = FMath::Clamp(FallbackPoint.Z, Bounds.Min.Z, Bounds.Max.Z);
		const bool bXAxisNormal = Side == EParadoxPuzzlePortSide::East || Side == EParadoxPuzzlePortSide::West;
		const double IntervalMin = (bXAxisNormal ? Bounds.Min.Y : Bounds.Min.X) + Settings.PortEdgeInset;
		const double IntervalMax = (bXAxisNormal ? Bounds.Max.Y : Bounds.Max.X) - Settings.PortEdgeInset;
		const double PreferredTangent = TangentialPosition.IsSet()
			? TangentialPosition.GetValue()
			: (bXAxisNormal ? RemoteCenter.Y : RemoteCenter.X);
		const double Tangent = IntervalMin <= IntervalMax
			? FMath::Clamp(PreferredTangent, IntervalMin, IntervalMax)
			: (bXAxisNormal ? Bounds.GetCenter().Y : Bounds.GetCenter().X);
		const double FaceCenterTangent = bXAxisNormal ? Bounds.GetCenter().Y : Bounds.GetCenter().X;
		const double UsableHalfSpan = FMath::Max(0.0, IntervalMax - IntervalMin) * 0.5;
		Port.NormalizedDistanceFromFaceCenter = UsableHalfSpan > CoordinateTolerance
			? FMath::Clamp(FMath::Abs(Tangent - FaceCenterTangent) / UsableHalfSpan, 0.0, 1.0)
			: 0.0;
		if (bXAxisNormal)
		{
			Port.Position.X = Side == EParadoxPuzzlePortSide::East ? Bounds.Max.X : Bounds.Min.X;
			Port.Position.Y = Tangent;
		}
		else
		{
			Port.Position.X = Tangent;
			Port.Position.Y = Side == EParadoxPuzzlePortSide::North ? Bounds.Max.Y : Bounds.Min.Y;
		}
		const double FanoutLength = Port.FaceSlotCount > 1 ? Settings.MultiPortFanoutLength : 0.0;
		Port.ClearancePoint = Port.Position + Port.Normal * (Settings.EndpointClearance + FanoutLength);
		return Port;
	}

	bool AreSameEndpointBounds(
		const FParadoxPuzzleWireEndpointBounds& A,
		const FParadoxPuzzleWireEndpointBounds& B)
	{
		if (!A.EndpointKey.IsEmpty() && !B.EndpointKey.IsEmpty())
		{
			return A.EndpointKey == B.EndpointKey;
		}
		return A.Min.Equals(B.Min, CoordinateTolerance) && A.Max.Equals(B.Max, CoordinateTolerance);
	}

	TArray<FParadoxPuzzleWireEndpointBounds> GatherNetworkObstacles(
		const TArray<FParadoxPuzzleRoutingLink>& Links,
		const FParadoxPuzzleRoutingLink& RoutedLink)
	{
		TArray<FParadoxPuzzleWireEndpointBounds> Obstacles;
		const auto AddObstacle = [&Obstacles, &RoutedLink](const FParadoxPuzzleWireEndpointBounds& Bounds)
		{
			if (!Bounds.bValid
				|| AreSameEndpointBounds(Bounds, RoutedLink.SourceBounds)
				|| AreSameEndpointBounds(Bounds, RoutedLink.TargetBounds))
			{
				return;
			}
			for (const FParadoxPuzzleWireEndpointBounds& Existing : Obstacles)
			{
				if (AreSameEndpointBounds(Bounds, Existing))
				{
					return;
				}
			}
			Obstacles.Add(Bounds);
		};

		for (const FParadoxPuzzleRoutingLink& Link : Links)
		{
			AddObstacle(Link.SourceBounds);
			AddObstacle(Link.TargetBounds);
		}
		return Obstacles;
	}

	struct FFacePairSelection
	{
		EParadoxPuzzlePortSide SourceSide = EParadoxPuzzlePortSide::East;
		EParadoxPuzzlePortSide TargetSide = EParadoxPuzzlePortSide::West;
		double Distance = TNumericLimits<double>::Max();
		bool bAvoidsNetworkBounds = false;
		bool bValid = false;
	};

	FFacePairSelection SelectEndpointFacePair(
		const FParadoxPuzzleRoutingLink& Link,
		const TArray<FParadoxPuzzleRoutingLink>& AllLinks,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		FFacePairSelection Best;
		const FVector SourceCenter = GetEndpointCenter(Link.SourceBounds, Link.Source, Settings);
		const FVector TargetCenter = GetEndpointCenter(Link.TargetBounds, Link.Target, Settings);
		const TArray<FParadoxPuzzleWireEndpointBounds> Obstacles = GatherNetworkObstacles(AllLinks, Link);
		const EParadoxPuzzlePortSide Sides[] = {
			EParadoxPuzzlePortSide::North,
			EParadoxPuzzlePortSide::South,
			EParadoxPuzzlePortSide::East,
			EParadoxPuzzlePortSide::West};

		for (const EParadoxPuzzlePortSide SourceSide : Sides)
		{
			const bool bSourceXAxisNormal = SourceSide == EParadoxPuzzlePortSide::East
				|| SourceSide == EParadoxPuzzlePortSide::West;
			const double SourceTangent = bSourceXAxisNormal ? SourceCenter.Y : SourceCenter.X;
			for (const EParadoxPuzzlePortSide TargetSide : Sides)
			{
				const bool bTargetXAxisNormal = TargetSide == EParadoxPuzzlePortSide::East
					|| TargetSide == EParadoxPuzzlePortSide::West;
				const double TargetTangent = bTargetXAxisNormal ? TargetCenter.Y : TargetCenter.X;
				const FParadoxPuzzleWirePort SourcePort = MakePort(
					Link.SourceBounds,
					Link.Source,
					TargetCenter,
					SourceSide,
					Settings,
					SourceTangent,
					0);
				const FParadoxPuzzleWirePort TargetPort = MakePort(
					Link.TargetBounds,
					Link.Target,
					SourceCenter,
					TargetSide,
					Settings,
					TargetTangent,
					0);
				const bool bAvoidsNetworkBounds = !BuildCandidateSpecs(
					Link,
					SourcePort,
					TargetPort,
					Settings,
					Obstacles,
					true).IsEmpty();
				const double Distance = FVector::Distance(SourcePort.Position, TargetPort.Position);
				const bool bBetterAvoidance = bAvoidsNetworkBounds && !Best.bAvoidsNetworkBounds;
				const bool bSameAvoidance = bAvoidsNetworkBounds == Best.bAvoidsNetworkBounds;
				const bool bShorter = Distance < Best.Distance - CoordinateTolerance;
				const bool bSameDistance = FMath::IsNearlyEqual(Distance, Best.Distance, CoordinateTolerance);
				const bool bStableEarlier = static_cast<uint8>(SourceSide) < static_cast<uint8>(Best.SourceSide)
					|| (SourceSide == Best.SourceSide
						&& static_cast<uint8>(TargetSide) < static_cast<uint8>(Best.TargetSide));
				if (!Best.bValid
					|| bBetterAvoidance
					|| (bSameAvoidance && (bShorter || (bSameDistance && bStableEarlier))))
				{
					Best.SourceSide = SourceSide;
					Best.TargetSide = TargetSide;
					Best.Distance = Distance;
					Best.bAvoidsNetworkBounds = bAvoidsNetworkBounds;
					Best.bValid = true;
				}
			}
		}
		return Best;
	}

	struct FPortRequest
	{
		int32 LinkIndex = INDEX_NONE;
		bool bSource = false;
		EParadoxPuzzlePortSide Side = EParadoxPuzzlePortSide::East;
		double PreferredTangent = 0.0;
	};

	struct FPortDistribution
	{
		double Start = 0.0;
		double Spacing = 0.0;
	};

	FPortDistribution CalculatePortDistribution(
		const int32 PortCount,
		const double FaceMin,
		const double FaceMax,
		const double MinimumEdgeInset)
	{
		FPortDistribution Distribution;
		const double FaceSpan = FMath::Max(0.0, FaceMax - FaceMin);
		const double FaceCenter = (FaceMin + FaceMax) * 0.5;
		Distribution.Start = FaceCenter;
		if (PortCount <= 1 || FaceSpan <= 0.0)
		{
			return Distribution;
		}

		const double EqualGap = FaceSpan / static_cast<double>(PortCount + 1);
		const double EdgeGap = FMath::Min(
			FaceSpan * 0.5,
			FMath::Max(FMath::Max(0.0, MinimumEdgeInset), EqualGap));
		Distribution.Start = FaceMin + EdgeGap;
		Distribution.Spacing = (FaceSpan - 2.0 * EdgeGap) / static_cast<double>(PortCount - 1);
		return Distribution;
	}

	void AssignEndpointPorts(
		TArray<FParadoxPuzzleRoutingLink>& Links,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		TMap<FString, TArray<FPortRequest>> Groups;
		for (int32 LinkIndex = 0; LinkIndex < Links.Num(); ++LinkIndex)
		{
			FParadoxPuzzleRoutingLink& Link = Links[LinkIndex];
			const FVector SourceCenter = GetEndpointCenter(Link.SourceBounds, Link.Source, Settings);
			const FVector TargetCenter = GetEndpointCenter(Link.TargetBounds, Link.Target, Settings);
			const bool bSelfLink = !Link.SourceBounds.EndpointKey.IsEmpty()
				&& Link.SourceBounds.EndpointKey == Link.TargetBounds.EndpointKey;
			const EParadoxPuzzlePortSide DirectionalSourceSide = DeterminePortSide(
				Link.SourceBounds, Link.Source, Link.TargetBounds, Link.Target, Settings);
			const FFacePairSelection FacePair = bSelfLink
				? FFacePairSelection{DirectionalSourceSide, GetOppositeSide(DirectionalSourceSide)}
				: SelectEndpointFacePair(Link, Links, Settings);
			const EParadoxPuzzlePortSide SourceSide = FacePair.SourceSide;
			const EParadoxPuzzlePortSide TargetSide = FacePair.TargetSide;
			Link.SourcePort = MakePort(Link.SourceBounds, Link.Source, TargetCenter, SourceSide, Settings);
			Link.TargetPort = MakePort(Link.TargetBounds, Link.Target, SourceCenter, TargetSide, Settings);

			const auto AddRequest = [&Groups, LinkIndex](
				const FParadoxPuzzleWirePort& Port,
				const FVector& RemoteCenter,
				const bool bSource)
			{
				if (!Port.Bounds.bValid)
				{
					return;
				}
				const bool bXAxisNormal = Port.Side == EParadoxPuzzlePortSide::East
					|| Port.Side == EParadoxPuzzlePortSide::West;
				const FString GroupKey = FString::Printf(
					TEXT("%s|%d"), *Port.Bounds.EndpointKey, static_cast<int32>(Port.Side));
				Groups.FindOrAdd(GroupKey).Add(
					{LinkIndex, bSource, Port.Side, bXAxisNormal ? RemoteCenter.Y : RemoteCenter.X});
			};
			AddRequest(Link.SourcePort, TargetCenter, true);
			AddRequest(Link.TargetPort, SourceCenter, false);
		}

		for (TPair<FString, TArray<FPortRequest>>& Pair : Groups)
		{
			TArray<FPortRequest>& Requests = Pair.Value;
			Requests.Sort([&Links](const FPortRequest& A, const FPortRequest& B)
			{
				if (!FMath::IsNearlyEqual(A.PreferredTangent, B.PreferredTangent))
				{
					return A.PreferredTangent < B.PreferredTangent;
				}
				if (Links[A.LinkIndex].StableOrder != Links[B.LinkIndex].StableOrder)
				{
					return Links[A.LinkIndex].StableOrder < Links[B.LinkIndex].StableOrder;
				}
				return A.bSource && !B.bSource;
			});
			if (Requests.IsEmpty())
			{
				continue;
			}

			const FPortRequest& First = Requests[0];
			const FParadoxPuzzleRoutingLink& FirstLink = Links[First.LinkIndex];
			const FParadoxPuzzleWireEndpointBounds& Bounds = First.bSource
				? FirstLink.SourceBounds : FirstLink.TargetBounds;
			const bool bXAxisNormal = First.Side == EParadoxPuzzlePortSide::East
				|| First.Side == EParadoxPuzzlePortSide::West;
			const double FaceMin = bXAxisNormal ? Bounds.Min.Y : Bounds.Min.X;
			const double FaceMax = bXAxisNormal ? Bounds.Max.Y : Bounds.Max.X;
			const FPortDistribution Distribution = CalculatePortDistribution(
				Requests.Num(),
				FaceMin,
				FaceMax,
				Settings.PortEdgeInset);
			for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
			{
				const FPortRequest& Request = Requests[RequestIndex];
				FParadoxPuzzleRoutingLink& Link = Links[Request.LinkIndex];
				const FVector RemoteCenter = Request.bSource
					? GetEndpointCenter(Link.TargetBounds, Link.Target, Settings)
					: GetEndpointCenter(Link.SourceBounds, Link.Source, Settings);
				FParadoxPuzzleWirePort Port = MakePort(
					Request.bSource ? Link.SourceBounds : Link.TargetBounds,
					Request.bSource ? Link.Source : Link.Target,
					RemoteCenter,
					Request.Side,
					Settings,
					Distribution.Start + Distribution.Spacing * RequestIndex,
					RequestIndex,
					Requests.Num());
				(Request.bSource ? Link.SourcePort : Link.TargetPort) = MoveTemp(Port);
			}
		}
	}

	TArray<FParadoxPuzzleRoutingCoord> GetClearanceLatticeCoords(
		const FParadoxPuzzleWirePort& Port,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		FParadoxPuzzleRoutingCoord Base;
		Base.Z = Port.ClearancePoint.Z;
		Base.X = FMath::RoundToInt(Port.ClearancePoint.X / Settings.PitchX);
		Base.Y = FMath::RoundToInt(Port.ClearancePoint.Y / Settings.PitchY);
		const bool bXAxisNormal = Port.Side == EParadoxPuzzlePortSide::East
			|| Port.Side == EParadoxPuzzlePortSide::West;
		switch (Port.Side)
		{
		case EParadoxPuzzlePortSide::East:
			Base.X = FMath::CeilToInt(Port.ClearancePoint.X / Settings.PitchX);
			break;
		case EParadoxPuzzlePortSide::West:
			Base.X = FMath::FloorToInt(Port.ClearancePoint.X / Settings.PitchX);
			break;
		case EParadoxPuzzlePortSide::North:
			Base.Y = FMath::CeilToInt(Port.ClearancePoint.Y / Settings.PitchY);
			break;
		case EParadoxPuzzlePortSide::South:
			Base.Y = FMath::FloorToInt(Port.ClearancePoint.Y / Settings.PitchY);
			break;
		}

		TArray<FParadoxPuzzleRoutingCoord> Results;
		const auto AddUnique = [&Results](const FParadoxPuzzleRoutingCoord& Candidate)
		{
			if (!Results.ContainsByPredicate([&Candidate](const FParadoxPuzzleRoutingCoord& Existing)
			{
				return IsSameCoord(Existing, Candidate);
			}))
			{
				Results.Add(Candidate);
			}
		};
		AddUnique(Base);
		const double TangentCoordinate = bXAxisNormal
			? Port.ClearancePoint.Y / Settings.PitchY
			: Port.ClearancePoint.X / Settings.PitchX;
		for (const int32 Tangent : {FMath::FloorToInt(TangentCoordinate), FMath::CeilToInt(TangentCoordinate)})
		{
			FParadoxPuzzleRoutingCoord Candidate = Base;
			if (bXAxisNormal)
			{
				Candidate.Y = Tangent;
			}
			else
			{
				Candidate.X = Tangent;
			}
			AddUnique(Candidate);
		}
		Results.Sort([&Port, &Settings](const FParadoxPuzzleRoutingCoord& A, const FParadoxPuzzleRoutingCoord& B)
		{
			const double DistanceA = FVector::DistSquared(ToLocalPoint(A, Settings), Port.ClearancePoint);
			const double DistanceB = FVector::DistSquared(ToLocalPoint(B, Settings), Port.ClearancePoint);
			return !FMath::IsNearlyEqual(DistanceA, DistanceB) ? DistanceA < DistanceB : A < B;
		});
		return Results;
	}

	EParadoxPuzzleWireAxis GetVectorAxis(const FVector& Start, const FVector& End)
	{
		if (!FMath::IsNearlyEqual(Start.X, End.X, CoordinateTolerance))
		{
			return EParadoxPuzzleWireAxis::X;
		}
		if (!FMath::IsNearlyEqual(Start.Y, End.Y, CoordinateTolerance))
		{
			return EParadoxPuzzleWireAxis::Y;
		}
		return EParadoxPuzzleWireAxis::Z;
	}

	void AddTerminalSegment(
		TArray<FParadoxPuzzleWireSegment>& OutSegments,
		const FVector& Start,
		const FVector& End)
	{
		if (!Start.Equals(End, CoordinateTolerance))
		{
			OutSegments.Add({Start, End, GetVectorAxis(Start, End), EParadoxPuzzleWireSegmentKind::EndpointTerminal, 0});
		}
	}

	void BuildSourceTerminal(
		const FParadoxPuzzleWirePort& Port,
		const FVector& RouteStart,
		TArray<FParadoxPuzzleWireSegment>& OutSegments)
	{
		AddTerminalSegment(OutSegments, Port.Position, Port.ClearancePoint);
		FVector TangentSnap = Port.ClearancePoint;
		if (Port.Side == EParadoxPuzzlePortSide::East || Port.Side == EParadoxPuzzlePortSide::West)
		{
			TangentSnap.Y = RouteStart.Y;
		}
		else
		{
			TangentSnap.X = RouteStart.X;
		}
		AddTerminalSegment(OutSegments, Port.ClearancePoint, TangentSnap);
		AddTerminalSegment(OutSegments, TangentSnap, RouteStart);
	}

	void BuildTargetTerminal(
		const FParadoxPuzzleWirePort& Port,
		const FVector& RouteEnd,
		TArray<FParadoxPuzzleWireSegment>& OutSegments)
	{
		FVector TangentSnap = Port.ClearancePoint;
		if (Port.Side == EParadoxPuzzlePortSide::East || Port.Side == EParadoxPuzzlePortSide::West)
		{
			TangentSnap.Y = RouteEnd.Y;
		}
		else
		{
			TangentSnap.X = RouteEnd.X;
		}
		AddTerminalSegment(OutSegments, RouteEnd, TangentSnap);
		AddTerminalSegment(OutSegments, TangentSnap, Port.ClearancePoint);
		AddTerminalSegment(OutSegments, Port.ClearancePoint, Port.Position);
	}

	bool IsBridgeKind(const EParadoxPuzzleWireSegmentKind Kind)
	{
		return Kind == EParadoxPuzzleWireSegmentKind::BridgeHorizontal
			|| Kind == EParadoxPuzzleWireSegmentKind::BridgeVertical;
	}

	void NormalizeSegments(TArray<FParadoxPuzzleWireSegment>& InOutSegments)
	{
		InOutSegments.RemoveAll([](const FParadoxPuzzleWireSegment& Segment)
		{
			return Segment.Start.Equals(Segment.End, CoordinateTolerance);
		});

		for (int32 Index = 1; Index < InOutSegments.Num();)
		{
			FParadoxPuzzleWireSegment& Previous = InOutSegments[Index - 1];
			const FParadoxPuzzleWireSegment& Current = InOutSegments[Index];
			const bool bSameLine = Previous.Axis == Current.Axis
				&& Previous.End.Equals(Current.Start, CoordinateTolerance)
				&& (Previous.Axis == EParadoxPuzzleWireAxis::X
					? FMath::IsNearlyEqual(Previous.Start.Y, Current.Start.Y, CoordinateTolerance)
						&& FMath::IsNearlyEqual(Previous.Start.Z, Current.Start.Z, CoordinateTolerance)
					: Previous.Axis == EParadoxPuzzleWireAxis::Y
						? FMath::IsNearlyEqual(Previous.Start.X, Current.Start.X, CoordinateTolerance)
							&& FMath::IsNearlyEqual(Previous.Start.Z, Current.Start.Z, CoordinateTolerance)
						: FMath::IsNearlyEqual(Previous.Start.X, Current.Start.X, CoordinateTolerance)
							&& FMath::IsNearlyEqual(Previous.Start.Y, Current.Start.Y, CoordinateTolerance));
			const bool bCompatibleSemantics = Previous.Kind == Current.Kind
				&& Previous.Lane == Current.Lane;
			if (!bSameLine || !bCompatibleSemantics)
			{
				++Index;
				continue;
			}

			Previous.End = Current.End;
			InOutSegments.RemoveAt(Index);
			if (Previous.Start.Equals(Previous.End, CoordinateTolerance))
			{
				InOutSegments.RemoveAt(Index - 1);
				Index = FMath::Max(1, Index - 1);
			}
		}
	}

	FRouteCornerCounts CountRouteCorners(const TArray<FParadoxPuzzleWireSegment>& Segments)
	{
		FRouteCornerCounts Counts;
		for (int32 Index = 1; Index < Segments.Num(); ++Index)
		{
			const FParadoxPuzzleWireSegment& Previous = Segments[Index - 1];
			const FParadoxPuzzleWireSegment& Current = Segments[Index];
			if (Previous.Axis == Current.Axis)
			{
				continue;
			}
			++Counts.Rendered;
			if (IsBridgeKind(Previous.Kind) || IsBridgeKind(Current.Kind))
			{
				++Counts.Bridge;
				continue;
			}
			++Counts.Topology;
			if (Previous.Kind == EParadoxPuzzleWireSegmentKind::EndpointTerminal
				|| Current.Kind == EParadoxPuzzleWireSegmentKind::EndpointTerminal)
			{
				++Counts.Terminal;
			}
		}
		return Counts;
	}

	bool SegmentEntersBounds(
		const FVector& Start,
		const FVector& End,
		const FParadoxPuzzleWireEndpointBounds& Bounds)
	{
		if (!Bounds.bValid)
		{
			return false;
		}
		const double MinX = Bounds.Min.X + CoordinateTolerance;
		const double MaxX = Bounds.Max.X - CoordinateTolerance;
		const double MinY = Bounds.Min.Y + CoordinateTolerance;
		const double MaxY = Bounds.Max.Y - CoordinateTolerance;
		const double MinZ = Bounds.Min.Z + CoordinateTolerance;
		const double MaxZ = Bounds.Max.Z - CoordinateTolerance;
		const EParadoxPuzzleWireAxis Axis = GetVectorAxis(Start, End);
		if (Axis == EParadoxPuzzleWireAxis::X)
		{
			return Start.Y > MinY && Start.Y < MaxY && Start.Z > MinZ && Start.Z < MaxZ
				&& FMath::Max(FMath::Min(Start.X, End.X), MinX) < FMath::Min(FMath::Max(Start.X, End.X), MaxX);
		}
		if (Axis == EParadoxPuzzleWireAxis::Y)
		{
			return Start.X > MinX && Start.X < MaxX && Start.Z > MinZ && Start.Z < MaxZ
				&& FMath::Max(FMath::Min(Start.Y, End.Y), MinY) < FMath::Min(FMath::Max(Start.Y, End.Y), MaxY);
		}
		return Start.X > MinX && Start.X < MaxX && Start.Y > MinY && Start.Y < MaxY
			&& FMath::Max(FMath::Min(Start.Z, End.Z), MinZ) < FMath::Min(FMath::Max(Start.Z, End.Z), MaxZ);
	}

	bool CandidateAvoidsEndpointInteriors(
		const TArray<FParadoxPuzzleRoutingCoord>& Points,
		const FParadoxPuzzleRoutingLink& Link,
		const FParadoxPuzzleRoutingSettings& Settings,
		const TArray<FParadoxPuzzleWireEndpointBounds>& NetworkObstacles,
		const bool bRejectNetworkObstacles)
	{
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector Start = ToLocalPoint(Points[Index - 1], Settings);
			const FVector End = ToLocalPoint(Points[Index], Settings);
			if (SegmentEntersBounds(Start, End, Link.SourceBounds)
				|| SegmentEntersBounds(Start, End, Link.TargetBounds))
			{
				return false;
			}
			if (bRejectNetworkObstacles)
			{
				for (const FParadoxPuzzleWireEndpointBounds& Obstacle : NetworkObstacles)
				{
					if (SegmentEntersBounds(Start, End, Obstacle))
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	void BuildRoutePoints(
		const TArray<FParadoxPuzzleWireSegment>& Segments,
		TArray<FVector>& OutPoints)
	{
		OutPoints.Reset();
		if (Segments.IsEmpty())
		{
			return;
		}
		OutPoints.Add(Segments[0].Start);
		for (const FParadoxPuzzleWireSegment& Segment : Segments)
		{
			if (!OutPoints.Last().Equals(Segment.End, CoordinateTolerance))
			{
				OutPoints.Add(Segment.End);
			}
		}

		for (int32 Index = OutPoints.Num() - 2; Index > 0; --Index)
		{
			const FVector Incoming = OutPoints[Index] - OutPoints[Index - 1];
			const FVector Outgoing = OutPoints[Index + 1] - OutPoints[Index];
			if (FVector::CrossProduct(Incoming, Outgoing).IsNearlyZero(CoordinateTolerance))
			{
				OutPoints.RemoveAt(Index);
			}
		}
	}

	TArray<FCandidateSpec> BuildCandidateSpecs(
		const FParadoxPuzzleRoutingLink& Link,
		const FParadoxPuzzleWirePort& SourcePort,
		const FParadoxPuzzleWirePort& TargetPort,
		const FParadoxPuzzleRoutingSettings& Settings,
		const TArray<FParadoxPuzzleWireEndpointBounds>& NetworkObstacles,
		const bool bRejectNetworkObstacles)
	{
		TArray<FCandidateSpec> Specs;
		const TArray<FParadoxPuzzleRoutingCoord> SourceCoords = GetClearanceLatticeCoords(SourcePort, Settings);
		const TArray<FParadoxPuzzleRoutingCoord> TargetCoords = GetClearanceLatticeCoords(TargetPort, Settings);
		const int32 CandidateLimit = FMath::Max(1, Settings.MaxCandidatesPerLink);

		const auto GatherPhase = [&](const bool bDetoursOnly)
		{
			int32 GeneratedCount = 0;
			for (const FParadoxPuzzleRoutingCoord& SourceCoord : SourceCoords)
			{
				for (const FParadoxPuzzleRoutingCoord& TargetCoord : TargetCoords)
				{
					if (GeneratedCount >= CandidateLimit || Specs.Num() >= CandidateLimit)
					{
						return;
					}
					FParadoxPuzzleRoutingSettings PairSettings = Settings;
					PairSettings.MaxCandidatesPerLink = CandidateLimit - GeneratedCount;
					const TArray<TArray<FParadoxPuzzleRoutingCoord>> Candidates = BuildCandidates(
						SourceCoord,
						TargetCoord,
						PairSettings,
						bDetoursOnly);
					for (const TArray<FParadoxPuzzleRoutingCoord>& Candidate : Candidates)
					{
						++GeneratedCount;
						if (CandidateAvoidsEndpointInteriors(
							Candidate,
							Link,
							Settings,
							NetworkObstacles,
							bRejectNetworkObstacles))
						{
							FCandidateSpec& Spec = Specs.AddDefaulted_GetRef();
							Spec.SourcePort = SourcePort;
							Spec.TargetPort = TargetPort;
							Spec.Points = Candidate;
							Spec.StableOrder = Specs.Num() - 1;
						}
						if (GeneratedCount >= CandidateLimit || Specs.Num() >= CandidateLimit)
						{
							return;
						}
					}
				}
			}
		};

		GatherPhase(false);
		if (Specs.IsEmpty())
		{
			GatherPhase(true);
		}
		return Specs;
	}
}

FParadoxPuzzleRoutingResult UE::Paradox::PuzzleOverlay::Private::CalculateLegacyIndependentRoutes(
	const FParadoxPuzzleRoutingSnapshot& Snapshot)
{
	if (IsRoutingCancellationRequested())
	{
		return MakeCancelledRoutingResult(Snapshot);
	}
	using namespace ParadoxPuzzleWireRouter;
	const double StartSeconds = FPlatformTime::Seconds();

	FParadoxPuzzleRoutingResult Result;
	Result.RoutingGeneration = Snapshot.RoutingGeneration;
	Result.Diagnostics.Algorithm = EParadoxPuzzleRoutingAlgorithm::LegacyIndependent;

	FParadoxPuzzleRoutingSnapshot SanitizedSnapshot = Snapshot;
	SanitizedSnapshot.Settings.PitchX = FMath::Max(1.0, Snapshot.Settings.PitchX);
	SanitizedSnapshot.Settings.PitchY = FMath::Max(1.0, Snapshot.Settings.PitchY);
	SanitizedSnapshot.Settings.MaxCandidatesPerLink = FMath::Clamp(Snapshot.Settings.MaxCandidatesPerLink, 1, 64);
	SanitizedSnapshot.Settings.MaxRerouteAttempts = FMath::Clamp(Snapshot.Settings.MaxRerouteAttempts, 0, 16);
	SanitizedSnapshot.Settings.MaxLanesPerEdge = FMath::Clamp(Snapshot.Settings.MaxLanesPerEdge, 1, 31);
	SanitizedSnapshot.Settings.EndpointClearance = FMath::Max(0.0, Snapshot.Settings.EndpointClearance);
	SanitizedSnapshot.Settings.MultiPortFanoutLength = FMath::Max(0.0, Snapshot.Settings.MultiPortFanoutLength);
	SanitizedSnapshot.Settings.PortSpacing = FMath::Max(0.0, Snapshot.Settings.PortSpacing);
	SanitizedSnapshot.Settings.PortEdgeInset = FMath::Max(0.0, Snapshot.Settings.PortEdgeInset);

	TArray<FParadoxPuzzleRoutingLink> OrderedLinks = SanitizedSnapshot.Links;
	OrderedLinks.Sort([](const FParadoxPuzzleRoutingLink& A, const FParadoxPuzzleRoutingLink& B)
	{
		if (A.StableOrder != B.StableOrder)
		{
			return A.StableOrder < B.StableOrder;
		}
		if (A.Direction != B.Direction)
		{
			return static_cast<uint8>(A.Direction) < static_cast<uint8>(B.Direction);
		}
		if (A.LinkKind != B.LinkKind)
		{
			return static_cast<uint8>(A.LinkKind) < static_cast<uint8>(B.LinkKind);
		}
		return A.RemoteEndpointKey < B.RemoteEndpointKey;
	});
	TMap<FUnitEdge, uint32> OccupiedLanes;
	TArray<FReservedHorizontalSegment> ReservedSegments;
	Result.Routes = SanitizedSnapshot.PreservedRoutes;
	Result.Routes.Sort([](const FParadoxPuzzleWireRoute& A, const FParadoxPuzzleWireRoute& B)
	{
		return A.StableOrder < B.StableOrder;
	});
	for (FParadoxPuzzleWireRoute& PreservedRoute : Result.Routes)
	{
		PreservedRoute.RoutingGeneration = Snapshot.RoutingGeneration;
		ReserveRoute(
			PreservedRoute.Segments,
			SanitizedSnapshot.Settings,
			OccupiedLanes,
			ReservedSegments);
	}
	AssignEndpointPorts(OrderedLinks, SanitizedSnapshot.Settings);

	for (const FParadoxPuzzleRoutingLink& Link : OrderedLinks)
	{
		if (IsRoutingCancellationRequested())
		{
			return MakeCancelledRoutingResult(Snapshot);
		}
		FCandidateEvaluation BestEvaluation;
		bool bHasBest = false;
		int32 EvaluatedCandidateCount = 0;
		const TArray<FParadoxPuzzleWireEndpointBounds> NetworkObstacles = GatherNetworkObstacles(
			OrderedLinks,
			Link);
		const auto EvaluatePortPair = [&SanitizedSnapshot, &Link, &OccupiedLanes, &ReservedSegments,
			&BestEvaluation, &bHasBest, &EvaluatedCandidateCount, &Result, &NetworkObstacles](
				const FParadoxPuzzleWirePort& SourcePort,
				const FParadoxPuzzleWirePort& TargetPort,
				const bool bRejectNetworkObstacles)
		{
			const TArray<FCandidateSpec> Specs = BuildCandidateSpecs(
				Link,
				SourcePort,
				TargetPort,
				SanitizedSnapshot.Settings,
				NetworkObstacles,
				bRejectNetworkObstacles);
			Result.Diagnostics.CandidateCount += Specs.Num();
			if (Specs.IsEmpty())
			{
				return false;
			}

			const TMap<FUnitEdge, uint32> EmptyOccupiedLanes;
			const TArray<FReservedHorizontalSegment> EmptyReservedSegments;
			TArray<int32> MinimumCornerSpecs;
			int32 MinimumCornerCount = MAX_int32;
			for (int32 SpecIndex = 0; SpecIndex < Specs.Num(); ++SpecIndex)
			{
				const FCandidateSpec& Spec = Specs[SpecIndex];
				const FCandidateEvaluation Baseline = EvaluateCandidate(
					SanitizedSnapshot,
					Spec.Points,
					Spec.SourcePort,
					Spec.TargetPort,
					EmptyOccupiedLanes,
					EmptyReservedSegments,
					NetworkObstacles,
					bRejectNetworkObstacles);
				if (!Baseline.bValid)
				{
					Result.Diagnostics.RejectedEndpointInteriorCandidateCount +=
						Baseline.bRejectedEndpointInterior ? 1 : 0;
					Result.Diagnostics.RejectedNetworkBoundsCandidateCount +=
						Baseline.bRejectedNetworkBounds ? 1 : 0;
					continue;
				}
				if (Baseline.TopologyCornerCount < MinimumCornerCount)
				{
					MinimumCornerCount = Baseline.TopologyCornerCount;
					MinimumCornerSpecs.Reset();
					MinimumCornerSpecs.Add(SpecIndex);
				}
				else if (Baseline.TopologyCornerCount == MinimumCornerCount)
				{
					MinimumCornerSpecs.Add(SpecIndex);
				}
			}
			if (MinimumCornerSpecs.IsEmpty())
			{
				return false;
			}

			FCandidateEvaluation PairBest;
			bool bHasPairBest = false;
			for (const int32 SpecIndex : MinimumCornerSpecs)
			{
				const FCandidateSpec& Spec = Specs[SpecIndex];
				FCandidateEvaluation Evaluation = EvaluateCandidate(
					SanitizedSnapshot,
					Spec.Points,
					Spec.SourcePort,
					Spec.TargetPort,
					OccupiedLanes,
					ReservedSegments,
					NetworkObstacles,
					bRejectNetworkObstacles);
				++EvaluatedCandidateCount;
				if (!Evaluation.bValid)
				{
					Result.Diagnostics.RejectedEndpointInteriorCandidateCount +=
						Evaluation.bRejectedEndpointInterior ? 1 : 0;
					Result.Diagnostics.RejectedNetworkBoundsCandidateCount +=
						Evaluation.bRejectedNetworkBounds ? 1 : 0;
					continue;
				}
				if (!bHasPairBest || IsBetterEvaluation(Evaluation, PairBest))
				{
					PairBest = MoveTemp(Evaluation);
					bHasPairBest = true;
				}
			}
			if (bHasPairBest)
			{
				BestEvaluation = MoveTemp(PairBest);
				bHasBest = true;
			}
			return bHasPairBest;
		};

		struct FAlternatePortPair
		{
			FParadoxPuzzleWirePort SourcePort;
			FParadoxPuzzleWirePort TargetPort;
			double Distance = 0.0;
		};
		TArray<FAlternatePortPair> AlternatePortPairs;
		if (Link.SourceBounds.bValid && Link.TargetBounds.bValid)
		{
			const FVector SourceCenter = Link.SourceBounds.GetCenter();
			const FVector TargetCenter = Link.TargetBounds.GetCenter();
			const EParadoxPuzzlePortSide Sides[] = {
				EParadoxPuzzlePortSide::North,
				EParadoxPuzzlePortSide::South,
				EParadoxPuzzlePortSide::East,
				EParadoxPuzzlePortSide::West};
			for (const EParadoxPuzzlePortSide SourceSide : Sides)
			{
				for (const EParadoxPuzzlePortSide TargetSide : Sides)
				{
					if (SourceSide == Link.SourcePort.Side && TargetSide == Link.TargetPort.Side)
					{
						continue;
					}
					const bool bSourceXAxisNormal = SourceSide == EParadoxPuzzlePortSide::East
						|| SourceSide == EParadoxPuzzlePortSide::West;
					const bool bTargetXAxisNormal = TargetSide == EParadoxPuzzlePortSide::East
						|| TargetSide == EParadoxPuzzlePortSide::West;
					FAlternatePortPair& Pair = AlternatePortPairs.AddDefaulted_GetRef();
					Pair.SourcePort = MakePort(
						Link.SourceBounds,
						Link.Source,
						TargetCenter,
						SourceSide,
						SanitizedSnapshot.Settings,
						bSourceXAxisNormal ? SourceCenter.Y : SourceCenter.X,
						0);
					Pair.TargetPort = MakePort(
						Link.TargetBounds,
						Link.Target,
						SourceCenter,
						TargetSide,
						SanitizedSnapshot.Settings,
						bTargetXAxisNormal ? TargetCenter.Y : TargetCenter.X,
						0);
					Pair.Distance = FVector::Distance(Pair.SourcePort.Position, Pair.TargetPort.Position);
				}
			}
			AlternatePortPairs.Sort([](const FAlternatePortPair& A, const FAlternatePortPair& B)
			{
				if (!FMath::IsNearlyEqual(A.Distance, B.Distance, CoordinateTolerance))
				{
					return A.Distance < B.Distance;
				}
				if (A.SourcePort.Side != B.SourcePort.Side)
				{
					return static_cast<uint8>(A.SourcePort.Side) < static_cast<uint8>(B.SourcePort.Side);
				}
				return static_cast<uint8>(A.TargetPort.Side) < static_cast<uint8>(B.TargetPort.Side);
			});
		}

		const auto EvaluateFacePairs = [&]()
		{
			if (EvaluatePortPair(Link.SourcePort, Link.TargetPort, true))
			{
				return true;
			}
			for (const FAlternatePortPair& Pair : AlternatePortPairs)
			{
				if (EvaluatePortPair(Pair.SourcePort, Pair.TargetPort, true))
				{
					return true;
				}
			}
			return false;
		};
		if (!EvaluateFacePairs())
		{
			++Result.Diagnostics.NetworkBoundsFallbackCount;
			if (!EvaluatePortPair(Link.SourcePort, Link.TargetPort, false))
			{
				for (const FAlternatePortPair& Pair : AlternatePortPairs)
				{
					if (EvaluatePortPair(Pair.SourcePort, Pair.TargetPort, false))
					{
						break;
					}
				}
			}
		}

		if (!bHasBest)
		{
			continue;
		}

		Result.Diagnostics.CrossingCount += BestEvaluation.Crossings;
		Result.Diagnostics.RerouteAttempts += FMath::Max(0, EvaluatedCandidateCount - 1);
		Result.Diagnostics.SurfaceCacheHits += BestEvaluation.SurfaceHits;
		Result.Diagnostics.SurfaceCacheMisses += BestEvaluation.SurfaceMisses;
		if (BestEvaluation.Crossings > 0)
		{
			AddBridgeFallback(
				BestEvaluation.Segments,
				SanitizedSnapshot.Settings,
				BestEvaluation.FirstCrossingPoint,
				Result.Diagnostics.BridgeCount);
		}
		const FRouteCornerCounts FinalCorners = CountRouteCorners(BestEvaluation.Segments);

		FParadoxPuzzleWireRoute& Route = Result.Routes.AddDefaulted_GetRef();
		Route.LinkHandle = Link.LinkHandle;
		Route.Direction = Link.Direction;
		Route.LinkKind = Link.LinkKind;
		Route.SourcePort = BestEvaluation.SourcePort;
		Route.TargetPort = BestEvaluation.TargetPort;
		Route.Segments = MoveTemp(BestEvaluation.Segments);
		Route.TopologyCornerCount = FinalCorners.Topology;
		Route.TerminalCornerCount = FinalCorners.Terminal;
		Route.BridgeCornerCount = FinalCorners.Bridge;
		Route.RenderedCornerCount = FinalCorners.Rendered;
		Route.RoutingGeneration = Snapshot.RoutingGeneration;
		Route.bActive = Link.bActive;
		Route.bSignalValid = Link.bSignalValid;
		Route.GateMode = Link.GateMode;
		Route.bGateValid = Link.bGateValid;
		Route.bGateAllowsSignal = Link.bGateAllowsSignal;
		Route.bControllerResultValid = Link.bControllerResultValid;
		Route.bControllerResultActive = Link.bControllerResultActive;
		Route.StableOrder = Link.StableOrder;
		BuildRoutePoints(Route.Segments, Route.RoutePoints);

		ReserveRoute(Route.Segments, SanitizedSnapshot.Settings, OccupiedLanes, ReservedSegments);
	}
	Result.Routes.Sort([](const FParadoxPuzzleWireRoute& A, const FParadoxPuzzleWireRoute& B)
	{
		return A.StableOrder < B.StableOrder;
	});
	for (const FParadoxPuzzleWireRoute& Route : Result.Routes)
	{
		Result.Diagnostics.TotalTopologyCornerCount += Route.TopologyCornerCount;
		Result.Diagnostics.TotalTerminalCornerCount += Route.TerminalCornerCount;
		Result.Diagnostics.TotalBridgeCornerCount += Route.BridgeCornerCount;
		Result.Diagnostics.TotalRenderedCornerCount += Route.RenderedCornerCount;
		Result.Diagnostics.MaxRenderedCornerCount = FMath::Max(
			Result.Diagnostics.MaxRenderedCornerCount,
			Route.RenderedCornerCount);
	}

	Result.Diagnostics.RoutingMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	return Result;
}

FParadoxPuzzleRoutingResult FParadoxPuzzleWireRouter::CalculateRoutes(
	const FParadoxPuzzleRoutingSnapshot& Snapshot)
{
	if (UE::Paradox::PuzzleOverlay::Private::IsRoutingCancellationRequested())
	{
		return UE::Paradox::PuzzleOverlay::Private::MakeCancelledRoutingResult(Snapshot);
	}
	// Keep one stable public entry point while every strategy owns its cost model and working state.
	switch (Snapshot.Settings.Algorithm)
	{
	case EParadoxPuzzleRoutingAlgorithm::LegacyIndependent:
		return UE::Paradox::PuzzleOverlay::Private::CalculateLegacyIndependentRoutes(Snapshot);
	case EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive:
		return UE::Paradox::PuzzleOverlay::Private::CalculateDistributedRepulsiveRoutes(Snapshot);
	case EParadoxPuzzleRoutingAlgorithm::OrderedBundles:
	default:
		return UE::Paradox::PuzzleOverlay::Private::CalculateOrderedBundleRoutes(Snapshot);
	}
}
