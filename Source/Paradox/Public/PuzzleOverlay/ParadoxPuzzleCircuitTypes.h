#pragma once

#include "CoreMinimal.h"
#include "Graph/PuzzleGraphTypes.h"
#include "ParadoxPuzzleCircuitTypes.generated.h"

UENUM(BlueprintType)
enum class EParadoxPuzzleWireDirection : uint8
{
	Input,
	Output
};

UENUM(BlueprintType)
enum class EParadoxPuzzleWireAxis : uint8
{
	X,
	Y,
	Z
};

UENUM(BlueprintType)
enum class EParadoxPuzzleWireSegmentKind : uint8
{
	GroundSupported,
	GroundUnsupported,
	StructuralVertical,
	BridgeHorizontal,
	BridgeVertical,
	EndpointTerminal
};

UENUM(BlueprintType)
enum class EParadoxPuzzleRoutingAlgorithm : uint8
{
	OrderedBundles,
	LegacyIndependent UMETA(Deprecated, DisplayName = "Legacy Independent (Deprecated)"),
	DistributedRepulsive UMETA(DisplayName = "Distributed Repulsive")
};

/** Chooses where the pure routing solve runs. World queries and rendering always remain on the Game Thread. */
UENUM(BlueprintType)
enum class EParadoxPuzzleRoutingExecutionMode : uint8
{
	Standard,
	MultiThreaded UMETA(DisplayName = "Multi-Thread")
};

/** Terminal state of one wire-calculation request. */
UENUM(BlueprintType)
enum class EParadoxPuzzleWireCalculationCompletionStatus : uint8
{
	Applied,
	AppliedFromCache,
	Cancelled,
	Superseded,
	Failed
};

/** Divides each GridWorld cell into a finer visual routing lattice without changing GridWorld itself. */
UENUM(BlueprintType)
enum class EParadoxPuzzleRoutingSubdivision : uint8
{
	/** One routing cell for each GridWorld cell. */
	None UMETA(DisplayName = "None (1 x 1)"),
	/** Two routing cells per axis: one GridWorld cell becomes four visual routing cells. */
	TwoByTwo UMETA(DisplayName = "2 x 2 (4 Sub-cells)"),
	/** Four routing cells per axis: one GridWorld cell becomes sixteen visual routing cells. */
	FourByFour UMETA(DisplayName = "4 x 4 (16 Sub-cells)"),
	/** Eight routing cells per axis: one GridWorld cell becomes sixty-four visual routing cells. */
	EightByEight UMETA(DisplayName = "8 x 8 (64 Sub-cells)")
};

/** Converts the designer-facing subdivision preset to its per-axis integer divisor. */
FORCEINLINE constexpr int32 GetParadoxPuzzleRoutingSubdivisionFactor(
	const EParadoxPuzzleRoutingSubdivision Subdivision)
{
	switch (Subdivision)
	{
	case EParadoxPuzzleRoutingSubdivision::TwoByTwo:
		return 2;
	case EParadoxPuzzleRoutingSubdivision::FourByFour:
		return 4;
	case EParadoxPuzzleRoutingSubdivision::EightByEight:
		return 8;
	case EParadoxPuzzleRoutingSubdivision::None:
	default:
		return 1;
	}
}

UENUM(BlueprintType)
enum class EParadoxPuzzlePortSide : uint8
{
	North,
	South,
	East,
	West,
	PositiveZ,
	NegativeZ
};

UENUM(BlueprintType)
enum class EParadoxPuzzleWireBoxSource : uint8
{
	PointFallback,
	VisibleMeshes,
	CustomWireTarget
};

/** Axis-aligned endpoint bounds expressed in routing-frame local centimetres. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxPuzzleWireEndpointBounds
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	FString EndpointKey;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	FVector Min = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	FVector Max = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	bool bValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	bool bFromWireTarget = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	bool bPointFallback = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	EParadoxPuzzleWireBoxSource Source = EParadoxPuzzleWireBoxSource::PointFallback;

	FVector GetCenter() const { return (Min + Max) * 0.5; }
	FVector GetExtent() const { return (Max - Min) * 0.5; }
};

/** Resolved boundary port and outward-facing terminal clearance point. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxPuzzleWirePort
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	FParadoxPuzzleWireEndpointBounds Bounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	EParadoxPuzzlePortSide Side = EParadoxPuzzlePortSide::East;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	FVector Normal = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	FVector ClearancePoint = FVector::ZeroVector;

	/** Zero-based spatial slot on the resolved face, ordered from its negative tangent edge. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	int32 FaceSlotIndex = INDEX_NONE;

	/** Number of incident links distributed across the resolved face. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	int32 FaceSlotCount = 1;

	/** Absolute distance from the face centre normalized to its usable half-span. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	double NormalizedDistanceFromFaceCenter = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	bool bValid = false;
};

/** Integer X/Y coordinate in the visual lattice plus a real local-frame elevation. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxPuzzleRoutingCoord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	int32 X = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	int32 Y = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	double Z = 0.0;

	bool operator==(const FParadoxPuzzleRoutingCoord& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z;
	}

	bool operator<(const FParadoxPuzzleRoutingCoord& Other) const
	{
		return Z != Other.Z ? Z < Other.Z : (Y != Other.Y ? Y < Other.Y : X < Other.X);
	}
};

FORCEINLINE uint32 GetTypeHash(const FParadoxPuzzleRoutingCoord& Coord)
{
	return HashCombineFast(
		HashCombineFast(GetTypeHash(Coord.X), GetTypeHash(Coord.Y)),
		GetTypeHash(Coord.Z));
}

/** Height-aware cache identity. HeightBucket prevents floors at the same X/Y from aliasing. */
struct PARADOX_API FParadoxPuzzleSurfaceKey
{
	int32 X = 0;
	int32 Y = 0;
	int32 HeightBucket = 0;

	bool operator==(const FParadoxPuzzleSurfaceKey& Other) const
	{
		return X == Other.X && Y == Other.Y && HeightBucket == Other.HeightBucket;
	}
};

FORCEINLINE uint32 GetTypeHash(const FParadoxPuzzleSurfaceKey& Key)
{
	return HashCombineFast(
		HashCombineFast(GetTypeHash(Key.X), GetTypeHash(Key.Y)),
		GetTypeHash(Key.HeightBucket));
}

struct PARADOX_API FParadoxPuzzleSurfaceSample
{
	bool bHasSurface = false;
	double SurfaceZ = 0.0;
	FVector Normal = FVector::UpVector;
	bool bFromGridWorld = false;
};

USTRUCT(BlueprintType)
struct PARADOX_API FParadoxPuzzleWireSegment
{
	GENERATED_BODY()

	/** Start in routing-frame local centimetres. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	FVector Start = FVector::ZeroVector;

	/** End in routing-frame local centimetres. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	FVector End = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	EParadoxPuzzleWireAxis Axis = EParadoxPuzzleWireAxis::X;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	EParadoxPuzzleWireSegmentKind Kind = EParadoxPuzzleWireSegmentKind::GroundUnsupported;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	int32 Lane = 0;

	/** Ordered-bundle identity for this segment, or INDEX_NONE outside a shared corridor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Bundle")
	int32 BundleId = INDEX_NONE;

	/** Signed lateral offset applied by the orthogonal nudging pass. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Bundle")
	double NudgeOffset = 0.0;
};

/** One maximal shared ordered corridor. Link identity remains independent inside the bundle. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxPuzzleWireBundle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Bundle")
	int32 BundleId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Bundle")
	TArray<FPuzzleGraphLinkHandle> OrderedMembers;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Bundle")
	TArray<FParadoxPuzzleWireSegment> CenterlineSegments;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Bundle")
	int32 InversionsBeforeOrdering = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Bundle")
	int32 InversionsAfterOrdering = 0;
};

USTRUCT(BlueprintType)
struct PARADOX_API FParadoxPuzzleWireRoute
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	FPuzzleGraphLinkHandle LinkHandle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	EParadoxPuzzleWireDirection Direction = EParadoxPuzzleWireDirection::Input;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	EPuzzleGraphLinkKind LinkKind = EPuzzleGraphLinkKind::PrimarySignal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	FParadoxPuzzleWirePort SourcePort;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Endpoint")
	FParadoxPuzzleWirePort TargetPort;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	TArray<FVector> RoutePoints;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	TArray<FParadoxPuzzleWireSegment> Segments;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Bundle")
	TArray<int32> BundleIds;

	/** Non-bridge direction changes in the final normalized route, including terminals. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Diagnostics")
	int32 TopologyCornerCount = 0;

	/** Topology corners touching an endpoint terminal segment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Diagnostics")
	int32 TerminalCornerCount = 0;

	/** Direction changes introduced by anti-crossing bridge geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Diagnostics")
	int32 BridgeCornerCount = 0;

	/** All visible direction changes in Segments. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Diagnostics")
	int32 RenderedCornerCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	int64 RoutingGeneration = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay")
	bool bActive = false;

	/** Effective-primary validity for PrimarySignal, gate-input validity for GateInfluence. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|State")
	bool bSignalValid = false;

	/** Aggregated gate metadata retained for inspection; meaningful for GateInfluence routes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|State")
	EPuzzleGraphGateMode GateMode = EPuzzleGraphGateMode::Invalid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|State")
	bool bGateValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|State")
	bool bGateAllowsSignal = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|State")
	bool bControllerResultValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|State")
	bool bControllerResultActive = false;

	int32 StableOrder = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct PARADOX_API FParadoxPuzzleRoutingSettings
{
	GENERATED_BODY()

	/** Routing strategy. Distributed Repulsive is the production default, Ordered Bundles remains available for bundled layouts, and Legacy Independent remains available only for deprecated A/B rollback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Routing")
	EParadoxPuzzleRoutingAlgorithm Algorithm = EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive;

	/**
	 * Visual routing resolution relative to the selected GridWorld region. None uses one routing cell
	 * per GridWorld cell; 2 x 2 turns a 100 x 100 cm GridWorld cell into four 50 x 50 cm routing cells.
	 * This affects only circuit-wire routing and never subdivides navigation, occupancy, or presentation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Geometry")
	EParadoxPuzzleRoutingSubdivision GridCellSubdivision = EParadoxPuzzleRoutingSubdivision::None;

	/** Runtime-only X lattice pitch derived from GridWorld CellSize / GridCellSubdivision. Not authored or serialized. */
	double PitchX = 100.0;

	/** Runtime-only Y lattice pitch derived from GridWorld CellSize / GridCellSubdivision. Not authored or serialized. */
	double PitchY = 100.0;

	/** Centre-to-centre separation, in centimetres, between unavoidable shared-wire lanes. Ordered Bundles uses it for bundles; Distributed Repulsive uses it for final fallback lanes and bounded nudging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Geometry", meta = (ClampMin = "0.0"))
	double LaneSpacing = 8.0;

	/** Maximum number of wires that may receive distinct lanes on one directed lattice edge. Ordered Bundles also treats additional users as congestion without bundle credit; Distributed Repulsive uses the same limit for nudge attempts and exact-sharing fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Geometry", meta = (ClampMin = "1"))
	int32 MaxLanesPerEdge = 4;

	/** Vertical rise, in centimetres, used by the final anti-crossing bridge fallback. This does not affect ordinary StructuralVertical elevation changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Geometry", meta = (ClampMin = "1.0"))
	double BridgeHeightOffset = 25.0;

	/** LegacyIndependent only: maximum number of retained candidates per link. Other strategies expose their own candidate budgets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Legacy", meta = (ClampMin = "1", ClampMax = "64", DeprecatedProperty, DeprecationMessage = "Used only by LegacyIndependent.", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::LegacyIndependent", EditConditionHides))
	int32 MaxCandidatesPerLink = 24;

	/** Bounded detour/search depth shared by all strategies. Ordered Bundles uses alternate corridors and crossing retries; Distributed Repulsive uses bounded detour candidates; Legacy Independent uses local reroute attempts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Routing", meta = (ClampMin = "0", ClampMax = "16"))
	int32 MaxRerouteAttempts = 4;

	/** Hard per-link cap after Ordered Bundles candidate diversity and cost pruning. At least one candidate is retained for every source/target face pair. Higher values cost more CPU and memory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Ordered Bundles", meta = (ClampMin = "36", ClampMax = "512", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::OrderedBundles", EditConditionHides))
	int32 MaxOrderedBundleCandidatesPerLink = 128;

	/** Maximum deterministic global coordinate-descent passes used to reconsider route and face choices after bundle occupancy changes. Routing stops earlier when a pass makes no change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Ordered Bundles", meta = (ClampMin = "1", ClampMax = "16", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::OrderedBundles", EditConditionHides))
	int32 MaxBundleOptimizationPasses = 4;

	/** Maximum adjacent-swap passes used to reduce lane-order inversions at bundle merges and splits. A swap is accepted only when it lowers the inversion objective. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Ordered Bundles", meta = (ClampMin = "0", ClampMax = "32", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::OrderedBundles", EditConditionHides))
	int32 MaxMetroOrderingPasses = 8;

	/** Ordered Bundles and Distributed Repulsive: added cost for every direction change in a complete candidate, including terminal raccordi. Increase to prefer straighter routes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Routing", meta = (ClampMin = "0.0", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::OrderedBundles || Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	double BendPenalty = 100.0;

	/** Cost discount for each compatible directed unit edge reused by a candidate. It is applied once per edge, not once per existing member. Increase to form longer bundles; excessive values can favor detours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Ordered Bundles", meta = (ClampMin = "0.0", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::OrderedBundles", EditConditionHides))
	double BundleReuseBonus = 35.0;

	/** Distributed Repulsive only: hard cap on retained candidates for each link after all 36 source/target face pairs have contributed at least one valid candidate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "36", ClampMax = "512", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	int32 MaxDistributedCandidatesPerLink = 128;

	/** Distributed Repulsive only: maximum bounded rip-up/reroute negotiation passes. The solve stops earlier when no selection changes or all conflicts disappear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "1", ClampMax = "16", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	int32 MaxNegotiationPasses = 6;

	/** Distributed Repulsive only: multiplier applied to route length in centimetres. One preserves the project's existing one-cost-unit-per-centimetre convention. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "0.0", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	double LengthWeight = 1.0;

	/** Distributed Repulsive only: cost for each unit lattice edge already used by another wire. Usage grows triangularly, strongly discouraging sharing without making it impossible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "0.0", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	double SharedEdgePenalty = 10000.0;

	/** Distributed Repulsive only: cost per accumulated history unit on an edge that stayed shared in earlier passes of the current solve. History is discarded after the generation completes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "0.0", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	double HistoricalCongestionWeight = 2000.0;

	/** Distributed Repulsive only: lateral distance, measured in lattice cells, within which another edge contributes proximity cost. Beyond this radius there is no further separation reward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "0", ClampMax = "12", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	int32 ProximityRadius = 3;

	/** Distributed Repulsive only: cost per nearby parallel unit edge at distance one before falloff and endpoint escape scaling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "0.0", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	double ProximityPenalty = 250.0;

	/** Distributed Repulsive only: exponent of the discrete proximity falloff. Values above one concentrate repulsion near a wire; values below one spread it across the whole radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "0.1", ClampMax = "8.0", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	double ProximityFalloffExponent = 1.5;

	/** Distributed Repulsive only: super-linear cost applied to every maximal close parallel run, so a long cluster costs more than several isolated near passes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "0.0", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	double ParallelRunPenalty = 25.0;

	/** Distributed Repulsive only: scale applied to brief perpendicular proximity. Keep below one so a near approach costs less than an equally long parallel cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides, AdvancedDisplay))
	double PerpendicularProximityScale = 0.25;

	/** Distributed Repulsive only: number of unit edges from a shared endpoint over which proximity ramps from zero to full strength, allowing clean fan-out and fan-in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "0", ClampMax = "16", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	int32 EndpointEscapeDistance = 2;

	/** Distributed Repulsive only: maximum elevation difference, in centimetres, for two edges to repel. The default still relates the low bridge layer while separating structural floors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides))
	double VerticalProximityThreshold = 50.0;

	/** Distributed Repulsive only: ranks all 36 face pairs at the base GridWorld resolution, then generates fine-lattice candidates only for the best-ranked pairs. Disable to maximize face-choice quality at a substantially higher CPU and memory cost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive|Optimization", meta = (EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides, AdvancedDisplay))
	bool bEnableHierarchicalFacePairPruning = true;

	/** Distributed Repulsive only: maximum face pairs solved at fine resolution when the graph has one wire. Set to 36 to retain every source/target face combination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive|Optimization", meta = (ClampMin = "1", ClampMax = "36", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive && bEnableHierarchicalFacePairPruning", EditConditionHides, AdvancedDisplay))
	int32 SingleLinkFineFacePairLimit = 8;

	/** Distributed Repulsive only: maximum fine-resolution face pairs per wire when GridWorld cells are subdivided. Higher values preserve more aesthetic alternatives but increase solve time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive|Optimization", meta = (ClampMin = "1", ClampMax = "36", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive && bEnableHierarchicalFacePairPruning", EditConditionHides, AdvancedDisplay))
	int32 SubdividedFineFacePairLimit = 12;

	/** Distributed Repulsive only: maximum fine-resolution face pairs per wire without GridWorld subdivision. Set to 36 for full face-pair quality. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive|Optimization", meta = (ClampMin = "1", ClampMax = "36", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive && bEnableHierarchicalFacePairPruning", EditConditionHides, AdvancedDisplay))
	int32 BaseResolutionFineFacePairLimit = 18;

	/** Distributed Repulsive only: for a one-wire graph, skips detour candidates and retains the shortest fine candidates. Disable when comparing every authored detour alternative for aesthetic tuning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive|Optimization", meta = (EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides, AdvancedDisplay))
	bool bEnableSingleLinkFastPath = true;

	/** Distributed Repulsive only: skips negotiation when the independently selected generation has no crossings, sharing, or proximity conflict. This is result-preserving because no repulsive conflict remains to optimize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive|Optimization", meta = (EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides, AdvancedDisplay))
	bool bEnableConflictFreeNegotiationSkip = true;

	/** Distributed Repulsive only: below or at this wire count, crossing candidates are scanned directly instead of through spatial buckets. This performance threshold must not change route geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive|Optimization", meta = (ClampMin = "0", ClampMax = "128", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides, AdvancedDisplay))
	int32 SpatialIndexLinkThreshold = 8;

	/** Distributed Repulsive only: below or at this occupied-edge count, proximity uses a linear scan instead of spatial buckets. This performance threshold must not change route geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Distributed Repulsive|Optimization", meta = (ClampMin = "0", ClampMax = "4096", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive", EditConditionHides, AdvancedDisplay))
	int32 SpatialIndexEdgeThreshold = 64;

	/** @deprecated Serialized compatibility for LegacyIndependent. Ordered Bundles and Distributed Repulsive use BendPenalty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Ordered Bundles and Distributed Repulsive use BendPenalty.", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::LegacyIndependent", EditConditionHides))
	double CornerPenalty = 25.0;

	/** Added cost per unsupported centimetre of horizontal wire when no acceptable GridWorld cell or trace surface exists. Increase to prefer supported floor routes; geometry is still allowed over voids. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Routing", meta = (ClampMin = "0.0"))
	double UnsupportedPenalty = 4.0;

	/** Congestion cost for each existing edge use encountered beyond compatible free-lane reuse. Increase to spread bundles across different corridors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Routing", meta = (ClampMin = "0.0", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::LegacyIndependent || Algorithm == EParadoxPuzzleRoutingAlgorithm::OrderedBundles", EditConditionHides))
	double LanePenalty = 10.0;

	/** Cost per predicted planar crossing during global selection. Increase to prefer non-crossing candidates within the bounded candidate set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Routing", meta = (ClampMin = "0.0"))
	double CrossingPenalty = 100000.0;

	/** Additional cost per centimetre of vertical travel. Horizontal and vertical distance already pay ordinary length cost; this value expresses the extra preference for staying on one elevation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Routing", meta = (ClampMin = "0.0"))
	double VerticalPenalty = 2.0;

	/** Additional estimated cost per unresolved planar crossing that may require the final Z bridge fallback. BridgeHeightOffset controls its geometry; this value controls avoidance pressure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Routing", meta = (ClampMin = "0.0"))
	double BridgePenalty = 5000.0;

	/** @deprecated Compact-route promotion is disabled; routing always uses the minimum valid corner tier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Compact-route promotion is disabled. Routing always uses the minimum valid corner tier.", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::LegacyIndependent", EditConditionHides))
	int32 MaxCompactRouteExtraCorners = 0;

	/** @deprecated Retained only for serialized compatibility with the disabled compact-route experiment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Compact-route promotion is disabled.", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::LegacyIndependent", EditConditionHides))
	double CompactRouteMinDeviation = 300.0;

	/** @deprecated Retained only for serialized compatibility with the disabled compact-route experiment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Compact-route promotion is disabled.", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::LegacyIndependent", EditConditionHides))
	double CompactRouteMinImprovementRatio = 0.35;

	/** @deprecated Endpoint bounds and EndpointClearance now define every terminal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Use endpoint bounds, EndpointClearance and PortEdgeInset.", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::LegacyIndependent", EditConditionHides))
	double SelectedPortRadius = 60.0;

	/** Straight distance, in centimetres, travelled outward from every endpoint face before the route may turn. Increase when bends visually crowd the Actor boundary. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Ports", meta = (ClampMin = "0.0"))
	double EndpointClearance = 25.0;

	/** Additional straight fan-out, in centimetres, applied only to faces carrying multiple links. It keeps N+1 ports separated before their routes are allowed to merge into a bundle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Ports", meta = (ClampMin = "0.0"))
	double MultiPortFanoutLength = 75.0;

	/** @deprecated Ports now use N+1 distribution over the usable face span. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "Port spacing is derived from the usable face span and incident wire count.", EditCondition = "Algorithm == EParadoxPuzzleRoutingAlgorithm::LegacyIndependent", EditConditionHides))
	double PortSpacing = 16.0;

	/** Minimum distance, in centimetres, kept between an outer port and either edge of its face. N+1 distribution uses a larger equal margin when the face has enough room. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Puzzle Overlay|Ports", meta = (ClampMin = "0.0"))
	double PortEdgeInset = 8.0;
};

/** One copied graph relationship. All coordinates are in the immutable routing frame. */
struct PARADOX_API FParadoxPuzzleRoutingLink
{
	FPuzzleGraphLinkHandle LinkHandle;
	EParadoxPuzzleWireDirection Direction = EParadoxPuzzleWireDirection::Input;
	EPuzzleGraphLinkKind LinkKind = EPuzzleGraphLinkKind::PrimarySignal;
	FParadoxPuzzleRoutingCoord Source;
	FParadoxPuzzleRoutingCoord Target;
	FParadoxPuzzleWireEndpointBounds SourceBounds;
	FParadoxPuzzleWireEndpointBounds TargetBounds;
	FParadoxPuzzleWirePort SourcePort;
	FParadoxPuzzleWirePort TargetPort;
	FString RemoteEndpointKey;
	int32 StableOrder = INDEX_NONE;
	bool bActive = false;
	bool bSignalValid = false;
	EPuzzleGraphGateMode GateMode = EPuzzleGraphGateMode::Invalid;
	bool bGateValid = false;
	bool bGateAllowsSignal = false;
	bool bControllerResultValid = false;
	bool bControllerResultActive = false;
};

/** Plain-data routing input. It is safe to copy into a worker task. */
struct PARADOX_API FParadoxPuzzleRoutingSnapshot
{
	int64 RoutingGeneration = 0;
	FParadoxPuzzleRoutingCoord SelectedAnchor;
	FParadoxPuzzleRoutingSettings Settings;
	TArray<FParadoxPuzzleRoutingLink> Links;
	TMap<FParadoxPuzzleSurfaceKey, FParadoxPuzzleSurfaceSample> SurfaceSamples;
	/** Existing routes treated as immutable occupancy during a localized endpoint reroute. */
	TArray<FParadoxPuzzleWireRoute> PreservedRoutes;
	/** Enables optional pure-data routing debug records. Kept false unless both renderer debug gates are active. */
	bool bCollectDebugData = false;
};

struct PARADOX_API FParadoxPuzzleFaceCandidateDebug
{
	FPuzzleGraphLinkHandle LinkHandle;
	FParadoxPuzzleWirePort SourcePort;
	FParadoxPuzzleWirePort TargetPort;
	double Cost = 0.0;
	bool bChosen = false;
};

/** Optional per-edge negotiated-congestion record, populated only when routing debug collection is enabled. */
struct PARADOX_API FParadoxPuzzleCongestionEdgeDebug
{
	FParadoxPuzzleRoutingCoord Start;
	FParadoxPuzzleRoutingCoord End;
	int32 NegotiationPass = 0;
	int32 UsageCount = 0;
	double HistoricalCongestion = 0.0;
	double ProximityCost = 0.0;
};

/** Optional per-link conflict record for the Distributed Repulsive strategy. */
struct PARADOX_API FParadoxPuzzleWireConflictDebug
{
	FPuzzleGraphLinkHandle LinkHandle;
	double ConflictScore = 0.0;
	int32 SharedUnitEdgeCount = 0;
	int32 ParallelNearUnitEdgeCount = 0;
	int32 CrossingCount = 0;
	int32 RerouteCount = 0;
};

struct PARADOX_API FParadoxPuzzleRoutingDiagnostics
{
	EParadoxPuzzleRoutingAlgorithm Algorithm = EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive;
	int32 CandidateCount = 0;
	int32 CrossingCount = 0;
	int32 RerouteAttempts = 0;
	int32 BridgeCount = 0;
	int32 TotalTopologyCornerCount = 0;
	int32 TotalTerminalCornerCount = 0;
	int32 TotalBridgeCornerCount = 0;
	int32 TotalRenderedCornerCount = 0;
	int32 MaxRenderedCornerCount = 0;
	int32 SurfaceCacheHits = 0;
	int32 SurfaceCacheMisses = 0;
	int32 RejectedEndpointInteriorCandidateCount = 0;
	int32 RejectedNetworkBoundsCandidateCount = 0;
	int32 NetworkBoundsFallbackCount = 0;
	int32 BundleCount = 0;
	int32 BundleOptimizationPassCount = 0;
	int32 MetroOrderingPassCount = 0;
	int32 BundledUnitEdgeCount = 0;
	int32 CongestedUnitEdgeCount = 0;
	int32 NudgedSegmentCount = 0;
	int32 InversionsBeforeOrdering = 0;
	int32 InversionsAfterOrdering = 0;
	int32 CrossingsResolvedByReroute = 0;
	int32 CrossingsResolvedByOrdering = 0;
	int32 CrossingsResolvedByBridge = 0;
	double AppliedBundleReuseBonus = 0.0;
	int32 NegotiationPassCount = 0;
	int32 BestNegotiationPass = 0;
	int32 ReroutedWireCount = 0;
	int32 RepulsiveContextBuildCount = 0;
	int32 SpatialQueryCount = 0;
	int32 SpatialEdgeVisitCount = 0;
	int32 HierarchicalCoarseFacePairCount = 0;
	int32 PrunedFineFacePairCount = 0;
	int32 FastPathWireCount = 0;
	int32 SharedUnitEdgeLength = 0;
	int32 ParallelNearUnitEdgeLength = 0;
	int32 MaxEdgeUsageCount = 0;
	double TotalProximityCost = 0.0;
	double TotalHistoricalCongestionCost = 0.0;
	double RoutingMilliseconds = 0.0;
};

struct PARADOX_API FParadoxPuzzleRoutingResult
{
	int64 RoutingGeneration = 0;
	TArray<FParadoxPuzzleWireRoute> Routes;
	TArray<FParadoxPuzzleWireBundle> Bundles;
	TArray<FParadoxPuzzleFaceCandidateDebug> FaceCandidates;
	TArray<FParadoxPuzzleCongestionEdgeDebug> CongestionEdges;
	TArray<FParadoxPuzzleWireConflictDebug> WireConflicts;
	FParadoxPuzzleRoutingDiagnostics Diagnostics;
	/** Set by cooperative cancellation checkpoints. Cancelled results are never cached or rendered. */
	bool bCancelled = false;
};

/** Immutable identity and execution policy reported when one wire calculation starts. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxPuzzleWireCalculationContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	FGuid RequestId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	int64 RoutingGeneration = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	TWeakObjectPtr<AActor> TargetActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	EParadoxPuzzleRoutingAlgorithm Algorithm = EParadoxPuzzleRoutingAlgorithm::DistributedRepulsive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	EParadoxPuzzleRoutingExecutionMode ExecutionMode = EParadoxPuzzleRoutingExecutionMode::MultiThreaded;
};

/** Terminal timings and disposition for one started wire calculation. All values are reported on the Game Thread. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxPuzzleWireCalculationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	FParadoxPuzzleWireCalculationContext Context;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	EParadoxPuzzleWireCalculationCompletionStatus Status = EParadoxPuzzleWireCalculationCompletionStatus::Failed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	int32 RouteCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	double PreparationMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	double QueueMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	double SolveMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	double ApplyMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	double TotalMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Puzzle Overlay|Calculation")
	bool bResultApplied = false;
};
