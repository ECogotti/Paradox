#include "PuzzleOverlay/ParadoxPuzzleWireRouterInternal.h"

#include "HAL/PlatformTime.h"

namespace UE::Paradox::PuzzleOverlay::Private
{
namespace OrderedBundles
{
	constexpr double CoordinateTolerance = 0.01;
	constexpr double HeightBucketSize = 10.0;

	struct FNodeKey
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Z = 0;

		bool operator==(const FNodeKey& Other) const
		{
			return X == Other.X && Y == Other.Y && Z == Other.Z;
		}

		bool operator<(const FNodeKey& Other) const
		{
			return Z != Other.Z ? Z < Other.Z : (Y != Other.Y ? Y < Other.Y : X < Other.X);
		}
	};

	uint32 GetTypeHash(const FNodeKey& Node)
	{
		return HashCombineFast(HashCombineFast(::GetTypeHash(Node.X), ::GetTypeHash(Node.Y)), ::GetTypeHash(Node.Z));
	}

	struct FEdgeKey
	{
		FNodeKey A;
		FNodeKey B;

		bool operator==(const FEdgeKey& Other) const
		{
			return A == Other.A && B == Other.B;
		}

		bool operator<(const FEdgeKey& Other) const
		{
			return A == Other.A ? B < Other.B : A < Other.A;
		}
	};

	uint32 GetTypeHash(const FEdgeKey& Edge)
	{
		return HashCombineFast(GetTypeHash(Edge.A), GetTypeHash(Edge.B));
	}

	struct FDirectedEdge
	{
		FEdgeKey Key;
		FNodeKey StartNode;
		FNodeKey EndNode;
		FParadoxPuzzleRoutingCoord Start;
		FParadoxPuzzleRoutingCoord End;
		EParadoxPuzzleWireAxis Axis = EParadoxPuzzleWireAxis::X;
		bool bForward = true;
	};

	struct FBundleEdgeKey
	{
		FEdgeKey Edge;
		bool bForward = true;

		bool operator==(const FBundleEdgeKey& Other) const
		{
			return Edge == Other.Edge && bForward == Other.bForward;
		}
	};

	uint32 GetTypeHash(const FBundleEdgeKey& Key)
	{
		return HashCombineFast(GetTypeHash(Key.Edge), ::GetTypeHash(Key.bForward));
	}

	struct FCandidate
	{
		FParadoxPuzzleWirePort SourcePort;
		FParadoxPuzzleWirePort TargetPort;
		TArray<FParadoxPuzzleRoutingCoord> Points;
		TArray<FDirectedEdge> Edges;
		TArray<FVector> ProvisionalPolyline;
		TArray<FIntPoint> SpatialCells;
		FBox LocalBounds = FBox(ForceInit);
		double BaseCost = TNumericLimits<double>::Max();
		double Length = 0.0;
		double VerticalLength = 0.0;
		double UnsupportedLength = 0.0;
		int32 BendCount = 0;
		int32 FacePairIndex = INDEX_NONE;
		int32 StableOrder = INDEX_NONE;
		bool bDetour = false;
	};

	struct FBundleWork
	{
		int32 BundleId = INDEX_NONE;
		TArray<FDirectedEdge> Edges;
		TArray<int32> Members;
		TArray<int32> OrderedMembers;
		int32 InversionsBefore = 0;
		int32 InversionsAfter = 0;
	};

	struct FPortRequest
	{
		int32 LinkIndex = INDEX_NONE;
		bool bSource = false;
		double RemoteTangent = 0.0;
		int32 BundleId = INDEX_NONE;
		int32 BundleLane = INDEX_NONE;
		int32 StableOrder = INDEX_NONE;
	};

	int32 ToHeightBucket(const double Height)
	{
		return FMath::RoundToInt(Height / HeightBucketSize);
	}

	FNodeKey ToNodeKey(const FParadoxPuzzleRoutingCoord& Coord)
	{
		return {Coord.X, Coord.Y, ToHeightBucket(Coord.Z)};
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

	EParadoxPuzzleWireAxis GetVectorAxis(const FVector& A, const FVector& B)
	{
		if (!FMath::IsNearlyEqual(A.X, B.X, CoordinateTolerance))
		{
			return EParadoxPuzzleWireAxis::X;
		}
		if (!FMath::IsNearlyEqual(A.Y, B.Y, CoordinateTolerance))
		{
			return EParadoxPuzzleWireAxis::Y;
		}
		return EParadoxPuzzleWireAxis::Z;
	}

	FVector GetPortNormal(const EParadoxPuzzlePortSide Side)
	{
		switch (Side)
		{
		case EParadoxPuzzlePortSide::North: return FVector(0.0, 1.0, 0.0);
		case EParadoxPuzzlePortSide::South: return FVector(0.0, -1.0, 0.0);
		case EParadoxPuzzlePortSide::West: return FVector(-1.0, 0.0, 0.0);
		case EParadoxPuzzlePortSide::PositiveZ: return FVector(0.0, 0.0, 1.0);
		case EParadoxPuzzlePortSide::NegativeZ: return FVector(0.0, 0.0, -1.0);
		default: return FVector(1.0, 0.0, 0.0);
		}
	}

	FVector GetEndpointCenter(
		const FParadoxPuzzleWireEndpointBounds& Bounds,
		const FParadoxPuzzleRoutingCoord& Fallback,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		return Bounds.bValid ? Bounds.GetCenter() : ToLocalPoint(Fallback, Settings);
	}

	FParadoxPuzzleWirePort MakePort(
		const FParadoxPuzzleWireEndpointBounds& Bounds,
		const FParadoxPuzzleRoutingCoord& Fallback,
		const EParadoxPuzzlePortSide Side,
		const FParadoxPuzzleRoutingSettings& Settings,
		const TOptional<double>& Tangent = TOptional<double>(),
		const EParadoxPuzzleWireAxis TangentAxis = EParadoxPuzzleWireAxis::X,
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
		Port.Position = Bounds.bValid ? Bounds.GetCenter() : FallbackPoint;
		Port.bValid = Bounds.bValid;
		if (Bounds.bValid)
		{
			switch (Side)
			{
			case EParadoxPuzzlePortSide::North: Port.Position.Y = Bounds.Max.Y; break;
			case EParadoxPuzzlePortSide::South: Port.Position.Y = Bounds.Min.Y; break;
			case EParadoxPuzzlePortSide::East: Port.Position.X = Bounds.Max.X; break;
			case EParadoxPuzzlePortSide::West: Port.Position.X = Bounds.Min.X; break;
			case EParadoxPuzzlePortSide::PositiveZ: Port.Position.Z = Bounds.Max.Z; break;
			case EParadoxPuzzlePortSide::NegativeZ: Port.Position.Z = Bounds.Min.Z; break;
			}

			if (Side != EParadoxPuzzlePortSide::PositiveZ && Side != EParadoxPuzzlePortSide::NegativeZ)
			{
				Port.Position.Z = FMath::Clamp(FallbackPoint.Z, Bounds.Min.Z, Bounds.Max.Z);
			}
			if (Tangent.IsSet())
			{
				const double Min = TangentAxis == EParadoxPuzzleWireAxis::X ? Bounds.Min.X : Bounds.Min.Y;
				const double Max = TangentAxis == EParadoxPuzzleWireAxis::X ? Bounds.Max.X : Bounds.Max.Y;
				const double Value = FMath::Clamp(Tangent.GetValue(), Min, Max);
				if (TangentAxis == EParadoxPuzzleWireAxis::X)
				{
					Port.Position.X = Value;
				}
				else
				{
					Port.Position.Y = Value;
				}
				const double HalfSpan = FMath::Max(0.0, Max - Min) * 0.5;
				Port.NormalizedDistanceFromFaceCenter = HalfSpan > CoordinateTolerance
					? FMath::Clamp(FMath::Abs(Value - (Min + Max) * 0.5) / HalfSpan, 0.0, 1.0)
					: 0.0;
			}
		}
		const double Fanout = Port.FaceSlotCount > 1 ? Settings.MultiPortFanoutLength : 0.0;
		Port.ClearancePoint = Port.Position + Port.Normal * (Settings.EndpointClearance + Fanout);
		return Port;
	}

	FParadoxPuzzleRoutingCoord GetClearanceCoord(
		const FParadoxPuzzleWirePort& Port,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		FParadoxPuzzleRoutingCoord Coord;
		Coord.X = FMath::RoundToInt(Port.ClearancePoint.X / Settings.PitchX);
		Coord.Y = FMath::RoundToInt(Port.ClearancePoint.Y / Settings.PitchY);
		Coord.Z = Port.ClearancePoint.Z;
		switch (Port.Side)
		{
		case EParadoxPuzzlePortSide::East:
			Coord.X = FMath::CeilToInt(Port.ClearancePoint.X / Settings.PitchX);
			break;
		case EParadoxPuzzlePortSide::West:
			Coord.X = FMath::FloorToInt(Port.ClearancePoint.X / Settings.PitchX);
			break;
		case EParadoxPuzzlePortSide::North:
			Coord.Y = FMath::CeilToInt(Port.ClearancePoint.Y / Settings.PitchY);
			break;
		case EParadoxPuzzlePortSide::South:
			Coord.Y = FMath::FloorToInt(Port.ClearancePoint.Y / Settings.PitchY);
			break;
		default:
			break;
		}
		return Coord;
	}

	void AddPoint(TArray<FVector>& Points, const FVector& Point)
	{
		if (Points.IsEmpty() || !Points.Last().Equals(Point, CoordinateTolerance))
		{
			Points.Add(Point);
		}
	}

	void AddAxisMove(TArray<FVector>& Points, FVector& Cursor, const FVector& Target, const EParadoxPuzzleWireAxis Axis)
	{
		FVector Next = Cursor;
		switch (Axis)
		{
		case EParadoxPuzzleWireAxis::X: Next.X = Target.X; break;
		case EParadoxPuzzleWireAxis::Y: Next.Y = Target.Y; break;
		default: Next.Z = Target.Z; break;
		}
		AddPoint(Points, Next);
		Cursor = Next;
	}

	TArray<FVector> BuildOutwardTerminal(
		const FParadoxPuzzleWirePort& Port,
		const FVector& MainPoint)
	{
		TArray<FVector> Points;
		AddPoint(Points, Port.Position);
		AddPoint(Points, Port.ClearancePoint);
		FVector Cursor = Port.ClearancePoint;
		const EParadoxPuzzleWireAxis NormalAxis = GetVectorAxis(Port.Position, Port.ClearancePoint);
		for (const EParadoxPuzzleWireAxis Axis : {EParadoxPuzzleWireAxis::X, EParadoxPuzzleWireAxis::Y, EParadoxPuzzleWireAxis::Z})
		{
			if (Axis != NormalAxis)
			{
				AddAxisMove(Points, Cursor, MainPoint, Axis);
			}
		}
		AddAxisMove(Points, Cursor, MainPoint, NormalAxis);
		return Points;
	}

	void CompactPolyline(TArray<FVector>& Points)
	{
		for (int32 Index = Points.Num() - 1; Index > 0; --Index)
		{
			if (Points[Index].Equals(Points[Index - 1], CoordinateTolerance))
			{
				Points.RemoveAt(Index);
			}
		}
		for (int32 Index = Points.Num() - 2; Index > 0; --Index)
		{
			const FVector A = Points[Index] - Points[Index - 1];
			const FVector B = Points[Index + 1] - Points[Index];
			if (FVector::CrossProduct(A, B).IsNearlyZero(CoordinateTolerance)
				&& FVector::DotProduct(A, B) >= 0.0)
			{
				Points.RemoveAt(Index);
			}
		}
	}

	TArray<FVector> BuildCompletePolyline(
		const FParadoxPuzzleWirePort& SourcePort,
		const FParadoxPuzzleWirePort& TargetPort,
		const TArray<FParadoxPuzzleRoutingCoord>& MainPoints,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		TArray<FVector> Result;
		if (MainPoints.IsEmpty())
		{
			return Result;
		}
		const FVector MainStart = ToLocalPoint(MainPoints[0], Settings);
		const FVector MainEnd = ToLocalPoint(MainPoints.Last(), Settings);
		Result = BuildOutwardTerminal(SourcePort, MainStart);
		for (int32 Index = 1; Index < MainPoints.Num(); ++Index)
		{
			AddPoint(Result, ToLocalPoint(MainPoints[Index], Settings));
		}
		TArray<FVector> TargetTerminal = BuildOutwardTerminal(TargetPort, MainEnd);
		Algo::Reverse(TargetTerminal);
		for (int32 Index = 1; Index < TargetTerminal.Num(); ++Index)
		{
			AddPoint(Result, TargetTerminal[Index]);
		}
		CompactPolyline(Result);
		return Result;
	}

	bool SegmentEntersBounds(const FVector& Start, const FVector& End, const FParadoxPuzzleWireEndpointBounds& Bounds)
	{
		if (!Bounds.bValid)
		{
			return false;
		}
		const FVector Min = Bounds.Min + FVector(CoordinateTolerance);
		const FVector Max = Bounds.Max - FVector(CoordinateTolerance);
		const EParadoxPuzzleWireAxis Axis = GetVectorAxis(Start, End);
		if (Axis == EParadoxPuzzleWireAxis::X)
		{
			return Start.Y > Min.Y && Start.Y < Max.Y && Start.Z > Min.Z && Start.Z < Max.Z
				&& FMath::Max(FMath::Min(Start.X, End.X), Min.X) < FMath::Min(FMath::Max(Start.X, End.X), Max.X);
		}
		if (Axis == EParadoxPuzzleWireAxis::Y)
		{
			return Start.X > Min.X && Start.X < Max.X && Start.Z > Min.Z && Start.Z < Max.Z
				&& FMath::Max(FMath::Min(Start.Y, End.Y), Min.Y) < FMath::Min(FMath::Max(Start.Y, End.Y), Max.Y);
		}
		return Start.X > Min.X && Start.X < Max.X && Start.Y > Min.Y && Start.Y < Max.Y
			&& FMath::Max(FMath::Min(Start.Z, End.Z), Min.Z) < FMath::Min(FMath::Max(Start.Z, End.Z), Max.Z);
	}

	bool AvoidsOwnEndpointInteriors(
		const TArray<FVector>& Polyline,
		const FParadoxPuzzleRoutingLink& Link)
	{
		for (int32 Index = 1; Index < Polyline.Num(); ++Index)
		{
			if (SegmentEntersBounds(Polyline[Index - 1], Polyline[Index], Link.SourceBounds)
				|| SegmentEntersBounds(Polyline[Index - 1], Polyline[Index], Link.TargetBounds))
			{
				return false;
			}
		}
		return true;
	}

	void CompactCoords(TArray<FParadoxPuzzleRoutingCoord>& Points)
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

	void AddCoordCandidate(
		TArray<TPair<TArray<FParadoxPuzzleRoutingCoord>, bool>>& OutCandidates,
		TArray<FParadoxPuzzleRoutingCoord> Candidate,
		const bool bDetour)
	{
		CompactCoords(Candidate);
		if (Candidate.IsEmpty())
		{
			return;
		}
		for (const TPair<TArray<FParadoxPuzzleRoutingCoord>, bool>& Existing : OutCandidates)
		{
			if (Existing.Key.Num() != Candidate.Num())
			{
				continue;
			}
			bool bSame = true;
			for (int32 Index = 0; Index < Candidate.Num(); ++Index)
			{
				bSame &= IsSameCoord(Existing.Key[Index], Candidate[Index]);
			}
			if (bSame)
			{
				return;
			}
		}
		OutCandidates.Emplace(MoveTemp(Candidate), bDetour);
	}

	TArray<TPair<TArray<FParadoxPuzzleRoutingCoord>, bool>> BuildCoordinateCandidates(
		const FParadoxPuzzleRoutingCoord& Start,
		const FParadoxPuzzleRoutingCoord& End,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		TArray<TPair<TArray<FParadoxPuzzleRoutingCoord>, bool>> Result;
		const auto AddPermutation = [&Result, &Start, &End](const int32 A, const int32 B, const int32 C)
		{
			TArray<FParadoxPuzzleRoutingCoord> Points{Start};
			FParadoxPuzzleRoutingCoord Cursor = Start;
			for (const int32 Axis : {A, B, C})
			{
				if (Axis == 0) Cursor.X = End.X;
				else if (Axis == 1) Cursor.Y = End.Y;
				else Cursor.Z = End.Z;
				Points.Add(Cursor);
			}
			AddCoordCandidate(Result, MoveTemp(Points), false);
		};
		AddPermutation(0, 1, 2);
		AddPermutation(1, 0, 2);
		AddPermutation(0, 2, 1);
		AddPermutation(1, 2, 0);
		AddPermutation(2, 0, 1);
		AddPermutation(2, 1, 0);

		for (int32 Offset = 1; Offset <= Settings.MaxRerouteAttempts; ++Offset)
		{
			for (const int32 Sign : {-1, 1})
			{
				const int32 X = Start.X + Offset * Sign;
				AddCoordCandidate(Result,
					{Start, {X, Start.Y, Start.Z}, {X, End.Y, Start.Z}, {End.X, End.Y, Start.Z}, End}, true);
				const int32 Y = Start.Y + Offset * Sign;
				AddCoordCandidate(Result,
					{Start, {Start.X, Y, Start.Z}, {End.X, Y, Start.Z}, {End.X, End.Y, Start.Z}, End}, true);
			}
		}
		return Result;
	}

	void AppendUnitEdges(
		const TArray<FParadoxPuzzleRoutingCoord>& Points,
		TArray<FDirectedEdge>& OutEdges)
	{
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			FParadoxPuzzleRoutingCoord Cursor = Points[Index - 1];
			const FParadoxPuzzleRoutingCoord End = Points[Index];
			const EParadoxPuzzleWireAxis Axis = GetAxis(Cursor, End);
			const int32 StepCount = Axis == EParadoxPuzzleWireAxis::X
				? FMath::Abs(End.X - Cursor.X)
				: (Axis == EParadoxPuzzleWireAxis::Y ? FMath::Abs(End.Y - Cursor.Y) : 1);
			const int32 Count = FMath::Max(1, StepCount);
			for (int32 Step = 0; Step < Count; ++Step)
			{
				FParadoxPuzzleRoutingCoord Next = Cursor;
				if (Axis == EParadoxPuzzleWireAxis::X)
				{
					Next.X += End.X > Cursor.X ? 1 : -1;
				}
				else if (Axis == EParadoxPuzzleWireAxis::Y)
				{
					Next.Y += End.Y > Cursor.Y ? 1 : -1;
				}
				else
				{
					Next.Z = End.Z;
				}
				FDirectedEdge& Edge = OutEdges.AddDefaulted_GetRef();
				Edge.Start = Cursor;
				Edge.End = Next;
				Edge.StartNode = ToNodeKey(Cursor);
				Edge.EndNode = ToNodeKey(Next);
				Edge.Axis = Axis;
				Edge.bForward = !(Edge.EndNode < Edge.StartNode);
				Edge.Key = Edge.bForward
					? FEdgeKey{Edge.StartNode, Edge.EndNode}
					: FEdgeKey{Edge.EndNode, Edge.StartNode};
				Cursor = Next;
			}
		}
	}

	int32 CountBends(const TArray<FVector>& Points)
	{
		int32 Count = 0;
		for (int32 Index = 2; Index < Points.Num(); ++Index)
		{
			if (GetVectorAxis(Points[Index - 2], Points[Index - 1])
				!= GetVectorAxis(Points[Index - 1], Points[Index]))
			{
				++Count;
			}
		}
		return Count;
	}

	bool IsSurfaceSupported(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const FParadoxPuzzleRoutingCoord& Coord)
	{
		const FParadoxPuzzleSurfaceKey Key{Coord.X, Coord.Y, ToHeightBucket(Coord.Z)};
		const FParadoxPuzzleSurfaceSample* Sample = Snapshot.SurfaceSamples.Find(Key);
		return Sample && Sample->bHasSurface;
	}

	void CacheSpatialCells(
		const TArray<FVector>& Polyline,
		const FParadoxPuzzleRoutingSettings& Settings,
		TArray<FIntPoint>& OutCells)
	{
		TSet<FIntPoint> UniqueCells;
		const double PitchX = FMath::Max(1.0, Settings.PitchX);
		const double PitchY = FMath::Max(1.0, Settings.PitchY);
		for (int32 PointIndex = 1; PointIndex < Polyline.Num(); ++PointIndex)
		{
			const FVector& Start = Polyline[PointIndex - 1];
			const FVector& End = Polyline[PointIndex];
			const int32 StartX = FMath::FloorToInt(Start.X / PitchX);
			const int32 EndX = FMath::FloorToInt(End.X / PitchX);
			const int32 StartY = FMath::FloorToInt(Start.Y / PitchY);
			const int32 EndY = FMath::FloorToInt(End.Y / PitchY);
			if (FMath::Abs(Start.X - End.X) > CoordinateTolerance)
			{
				for (int32 X = FMath::Min(StartX, EndX); X <= FMath::Max(StartX, EndX); ++X)
				{
					UniqueCells.Add({X, StartY});
				}
			}
			else if (FMath::Abs(Start.Y - End.Y) > CoordinateTolerance)
			{
				for (int32 Y = FMath::Min(StartY, EndY); Y <= FMath::Max(StartY, EndY); ++Y)
				{
					UniqueCells.Add({StartX, Y});
				}
			}
			else
			{
				UniqueCells.Add({StartX, StartY});
			}
		}
		OutCells = UniqueCells.Array();
		OutCells.Sort([](const FIntPoint& A, const FIntPoint& B)
		{
			return A.X != B.X ? A.X < B.X : A.Y < B.Y;
		});
	}

	double EvaluateBaseCost(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		FCandidate& Candidate)
	{
		Candidate.ProvisionalPolyline = BuildCompletePolyline(
			Candidate.SourcePort,
			Candidate.TargetPort,
			Candidate.Points,
			Snapshot.Settings);
		Candidate.BendCount = CountBends(Candidate.ProvisionalPolyline);
		CacheSpatialCells(Candidate.ProvisionalPolyline, Snapshot.Settings, Candidate.SpatialCells);
		double Cost = Candidate.BendCount * Snapshot.Settings.BendPenalty;
		Candidate.LocalBounds = FBox(ForceInit);
		Candidate.Length = 0.0;
		Candidate.VerticalLength = 0.0;
		Candidate.UnsupportedLength = 0.0;
		for (const FVector& Point : Candidate.ProvisionalPolyline)
		{
			Candidate.LocalBounds += Point;
		}
		for (int32 Index = 1; Index < Candidate.ProvisionalPolyline.Num(); ++Index)
		{
			const FVector& Start = Candidate.ProvisionalPolyline[Index - 1];
			const FVector& End = Candidate.ProvisionalPolyline[Index];
			const double Length = FVector::Distance(Start, End);
			Candidate.Length += Length;
			Cost += Length;
			if (GetVectorAxis(Start, End) == EParadoxPuzzleWireAxis::Z)
			{
				Candidate.VerticalLength += Length;
				Cost += Length * Snapshot.Settings.VerticalPenalty;
			}
		}
		for (const FDirectedEdge& Edge : Candidate.Edges)
		{
			if (Edge.Axis != EParadoxPuzzleWireAxis::Z
				&& (!IsSurfaceSupported(Snapshot, Edge.Start) || !IsSurfaceSupported(Snapshot, Edge.End)))
			{
				const double UnsupportedLength = FVector::Distance(
					ToLocalPoint(Edge.Start, Snapshot.Settings),
					ToLocalPoint(Edge.End, Snapshot.Settings));
				Candidate.UnsupportedLength += UnsupportedLength;
				Cost += UnsupportedLength * Snapshot.Settings.UnsupportedPenalty;
			}
		}
		Candidate.BaseCost = Cost;
		return Cost;
	}

	struct FFacePairRank
	{
		EParadoxPuzzlePortSide SourceSide = EParadoxPuzzlePortSide::East;
		EParadoxPuzzlePortSide TargetSide = EParadoxPuzzlePortSide::West;
		int32 PairIndex = INDEX_NONE;
		double LowerBound = TNumericLimits<double>::Max();
	};

	TArray<FFacePairRank> MakeStableFacePairs()
	{
		static constexpr EParadoxPuzzlePortSide Sides[] = {
			EParadoxPuzzlePortSide::North,
			EParadoxPuzzlePortSide::South,
			EParadoxPuzzlePortSide::East,
			EParadoxPuzzlePortSide::West,
			EParadoxPuzzlePortSide::PositiveZ,
			EParadoxPuzzlePortSide::NegativeZ};
		TArray<FFacePairRank> Result;
		Result.Reserve(UE_ARRAY_COUNT(Sides) * UE_ARRAY_COUNT(Sides));
		for (const EParadoxPuzzlePortSide SourceSide : Sides)
		{
			for (const EParadoxPuzzlePortSide TargetSide : Sides)
			{
				FFacePairRank& Pair = Result.AddDefaulted_GetRef();
				Pair.SourceSide = SourceSide;
				Pair.TargetSide = TargetSide;
				Pair.PairIndex = static_cast<int32>(SourceSide) * UE_ARRAY_COUNT(Sides)
					+ static_cast<int32>(TargetSide);
			}
		}
		return Result;
	}

	TArray<FFacePairRank> RankFacePairsAtBaseResolution(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const FParadoxPuzzleRoutingLink& Link)
	{
		FParadoxPuzzleRoutingSettings CoarseSettings = Snapshot.Settings;
		const int32 SubdivisionFactor = GetParadoxPuzzleRoutingSubdivisionFactor(
			Snapshot.Settings.GridCellSubdivision);
		CoarseSettings.PitchX *= SubdivisionFactor;
		CoarseSettings.PitchY *= SubdivisionFactor;
		CoarseSettings.MaxRerouteAttempts = 0;

		TArray<FFacePairRank> Result = MakeStableFacePairs();
		for (FFacePairRank& Rank : Result)
		{
				const FParadoxPuzzleWirePort SourcePort = MakePort(
					Link.SourceBounds, Link.Source, Rank.SourceSide, CoarseSettings);
				const FParadoxPuzzleWirePort TargetPort = MakePort(
					Link.TargetBounds, Link.Target, Rank.TargetSide, CoarseSettings);
				const FParadoxPuzzleRoutingCoord Start = GetClearanceCoord(SourcePort, CoarseSettings);
				const FParadoxPuzzleRoutingCoord End = GetClearanceCoord(TargetPort, CoarseSettings);
				for (const TPair<TArray<FParadoxPuzzleRoutingCoord>, bool>& CoarseCandidate
					: BuildCoordinateCandidates(Start, End, CoarseSettings))
				{
					if (CoarseCandidate.Value)
					{
						continue;
					}
					const TArray<FVector> Polyline = BuildCompletePolyline(
						SourcePort, TargetPort, CoarseCandidate.Key, CoarseSettings);
					if (!AvoidsOwnEndpointInteriors(Polyline, Link))
					{
						continue;
					}
					double Length = 0.0;
					double VerticalLength = 0.0;
					for (int32 PointIndex = 1; PointIndex < Polyline.Num(); ++PointIndex)
					{
						const double SegmentLength = FVector::Distance(
							Polyline[PointIndex - 1], Polyline[PointIndex]);
						Length += SegmentLength;
						if (GetVectorAxis(Polyline[PointIndex - 1], Polyline[PointIndex])
							== EParadoxPuzzleWireAxis::Z)
						{
							VerticalLength += SegmentLength;
						}
					}
					const double LowerBound = Length
						+ CountBends(Polyline) * CoarseSettings.BendPenalty
						+ VerticalLength * CoarseSettings.VerticalPenalty;
					Rank.LowerBound = FMath::Min(Rank.LowerBound, LowerBound);
				}
		}
		Result.Sort([](const FFacePairRank& A, const FFacePairRank& B)
		{
			if (!FMath::IsNearlyEqual(A.LowerBound, B.LowerBound, CoordinateTolerance))
			{
				return A.LowerBound < B.LowerBound;
			}
			return A.PairIndex < B.PairIndex;
		});
		return Result;
	}

	TArray<FCandidate> BuildCandidatesForLink(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const FParadoxPuzzleRoutingLink& Link,
		FParadoxPuzzleRoutingDiagnostics& Diagnostics,
		const bool bUseHierarchicalPruning = false,
		const bool bSingleLinkFastPath = false)
	{
		TArray<FCandidate> Generated;
		const int32 Limit = FMath::Clamp(Snapshot.Settings.MaxOrderedBundleCandidatesPerLink, 36, 512);
		const int32 PerFacePairBudget = FMath::Max(2, FMath::DivideAndRoundUp(Limit, 36) + 1);
		TArray<FFacePairRank> RankedPairs = bUseHierarchicalPruning
			? RankFacePairsAtBaseResolution(Snapshot, Link)
			: MakeStableFacePairs();
		int32 FineFacePairCount = RankedPairs.Num();
		if (bUseHierarchicalPruning)
		{
			const int32 SubdivisionFactor = GetParadoxPuzzleRoutingSubdivisionFactor(
				Snapshot.Settings.GridCellSubdivision);
			const int32 MaximumFinePairs = bSingleLinkFastPath
				? Snapshot.Settings.SingleLinkFineFacePairLimit
				: (SubdivisionFactor > 1
					? Snapshot.Settings.SubdividedFineFacePairLimit
					: Snapshot.Settings.BaseResolutionFineFacePairLimit);
			FineFacePairCount = FMath::Clamp(
				FMath::DivideAndRoundUp(Limit, 8),
				8,
				MaximumFinePairs);
			FineFacePairCount = FMath::Min(FineFacePairCount, RankedPairs.Num());
			Diagnostics.HierarchicalCoarseFacePairCount += RankedPairs.Num();
			Diagnostics.PrunedFineFacePairCount += RankedPairs.Num() - FineFacePairCount;
		}
		for (int32 RankedPairIndex = 0; RankedPairIndex < FineFacePairCount; ++RankedPairIndex)
		{
			if (IsRoutingCancellationRequested())
			{
				return {};
			}
			const FFacePairRank& RankedPair = RankedPairs[RankedPairIndex];
			const EParadoxPuzzlePortSide SourceSide = RankedPair.SourceSide;
			const EParadoxPuzzlePortSide TargetSide = RankedPair.TargetSide;
			TArray<FCandidate> FacePairCandidates;
			const int32 PairIndex = RankedPair.PairIndex;
			const FParadoxPuzzleWirePort SourcePort = MakePort(
				Link.SourceBounds, Link.Source, SourceSide, Snapshot.Settings);
			const FParadoxPuzzleWirePort TargetPort = MakePort(
				Link.TargetBounds, Link.Target, TargetSide, Snapshot.Settings);
			const FParadoxPuzzleRoutingCoord Start = GetClearanceCoord(SourcePort, Snapshot.Settings);
			const FParadoxPuzzleRoutingCoord End = GetClearanceCoord(TargetPort, Snapshot.Settings);
			int32 PairCandidateOrder = 0;
			for (TPair<TArray<FParadoxPuzzleRoutingCoord>, bool>& CoordCandidate
				: BuildCoordinateCandidates(Start, End, Snapshot.Settings))
			{
				if (bSingleLinkFastPath && CoordCandidate.Value)
				{
					continue;
				}
				FCandidate Candidate;
				Candidate.SourcePort = SourcePort;
				Candidate.TargetPort = TargetPort;
				Candidate.Points = MoveTemp(CoordCandidate.Key);
				Candidate.FacePairIndex = PairIndex;
				Candidate.StableOrder = PairIndex * 64 + PairCandidateOrder++;
				Candidate.bDetour = CoordCandidate.Value;
				AppendUnitEdges(Candidate.Points, Candidate.Edges);
				EvaluateBaseCost(Snapshot, Candidate);
				if (!AvoidsOwnEndpointInteriors(Candidate.ProvisionalPolyline, Link))
				{
					++Diagnostics.RejectedEndpointInteriorCandidateCount;
					continue;
				}
				FacePairCandidates.Add(MoveTemp(Candidate));
			}

			FacePairCandidates.Sort([](const FCandidate& A, const FCandidate& B)
			{
				if (!FMath::IsNearlyEqual(A.BaseCost, B.BaseCost, CoordinateTolerance))
				{
					return A.BaseCost < B.BaseCost;
				}
				return A.StableOrder < B.StableOrder;
			});
			TSet<int32> AddedOrders;
			const auto AddFirstMatching = [&FacePairCandidates, &Generated, &AddedOrders](const bool bDetour)
			{
				if (const FCandidate* Match = FacePairCandidates.FindByPredicate([bDetour](const FCandidate& Candidate)
				{
					return Candidate.bDetour == bDetour;
				}))
				{
					Generated.Add(*Match);
					AddedOrders.Add(Match->StableOrder);
				}
			};
			AddFirstMatching(false);
			if (!bSingleLinkFastPath)
			{
				AddFirstMatching(true);
			}
			for (const FCandidate& Candidate : FacePairCandidates)
			{
				if (AddedOrders.Num() >= PerFacePairBudget)
				{
					break;
				}
				if (!AddedOrders.Contains(Candidate.StableOrder))
				{
					Generated.Add(Candidate);
					AddedOrders.Add(Candidate.StableOrder);
				}
			}
		}
		if (Generated.IsEmpty() && bUseHierarchicalPruning)
		{
			return BuildCandidatesForLink(Snapshot, Link, Diagnostics, false, bSingleLinkFastPath);
		}

		Generated.Sort([](const FCandidate& A, const FCandidate& B)
		{
			if (!FMath::IsNearlyEqual(A.BaseCost, B.BaseCost, CoordinateTolerance))
			{
				return A.BaseCost < B.BaseCost;
			}
			return A.StableOrder < B.StableOrder;
		});

		TArray<FCandidate> Retained;
		TSet<int32> RetainedPairs;
		TSet<int32> RetainedDetourPairs;
		TSet<int32> RetainedOrders;
		for (const FCandidate& Candidate : Generated)
		{
			if (Candidate.bDetour || RetainedPairs.Contains(Candidate.FacePairIndex))
			{
				continue;
			}
			Retained.Add(Candidate);
			RetainedPairs.Add(Candidate.FacePairIndex);
			RetainedOrders.Add(Candidate.StableOrder);
		}
		// Keep one bounded alternative corridor for every face pair before filling the
		// remaining budget by scalar cost. Without this diversity the global bundle
		// pass could only compare independent shortest paths and could never attract
		// nearby routes onto a compatible shared centerline.
		for (const FCandidate& Candidate : Generated)
		{
			if (Retained.Num() >= Limit)
			{
				break;
			}
			if (!Candidate.bDetour || RetainedDetourPairs.Contains(Candidate.FacePairIndex))
			{
				continue;
			}
			Retained.Add(Candidate);
			RetainedDetourPairs.Add(Candidate.FacePairIndex);
			RetainedOrders.Add(Candidate.StableOrder);
		}
		for (const FCandidate& Candidate : Generated)
		{
			if (Retained.Num() >= Limit)
			{
				break;
			}
			if (!RetainedOrders.Contains(Candidate.StableOrder))
			{
				Retained.Add(Candidate);
				RetainedOrders.Add(Candidate.StableOrder);
			}
		}
		Retained.Sort([](const FCandidate& A, const FCandidate& B)
		{
			return A.StableOrder < B.StableOrder;
		});
		Diagnostics.CandidateCount += Retained.Num();
		return Retained;
	}

	bool DoSegmentsCross(
		const FVector& A0,
		const FVector& A1,
		const FVector& B0,
		const FVector& B1,
		FVector* OutPoint = nullptr)
	{
		if (!FMath::IsNearlyEqual(A0.Z, B0.Z, HeightBucketSize * 0.5))
		{
			return false;
		}
		const EParadoxPuzzleWireAxis AAxis = GetVectorAxis(A0, A1);
		const EParadoxPuzzleWireAxis BAxis = GetVectorAxis(B0, B1);
		if (AAxis == EParadoxPuzzleWireAxis::Z || BAxis == EParadoxPuzzleWireAxis::Z || AAxis == BAxis)
		{
			return false;
		}
		const FVector& X0 = AAxis == EParadoxPuzzleWireAxis::X ? A0 : B0;
		const FVector& X1 = AAxis == EParadoxPuzzleWireAxis::X ? A1 : B1;
		const FVector& Y0 = AAxis == EParadoxPuzzleWireAxis::Y ? A0 : B0;
		const FVector& Y1 = AAxis == EParadoxPuzzleWireAxis::Y ? A1 : B1;
		const FVector Point(Y0.X, X0.Y, A0.Z);
		const bool bInside = Point.X > FMath::Min(X0.X, X1.X) + CoordinateTolerance
			&& Point.X < FMath::Max(X0.X, X1.X) - CoordinateTolerance
			&& Point.Y > FMath::Min(Y0.Y, Y1.Y) + CoordinateTolerance
			&& Point.Y < FMath::Max(Y0.Y, Y1.Y) - CoordinateTolerance;
		if (bInside && OutPoint)
		{
			*OutPoint = Point;
		}
		return bInside;
	}

	int32 CountCrossings(const FCandidate& Candidate, const FCandidate& Other)
	{
		int32 Count = 0;
		for (int32 A = 1; A < Candidate.ProvisionalPolyline.Num(); ++A)
		{
			for (int32 B = 1; B < Other.ProvisionalPolyline.Num(); ++B)
			{
				Count += DoSegmentsCross(
					Candidate.ProvisionalPolyline[A - 1], Candidate.ProvisionalPolyline[A],
					Other.ProvisionalPolyline[B - 1], Other.ProvisionalPolyline[B]) ? 1 : 0;
			}
		}
		return Count;
	}

	struct FEdgeUse
	{
		bool bForward = false;
		const FCandidate* Candidate = nullptr;
		int32 EdgeIndex = INDEX_NONE;
	};

	struct FGlobalCostContext
	{
		TMap<FEdgeKey, TArray<FEdgeUse>> Occupancy;
		TArray<const FCandidate*> OtherCandidates;
	};

	FGlobalCostContext BuildGlobalCostContext(
		const int32 LinkIndex,
		const TArray<TArray<FCandidate>>& Candidates,
		const TArray<int32>& Selection)
	{
		FGlobalCostContext Context;
		Context.OtherCandidates.Reserve(FMath::Max(0, Selection.Num() - 1));
		for (int32 OtherIndex = 0; OtherIndex < Selection.Num(); ++OtherIndex)
		{
			if (OtherIndex == LinkIndex || !Candidates.IsValidIndex(OtherIndex)
				|| !Candidates[OtherIndex].IsValidIndex(Selection[OtherIndex]))
			{
				continue;
			}

			const FCandidate* Other = &Candidates[OtherIndex][Selection[OtherIndex]];
			Context.OtherCandidates.Add(Other);
			for (int32 EdgeIndex = 0; EdgeIndex < Other->Edges.Num(); ++EdgeIndex)
			{
				const FDirectedEdge& Edge = Other->Edges[EdgeIndex];
				Context.Occupancy.FindOrAdd(Edge.Key).Add({Edge.bForward, Other, EdgeIndex});
			}
		}
		return Context;
	}

	double EvaluateGlobalCost(
		const FCandidate& Candidate,
		const FGlobalCostContext& Context,
		const FParadoxPuzzleRoutingSettings& Settings,
		double* OutBonus = nullptr,
		int32* OutCongestion = nullptr,
		int32* OutCrossings = nullptr)
	{
		double Cost = Candidate.BaseCost;
		double Bonus = 0.0;
		int32 Congestion = 0;
		int32 Crossings = 0;
		for (const FCandidate* Other : Context.OtherCandidates)
		{
			if (!Other)
			{
				continue;
			}
			Crossings += CountCrossings(Candidate, *Other);
		}

		TSet<FEdgeKey> RewardedEdges;
		const auto TangentAroundEdge = [](const FCandidate& Route, const int32 EdgeIndex, const bool bEntry)
		{
			const FDirectedEdge& Edge = Route.Edges[EdgeIndex];
			const auto CoordVector = [](const FParadoxPuzzleRoutingCoord& Coord)
			{
				return FVector(Coord.X, Coord.Y, Coord.Z);
			};
			const FVector Point = bEntry
				? (EdgeIndex > 0
					? CoordVector(Route.Edges[EdgeIndex - 1].Start)
					: Route.SourcePort.Position)
				: (EdgeIndex + 1 < Route.Edges.Num()
					? CoordVector(Route.Edges[EdgeIndex + 1].End)
					: Route.TargetPort.Position);
			return Edge.Axis == EParadoxPuzzleWireAxis::X ? Point.Y : Point.X;
		};
		for (int32 CandidateEdgeIndex = 0; CandidateEdgeIndex < Candidate.Edges.Num(); ++CandidateEdgeIndex)
		{
			const FDirectedEdge& Edge = Candidate.Edges[CandidateEdgeIndex];
			const TArray<FEdgeUse>* Uses = Context.Occupancy.Find(Edge.Key);
			if (!Uses)
			{
				continue;
			}
			bool bCompatible = false;
			for (const FEdgeUse& Use : *Uses)
			{
				if (Use.bForward != Edge.bForward || !Use.Candidate || Use.EdgeIndex == INDEX_NONE)
				{
					continue;
				}
				const double EntryDelta = TangentAroundEdge(Candidate, CandidateEdgeIndex, true)
					- TangentAroundEdge(*Use.Candidate, Use.EdgeIndex, true);
				const double ExitDelta = TangentAroundEdge(Candidate, CandidateEdgeIndex, false)
					- TangentAroundEdge(*Use.Candidate, Use.EdgeIndex, false);
				if (EntryDelta * ExitDelta >= -CoordinateTolerance)
				{
					bCompatible = true;
					break;
				}
			}
			if (bCompatible && Uses->Num() < Settings.MaxLanesPerEdge && !RewardedEdges.Contains(Edge.Key))
			{
				RewardedEdges.Add(Edge.Key);
				Bonus += Settings.BundleReuseBonus;
			}
			else
			{
				Congestion += Uses->Num();
			}
		}
		Cost += Crossings * Settings.CrossingPenalty;
		Cost += Crossings * Settings.BridgePenalty;
		Cost += Congestion * Settings.LanePenalty;
		Cost -= Bonus;
		if (OutBonus) *OutBonus = Bonus;
		if (OutCongestion) *OutCongestion = Congestion;
		if (OutCrossings) *OutCrossings = Crossings;
		return Cost;
	}

	bool IsBetterCandidate(const FCandidate& Candidate, const double Cost, const FCandidate& Current, const double CurrentCost)
	{
		if (!FMath::IsNearlyEqual(Cost, CurrentCost, CoordinateTolerance))
		{
			return Cost < CurrentCost;
		}
		if (Candidate.BendCount != Current.BendCount)
		{
			return Candidate.BendCount < Current.BendCount;
		}
		if (Candidate.FacePairIndex != Current.FacePairIndex)
		{
			return Candidate.FacePairIndex < Current.FacePairIndex;
		}
		return Candidate.StableOrder < Current.StableOrder;
	}

	int32 CountSelectionCrossings(
		const TArray<TArray<FCandidate>>& Candidates,
		const TArray<int32>& Selection)
	{
		int32 Count = 0;
		for (int32 A = 0; A < Selection.Num(); ++A)
		{
			if (!Candidates.IsValidIndex(A) || !Candidates[A].IsValidIndex(Selection[A])) continue;
			for (int32 B = A + 1; B < Selection.Num(); ++B)
			{
				if (!Candidates.IsValidIndex(B) || !Candidates[B].IsValidIndex(Selection[B])) continue;
				Count += CountCrossings(Candidates[A][Selection[A]], Candidates[B][Selection[B]]);
			}
		}
		return Count;
	}

	void OptimizeSelection(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const TArray<TArray<FCandidate>>& Candidates,
		TArray<int32>& InOutSelection,
		FParadoxPuzzleRoutingDiagnostics& Diagnostics)
	{
		if (IsRoutingCancellationRequested())
		{
			return;
		}
		for (int32 LinkIndex = 0; LinkIndex < Candidates.Num(); ++LinkIndex)
		{
			double BestCost = TNumericLimits<double>::Max();
			int32 BestIndex = INDEX_NONE;
			for (int32 CandidateIndex = 0; CandidateIndex < Candidates[LinkIndex].Num(); ++CandidateIndex)
			{
				if (Candidates[LinkIndex][CandidateIndex].bDetour) continue;
				const double Cost = Candidates[LinkIndex][CandidateIndex].BaseCost;
				if (BestIndex == INDEX_NONE || IsBetterCandidate(
					Candidates[LinkIndex][CandidateIndex], Cost,
					Candidates[LinkIndex][BestIndex], BestCost))
				{
					BestIndex = CandidateIndex;
					BestCost = Cost;
				}
			}
			InOutSelection[LinkIndex] = BestIndex;
		}

		for (int32 Pass = 0; Pass < Snapshot.Settings.MaxBundleOptimizationPasses; ++Pass)
		{
			if (IsRoutingCancellationRequested())
			{
				return;
			}
			Diagnostics.BundleOptimizationPassCount = Pass + 1;
			bool bChanged = false;
			for (int32 LinkIndex = 0; LinkIndex < Candidates.Num(); ++LinkIndex)
			{
				if (!Candidates[LinkIndex].IsValidIndex(InOutSelection[LinkIndex])) continue;
				const FGlobalCostContext Context = BuildGlobalCostContext(LinkIndex, Candidates, InOutSelection);
				int32 BestIndex = InOutSelection[LinkIndex];
				double BestCost = EvaluateGlobalCost(
					Candidates[LinkIndex][BestIndex], Context, Snapshot.Settings);
				for (int32 CandidateIndex = 0; CandidateIndex < Candidates[LinkIndex].Num(); ++CandidateIndex)
				{
					const double Cost = EvaluateGlobalCost(
						Candidates[LinkIndex][CandidateIndex], Context, Snapshot.Settings);
					if (IsBetterCandidate(
						Candidates[LinkIndex][CandidateIndex], Cost,
						Candidates[LinkIndex][BestIndex], BestCost))
					{
						BestIndex = CandidateIndex;
						BestCost = Cost;
					}
				}
				if (BestIndex != InOutSelection[LinkIndex])
				{
					InOutSelection[LinkIndex] = BestIndex;
					bChanged = true;
				}
			}
			if (!bChanged) break;
		}

		const int32 BeforeReroute = CountSelectionCrossings(Candidates, InOutSelection);
		for (int32 Pass = 0; Pass < Snapshot.Settings.MaxRerouteAttempts; ++Pass)
		{
			if (IsRoutingCancellationRequested())
			{
				return;
			}
			bool bChanged = false;
			for (int32 LinkIndex = 0; LinkIndex < Candidates.Num(); ++LinkIndex)
			{
				if (!Candidates[LinkIndex].IsValidIndex(InOutSelection[LinkIndex])) continue;
				const FGlobalCostContext Context = BuildGlobalCostContext(LinkIndex, Candidates, InOutSelection);
				int32 CurrentCrossings = 0;
				const FCandidate& Current = Candidates[LinkIndex][InOutSelection[LinkIndex]];
				EvaluateGlobalCost(Current, Context, Snapshot.Settings, nullptr, nullptr, &CurrentCrossings);
				if (CurrentCrossings == 0) continue;
				int32 BestIndex = InOutSelection[LinkIndex];
				int32 BestCrossings = CurrentCrossings;
				double BestCost = EvaluateGlobalCost(Current, Context, Snapshot.Settings);
				for (int32 CandidateIndex = 0; CandidateIndex < Candidates[LinkIndex].Num(); ++CandidateIndex)
				{
					int32 Crossings = 0;
					const double Cost = EvaluateGlobalCost(
						Candidates[LinkIndex][CandidateIndex], Context,
						Snapshot.Settings, nullptr, nullptr, &Crossings);
					if (Crossings < BestCrossings
						|| (Crossings == BestCrossings && IsBetterCandidate(
							Candidates[LinkIndex][CandidateIndex], Cost,
							Candidates[LinkIndex][BestIndex], BestCost)))
					{
						BestIndex = CandidateIndex;
						BestCrossings = Crossings;
						BestCost = Cost;
					}
				}
				if (BestIndex != InOutSelection[LinkIndex] && BestCrossings < CurrentCrossings)
				{
					InOutSelection[LinkIndex] = BestIndex;
					++Diagnostics.RerouteAttempts;
					bChanged = true;
				}
			}
			if (!bChanged) break;
		}
		Diagnostics.CrossingsResolvedByReroute = FMath::Max(
			0, BeforeReroute - CountSelectionCrossings(Candidates, InOutSelection));
	}

	bool SameMembers(const TArray<int32>& A, const TArray<int32>& B)
	{
		return A == B;
	}

	bool LexicographicalMembersLess(const TArray<int32>& A, const TArray<int32>& B)
	{
		const int32 SharedCount = FMath::Min(A.Num(), B.Num());
		for (int32 Index = 0; Index < SharedCount; ++Index)
		{
			if (A[Index] != B[Index])
			{
				return A[Index] < B[Index];
			}
		}
		return A.Num() < B.Num();
	}

	TArray<FBundleWork> ExtractBundles(
		const TArray<TArray<FCandidate>>& Candidates,
		const TArray<int32>& Selection,
		const int32 MaxLanes)
	{
		struct FUsage
		{
			FDirectedEdge Edge;
			TArray<int32> Members;
		};
		TMap<FBundleEdgeKey, FUsage> Usage;
		for (int32 LinkIndex = 0; LinkIndex < Selection.Num(); ++LinkIndex)
		{
			if (!Candidates.IsValidIndex(LinkIndex) || !Candidates[LinkIndex].IsValidIndex(Selection[LinkIndex])) continue;
			for (const FDirectedEdge& Edge : Candidates[LinkIndex][Selection[LinkIndex]].Edges)
			{
				FUsage& Entry = Usage.FindOrAdd({Edge.Key, Edge.bForward});
				Entry.Edge = Edge;
				Entry.Members.Add(LinkIndex);
			}
		}

		TArray<FUsage> SharedEdges;
		for (TPair<FBundleEdgeKey, FUsage>& Pair : Usage)
		{
			Pair.Value.Members.Sort();
			if (Pair.Value.Members.Num() >= 2 && Pair.Value.Members.Num() <= MaxLanes)
			{
				SharedEdges.Add(Pair.Value);
			}
		}
		SharedEdges.Sort([](const FUsage& A, const FUsage& B)
		{
			if (A.Members != B.Members)
			{
				return LexicographicalMembersLess(A.Members, B.Members);
			}
			if (A.Edge.Axis != B.Edge.Axis)
			{
				return static_cast<uint8>(A.Edge.Axis) < static_cast<uint8>(B.Edge.Axis);
			}
			return A.Edge.Key < B.Edge.Key;
		});

		TArray<FBundleWork> Bundles;
		TSet<int32> Visited;
		for (int32 SeedIndex = 0; SeedIndex < SharedEdges.Num(); ++SeedIndex)
		{
			if (Visited.Contains(SeedIndex)) continue;
			FBundleWork Bundle;
			Bundle.Members = SharedEdges[SeedIndex].Members;
			Bundle.Edges.Add(SharedEdges[SeedIndex].Edge);
			Visited.Add(SeedIndex);
			bool bExtended = true;
			while (bExtended)
			{
				bExtended = false;
				const FNodeKey Tail = Bundle.Edges.Last().EndNode;
				for (int32 CandidateIndex = 0; CandidateIndex < SharedEdges.Num(); ++CandidateIndex)
				{
					if (Visited.Contains(CandidateIndex)) continue;
					const FUsage& Candidate = SharedEdges[CandidateIndex];
					if (SameMembers(Bundle.Members, Candidate.Members)
						&& Bundle.Edges.Last().Axis == Candidate.Edge.Axis
						&& Candidate.Edge.StartNode == Tail)
					{
						Bundle.Edges.Add(Candidate.Edge);
						Visited.Add(CandidateIndex);
						bExtended = true;
						break;
					}
				}
			}
			Bundles.Add(MoveTemp(Bundle));
		}
		Bundles.Sort([](const FBundleWork& A, const FBundleWork& B)
		{
			if (A.Members != B.Members) return LexicographicalMembersLess(A.Members, B.Members);
			return A.Edges[0].Key < B.Edges[0].Key;
		});
		for (int32 Index = 0; Index < Bundles.Num(); ++Index)
		{
			Bundles[Index].BundleId = Index;
		}
		return Bundles;
	}

	double GetBundleOrderValue(
		const FCandidate& Candidate,
		const FBundleWork& Bundle,
		const bool bEntry,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		int32 EdgeIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Candidate.Edges.Num(); ++Index)
		{
			for (const FDirectedEdge& BundleEdge : Bundle.Edges)
			{
				if (Candidate.Edges[Index].Key == BundleEdge.Key
					&& Candidate.Edges[Index].bForward == BundleEdge.bForward)
				{
					EdgeIndex = bEntry ? Index : FMath::Max(EdgeIndex, Index);
					if (bEntry) break;
				}
			}
			if (bEntry && EdgeIndex != INDEX_NONE) break;
		}
		if (EdgeIndex == INDEX_NONE) return 0.0;
		const FDirectedEdge& Edge = Candidate.Edges[EdgeIndex];
		FVector Point;
		if (bEntry)
		{
			Point = EdgeIndex > 0
				? ToLocalPoint(Candidate.Edges[EdgeIndex - 1].Start, Settings)
				: Candidate.SourcePort.Position;
		}
		else
		{
			Point = EdgeIndex + 1 < Candidate.Edges.Num()
				? ToLocalPoint(Candidate.Edges[EdgeIndex + 1].End, Settings)
				: Candidate.TargetPort.Position;
		}
		return Edge.Axis == EParadoxPuzzleWireAxis::X ? Point.Y : Point.X;
	}

	int32 CountOrderInversions(const TArray<int32>& Order, const TArray<int32>& Desired)
	{
		TMap<int32, int32> DesiredPosition;
		for (int32 Index = 0; Index < Desired.Num(); ++Index)
		{
			DesiredPosition.Add(Desired[Index], Index);
		}
		int32 Count = 0;
		for (int32 A = 0; A < Order.Num(); ++A)
		{
			for (int32 B = A + 1; B < Order.Num(); ++B)
			{
				Count += DesiredPosition.FindRef(Order[A]) > DesiredPosition.FindRef(Order[B]) ? 1 : 0;
			}
		}
		return Count;
	}

	void OrderBundles(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const TArray<FParadoxPuzzleRoutingLink>& Links,
		const TArray<TArray<FCandidate>>& Candidates,
		const TArray<int32>& Selection,
		TArray<FBundleWork>& InOutBundles,
		FParadoxPuzzleRoutingDiagnostics& Diagnostics)
	{
		for (FBundleWork& Bundle : InOutBundles)
		{
			TArray<int32> EntryOrder = Bundle.Members;
			TArray<int32> ExitOrder = Bundle.Members;
			EntryOrder.Sort([&](const int32 A, const int32 B)
			{
				const double AV = GetBundleOrderValue(Candidates[A][Selection[A]], Bundle, true, Snapshot.Settings);
				const double BV = GetBundleOrderValue(Candidates[B][Selection[B]], Bundle, true, Snapshot.Settings);
				return !FMath::IsNearlyEqual(AV, BV) ? AV < BV : Links[A].StableOrder < Links[B].StableOrder;
			});
			ExitOrder.Sort([&](const int32 A, const int32 B)
			{
				const double AV = GetBundleOrderValue(Candidates[A][Selection[A]], Bundle, false, Snapshot.Settings);
				const double BV = GetBundleOrderValue(Candidates[B][Selection[B]], Bundle, false, Snapshot.Settings);
				return !FMath::IsNearlyEqual(AV, BV) ? AV < BV : Links[A].StableOrder < Links[B].StableOrder;
			});
			Bundle.OrderedMembers = Bundle.Members;
			Bundle.OrderedMembers.Sort([&Links](const int32 A, const int32 B)
			{
				return Links[A].StableOrder < Links[B].StableOrder;
			});
			const auto Objective = [&EntryOrder, &ExitOrder](const TArray<int32>& Order)
			{
				return CountOrderInversions(Order, EntryOrder) + CountOrderInversions(Order, ExitOrder);
			};
			Bundle.InversionsBefore = CountOrderInversions(EntryOrder, ExitOrder);
			for (int32 Pass = 0; Pass < Snapshot.Settings.MaxMetroOrderingPasses; ++Pass)
			{
				Diagnostics.MetroOrderingPassCount = FMath::Max(Diagnostics.MetroOrderingPassCount, Pass + 1);
				bool bImproved = false;
				for (int32 Index = 0; Index + 1 < Bundle.OrderedMembers.Num(); ++Index)
				{
					const int32 Before = Objective(Bundle.OrderedMembers);
					Bundle.OrderedMembers.Swap(Index, Index + 1);
					if (Objective(Bundle.OrderedMembers) < Before)
					{
						bImproved = true;
					}
					else
					{
						Bundle.OrderedMembers.Swap(Index, Index + 1);
					}
				}
				if (!bImproved) break;
			}
			Bundle.InversionsAfter = FMath::Min(
				CountOrderInversions(Bundle.OrderedMembers, EntryOrder),
				CountOrderInversions(Bundle.OrderedMembers, ExitOrder));
			Diagnostics.InversionsBeforeOrdering += Bundle.InversionsBefore;
			Diagnostics.InversionsAfterOrdering += Bundle.InversionsAfter;
		}
		Diagnostics.CrossingsResolvedByOrdering = FMath::Max(
			0, Diagnostics.InversionsBeforeOrdering - Diagnostics.InversionsAfterOrdering);
	}

	TMap<FBundleEdgeKey, int32> BuildBundleEdgeMap(const TArray<FBundleWork>& Bundles)
	{
		TMap<FBundleEdgeKey, int32> Result;
		for (const FBundleWork& Bundle : Bundles)
		{
			for (const FDirectedEdge& Edge : Bundle.Edges)
			{
				Result.Add({Edge.Key, Edge.bForward}, Bundle.BundleId);
			}
		}
		return Result;
	}

	int32 FindLane(const FBundleWork& Bundle, const int32 LinkIndex)
	{
		return Bundle.OrderedMembers.IndexOfByKey(LinkIndex);
	}

	EParadoxPuzzleWireAxis GetFaceTangentAxis(
		const EParadoxPuzzlePortSide Side,
		const FCandidate& Candidate,
		const bool bSource)
	{
		if (Side == EParadoxPuzzlePortSide::East || Side == EParadoxPuzzlePortSide::West)
		{
			return EParadoxPuzzleWireAxis::Y;
		}
		if (Side == EParadoxPuzzlePortSide::North || Side == EParadoxPuzzlePortSide::South)
		{
			return EParadoxPuzzleWireAxis::X;
		}
		const FDirectedEdge* Adjacent = Candidate.Edges.IsEmpty()
			? nullptr
			: (bSource ? &Candidate.Edges[0] : &Candidate.Edges.Last());
		return Adjacent && Adjacent->Axis == EParadoxPuzzleWireAxis::X
			? EParadoxPuzzleWireAxis::Y
			: EParadoxPuzzleWireAxis::X;
	}

	void ResolveFinalPorts(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const TArray<FParadoxPuzzleRoutingLink>& Links,
		const TArray<TArray<FCandidate>>& Candidates,
		const TArray<int32>& Selection,
		const TArray<FBundleWork>& Bundles,
		const TMap<FBundleEdgeKey, int32>& EdgeToBundle,
		TArray<FParadoxPuzzleWirePort>& OutSourcePorts,
		TArray<FParadoxPuzzleWirePort>& OutTargetPorts)
	{
		OutSourcePorts.SetNum(Links.Num());
		OutTargetPorts.SetNum(Links.Num());
		TMap<FString, TArray<FPortRequest>> Groups;
		for (int32 LinkIndex = 0; LinkIndex < Links.Num(); ++LinkIndex)
		{
			if (!Candidates[LinkIndex].IsValidIndex(Selection[LinkIndex])) continue;
			const FCandidate& Candidate = Candidates[LinkIndex][Selection[LinkIndex]];
			OutSourcePorts[LinkIndex] = Candidate.SourcePort;
			OutTargetPorts[LinkIndex] = Candidate.TargetPort;
			const auto AddRequest = [&](const bool bSource)
			{
				const FParadoxPuzzleWirePort& Port = bSource ? Candidate.SourcePort : Candidate.TargetPort;
				const FDirectedEdge* Adjacent = Candidate.Edges.IsEmpty()
					? nullptr : (bSource ? &Candidate.Edges[0] : &Candidate.Edges.Last());
				int32 BundleId = INDEX_NONE;
				int32 Lane = INDEX_NONE;
				if (Adjacent)
				{
					if (const int32* Found = EdgeToBundle.Find({Adjacent->Key, Adjacent->bForward}))
					{
						BundleId = *Found;
						Lane = FindLane(Bundles[BundleId], LinkIndex);
					}
				}
				const EParadoxPuzzleWireAxis TangentAxis = GetFaceTangentAxis(Port.Side, Candidate, bSource);
				const FVector Remote = bSource
					? GetEndpointCenter(Links[LinkIndex].TargetBounds, Links[LinkIndex].Target, Snapshot.Settings)
					: GetEndpointCenter(Links[LinkIndex].SourceBounds, Links[LinkIndex].Source, Snapshot.Settings);
				const double RemoteTangent = TangentAxis == EParadoxPuzzleWireAxis::X ? Remote.X : Remote.Y;
				const FParadoxPuzzleRoutingCoord EndpointCoord = bSource
					? Links[LinkIndex].Source
					: Links[LinkIndex].Target;
				const FString EndpointKey = Port.Bounds.EndpointKey.IsEmpty()
					? FString::Printf(
						TEXT("Point:%d:%d:%lld"),
						EndpointCoord.X,
						EndpointCoord.Y,
						static_cast<int64>(FMath::RoundToDouble(EndpointCoord.Z / HeightBucketSize)))
					: Port.Bounds.EndpointKey;
				const FString Key = FString::Printf(TEXT("%s|%d"),
					*EndpointKey, static_cast<int32>(Port.Side));
				Groups.FindOrAdd(Key).Add({LinkIndex, bSource, RemoteTangent, BundleId, Lane, Links[LinkIndex].StableOrder});
			};
			AddRequest(true);
			AddRequest(false);
		}

		for (TPair<FString, TArray<FPortRequest>>& Pair : Groups)
		{
			TArray<FPortRequest>& Requests = Pair.Value;
			Requests.Sort([](const FPortRequest& A, const FPortRequest& B)
			{
				if (A.BundleId != B.BundleId) return A.BundleId < B.BundleId;
				if (A.BundleLane != B.BundleLane) return A.BundleLane < B.BundleLane;
				if (!FMath::IsNearlyEqual(A.RemoteTangent, B.RemoteTangent)) return A.RemoteTangent < B.RemoteTangent;
				return A.StableOrder < B.StableOrder;
			});
			if (Requests.IsEmpty()) continue;
			const FPortRequest& First = Requests[0];
			const FCandidate& FirstCandidate = Candidates[First.LinkIndex][Selection[First.LinkIndex]];
			const FParadoxPuzzleWirePort& BasePort = First.bSource ? FirstCandidate.SourcePort : FirstCandidate.TargetPort;
			const EParadoxPuzzleWireAxis TangentAxis = GetFaceTangentAxis(BasePort.Side, FirstCandidate, First.bSource);
			const double FaceMin = (TangentAxis == EParadoxPuzzleWireAxis::X ? BasePort.Bounds.Min.X : BasePort.Bounds.Min.Y);
			const double FaceMax = (TangentAxis == EParadoxPuzzleWireAxis::X ? BasePort.Bounds.Max.X : BasePort.Bounds.Max.Y);
			const double FaceSpan = FMath::Max(0.0, FaceMax - FaceMin);
			const double EqualGap = Requests.Num() > 0 ? FaceSpan / static_cast<double>(Requests.Num() + 1) : 0.0;
			const double EdgeGap = Requests.Num() <= 1
				? FaceSpan * 0.5
				: FMath::Min(FaceSpan * 0.5, FMath::Max(Snapshot.Settings.PortEdgeInset, EqualGap));
			const double Spacing = Requests.Num() > 1
				? (FaceSpan - 2.0 * EdgeGap) / static_cast<double>(Requests.Num() - 1)
				: 0.0;
			for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
			{
				const FPortRequest& Request = Requests[RequestIndex];
				const FParadoxPuzzleRoutingLink& Link = Links[Request.LinkIndex];
				const FCandidate& Candidate = Candidates[Request.LinkIndex][Selection[Request.LinkIndex]];
				const FParadoxPuzzleWirePort& Provisional = Request.bSource ? Candidate.SourcePort : Candidate.TargetPort;
				const double Tangent = Requests.Num() == 1 ? (FaceMin + FaceMax) * 0.5 : FaceMin + EdgeGap + Spacing * RequestIndex;
				FParadoxPuzzleWirePort Final = MakePort(
					Provisional.Bounds,
					Request.bSource ? Link.Source : Link.Target,
					Provisional.Side,
					Snapshot.Settings,
					Tangent,
					TangentAxis,
					RequestIndex,
					Requests.Num());
				(Request.bSource ? OutSourcePorts[Request.LinkIndex] : OutTargetPorts[Request.LinkIndex]) = Final;
			}
		}
	}

	FVector GetNudgedStart(
		const FDirectedEdge& Edge,
		const FParadoxPuzzleRoutingSettings& Settings,
		const double Offset)
	{
		FVector Point = ToLocalPoint(Edge.Start, Settings);
		if (Edge.Axis == EParadoxPuzzleWireAxis::X) Point.Y += Offset;
		else Point.X += Offset;
		return Point;
	}

	FVector GetNudgedEnd(
		const FDirectedEdge& Edge,
		const FParadoxPuzzleRoutingSettings& Settings,
		const double Offset)
	{
		FVector Point = ToLocalPoint(Edge.End, Settings);
		if (Edge.Axis == EParadoxPuzzleWireAxis::X) Point.Y += Offset;
		else Point.X += Offset;
		return Point;
	}

	EParadoxPuzzleWireSegmentKind ResolveSegmentKind(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const FDirectedEdge& Edge)
	{
		if (Edge.Axis == EParadoxPuzzleWireAxis::Z)
		{
			return EParadoxPuzzleWireSegmentKind::StructuralVertical;
		}
		return IsSurfaceSupported(Snapshot, Edge.Start) && IsSurfaceSupported(Snapshot, Edge.End)
			? EParadoxPuzzleWireSegmentKind::GroundSupported
			: EParadoxPuzzleWireSegmentKind::GroundUnsupported;
	}

	void AppendOrthogonalConnector(
		const FVector& Start,
		const FVector& End,
		const int32 Lane,
		TArray<FParadoxPuzzleWireSegment>& OutSegments)
	{
		FVector Cursor = Start;
		for (const EParadoxPuzzleWireAxis Axis : {EParadoxPuzzleWireAxis::X, EParadoxPuzzleWireAxis::Y, EParadoxPuzzleWireAxis::Z})
		{
			FVector Next = Cursor;
			if (Axis == EParadoxPuzzleWireAxis::X) Next.X = End.X;
			else if (Axis == EParadoxPuzzleWireAxis::Y) Next.Y = End.Y;
			else Next.Z = End.Z;
			if (!Cursor.Equals(Next, CoordinateTolerance))
			{
				FParadoxPuzzleWireSegment& Segment = OutSegments.AddDefaulted_GetRef();
				Segment.Start = Cursor;
				Segment.End = Next;
				Segment.Axis = Axis;
				Segment.Kind = EParadoxPuzzleWireSegmentKind::GroundUnsupported;
				Segment.Lane = Lane;
			}
			Cursor = Next;
		}
	}

	void AppendTerminal(
		const TArray<FVector>& Points,
		const bool bReverse,
		const int32 Lane,
		TArray<FParadoxPuzzleWireSegment>& OutSegments)
	{
		TArray<FVector> Ordered = Points;
		if (bReverse) Algo::Reverse(Ordered);
		for (int32 Index = 1; Index < Ordered.Num(); ++Index)
		{
			if (Ordered[Index - 1].Equals(Ordered[Index], CoordinateTolerance)) continue;
			FParadoxPuzzleWireSegment& Segment = OutSegments.AddDefaulted_GetRef();
			Segment.Start = Ordered[Index - 1];
			Segment.End = Ordered[Index];
			Segment.Axis = GetVectorAxis(Segment.Start, Segment.End);
			Segment.Kind = EParadoxPuzzleWireSegmentKind::EndpointTerminal;
			Segment.Lane = Lane;
		}
	}

	void NormalizeSegments(TArray<FParadoxPuzzleWireSegment>& Segments)
	{
		Segments.RemoveAll([](const FParadoxPuzzleWireSegment& Segment)
		{
			return Segment.Start.Equals(Segment.End, CoordinateTolerance);
		});
		for (int32 Index = Segments.Num() - 1; Index > 0; --Index)
		{
			FParadoxPuzzleWireSegment& Previous = Segments[Index - 1];
			FParadoxPuzzleWireSegment& Current = Segments[Index];
			if (!Previous.End.Equals(Current.Start, CoordinateTolerance)) continue;
			const bool bSameLine = Previous.Axis == Current.Axis
				&& FVector::CrossProduct(Previous.End - Previous.Start, Current.End - Current.Start).IsNearlyZero(CoordinateTolerance);
			if (bSameLine && Previous.Kind == Current.Kind && Previous.Lane == Current.Lane
				&& Previous.BundleId == Current.BundleId
				&& FMath::IsNearlyEqual(Previous.NudgeOffset, Current.NudgeOffset))
			{
				Previous.End = Current.End;
				Segments.RemoveAt(Index);
			}
		}
	}

	void BuildRoutePoints(const TArray<FParadoxPuzzleWireSegment>& Segments, TArray<FVector>& OutPoints)
	{
		OutPoints.Reset();
		if (Segments.IsEmpty()) return;
		OutPoints.Add(Segments[0].Start);
		for (const FParadoxPuzzleWireSegment& Segment : Segments)
		{
			AddPoint(OutPoints, Segment.End);
		}
	}

	void CountCorners(FParadoxPuzzleWireRoute& Route)
	{
		Route.TopologyCornerCount = 0;
		Route.TerminalCornerCount = 0;
		Route.BridgeCornerCount = 0;
		Route.RenderedCornerCount = 0;
		for (int32 Index = 1; Index < Route.Segments.Num(); ++Index)
		{
			if (Route.Segments[Index - 1].Axis == Route.Segments[Index].Axis) continue;
			++Route.RenderedCornerCount;
			const bool bBridge = Route.Segments[Index - 1].Kind == EParadoxPuzzleWireSegmentKind::BridgeHorizontal
				|| Route.Segments[Index - 1].Kind == EParadoxPuzzleWireSegmentKind::BridgeVertical
				|| Route.Segments[Index].Kind == EParadoxPuzzleWireSegmentKind::BridgeHorizontal
				|| Route.Segments[Index].Kind == EParadoxPuzzleWireSegmentKind::BridgeVertical;
			const bool bTerminal = Route.Segments[Index - 1].Kind == EParadoxPuzzleWireSegmentKind::EndpointTerminal
				|| Route.Segments[Index].Kind == EParadoxPuzzleWireSegmentKind::EndpointTerminal;
			if (bBridge) ++Route.BridgeCornerCount;
			else
			{
				++Route.TopologyCornerCount;
				if (bTerminal) ++Route.TerminalCornerCount;
			}
		}
	}

	TArray<FParadoxPuzzleWireRoute> BuildFinalRoutes(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const TArray<FParadoxPuzzleRoutingLink>& Links,
		const TArray<TArray<FCandidate>>& Candidates,
		const TArray<int32>& Selection,
		const TArray<FBundleWork>& Bundles,
		const TMap<FBundleEdgeKey, int32>& EdgeToBundle,
		const TArray<FParadoxPuzzleWirePort>& SourcePorts,
		const TArray<FParadoxPuzzleWirePort>& TargetPorts,
		FParadoxPuzzleRoutingDiagnostics& Diagnostics)
	{
		TArray<FParadoxPuzzleWireRoute> Routes;
		for (int32 LinkIndex = 0; LinkIndex < Links.Num(); ++LinkIndex)
		{
			if (!Candidates[LinkIndex].IsValidIndex(Selection[LinkIndex])) continue;
			const FParadoxPuzzleRoutingLink& Link = Links[LinkIndex];
			const FCandidate& Candidate = Candidates[LinkIndex][Selection[LinkIndex]];
			FParadoxPuzzleWireRoute Route;
			Route.LinkHandle = Link.LinkHandle;
			Route.Direction = Link.Direction;
			Route.LinkKind = Link.LinkKind;
			Route.SourcePort = SourcePorts[LinkIndex];
			Route.TargetPort = TargetPorts[LinkIndex];
			Route.RoutingGeneration = Snapshot.RoutingGeneration;
			Route.bActive = Link.bActive;
			Route.bSignalValid = Link.bSignalValid;
			Route.GateMode = Link.GateMode;
			Route.bGateValid = Link.bGateValid;
			Route.bGateAllowsSignal = Link.bGateAllowsSignal;
			Route.bControllerResultValid = Link.bControllerResultValid;
			Route.bControllerResultActive = Link.bControllerResultActive;
			Route.StableOrder = Link.StableOrder;

			TArray<FParadoxPuzzleWireSegment> MainSegments;
			for (const FDirectedEdge& Edge : Candidate.Edges)
			{
				int32 BundleId = INDEX_NONE;
				int32 Lane = 0;
				double Offset = 0.0;
				if (const int32* Found = EdgeToBundle.Find({Edge.Key, Edge.bForward}))
				{
					BundleId = *Found;
					Lane = FindLane(Bundles[BundleId], LinkIndex);
					Offset = (static_cast<double>(Lane) - (Bundles[BundleId].Members.Num() - 1) * 0.5)
						* Snapshot.Settings.LaneSpacing;
					Route.BundleIds.AddUnique(BundleId);
				}
				FParadoxPuzzleWireSegment Segment;
				Segment.Start = GetNudgedStart(Edge, Snapshot.Settings, Offset);
				Segment.End = GetNudgedEnd(Edge, Snapshot.Settings, Offset);
				Segment.Axis = Edge.Axis;
				Segment.Kind = ResolveSegmentKind(Snapshot, Edge);
				Segment.Lane = Lane;
				Segment.BundleId = BundleId;
				Segment.NudgeOffset = Offset;
				if (!FMath::IsNearlyZero(Offset)) ++Diagnostics.NudgedSegmentCount;
				if (!MainSegments.IsEmpty() && !MainSegments.Last().End.Equals(Segment.Start, CoordinateTolerance))
				{
					AppendOrthogonalConnector(MainSegments.Last().End, Segment.Start, Lane, MainSegments);
				}
				MainSegments.Add(Segment);
			}

			if (MainSegments.IsEmpty())
			{
				const TArray<FVector> DirectPolyline = BuildCompletePolyline(
					Route.SourcePort,
					Route.TargetPort,
					Candidate.Points,
					Snapshot.Settings);
				for (int32 PointIndex = 1; PointIndex < DirectPolyline.Num(); ++PointIndex)
				{
					if (DirectPolyline[PointIndex - 1].Equals(DirectPolyline[PointIndex], CoordinateTolerance)) continue;
					FParadoxPuzzleWireSegment& Segment = Route.Segments.AddDefaulted_GetRef();
					Segment.Start = DirectPolyline[PointIndex - 1];
					Segment.End = DirectPolyline[PointIndex];
					Segment.Axis = GetVectorAxis(Segment.Start, Segment.End);
					Segment.Kind = EParadoxPuzzleWireSegmentKind::EndpointTerminal;
				}
				NormalizeSegments(Route.Segments);
				bool bValid = true;
				for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
				{
					bValid &= !SegmentEntersBounds(Segment.Start, Segment.End, Link.SourceBounds)
						&& !SegmentEntersBounds(Segment.Start, Segment.End, Link.TargetBounds);
				}
				if (!bValid)
				{
					++Diagnostics.RejectedEndpointInteriorCandidateCount;
					continue;
				}
				BuildRoutePoints(Route.Segments, Route.RoutePoints);
				CountCorners(Route);
				Routes.Add(MoveTemp(Route));
				continue;
			}
			const int32 SourceLane = MainSegments[0].Lane;
			const int32 TargetLane = MainSegments.Last().Lane;
			AppendTerminal(BuildOutwardTerminal(Route.SourcePort, MainSegments[0].Start), false, SourceLane, Route.Segments);
			Route.Segments.Append(MainSegments);
			AppendTerminal(BuildOutwardTerminal(Route.TargetPort, MainSegments.Last().End), true, TargetLane, Route.Segments);
			NormalizeSegments(Route.Segments);
			bool bValid = true;
			for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
			{
				bValid &= !SegmentEntersBounds(Segment.Start, Segment.End, Link.SourceBounds)
					&& !SegmentEntersBounds(Segment.Start, Segment.End, Link.TargetBounds);
			}
			if (!bValid)
			{
				++Diagnostics.RejectedEndpointInteriorCandidateCount;
				continue;
			}
			BuildRoutePoints(Route.Segments, Route.RoutePoints);
			CountCorners(Route);
			Routes.Add(MoveTemp(Route));
		}
		Routes.Sort([](const FParadoxPuzzleWireRoute& A, const FParadoxPuzzleWireRoute& B)
		{
			return A.StableOrder < B.StableOrder;
		});
		return Routes;
	}

	bool FindFirstCrossing(
		const FParadoxPuzzleWireRoute& Route,
		const TArray<FParadoxPuzzleWireRoute>& Previous,
		FVector& OutPoint)
	{
		for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
		{
			if (Segment.Kind == EParadoxPuzzleWireSegmentKind::EndpointTerminal) continue;
			for (const FParadoxPuzzleWireRoute& Other : Previous)
			{
				for (const FParadoxPuzzleWireSegment& OtherSegment : Other.Segments)
				{
					if (OtherSegment.Kind == EParadoxPuzzleWireSegmentKind::EndpointTerminal) continue;
					if (DoSegmentsCross(Segment.Start, Segment.End, OtherSegment.Start, OtherSegment.End, &OutPoint))
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	void AddBridge(
		FParadoxPuzzleWireRoute& Route,
		const FVector& Crossing,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		for (int32 Index = 0; Index < Route.Segments.Num(); ++Index)
		{
			const FParadoxPuzzleWireSegment Original = Route.Segments[Index];
			if (Original.Axis == EParadoxPuzzleWireAxis::Z) continue;
			const double Along = Original.Axis == EParadoxPuzzleWireAxis::X ? Crossing.X : Crossing.Y;
			const double Min = Original.Axis == EParadoxPuzzleWireAxis::X
				? FMath::Min(Original.Start.X, Original.End.X) : FMath::Min(Original.Start.Y, Original.End.Y);
			const double Max = Original.Axis == EParadoxPuzzleWireAxis::X
				? FMath::Max(Original.Start.X, Original.End.X) : FMath::Max(Original.Start.Y, Original.End.Y);
			if (Along <= Min + CoordinateTolerance || Along >= Max - CoordinateTolerance) continue;
			const FVector Direction = (Original.End - Original.Start).GetSafeNormal();
			const double Pitch = Original.Axis == EParadoxPuzzleWireAxis::X ? Settings.PitchX : Settings.PitchY;
			const double HalfSpan = FMath::Min(Pitch * 0.35, FVector::Distance(Original.Start, Original.End) * 0.25);
			const FVector BridgeStart = Crossing - Direction * HalfSpan;
			const FVector BridgeEnd = Crossing + Direction * HalfSpan;
			const FVector RaisedStart = BridgeStart + FVector(0.0, 0.0, Settings.BridgeHeightOffset);
			const FVector RaisedEnd = BridgeEnd + FVector(0.0, 0.0, Settings.BridgeHeightOffset);
			TArray<FParadoxPuzzleWireSegment> Replacement;
			const auto Add = [&Replacement, &Original](const FVector& Start, const FVector& End,
				const EParadoxPuzzleWireAxis Axis, const EParadoxPuzzleWireSegmentKind Kind)
			{
				if (Start.Equals(End, CoordinateTolerance)) return;
				FParadoxPuzzleWireSegment Segment = Original;
				Segment.Start = Start;
				Segment.End = End;
				Segment.Axis = Axis;
				Segment.Kind = Kind;
				Replacement.Add(Segment);
			};
			Add(Original.Start, BridgeStart, Original.Axis, Original.Kind);
			Add(BridgeStart, RaisedStart, EParadoxPuzzleWireAxis::Z, EParadoxPuzzleWireSegmentKind::BridgeVertical);
			Add(RaisedStart, RaisedEnd, Original.Axis, EParadoxPuzzleWireSegmentKind::BridgeHorizontal);
			Add(RaisedEnd, BridgeEnd, EParadoxPuzzleWireAxis::Z, EParadoxPuzzleWireSegmentKind::BridgeVertical);
			Add(BridgeEnd, Original.End, Original.Axis, Original.Kind);
			Route.Segments.RemoveAt(Index);
			for (int32 ReplacementIndex = Replacement.Num() - 1; ReplacementIndex >= 0; --ReplacementIndex)
			{
				Route.Segments.Insert(Replacement[ReplacementIndex], Index);
			}
			NormalizeSegments(Route.Segments);
			BuildRoutePoints(Route.Segments, Route.RoutePoints);
			CountCorners(Route);
			return;
		}
	}
}

FParadoxPuzzleRoutingResult CalculateOrderedBundleRoutes(
	const FParadoxPuzzleRoutingSnapshot& Snapshot)
{
	if (IsRoutingCancellationRequested())
	{
		return MakeCancelledRoutingResult(Snapshot);
	}
	using namespace OrderedBundles;
	const double StartSeconds = FPlatformTime::Seconds();
	FParadoxPuzzleRoutingResult Result;
	Result.RoutingGeneration = Snapshot.RoutingGeneration;
	Result.Diagnostics.Algorithm = EParadoxPuzzleRoutingAlgorithm::OrderedBundles;

	FParadoxPuzzleRoutingSnapshot Sanitized = Snapshot;
	Sanitized.Settings.PitchX = FMath::Max(1.0, Snapshot.Settings.PitchX);
	Sanitized.Settings.PitchY = FMath::Max(1.0, Snapshot.Settings.PitchY);
	Sanitized.Settings.MaxLanesPerEdge = FMath::Clamp(Snapshot.Settings.MaxLanesPerEdge, 1, 31);
	Sanitized.Settings.MaxRerouteAttempts = FMath::Clamp(Snapshot.Settings.MaxRerouteAttempts, 0, 16);
	Sanitized.Settings.MaxOrderedBundleCandidatesPerLink = FMath::Clamp(
		Snapshot.Settings.MaxOrderedBundleCandidatesPerLink, 36, 512);
	Sanitized.Settings.MaxBundleOptimizationPasses = FMath::Clamp(
		Snapshot.Settings.MaxBundleOptimizationPasses, 1, 16);
	Sanitized.Settings.MaxMetroOrderingPasses = FMath::Clamp(
		Snapshot.Settings.MaxMetroOrderingPasses, 0, 32);
	Sanitized.Settings.EndpointClearance = FMath::Max(0.0, Snapshot.Settings.EndpointClearance);
	Sanitized.Settings.MultiPortFanoutLength = FMath::Max(0.0, Snapshot.Settings.MultiPortFanoutLength);
	Sanitized.Settings.PortEdgeInset = FMath::Max(0.0, Snapshot.Settings.PortEdgeInset);
	Sanitized.Settings.BendPenalty = FMath::Max(0.0, Snapshot.Settings.BendPenalty);
	Sanitized.Settings.BundleReuseBonus = FMath::Max(0.0, Snapshot.Settings.BundleReuseBonus);

	TArray<FParadoxPuzzleRoutingLink> Links = Sanitized.Links;
	Links.Sort([](const FParadoxPuzzleRoutingLink& A, const FParadoxPuzzleRoutingLink& B)
	{
		if (A.StableOrder != B.StableOrder) return A.StableOrder < B.StableOrder;
		if (A.Direction != B.Direction) return static_cast<uint8>(A.Direction) < static_cast<uint8>(B.Direction);
		if (A.LinkKind != B.LinkKind) return static_cast<uint8>(A.LinkKind) < static_cast<uint8>(B.LinkKind);
		if (A.SourceBounds.EndpointKey != B.SourceBounds.EndpointKey)
		{
			return A.SourceBounds.EndpointKey < B.SourceBounds.EndpointKey;
		}
		if (A.TargetBounds.EndpointKey != B.TargetBounds.EndpointKey)
		{
			return A.TargetBounds.EndpointKey < B.TargetBounds.EndpointKey;
		}
		if (A.RemoteEndpointKey != B.RemoteEndpointKey) return A.RemoteEndpointKey < B.RemoteEndpointKey;
		if (!(A.Source == B.Source)) return A.Source < B.Source;
		return A.Target < B.Target;
	});

	TArray<TArray<FCandidate>> Candidates;
	Candidates.Reserve(Links.Num());
	for (const FParadoxPuzzleRoutingLink& Link : Links)
	{
		if (IsRoutingCancellationRequested())
		{
			return MakeCancelledRoutingResult(Snapshot);
		}
		Candidates.Add(BuildCandidatesForLink(Sanitized, Link, Result.Diagnostics));
	}
	TArray<int32> Selection;
	Selection.Init(INDEX_NONE, Links.Num());
	OptimizeSelection(Sanitized, Candidates, Selection, Result.Diagnostics);
	if (IsRoutingCancellationRequested())
	{
		return MakeCancelledRoutingResult(Snapshot);
	}

	TArray<FBundleWork> Bundles = ExtractBundles(Candidates, Selection, Sanitized.Settings.MaxLanesPerEdge);
	OrderBundles(Sanitized, Links, Candidates, Selection, Bundles, Result.Diagnostics);
	const TMap<FBundleEdgeKey, int32> EdgeToBundle = BuildBundleEdgeMap(Bundles);
	TArray<FParadoxPuzzleWirePort> SourcePorts;
	TArray<FParadoxPuzzleWirePort> TargetPorts;
	ResolveFinalPorts(Sanitized, Links, Candidates, Selection, Bundles, EdgeToBundle, SourcePorts, TargetPorts);
	Result.Routes = BuildFinalRoutes(
		Sanitized, Links, Candidates, Selection, Bundles, EdgeToBundle,
		SourcePorts, TargetPorts, Result.Diagnostics);

	TArray<FParadoxPuzzleWireRoute> AcceptedRoutes = Sanitized.PreservedRoutes;
	for (FParadoxPuzzleWireRoute& Route : Result.Routes)
	{
		FVector Crossing;
		if (FindFirstCrossing(Route, AcceptedRoutes, Crossing))
		{
			++Result.Diagnostics.CrossingCount;
			AddBridge(Route, Crossing, Sanitized.Settings);
			++Result.Diagnostics.BridgeCount;
			++Result.Diagnostics.CrossingsResolvedByBridge;
		}
		AcceptedRoutes.Add(Route);
	}
	if (!Sanitized.PreservedRoutes.IsEmpty())
	{
		Result.Routes.Append(Sanitized.PreservedRoutes);
		Result.Routes.Sort([](const FParadoxPuzzleWireRoute& A, const FParadoxPuzzleWireRoute& B)
		{
			return A.StableOrder < B.StableOrder;
		});
	}

	for (const FBundleWork& Work : Bundles)
	{
		FParadoxPuzzleWireBundle& Bundle = Result.Bundles.AddDefaulted_GetRef();
		Bundle.BundleId = Work.BundleId;
		Bundle.InversionsBeforeOrdering = Work.InversionsBefore;
		Bundle.InversionsAfterOrdering = Work.InversionsAfter;
		for (const int32 Member : Work.OrderedMembers)
		{
			Bundle.OrderedMembers.Add(Links[Member].LinkHandle);
		}
		for (const FDirectedEdge& Edge : Work.Edges)
		{
			FParadoxPuzzleWireSegment& Segment = Bundle.CenterlineSegments.AddDefaulted_GetRef();
			Segment.Start = ToLocalPoint(Edge.Start, Sanitized.Settings);
			Segment.End = ToLocalPoint(Edge.End, Sanitized.Settings);
			Segment.Axis = Edge.Axis;
			Segment.BundleId = Work.BundleId;
		}
		Result.Diagnostics.BundledUnitEdgeCount += Work.Edges.Num();
	}
	Result.Diagnostics.BundleCount = Result.Bundles.Num();

	for (int32 LinkIndex = 0; LinkIndex < Links.Num(); ++LinkIndex)
	{
		if (IsRoutingCancellationRequested())
		{
			return MakeCancelledRoutingResult(Snapshot);
		}
		if (!Candidates.IsValidIndex(LinkIndex) || !Candidates[LinkIndex].IsValidIndex(Selection[LinkIndex])) continue;
		double Bonus = 0.0;
		int32 Congestion = 0;
		const FGlobalCostContext Context = BuildGlobalCostContext(LinkIndex, Candidates, Selection);
		EvaluateGlobalCost(
			Candidates[LinkIndex][Selection[LinkIndex]], Context,
			Sanitized.Settings, &Bonus, &Congestion, nullptr);
		Result.Diagnostics.AppliedBundleReuseBonus += Bonus;
		Result.Diagnostics.CongestedUnitEdgeCount += Congestion;
		if (Sanitized.bCollectDebugData)
		{
			for (const FCandidate& Candidate : Candidates[LinkIndex])
			{
				FParadoxPuzzleFaceCandidateDebug& Debug = Result.FaceCandidates.AddDefaulted_GetRef();
				Debug.LinkHandle = Links[LinkIndex].LinkHandle;
				Debug.SourcePort = Candidate.SourcePort;
				Debug.TargetPort = Candidate.TargetPort;
				Debug.Cost = Candidate.BaseCost;
				Debug.bChosen = Candidate.StableOrder == Candidates[LinkIndex][Selection[LinkIndex]].StableOrder;
			}
		}
	}

	for (const FParadoxPuzzleWireRoute& Route : Result.Routes)
	{
		Result.Diagnostics.TotalTopologyCornerCount += Route.TopologyCornerCount;
		Result.Diagnostics.TotalTerminalCornerCount += Route.TerminalCornerCount;
		Result.Diagnostics.TotalBridgeCornerCount += Route.BridgeCornerCount;
		Result.Diagnostics.TotalRenderedCornerCount += Route.RenderedCornerCount;
		Result.Diagnostics.MaxRenderedCornerCount = FMath::Max(
			Result.Diagnostics.MaxRenderedCornerCount, Route.RenderedCornerCount);
	}
	Result.Diagnostics.RoutingMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	return Result;
}

namespace DistributedRepulsive
{
	using namespace OrderedBundles;

	struct FRepulsiveEdgeUse
	{
		int32 LinkIndex = INDEX_NONE;
		const FCandidate* Candidate = nullptr;
		int32 EdgeIndex = INDEX_NONE;
		const FDirectedEdge* Edge = nullptr;
	};

	struct FRepulsiveGlobalContext
	{
		TMap<FEdgeKey, TArray<FRepulsiveEdgeUse>> Occupancy;
		TMap<FIntPoint, TArray<FRepulsiveEdgeUse>> SpatialBuckets;
		TMap<FIntPoint, TArray<int32>> RouteBuckets;
		TArray<FRepulsiveEdgeUse> Edges;
		TArray<const FCandidate*> SelectedCandidates;
		int32 SpatialQueryCount = 0;
		int32 SpatialEdgeVisitCount = 0;
	};

	struct FRepulsiveEvaluation
	{
		double Cost = 0.0;
		double ProximityCost = 0.0;
		double HistoricalCost = 0.0;
		int32 Crossings = 0;
		int32 SharedUnitEdges = 0;
		int32 ParallelNearUnitEdges = 0;
	};

	struct FCandidateMetrics
	{
		double Length = 0.0;
		double VerticalLength = 0.0;
		double UnsupportedLength = 0.0;
		int32 Bends = 0;
	};

	struct FGenerationQuality
	{
		bool bValid = false;
		int32 Crossings = 0;
		int32 SharedUnitEdges = 0;
		double ProximityCost = 0.0;
		int32 BridgeEstimate = 0;
		int32 Bends = 0;
		double UnsupportedLength = 0.0;
		double Length = 0.0;
		TArray<int32> StableSelection;
	};

	FString GetStableEndpointKey(const FParadoxPuzzleRoutingLink& Link, const bool bSource)
	{
		const FParadoxPuzzleWireEndpointBounds& Bounds = bSource ? Link.SourceBounds : Link.TargetBounds;
		if (!Bounds.EndpointKey.IsEmpty())
		{
			return Bounds.EndpointKey;
		}
		const FParadoxPuzzleRoutingCoord& Coord = bSource ? Link.Source : Link.Target;
		return FString::Printf(
			TEXT("Point:%d:%d:%lld"),
			Coord.X,
			Coord.Y,
			static_cast<int64>(FMath::RoundToDouble(Coord.Z / HeightBucketSize)));
	}

	FString FindCommonEndpointKey(
		const FParadoxPuzzleRoutingLink& A,
		const FParadoxPuzzleRoutingLink& B)
	{
		const FString ASource = GetStableEndpointKey(A, true);
		const FString ATarget = GetStableEndpointKey(A, false);
		const FString BSource = GetStableEndpointKey(B, true);
		const FString BTarget = GetStableEndpointKey(B, false);
		if (ASource == BSource || ASource == BTarget) return ASource;
		if (ATarget == BSource || ATarget == BTarget) return ATarget;
		return FString();
	}

	int32 GetEdgeDistanceFromEndpoint(
		const FParadoxPuzzleRoutingLink& Link,
		const FCandidate& Candidate,
		const int32 EdgeIndex,
		const FString& EndpointKey)
	{
		if (EndpointKey == GetStableEndpointKey(Link, true))
		{
			return EdgeIndex;
		}
		if (EndpointKey == GetStableEndpointKey(Link, false))
		{
			return FMath::Max(0, Candidate.Edges.Num() - 1 - EdgeIndex);
		}
		return TNumericLimits<int32>::Max();
	}

	double GetEndpointEscapeScale(
		const FParadoxPuzzleRoutingSettings& Settings,
		const FParadoxPuzzleRoutingLink& Link,
		const FCandidate& Candidate,
		const int32 EdgeIndex,
		const FParadoxPuzzleRoutingLink& OtherLink,
		const FCandidate& OtherCandidate,
		const int32 OtherEdgeIndex)
	{
		if (Settings.EndpointEscapeDistance <= 0)
		{
			return 1.0;
		}
		const FString CommonEndpoint = FindCommonEndpointKey(Link, OtherLink);
		if (CommonEndpoint.IsEmpty())
		{
			return 1.0;
		}
		const int32 Distance = FMath::Min(
			GetEdgeDistanceFromEndpoint(Link, Candidate, EdgeIndex, CommonEndpoint),
			GetEdgeDistanceFromEndpoint(OtherLink, OtherCandidate, OtherEdgeIndex, CommonEndpoint));
		return FMath::Clamp(
			static_cast<double>(Distance) / static_cast<double>(Settings.EndpointEscapeDistance),
			0.0,
			1.0);
	}

	FCandidateMetrics CalculateCandidateMetrics(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const FCandidate& Candidate)
	{
		(void)Snapshot;
		FCandidateMetrics Metrics;
		Metrics.Bends = Candidate.BendCount;
		Metrics.Length = Candidate.Length;
		Metrics.VerticalLength = Candidate.VerticalLength;
		Metrics.UnsupportedLength = Candidate.UnsupportedLength;
		return Metrics;
	}

	double CalculateDistributedBaseCost(
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const FCandidate& Candidate)
	{
		const FCandidateMetrics Metrics = CalculateCandidateMetrics(Snapshot, Candidate);
		return Metrics.Length * Snapshot.Settings.LengthWeight
			+ Metrics.Bends * Snapshot.Settings.BendPenalty
			+ Metrics.UnsupportedLength * Snapshot.Settings.UnsupportedPenalty
			+ Metrics.VerticalLength * Snapshot.Settings.VerticalPenalty;
	}

	FIntPoint GetSpatialBucketKey(const FDirectedEdge& Edge)
	{
		return {
			FMath::Min(Edge.Start.X, Edge.End.X),
			FMath::Min(Edge.Start.Y, Edge.End.Y)};
	}

	void AddCandidateToRepulsiveContext(
		FRepulsiveGlobalContext& Context,
		const int32 LinkIndex,
		const FCandidate& Candidate)
	{
		if (!Context.SelectedCandidates.IsValidIndex(LinkIndex))
		{
			Context.SelectedCandidates.SetNum(LinkIndex + 1);
		}
		Context.SelectedCandidates[LinkIndex] = &Candidate;
		for (const FIntPoint& Cell : Candidate.SpatialCells)
		{
			Context.RouteBuckets.FindOrAdd(Cell).Add(LinkIndex);
		}
		for (int32 EdgeIndex = 0; EdgeIndex < Candidate.Edges.Num(); ++EdgeIndex)
		{
			const FDirectedEdge* Edge = &Candidate.Edges[EdgeIndex];
			const FRepulsiveEdgeUse Use{LinkIndex, &Candidate, EdgeIndex, Edge};
			Context.Occupancy.FindOrAdd(Edge->Key).Add(Use);
			Context.SpatialBuckets.FindOrAdd(GetSpatialBucketKey(*Edge)).Add(Use);
			Context.Edges.Add(Use);
		}
	}

	void RemoveCandidateFromRepulsiveContext(
		FRepulsiveGlobalContext& Context,
		const int32 LinkIndex)
	{
		const FCandidate* Previous = Context.SelectedCandidates.IsValidIndex(LinkIndex)
			? Context.SelectedCandidates[LinkIndex]
			: nullptr;
		if (!Previous)
		{
			return;
		}
		for (const FIntPoint& Cell : Previous->SpatialCells)
		{
			if (TArray<int32>* LinksInCell = Context.RouteBuckets.Find(Cell))
			{
				LinksInCell->Remove(LinkIndex);
				if (LinksInCell->IsEmpty())
				{
					Context.RouteBuckets.Remove(Cell);
				}
			}
		}
		for (const FDirectedEdge& Edge : Previous->Edges)
		{
			if (TArray<FRepulsiveEdgeUse>* Uses = Context.Occupancy.Find(Edge.Key))
			{
				Uses->RemoveAll([LinkIndex](const FRepulsiveEdgeUse& Use)
				{
					return Use.LinkIndex == LinkIndex;
				});
				if (Uses->IsEmpty())
				{
					Context.Occupancy.Remove(Edge.Key);
				}
			}
			const FIntPoint BucketKey = GetSpatialBucketKey(Edge);
			if (TArray<FRepulsiveEdgeUse>* Uses = Context.SpatialBuckets.Find(BucketKey))
			{
				Uses->RemoveAll([LinkIndex](const FRepulsiveEdgeUse& Use)
				{
					return Use.LinkIndex == LinkIndex;
				});
				if (Uses->IsEmpty())
				{
					Context.SpatialBuckets.Remove(BucketKey);
				}
			}
		}
		Context.Edges.RemoveAll([LinkIndex](const FRepulsiveEdgeUse& Use)
		{
			return Use.LinkIndex == LinkIndex;
		});
		Context.SelectedCandidates[LinkIndex] = nullptr;
	}

	void UpdateCandidateInRepulsiveContext(
		FRepulsiveGlobalContext& Context,
		const int32 LinkIndex,
		const FCandidate& Candidate)
	{
		RemoveCandidateFromRepulsiveContext(Context, LinkIndex);
		AddCandidateToRepulsiveContext(Context, LinkIndex, Candidate);
	}

	FRepulsiveGlobalContext BuildRepulsiveContext(
		const TArray<TArray<FCandidate>>& Candidates,
		const TArray<int32>& Selection)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleWire_ProximityField);
		FRepulsiveGlobalContext Context;
		Context.SelectedCandidates.SetNumZeroed(Selection.Num());
		for (int32 LinkIndex = 0; LinkIndex < Selection.Num(); ++LinkIndex)
		{
			if (!Candidates.IsValidIndex(LinkIndex)
				|| !Candidates[LinkIndex].IsValidIndex(Selection[LinkIndex]))
			{
				continue;
			}
			AddCandidateToRepulsiveContext(
				Context, LinkIndex, Candidates[LinkIndex][Selection[LinkIndex]]);
		}
		return Context;
	}

	bool GetParallelDistance(
		const FDirectedEdge& A,
		const FDirectedEdge& B,
		const FParadoxPuzzleRoutingSettings& Settings,
		int32& OutDistance)
	{
		if (A.Axis != B.Axis || A.Key == B.Key)
		{
			return false;
		}
		const double AZ = (A.Start.Z + A.End.Z) * 0.5;
		const double BZ = (B.Start.Z + B.End.Z) * 0.5;
		if (FMath::Abs(AZ - BZ) > Settings.VerticalProximityThreshold)
		{
			return false;
		}
		if (A.Axis == EParadoxPuzzleWireAxis::X)
		{
			if (FMath::Min(A.Start.X, A.End.X) != FMath::Min(B.Start.X, B.End.X)) return false;
			OutDistance = FMath::Abs(A.Start.Y - B.Start.Y);
		}
		else if (A.Axis == EParadoxPuzzleWireAxis::Y)
		{
			if (FMath::Min(A.Start.Y, A.End.Y) != FMath::Min(B.Start.Y, B.End.Y)) return false;
			OutDistance = FMath::Abs(A.Start.X - B.Start.X);
		}
		else
		{
			OutDistance = FMath::Abs(A.Start.X - B.Start.X) + FMath::Abs(A.Start.Y - B.Start.Y);
		}
		OutDistance = FMath::Max(1, OutDistance);
		return OutDistance <= Settings.ProximityRadius;
	}

	bool GetPerpendicularDistance(
		const FDirectedEdge& A,
		const FDirectedEdge& B,
		const FParadoxPuzzleRoutingSettings& Settings,
		int32& OutDistance)
	{
		if (A.Axis == B.Axis || A.Axis == EParadoxPuzzleWireAxis::Z || B.Axis == EParadoxPuzzleWireAxis::Z)
		{
			return false;
		}
		const double AZ = (A.Start.Z + A.End.Z) * 0.5;
		const double BZ = (B.Start.Z + B.End.Z) * 0.5;
		if (FMath::Abs(AZ - BZ) > Settings.VerticalProximityThreshold)
		{
			return false;
		}
		const double AX = (A.Start.X + A.End.X) * 0.5;
		const double AY = (A.Start.Y + A.End.Y) * 0.5;
		const double BX = (B.Start.X + B.End.X) * 0.5;
		const double BY = (B.Start.Y + B.End.Y) * 0.5;
		OutDistance = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(FMath::Square(AX - BX) + FMath::Square(AY - BY))));
		return OutDistance <= Settings.ProximityRadius;
	}

	double CalculateProximityFalloff(
		const FParadoxPuzzleRoutingSettings& Settings,
		const int32 Distance)
	{
		if (Settings.ProximityRadius <= 0 || Distance > Settings.ProximityRadius)
		{
			return 0.0;
		}
		const double Normalized = static_cast<double>(Settings.ProximityRadius - Distance + 1)
			/ static_cast<double>(Settings.ProximityRadius);
		return FMath::Pow(FMath::Clamp(Normalized, 0.0, 1.0), Settings.ProximityFalloffExponent);
	}

	void GatherPotentialCrossingLinks(
		const int32 LinkIndex,
		const FCandidate& Candidate,
		FRepulsiveGlobalContext& Context,
		TSet<int32>& OutLinkIndices,
		const int32 LinearScanThreshold)
	{
		if (Context.SelectedCandidates.Num() <= LinearScanThreshold)
		{
			for (int32 OtherIndex = 0; OtherIndex < Context.SelectedCandidates.Num(); ++OtherIndex)
			{
				if (OtherIndex != LinkIndex && Context.SelectedCandidates[OtherIndex])
				{
					OutLinkIndices.Add(OtherIndex);
				}
			}
			return;
		}

		++Context.SpatialQueryCount;
		for (const FIntPoint& Cell : Candidate.SpatialCells)
		{
			// One neighboring cell is included because terminal points may lie exactly
			// on a lattice boundary and be floored into the adjacent bucket.
			for (int32 Y = Cell.Y - 1; Y <= Cell.Y + 1; ++Y)
			{
				for (int32 X = Cell.X - 1; X <= Cell.X + 1; ++X)
				{
					if (const TArray<int32>* LinksInCell = Context.RouteBuckets.Find({X, Y}))
					{
						Context.SpatialEdgeVisitCount += LinksInCell->Num();
						for (const int32 OtherIndex : *LinksInCell)
						{
							if (OtherIndex != LinkIndex)
							{
								OutLinkIndices.Add(OtherIndex);
							}
						}
					}
				}
			}
		}
	}

	FRepulsiveEvaluation EvaluateRepulsiveCandidate(
		const int32 LinkIndex,
		const FCandidate& Candidate,
		FRepulsiveGlobalContext& Context,
		const TArray<FParadoxPuzzleRoutingLink>& Links,
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		const TMap<FEdgeKey, double>& History)
	{
		FRepulsiveEvaluation Evaluation;
		Evaluation.Cost = CalculateDistributedBaseCost(Snapshot, Candidate);
		TArray<bool> ParallelNearFlags;
		ParallelNearFlags.Init(false, Candidate.Edges.Num());

		TSet<int32> PotentialCrossingLinks;
		GatherPotentialCrossingLinks(
			LinkIndex,
			Candidate,
			Context,
			PotentialCrossingLinks,
			Snapshot.Settings.SpatialIndexLinkThreshold);
		for (const int32 OtherIndex : PotentialCrossingLinks)
		{
			const FCandidate* Other = Context.SelectedCandidates.IsValidIndex(OtherIndex)
				? Context.SelectedCandidates[OtherIndex]
				: nullptr;
			if (Other
				&& Candidate.LocalBounds.Intersect(Other->LocalBounds))
			{
				Evaluation.Crossings += CountCrossings(Candidate, *Other);
			}
		}

		for (int32 EdgeIndex = 0; EdgeIndex < Candidate.Edges.Num(); ++EdgeIndex)
		{
			const FDirectedEdge& Edge = Candidate.Edges[EdgeIndex];
			if (const TArray<FRepulsiveEdgeUse>* Uses = Context.Occupancy.Find(Edge.Key))
			{
				int32 Usage = 0;
				for (const FRepulsiveEdgeUse& Use : *Uses)
				{
					Usage += Use.LinkIndex != LinkIndex ? 1 : 0;
				}
				Evaluation.SharedUnitEdges += Usage;
				Evaluation.Cost += Snapshot.Settings.SharedEdgePenalty
					* static_cast<double>(Usage * (Usage + 1)) * 0.5;
			}
			if (const double* Historical = History.Find(Edge.Key))
			{
				const double Cost = *Historical * Snapshot.Settings.HistoricalCongestionWeight;
				Evaluation.HistoricalCost += Cost;
				Evaluation.Cost += Cost;
			}

			const auto VisitNearbyUse = [
				&Context,
				&Snapshot,
				&Links,
				&Candidate,
				&Edge,
				&Evaluation,
				&ParallelNearFlags,
				LinkIndex,
				EdgeIndex](const FRepulsiveEdgeUse& Use)
			{
				if (Use.LinkIndex == LinkIndex || !Use.Edge || !Use.Candidate
					|| !Links.IsValidIndex(Use.LinkIndex))
				{
					return;
				}
				++Context.SpatialEdgeVisitCount;
				int32 Distance = 0;
				const double EscapeScale = GetEndpointEscapeScale(
					Snapshot.Settings,
					Links[LinkIndex], Candidate, EdgeIndex,
					Links[Use.LinkIndex], *Use.Candidate, Use.EdgeIndex);
				if (GetParallelDistance(Edge, *Use.Edge, Snapshot.Settings, Distance))
				{
					const double Cost = Snapshot.Settings.ProximityPenalty
						* CalculateProximityFalloff(Snapshot.Settings, Distance)
						* EscapeScale;
					Evaluation.ProximityCost += Cost;
					ParallelNearFlags[EdgeIndex] = true;
				}
				else if (GetPerpendicularDistance(Edge, *Use.Edge, Snapshot.Settings, Distance))
				{
					Evaluation.ProximityCost += Snapshot.Settings.ProximityPenalty
						* Snapshot.Settings.PerpendicularProximityScale
						* CalculateProximityFalloff(Snapshot.Settings, Distance)
						* EscapeScale;
				}
			};

			if (Snapshot.Settings.ProximityRadius > 0)
			{
				++Context.SpatialQueryCount;
				if (Context.Edges.Num() <= Snapshot.Settings.SpatialIndexEdgeThreshold)
				{
					for (const FRepulsiveEdgeUse& Use : Context.Edges)
					{
						VisitNearbyUse(Use);
					}
				}
				else
				{
					const FIntPoint Center = GetSpatialBucketKey(Edge);
					const int32 SearchRadius = Snapshot.Settings.ProximityRadius + 1;
					for (int32 Y = Center.Y - SearchRadius; Y <= Center.Y + SearchRadius; ++Y)
					{
						for (int32 X = Center.X - SearchRadius; X <= Center.X + SearchRadius; ++X)
						{
							if (const TArray<FRepulsiveEdgeUse>* Uses = Context.SpatialBuckets.Find({X, Y}))
							{
								for (const FRepulsiveEdgeUse& Use : *Uses)
								{
									VisitNearbyUse(Use);
								}
							}
						}
					}
				}
			}
		}

		for (int32 Index = 0; Index < ParallelNearFlags.Num();)
		{
			if (!ParallelNearFlags[Index])
			{
				++Index;
				continue;
			}
			const EParadoxPuzzleWireAxis RunAxis = Candidate.Edges[Index].Axis;
			int32 RunLength = 0;
			while (Index < ParallelNearFlags.Num()
				&& ParallelNearFlags[Index]
				&& Candidate.Edges[Index].Axis == RunAxis)
			{
				++RunLength;
				++Index;
			}
			Evaluation.ParallelNearUnitEdges += RunLength;
			Evaluation.ProximityCost += Snapshot.Settings.ParallelRunPenalty
				* static_cast<double>(RunLength * (RunLength - 1)) * 0.5;
		}

		Evaluation.Cost += Evaluation.ProximityCost;
		Evaluation.Cost += Evaluation.Crossings
			* (Snapshot.Settings.CrossingPenalty + Snapshot.Settings.BridgePenalty);
		return Evaluation;
	}

	bool IsBetterRepulsiveCandidate(
		const FCandidate& Candidate,
		const double Cost,
		const FCandidate& Current,
		const double CurrentCost,
		const FParadoxPuzzleRoutingSnapshot& Snapshot)
	{
		if (!FMath::IsNearlyEqual(Cost, CurrentCost, CoordinateTolerance)) return Cost < CurrentCost;
		if (Candidate.BendCount != Current.BendCount) return Candidate.BendCount < Current.BendCount;
		const double CandidateLength = CalculateCandidateMetrics(Snapshot, Candidate).Length;
		const double CurrentLength = CalculateCandidateMetrics(Snapshot, Current).Length;
		if (!FMath::IsNearlyEqual(CandidateLength, CurrentLength, CoordinateTolerance))
		{
			return CandidateLength < CurrentLength;
		}
		if (Candidate.FacePairIndex != Current.FacePairIndex) return Candidate.FacePairIndex < Current.FacePairIndex;
		return Candidate.StableOrder < Current.StableOrder;
	}

	FGenerationQuality EvaluateGenerationQuality(
		const TArray<TArray<FCandidate>>& Candidates,
		const TArray<int32>& Selection,
		const TArray<FParadoxPuzzleRoutingLink>& Links,
		const FParadoxPuzzleRoutingSnapshot& Snapshot,
		FRepulsiveGlobalContext& Context)
	{
		FGenerationQuality Quality;
		Quality.bValid = true;
		TMap<FEdgeKey, double> NoHistory;
		for (int32 LinkIndex = 0; LinkIndex < Selection.Num(); ++LinkIndex)
		{
			if (!Candidates.IsValidIndex(LinkIndex) || !Candidates[LinkIndex].IsValidIndex(Selection[LinkIndex]))
			{
				Quality.bValid = false;
				continue;
			}
			const FCandidate& Candidate = Candidates[LinkIndex][Selection[LinkIndex]];
			const FCandidateMetrics Metrics = CalculateCandidateMetrics(Snapshot, Candidate);
			Quality.Bends += Metrics.Bends;
			Quality.UnsupportedLength += Metrics.UnsupportedLength;
			Quality.Length += Metrics.Length;
			Quality.StableSelection.Add(Candidate.StableOrder);
		}
		for (const TPair<FEdgeKey, TArray<FRepulsiveEdgeUse>>& Pair : Context.Occupancy)
		{
			Quality.SharedUnitEdges += FMath::Max(0, Pair.Value.Num() - 1);
		}
		Quality.Crossings = 0;
		for (int32 LinkIndex = 0; LinkIndex < Selection.Num(); ++LinkIndex)
		{
			if (!Candidates.IsValidIndex(LinkIndex) || !Candidates[LinkIndex].IsValidIndex(Selection[LinkIndex])) continue;
			const FRepulsiveEvaluation Evaluation = EvaluateRepulsiveCandidate(
				LinkIndex,
				Candidates[LinkIndex][Selection[LinkIndex]],
				Context,
				Links,
				Snapshot,
				NoHistory);
			Quality.ProximityCost += Evaluation.ProximityCost * 0.5;
			Quality.Crossings += Evaluation.Crossings;
		}
		// Every crossing is observed once from each participating route.
		Quality.Crossings /= 2;
		Quality.BridgeEstimate = Quality.Crossings;
		return Quality;
	}

	bool IsBetterQuality(const FGenerationQuality& A, const FGenerationQuality& B)
	{
		if (!A.bValid) return false;
		if (!B.bValid) return true;
		if (A.Crossings != B.Crossings) return A.Crossings < B.Crossings;
		if (A.SharedUnitEdges != B.SharedUnitEdges) return A.SharedUnitEdges < B.SharedUnitEdges;
		if (!FMath::IsNearlyEqual(A.ProximityCost, B.ProximityCost, CoordinateTolerance))
			return A.ProximityCost < B.ProximityCost;
		if (A.BridgeEstimate != B.BridgeEstimate) return A.BridgeEstimate < B.BridgeEstimate;
		if (A.Bends != B.Bends) return A.Bends < B.Bends;
		if (!FMath::IsNearlyEqual(A.UnsupportedLength, B.UnsupportedLength, CoordinateTolerance))
			return A.UnsupportedLength < B.UnsupportedLength;
		if (!FMath::IsNearlyEqual(A.Length, B.Length, CoordinateTolerance)) return A.Length < B.Length;
		const int32 SharedCount = FMath::Min(A.StableSelection.Num(), B.StableSelection.Num());
		for (int32 Index = 0; Index < SharedCount; ++Index)
		{
			if (A.StableSelection[Index] != B.StableSelection[Index])
			{
				return A.StableSelection[Index] < B.StableSelection[Index];
			}
		}
		return A.StableSelection.Num() < B.StableSelection.Num();
	}

	void UpdateHistory(
		const FRepulsiveGlobalContext& Context,
		TMap<FEdgeKey, double>& InOutHistory)
	{
		for (const TPair<FEdgeKey, TArray<FRepulsiveEdgeUse>>& Pair : Context.Occupancy)
		{
			if (Pair.Value.Num() > 1)
			{
				InOutHistory.FindOrAdd(Pair.Key) += static_cast<double>(Pair.Value.Num() - 1);
			}
		}
	}

	double GetRouteLength(const FParadoxPuzzleWireRoute& Route, const bool bUnsupportedOnly)
	{
		double Length = 0.0;
		for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
		{
			if (!bUnsupportedOnly || Segment.Kind == EParadoxPuzzleWireSegmentKind::GroundUnsupported)
			{
				Length += FVector::Distance(Segment.Start, Segment.End);
			}
		}
		return Length;
	}

	int32 CountRouteCrossings(
		const FParadoxPuzzleWireRoute& Route,
		const TArray<FParadoxPuzzleWireRoute>& Routes,
		const int32 RouteIndex)
	{
		int32 Count = 0;
		for (int32 OtherIndex = 0; OtherIndex < Routes.Num(); ++OtherIndex)
		{
			if (OtherIndex == RouteIndex) continue;
			for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
			{
				for (const FParadoxPuzzleWireSegment& Other : Routes[OtherIndex].Segments)
				{
					Count += DoSegmentsCross(Segment.Start, Segment.End, Other.Start, Other.End) ? 1 : 0;
				}
			}
		}
		return Count;
	}

	double CalculateRenderedProximity(
		const FParadoxPuzzleWireRoute& Route,
		const TArray<FParadoxPuzzleWireRoute>& Routes,
		const int32 RouteIndex,
		const FParadoxPuzzleRoutingSettings& Settings)
	{
		double Score = 0.0;
		for (int32 OtherIndex = 0; OtherIndex < Routes.Num(); ++OtherIndex)
		{
			if (OtherIndex == RouteIndex) continue;
			for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
			{
				if (Segment.Axis == EParadoxPuzzleWireAxis::Z) continue;
				for (const FParadoxPuzzleWireSegment& Other : Routes[OtherIndex].Segments)
				{
					if (Segment.Axis != Other.Axis || Other.Axis == EParadoxPuzzleWireAxis::Z
						|| FMath::Abs(Segment.Start.Z - Other.Start.Z) > Settings.VerticalProximityThreshold)
					{
						continue;
					}
					const bool bX = Segment.Axis == EParadoxPuzzleWireAxis::X;
					const double Overlap = FMath::Max(0.0,
						FMath::Min(bX ? FMath::Max(Segment.Start.X, Segment.End.X) : FMath::Max(Segment.Start.Y, Segment.End.Y),
							bX ? FMath::Max(Other.Start.X, Other.End.X) : FMath::Max(Other.Start.Y, Other.End.Y))
						- FMath::Max(bX ? FMath::Min(Segment.Start.X, Segment.End.X) : FMath::Min(Segment.Start.Y, Segment.End.Y),
							bX ? FMath::Min(Other.Start.X, Other.End.X) : FMath::Min(Other.Start.Y, Other.End.Y)));
					if (Overlap <= CoordinateTolerance) continue;
					const double Pitch = bX ? Settings.PitchY : Settings.PitchX;
					const double Lateral = FMath::Abs(
						(bX ? Segment.Start.Y : Segment.Start.X) - (bX ? Other.Start.Y : Other.Start.X));
					if (Lateral <= CoordinateTolerance)
					{
						Score += Settings.SharedEdgePenalty * Overlap / FMath::Max(1.0, bX ? Settings.PitchX : Settings.PitchY);
						continue;
					}
					const int32 Distance = FMath::Max(1, FMath::CeilToInt(Lateral / FMath::Max(1.0, Pitch)));
					Score += Settings.ProximityPenalty
						* CalculateProximityFalloff(Settings, Distance)
						* Overlap / FMath::Max(1.0, bX ? Settings.PitchX : Settings.PitchY);
				}
			}
		}
		return Score;
	}

	bool AvoidsEndpointBounds(
		const FParadoxPuzzleWireRoute& Route,
		const FParadoxPuzzleRoutingLink& Link)
	{
		for (const FParadoxPuzzleWireSegment& Segment : Route.Segments)
		{
			if (SegmentEntersBounds(Segment.Start, Segment.End, Link.SourceBounds)
				|| SegmentEntersBounds(Segment.Start, Segment.End, Link.TargetBounds))
			{
				return false;
			}
		}
		return true;
	}

	void ApplyRepulsiveNudging(
		const TArray<FParadoxPuzzleRoutingLink>& Links,
		const FParadoxPuzzleRoutingSettings& Settings,
		TArray<FParadoxPuzzleWireRoute>& InOutRoutes,
		FParadoxPuzzleRoutingDiagnostics& Diagnostics)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleWire_DistributedNudge);
		for (int32 RouteIndex = 0; RouteIndex < InOutRoutes.Num(); ++RouteIndex)
		{
			FParadoxPuzzleWireRoute& Route = InOutRoutes[RouteIndex];
			const FParadoxPuzzleRoutingLink* Link = Links.FindByPredicate([&Route](const FParadoxPuzzleRoutingLink& Candidate)
			{
				return Candidate.LinkHandle == Route.LinkHandle;
			});
			if (!Link) continue;
			for (int32 SegmentIndex = 1; SegmentIndex + 1 < Route.Segments.Num(); ++SegmentIndex)
			{
				const FParadoxPuzzleWireSegment& Segment = Route.Segments[SegmentIndex];
				if (Segment.Axis == EParadoxPuzzleWireAxis::Z
					|| Segment.Kind == EParadoxPuzzleWireSegmentKind::EndpointTerminal
					|| Segment.Kind == EParadoxPuzzleWireSegmentKind::BridgeHorizontal
					|| Segment.Kind == EParadoxPuzzleWireSegmentKind::BridgeVertical)
				{
					continue;
				}
				const EParadoxPuzzleWireAxis OffsetAxis = Segment.Axis == EParadoxPuzzleWireAxis::X
					? EParadoxPuzzleWireAxis::Y : EParadoxPuzzleWireAxis::X;
				if (Route.Segments[SegmentIndex - 1].Axis != OffsetAxis
					|| Route.Segments[SegmentIndex + 1].Axis != OffsetAxis)
				{
					continue;
				}
				const int32 OriginalCrossings = CountRouteCrossings(Route, InOutRoutes, RouteIndex);
				const double OriginalProximity = CalculateRenderedProximity(Route, InOutRoutes, RouteIndex, Settings);
				const double OriginalLength = GetRouteLength(Route, false);
				const double OriginalUnsupported = GetRouteLength(Route, true);
				FParadoxPuzzleWireRoute Best = Route;
				double BestProximity = OriginalProximity;
				for (int32 Lane = 1; Lane <= Settings.MaxLanesPerEdge; ++Lane)
				{
					for (const int32 Sign : {-1, 1})
					{
						const double Offset = Sign * Lane * Settings.LaneSpacing;
						const double MaxOffset = (OffsetAxis == EParadoxPuzzleWireAxis::X ? Settings.PitchX : Settings.PitchY) * 0.45;
						if (FMath::Abs(Offset) > MaxOffset + CoordinateTolerance) continue;
						FParadoxPuzzleWireRoute CandidateRoute = Route;
						FParadoxPuzzleWireSegment& Previous = CandidateRoute.Segments[SegmentIndex - 1];
						FParadoxPuzzleWireSegment& Shifted = CandidateRoute.Segments[SegmentIndex];
						FParadoxPuzzleWireSegment& Next = CandidateRoute.Segments[SegmentIndex + 1];
						if (OffsetAxis == EParadoxPuzzleWireAxis::X)
						{
							Shifted.Start.X += Offset;
							Shifted.End.X += Offset;
							Previous.End.X = Shifted.Start.X;
							Next.Start.X = Shifted.End.X;
						}
						else
						{
							Shifted.Start.Y += Offset;
							Shifted.End.Y += Offset;
							Previous.End.Y = Shifted.Start.Y;
							Next.Start.Y = Shifted.End.Y;
						}
						Shifted.NudgeOffset += Offset;
						NormalizeSegments(CandidateRoute.Segments);
						BuildRoutePoints(CandidateRoute.Segments, CandidateRoute.RoutePoints);
						CountCorners(CandidateRoute);
						if (CandidateRoute.RenderedCornerCount != Route.RenderedCornerCount
							|| CountRouteCrossings(CandidateRoute, InOutRoutes, RouteIndex) > OriginalCrossings
							|| GetRouteLength(CandidateRoute, false) > OriginalLength + CoordinateTolerance
							|| GetRouteLength(CandidateRoute, true) > OriginalUnsupported + CoordinateTolerance
							|| !AvoidsEndpointBounds(CandidateRoute, *Link))
						{
							continue;
						}
						const double Proximity = CalculateRenderedProximity(CandidateRoute, InOutRoutes, RouteIndex, Settings);
						if (Proximity + CoordinateTolerance < BestProximity)
						{
							Best = MoveTemp(CandidateRoute);
							BestProximity = Proximity;
						}
					}
				}
				if (BestProximity + CoordinateTolerance < OriginalProximity)
				{
					Route = MoveTemp(Best);
					++Diagnostics.NudgedSegmentCount;
				}
			}
		}
	}
}

FParadoxPuzzleRoutingResult CalculateDistributedRepulsiveRoutes(
	const FParadoxPuzzleRoutingSnapshot& Snapshot)
{
	if (IsRoutingCancellationRequested())
	{
		return MakeCancelledRoutingResult(Snapshot);
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleWire_DistributedRouting);
	using namespace OrderedBundles;
	using namespace DistributedRepulsive;
	const double StartSeconds = FPlatformTime::Seconds();
	FParadoxPuzzleRoutingResult Result;
	Result.RoutingGeneration = Snapshot.RoutingGeneration;
	Result.Diagnostics.Algorithm = EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive;

	FParadoxPuzzleRoutingSnapshot Sanitized = Snapshot;
	Sanitized.Settings.PitchX = FMath::Max(1.0, Snapshot.Settings.PitchX);
	Sanitized.Settings.PitchY = FMath::Max(1.0, Snapshot.Settings.PitchY);
	Sanitized.Settings.MaxLanesPerEdge = FMath::Clamp(Snapshot.Settings.MaxLanesPerEdge, 1, 31);
	Sanitized.Settings.MaxRerouteAttempts = FMath::Clamp(Snapshot.Settings.MaxRerouteAttempts, 0, 16);
	Sanitized.Settings.MaxDistributedCandidatesPerLink = FMath::Clamp(
		Snapshot.Settings.MaxDistributedCandidatesPerLink, 36, 512);
	Sanitized.Settings.MaxOrderedBundleCandidatesPerLink = Sanitized.Settings.MaxDistributedCandidatesPerLink;
	Sanitized.Settings.MaxNegotiationPasses = FMath::Clamp(Snapshot.Settings.MaxNegotiationPasses, 1, 16);
	Sanitized.Settings.EndpointClearance = FMath::Max(0.0, Snapshot.Settings.EndpointClearance);
	Sanitized.Settings.MultiPortFanoutLength = FMath::Max(0.0, Snapshot.Settings.MultiPortFanoutLength);
	Sanitized.Settings.PortEdgeInset = FMath::Max(0.0, Snapshot.Settings.PortEdgeInset);
	Sanitized.Settings.BendPenalty = FMath::Max(0.0, Snapshot.Settings.BendPenalty);
	Sanitized.Settings.LengthWeight = FMath::Max(0.0, Snapshot.Settings.LengthWeight);
	Sanitized.Settings.SharedEdgePenalty = FMath::Max(0.0, Snapshot.Settings.SharedEdgePenalty);
	Sanitized.Settings.HistoricalCongestionWeight = FMath::Max(0.0, Snapshot.Settings.HistoricalCongestionWeight);
	Sanitized.Settings.ProximityRadius = FMath::Clamp(Snapshot.Settings.ProximityRadius, 0, 12);
	Sanitized.Settings.ProximityPenalty = FMath::Max(0.0, Snapshot.Settings.ProximityPenalty);
	Sanitized.Settings.ProximityFalloffExponent = FMath::Clamp(Snapshot.Settings.ProximityFalloffExponent, 0.1, 8.0);
	Sanitized.Settings.ParallelRunPenalty = FMath::Max(0.0, Snapshot.Settings.ParallelRunPenalty);
	Sanitized.Settings.PerpendicularProximityScale = FMath::Clamp(Snapshot.Settings.PerpendicularProximityScale, 0.0, 1.0);
	Sanitized.Settings.EndpointEscapeDistance = FMath::Clamp(Snapshot.Settings.EndpointEscapeDistance, 0, 16);
	Sanitized.Settings.VerticalProximityThreshold = FMath::Max(0.0, Snapshot.Settings.VerticalProximityThreshold);
	Sanitized.Settings.SingleLinkFineFacePairLimit = FMath::Clamp(
		Snapshot.Settings.SingleLinkFineFacePairLimit, 1, 36);
	Sanitized.Settings.SubdividedFineFacePairLimit = FMath::Clamp(
		Snapshot.Settings.SubdividedFineFacePairLimit, 1, 36);
	Sanitized.Settings.BaseResolutionFineFacePairLimit = FMath::Clamp(
		Snapshot.Settings.BaseResolutionFineFacePairLimit, 1, 36);
	Sanitized.Settings.SpatialIndexLinkThreshold = FMath::Clamp(
		Snapshot.Settings.SpatialIndexLinkThreshold, 0, 128);
	Sanitized.Settings.SpatialIndexEdgeThreshold = FMath::Clamp(
		Snapshot.Settings.SpatialIndexEdgeThreshold, 0, 4096);

	TArray<FParadoxPuzzleRoutingLink> Links = Sanitized.Links;
	Links.Sort([](const FParadoxPuzzleRoutingLink& A, const FParadoxPuzzleRoutingLink& B)
	{
		if (A.StableOrder != B.StableOrder) return A.StableOrder < B.StableOrder;
		if (A.Direction != B.Direction) return static_cast<uint8>(A.Direction) < static_cast<uint8>(B.Direction);
		if (A.LinkKind != B.LinkKind) return static_cast<uint8>(A.LinkKind) < static_cast<uint8>(B.LinkKind);
		if (A.SourceBounds.EndpointKey != B.SourceBounds.EndpointKey) return A.SourceBounds.EndpointKey < B.SourceBounds.EndpointKey;
		if (A.TargetBounds.EndpointKey != B.TargetBounds.EndpointKey) return A.TargetBounds.EndpointKey < B.TargetBounds.EndpointKey;
		if (A.RemoteEndpointKey != B.RemoteEndpointKey) return A.RemoteEndpointKey < B.RemoteEndpointKey;
		if (!(A.Source == B.Source)) return A.Source < B.Source;
		return A.Target < B.Target;
	});

	TArray<TArray<FCandidate>> Candidates;
	Candidates.Reserve(Links.Num());
	const bool bSingleLinkFastPath = Links.Num() == 1
		&& Sanitized.Settings.bEnableSingleLinkFastPath;
	for (const FParadoxPuzzleRoutingLink& Link : Links)
	{
		if (IsRoutingCancellationRequested())
		{
			return MakeCancelledRoutingResult(Snapshot);
		}
		Candidates.Add(BuildCandidatesForLink(
			Sanitized,
			Link,
			Result.Diagnostics,
			Sanitized.Settings.bEnableHierarchicalFacePairPruning,
			bSingleLinkFastPath));
	}

	TArray<int32> Selection;
	Selection.Init(INDEX_NONE, Links.Num());
	for (int32 LinkIndex = 0; LinkIndex < Candidates.Num(); ++LinkIndex)
	{
		int32 BestIndex = INDEX_NONE;
		double BestCost = TNumericLimits<double>::Max();
		for (int32 CandidateIndex = 0; CandidateIndex < Candidates[LinkIndex].Num(); ++CandidateIndex)
		{
			const FCandidate& Candidate = Candidates[LinkIndex][CandidateIndex];
			if (Candidate.bDetour) continue;
			const double Cost = CalculateDistributedBaseCost(Sanitized, Candidate);
			if (BestIndex == INDEX_NONE || IsBetterRepulsiveCandidate(
				Candidate, Cost, Candidates[LinkIndex][BestIndex], BestCost, Sanitized))
			{
				BestIndex = CandidateIndex;
				BestCost = Cost;
			}
		}
		if (BestIndex == INDEX_NONE && !Candidates[LinkIndex].IsEmpty()) BestIndex = 0;
		Selection[LinkIndex] = BestIndex;
	}

	TArray<int32> BestSelection = Selection;
	TArray<int32> ReroutesPerLink;
	ReroutesPerLink.Init(0, Links.Num());
	FRepulsiveGlobalContext GlobalContext = BuildRepulsiveContext(Candidates, Selection);
	++Result.Diagnostics.RepulsiveContextBuildCount;
	FGenerationQuality BestQuality = EvaluateGenerationQuality(
		Candidates, Selection, Links, Sanitized, GlobalContext);
	const bool bHasRepulsiveConflict = BestQuality.Crossings > 0
			|| BestQuality.SharedUnitEdges > 0
			|| BestQuality.ProximityCost > CoordinateTolerance;
	const bool bNeedsNegotiation = Links.Num() > 1
		&& (!Sanitized.Settings.bEnableConflictFreeNegotiationSkip
			|| bHasRepulsiveConflict);
	if (!bNeedsNegotiation)
	{
		Result.Diagnostics.FastPathWireCount = Links.Num();
	}
	TMap<FEdgeKey, double> History;
	for (int32 Pass = 0; bNeedsNegotiation && Pass < Sanitized.Settings.MaxNegotiationPasses; ++Pass)
	{
		if (IsRoutingCancellationRequested())
		{
			return MakeCancelledRoutingResult(Snapshot);
		}
		TRACE_CPUPROFILER_EVENT_SCOPE(ParadoxPuzzleWire_NegotiationPass);
		Result.Diagnostics.NegotiationPassCount = Pass + 1;
		UpdateHistory(GlobalContext, History);
		bool bChanged = false;
		const int32 StartIndex = Links.IsEmpty() ? 0 : Pass % Links.Num();
		for (int32 Offset = 0; Offset < Links.Num(); ++Offset)
		{
			if (IsRoutingCancellationRequested())
			{
				return MakeCancelledRoutingResult(Snapshot);
			}
			const int32 LinkIndex = (StartIndex + Offset) % Links.Num();
			if (!Candidates.IsValidIndex(LinkIndex) || !Candidates[LinkIndex].IsValidIndex(Selection[LinkIndex])) continue;
			int32 BestIndex = Selection[LinkIndex];
			FRepulsiveEvaluation BestEvaluation = EvaluateRepulsiveCandidate(
				LinkIndex, Candidates[LinkIndex][BestIndex], GlobalContext, Links, Sanitized, History);
			for (int32 CandidateIndex = 0; CandidateIndex < Candidates[LinkIndex].Num(); ++CandidateIndex)
			{
				const FRepulsiveEvaluation Evaluation = EvaluateRepulsiveCandidate(
					LinkIndex, Candidates[LinkIndex][CandidateIndex], GlobalContext, Links, Sanitized, History);
				if (IsBetterRepulsiveCandidate(
					Candidates[LinkIndex][CandidateIndex], Evaluation.Cost,
					Candidates[LinkIndex][BestIndex], BestEvaluation.Cost,
					Sanitized))
				{
					BestIndex = CandidateIndex;
					BestEvaluation = Evaluation;
				}
			}
			if (BestIndex != Selection[LinkIndex])
			{
				Selection[LinkIndex] = BestIndex;
				UpdateCandidateInRepulsiveContext(
					GlobalContext, LinkIndex, Candidates[LinkIndex][BestIndex]);
				++ReroutesPerLink[LinkIndex];
				++Result.Diagnostics.ReroutedWireCount;
				bChanged = true;
			}
		}
		const FGenerationQuality Quality = EvaluateGenerationQuality(
			Candidates, Selection, Links, Sanitized, GlobalContext);
		if (IsBetterQuality(Quality, BestQuality))
		{
			BestQuality = Quality;
			BestSelection = Selection;
			Result.Diagnostics.BestNegotiationPass = Pass + 1;
		}
		if (!bChanged || (Quality.Crossings == 0 && Quality.SharedUnitEdges == 0
			&& Quality.ProximityCost <= CoordinateTolerance))
		{
			break;
		}
	}
	int32 PreviousSpatialQueryCount = 0;
	int32 PreviousSpatialEdgeVisitCount = 0;
	if (Selection != BestSelection)
	{
		PreviousSpatialQueryCount = GlobalContext.SpatialQueryCount;
		PreviousSpatialEdgeVisitCount = GlobalContext.SpatialEdgeVisitCount;
		Selection = BestSelection;
		GlobalContext = BuildRepulsiveContext(Candidates, Selection);
		++Result.Diagnostics.RepulsiveContextBuildCount;
	}

	// Remaining exact sharing is a valid narrow-corridor fallback. Reuse only the
	// lane-order/nudge representation; it never participates in repulsive route selection.
	TArray<FBundleWork> FallbackBundles = ExtractBundles(Candidates, Selection, Sanitized.Settings.MaxLanesPerEdge);
	FParadoxPuzzleRoutingSnapshot PortSnapshot = Sanitized;
	PortSnapshot.Settings.MaxMetroOrderingPasses = 8;
	OrderBundles(PortSnapshot, Links, Candidates, Selection, FallbackBundles, Result.Diagnostics);
	const TMap<FBundleEdgeKey, int32> EdgeToBundle = BuildBundleEdgeMap(FallbackBundles);
	TArray<FParadoxPuzzleWirePort> SourcePorts;
	TArray<FParadoxPuzzleWirePort> TargetPorts;
	ResolveFinalPorts(PortSnapshot, Links, Candidates, Selection, FallbackBundles, EdgeToBundle, SourcePorts, TargetPorts);
	Result.Routes = BuildFinalRoutes(
		PortSnapshot, Links, Candidates, Selection, FallbackBundles, EdgeToBundle,
		SourcePorts, TargetPorts, Result.Diagnostics);
	if (bNeedsNegotiation)
	{
		ApplyRepulsiveNudging(Links, Sanitized.Settings, Result.Routes, Result.Diagnostics);
	}

	TArray<FParadoxPuzzleWireRoute> AcceptedRoutes = Sanitized.PreservedRoutes;
	for (FParadoxPuzzleWireRoute& Route : Result.Routes)
	{
		FVector Crossing;
		if (FindFirstCrossing(Route, AcceptedRoutes, Crossing))
		{
			++Result.Diagnostics.CrossingCount;
			AddBridge(Route, Crossing, Sanitized.Settings);
			++Result.Diagnostics.BridgeCount;
			++Result.Diagnostics.CrossingsResolvedByBridge;
		}
		AcceptedRoutes.Add(Route);
	}

	for (const FBundleWork& Work : FallbackBundles)
	{
		FParadoxPuzzleWireBundle& Bundle = Result.Bundles.AddDefaulted_GetRef();
		Bundle.BundleId = Work.BundleId;
		Bundle.InversionsBeforeOrdering = Work.InversionsBefore;
		Bundle.InversionsAfterOrdering = Work.InversionsAfter;
		for (const int32 Member : Work.OrderedMembers)
		{
			Bundle.OrderedMembers.Add(Links[Member].LinkHandle);
		}
		for (const FDirectedEdge& Edge : Work.Edges)
		{
			FParadoxPuzzleWireSegment& Segment = Bundle.CenterlineSegments.AddDefaulted_GetRef();
			Segment.Start = ToLocalPoint(Edge.Start, Sanitized.Settings);
			Segment.End = ToLocalPoint(Edge.End, Sanitized.Settings);
			Segment.Axis = Edge.Axis;
			Segment.BundleId = Work.BundleId;
		}
	}
	Result.Diagnostics.BundleCount = Result.Bundles.Num();
	Result.Diagnostics.SharedUnitEdgeLength = BestQuality.SharedUnitEdges;
	Result.Diagnostics.ParallelNearUnitEdgeLength = 0;
	Result.Diagnostics.TotalProximityCost = BestQuality.ProximityCost;

	TMap<FEdgeKey, TArray<FRepulsiveEdgeUse>> FinalUsage;
	for (int32 LinkIndex = 0; LinkIndex < Selection.Num(); ++LinkIndex)
	{
		if (IsRoutingCancellationRequested())
		{
			return MakeCancelledRoutingResult(Snapshot);
		}
		if (!Candidates.IsValidIndex(LinkIndex) || !Candidates[LinkIndex].IsValidIndex(Selection[LinkIndex])) continue;
		const FCandidate& Candidate = Candidates[LinkIndex][Selection[LinkIndex]];
		const FRepulsiveEvaluation Evaluation = EvaluateRepulsiveCandidate(
			LinkIndex, Candidate, GlobalContext, Links, Sanitized, History);
		Result.Diagnostics.ParallelNearUnitEdgeLength += Evaluation.ParallelNearUnitEdges;
		Result.Diagnostics.TotalHistoricalCongestionCost += Evaluation.HistoricalCost;
		if (Sanitized.bCollectDebugData)
		{
			FParadoxPuzzleWireConflictDebug& Conflict = Result.WireConflicts.AddDefaulted_GetRef();
			Conflict.LinkHandle = Links[LinkIndex].LinkHandle;
			Conflict.ConflictScore = Evaluation.Cost - CalculateDistributedBaseCost(Sanitized, Candidate);
			Conflict.SharedUnitEdgeCount = Evaluation.SharedUnitEdges;
			Conflict.ParallelNearUnitEdgeCount = Evaluation.ParallelNearUnitEdges;
			Conflict.CrossingCount = Evaluation.Crossings;
			Conflict.RerouteCount = ReroutesPerLink[LinkIndex];
			for (int32 CandidateIndex = 0; CandidateIndex < Candidates[LinkIndex].Num(); ++CandidateIndex)
			{
				FParadoxPuzzleFaceCandidateDebug& Debug = Result.FaceCandidates.AddDefaulted_GetRef();
				Debug.LinkHandle = Links[LinkIndex].LinkHandle;
				Debug.SourcePort = Candidates[LinkIndex][CandidateIndex].SourcePort;
				Debug.TargetPort = Candidates[LinkIndex][CandidateIndex].TargetPort;
				Debug.Cost = EvaluateRepulsiveCandidate(
					LinkIndex, Candidates[LinkIndex][CandidateIndex], GlobalContext, Links, Sanitized, History).Cost;
				Debug.bChosen = CandidateIndex == Selection[LinkIndex];
			}
		}
		for (int32 EdgeIndex = 0; EdgeIndex < Candidate.Edges.Num(); ++EdgeIndex)
		{
			const FDirectedEdge& Edge = Candidate.Edges[EdgeIndex];
			FinalUsage.FindOrAdd(Edge.Key).Add({LinkIndex, &Candidate, EdgeIndex, &Edge});
		}
	}
	Result.Diagnostics.SpatialQueryCount = PreviousSpatialQueryCount + GlobalContext.SpatialQueryCount;
	Result.Diagnostics.SpatialEdgeVisitCount = PreviousSpatialEdgeVisitCount + GlobalContext.SpatialEdgeVisitCount;
	// Every near pair is observed once from each participating route.
	Result.Diagnostics.ParallelNearUnitEdgeLength /= 2;
	for (const TPair<FEdgeKey, TArray<FRepulsiveEdgeUse>>& Pair : FinalUsage)
	{
		Result.Diagnostics.MaxEdgeUsageCount = FMath::Max(Result.Diagnostics.MaxEdgeUsageCount, Pair.Value.Num());
		if (Sanitized.bCollectDebugData && !Pair.Value.IsEmpty() && Pair.Value[0].Edge)
		{
			FParadoxPuzzleCongestionEdgeDebug& Debug = Result.CongestionEdges.AddDefaulted_GetRef();
			Debug.Start = Pair.Value[0].Edge->Start;
			Debug.End = Pair.Value[0].Edge->End;
			Debug.NegotiationPass = Result.Diagnostics.BestNegotiationPass;
			Debug.UsageCount = Pair.Value.Num();
			Debug.HistoricalCongestion = History.FindRef(Pair.Key);
			for (const TPair<FEdgeKey, TArray<FRepulsiveEdgeUse>>& OtherPair : FinalUsage)
			{
				if (OtherPair.Key == Pair.Key || OtherPair.Value.IsEmpty() || !OtherPair.Value[0].Edge)
				{
					continue;
				}
				int32 Distance = 0;
				if (GetParallelDistance(*Pair.Value[0].Edge, *OtherPair.Value[0].Edge, Sanitized.Settings, Distance))
				{
					Debug.ProximityCost += Sanitized.Settings.ProximityPenalty
						* CalculateProximityFalloff(Sanitized.Settings, Distance);
				}
				else if (GetPerpendicularDistance(*Pair.Value[0].Edge, *OtherPair.Value[0].Edge, Sanitized.Settings, Distance))
				{
					Debug.ProximityCost += Sanitized.Settings.ProximityPenalty
						* Sanitized.Settings.PerpendicularProximityScale
						* CalculateProximityFalloff(Sanitized.Settings, Distance);
				}
			}
		}
	}

	if (!Sanitized.PreservedRoutes.IsEmpty())
	{
		Result.Routes.Append(Sanitized.PreservedRoutes);
		Result.Routes.Sort([](const FParadoxPuzzleWireRoute& A, const FParadoxPuzzleWireRoute& B)
		{
			return A.StableOrder < B.StableOrder;
		});
	}
	for (const FParadoxPuzzleWireRoute& Route : Result.Routes)
	{
		Result.Diagnostics.TotalTopologyCornerCount += Route.TopologyCornerCount;
		Result.Diagnostics.TotalTerminalCornerCount += Route.TerminalCornerCount;
		Result.Diagnostics.TotalBridgeCornerCount += Route.BridgeCornerCount;
		Result.Diagnostics.TotalRenderedCornerCount += Route.RenderedCornerCount;
		Result.Diagnostics.MaxRenderedCornerCount = FMath::Max(
			Result.Diagnostics.MaxRenderedCornerCount, Route.RenderedCornerCount);
	}
	Result.Diagnostics.RoutingMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	return Result;
}
}
