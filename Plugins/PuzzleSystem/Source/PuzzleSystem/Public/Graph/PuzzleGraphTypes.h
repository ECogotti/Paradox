#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Misc/Guid.h"
#include "Receivers/PuzzleReceiverTypes.h"
#include "PuzzleGraphTypes.generated.h"

class APuzzleController;
class UPuzzleEmitterComponent;
class UPuzzleGraphSubsystem;
class UPuzzleReceiverComponent;
class UPuzzleSignalPayload;

/** Semantic role of one runtime puzzle graph relationship. */
UENUM(BlueprintType)
enum class EPuzzleGraphLinkKind : uint8
{
	PrimarySignal,
	GateInfluence
};

/** Aggregated admission mode for one Controller-local primary input gate. */
UENUM(BlueprintType)
enum class EPuzzleGraphGateMode : uint8
{
	Bypassed,
	Open,
	Closed,
	Invalid
};

/** Structural operation represented by a graph topology notification. */
UENUM(BlueprintType)
enum class EPuzzleGraphTopologyChangeKind : uint8
{
	Added,
	Removed,
	Refreshed
};

/** Opaque identity for one runtime graph relationship. Handles are not persistent across worlds or saves. */
USTRUCT(BlueprintType)
struct PUZZLESYSTEM_API FPuzzleGraphLinkHandle
{
	GENERATED_BODY()

public:
	FPuzzleGraphLinkHandle() = default;

	bool IsValid() const { return LinkId.IsValid(); }

	friend bool operator==(const FPuzzleGraphLinkHandle& Left, const FPuzzleGraphLinkHandle& Right)
	{
		return Left.LinkId == Right.LinkId;
	}

	friend bool operator!=(const FPuzzleGraphLinkHandle& Left, const FPuzzleGraphLinkHandle& Right)
	{
		return !(Left == Right);
	}

	friend uint32 GetTypeHash(const FPuzzleGraphLinkHandle& Handle)
	{
		return GetTypeHash(Handle.LinkId);
	}

private:
	UPROPERTY()
	FGuid LinkId;

	explicit FPuzzleGraphLinkHandle(const FGuid& InLinkId)
		: LinkId(InLinkId)
	{
	}

	friend class UPuzzleGraphSubsystem;
};

/** Read-only topology snapshot for one contextual puzzle relationship. */
USTRUCT(BlueprintType)
struct PUZZLESYSTEM_API FPuzzleGraphLink
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph")
	FPuzzleGraphLinkHandle LinkHandle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph")
	EPuzzleGraphLinkKind LinkKind = EPuzzleGraphLinkKind::PrimarySignal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph")
	TWeakObjectPtr<APuzzleController> Controller;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Primary")
	FName PrimaryInputId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Primary")
	FGameplayTag PrimarySignalTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Primary")
	TWeakObjectPtr<AActor> PrimaryEmitterActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Primary")
	TWeakObjectPtr<UPuzzleEmitterComponent> PrimaryEmitterComponent;

	/** Populated only by PrimarySignal links. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Receiver")
	TWeakObjectPtr<AActor> TargetReceiverActor;

	/** Populated only by PrimarySignal links. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Receiver")
	TWeakObjectPtr<UPuzzleReceiverComponent> TargetReceiverComponent;

	/** Populated only by GateInfluence links. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Gate")
	FName GateInputId;

	/** Populated only by GateInfluence links. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Gate")
	FGameplayTag GateSignalTag;

	/** Populated only by GateInfluence links. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Gate")
	TWeakObjectPtr<AActor> GateEmitterActor;

	/** Populated only by GateInfluence links. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Gate")
	TWeakObjectPtr<UPuzzleEmitterComponent> GateEmitterComponent;
};

/** Read-only runtime state snapshot kept separate from graph topology. */
USTRUCT(BlueprintType)
struct PUZZLESYSTEM_API FPuzzleGraphLinkState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Primary")
	bool bRawPrimaryValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Primary")
	bool bRawPrimaryActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Primary")
	int64 RawPrimaryRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Primary")
	TWeakObjectPtr<UPuzzleSignalPayload> RawPrimaryPayload;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Gate")
	EPuzzleGraphGateMode GateMode = EPuzzleGraphGateMode::Invalid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Gate")
	bool bGateValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Gate")
	bool bGateAllowsSignal = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Effective")
	bool bEffectivePrimaryValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Effective")
	bool bEffectivePrimaryActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Effective")
	int64 EffectivePrimaryRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Effective")
	TWeakObjectPtr<UPuzzleSignalPayload> EffectivePrimaryPayload;

	/** Meaningful only for GateInfluence links. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Gate Input")
	bool bGateInputValid = false;

	/** Meaningful only for GateInfluence links. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Gate Input")
	bool bGateInputActive = false;

	/** Meaningful only for GateInfluence links. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Gate Input")
	int64 GateInputRevision = 0;

	/** Meaningful only for GateInfluence links. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Gate Input")
	TWeakObjectPtr<UPuzzleSignalPayload> GateInputPayload;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Controller")
	bool bControllerResultValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Controller")
	bool bControllerResultActive = false;

	/** Meaningful only for PrimarySignal links. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Receiver")
	bool bTargetReceiverValid = false;

	/** Meaningful only for PrimarySignal links with a valid Receiver endpoint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Receiver")
	EPuzzleReceiverActivationMode TargetReceiverActivationMode =
		EPuzzleReceiverActivationMode::Automatic;

	/** OR-aggregated Controller authorization for the target Receiver. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Receiver")
	bool bTargetReceiverPrerequisitesSatisfied = false;

	/** Explicit activation latch used only when the target Receiver is in Manual mode. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Receiver")
	bool bTargetReceiverManualActivationRequested = false;

	/** Snapshot read on demand; normal Receiver aggregation changes do not independently emit graph state events. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph|Receiver")
	bool bTargetReceiverEffectiveActive = false;

	bool operator==(const FPuzzleGraphLinkState& Other) const
	{
		return bRawPrimaryValid == Other.bRawPrimaryValid
			&& bRawPrimaryActive == Other.bRawPrimaryActive
			&& RawPrimaryRevision == Other.RawPrimaryRevision
			&& RawPrimaryPayload == Other.RawPrimaryPayload
			&& GateMode == Other.GateMode
			&& bGateValid == Other.bGateValid
			&& bGateAllowsSignal == Other.bGateAllowsSignal
			&& bEffectivePrimaryValid == Other.bEffectivePrimaryValid
			&& bEffectivePrimaryActive == Other.bEffectivePrimaryActive
			&& EffectivePrimaryRevision == Other.EffectivePrimaryRevision
			&& EffectivePrimaryPayload == Other.EffectivePrimaryPayload
			&& bGateInputValid == Other.bGateInputValid
			&& bGateInputActive == Other.bGateInputActive
			&& GateInputRevision == Other.GateInputRevision
			&& GateInputPayload == Other.GateInputPayload
			&& bControllerResultValid == Other.bControllerResultValid
			&& bControllerResultActive == Other.bControllerResultActive
			&& bTargetReceiverValid == Other.bTargetReceiverValid
			&& TargetReceiverActivationMode == Other.TargetReceiverActivationMode
			&& bTargetReceiverPrerequisitesSatisfied == Other.bTargetReceiverPrerequisitesSatisfied
			&& bTargetReceiverManualActivationRequested == Other.bTargetReceiverManualActivationRequested
			&& bTargetReceiverEffectiveActive == Other.bTargetReceiverEffectiveActive;
	}

	bool operator!=(const FPuzzleGraphLinkState& Other) const
	{
		return !(*this == Other);
	}
};

/** Actor-centric read-only graph snapshot, grouped without losing component identity. */
USTRUCT(BlueprintType)
struct PUZZLESYSTEM_API FPuzzleActorGraphView
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph")
	int64 GraphTopologyRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph")
	TArray<FPuzzleGraphLink> IncomingPrimaryLinks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph")
	TArray<FPuzzleGraphLink> IncomingGateLinks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph")
	TArray<FPuzzleGraphLink> OutgoingPrimaryLinks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Graph")
	TArray<FPuzzleGraphLink> OutgoingGateLinks;
};
