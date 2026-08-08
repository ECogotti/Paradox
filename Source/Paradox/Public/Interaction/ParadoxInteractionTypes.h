#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GridWorldTypes.h"
#include "SmartObjectTypes.h"
#include "Types/GameplayActionTypes.h"
#include "ParadoxInteractionTypes.generated.h"

class AActor;
class UGameplayActionDefinition;
class UNavigationQueryFilter;

/** Standard binary command used by native Receiver and Emitter interaction actions. */
UENUM(BlueprintType)
enum class EParadoxInteractionStateCommand : uint8
{
	Activate,
	Deactivate
};

/** Required replay-safe semantic parameters shared by every Paradox interaction action. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxInteractionActionParameters
{
	GENERATED_BODY()

	/** Stable world-authored target identity. Runtime Smart Object handles are intentionally absent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Interaction")
	TSoftObjectPtr<AActor> Target;

	/** Exact semantic interaction requested from the target catalog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Interaction")
	FGameplayTag InteractionTag;
};

/** Optional common movement parameters authored by native Paradox interaction Definitions. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxInteractionMovementParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Interaction|Movement")
	TSubclassOf<UNavigationQueryFilter> NavigationFilter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Interaction|Movement")
	float AcceptanceRadius = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Interaction|Movement")
	bool bAllowStrafe = false;
};

/** Authored fields appended by UParadoxReceiverInteractionActionDefinition. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxReceiverInteractionActionParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Interaction|Receiver")
	FName ReceiverComponentName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Interaction|Receiver")
	EParadoxInteractionStateCommand Command = EParadoxInteractionStateCommand::Activate;
};

/** Authored fields appended by UParadoxEmitterInteractionActionDefinition. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxEmitterInteractionActionParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Interaction|Emitter")
	FName EmitterComponentName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Interaction|Emitter")
	FGameplayTag SignalTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paradox|Interaction|Emitter")
	EParadoxInteractionStateCommand Command = EParadoxInteractionStateCommand::Activate;
};

/** One Paradox-owned interaction that can be offered by every matching Smart Object slot. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxInteractionDefinition
{
	GENERATED_BODY()

	/** Stable semantic identity used by presentation and exact Gameplay Action submission. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FGameplayTag InteractionTag;

	/** Replayable Gameplay Action Definition submitted for this exact semantic interaction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	TSoftObjectPtr<UGameplayActionDefinition> GameplayActionDefinition;

	/** Empty accepts every slot; otherwise the query is evaluated against that slot's Activity Tags. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FGameplayTagQuery SlotActivityRequirements;
};

/** Non-authoritative availability shown for one interaction/slot pair. */
UENUM(BlueprintType)
enum class EParadoxInteractionOptionState : uint8
{
	Free,
	Occupied,
	GridUnresolved
};

/** Explicit outcome of a local interaction affordance query. */
UENUM(BlueprintType)
enum class EParadoxInteractionQueryStatus : uint8
{
	Success,
	NoOptions,
	InvalidRequester,
	MissingWorld,
	MissingSmartObjectSubsystem,
	MissingGridWorld,
	InvalidInteractionTag,
	MissingSmartObjectComponent
};

/** One catalog definition projected onto one current Smart Object slot. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxInteractionOption
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FGameplayTag InteractionTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	TSoftObjectPtr<UGameplayActionDefinition> GameplayActionDefinition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FSmartObjectHandle SmartObjectHandle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FSmartObjectSlotHandle SlotHandle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FTransform SlotWorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FGridCellId GridCellId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	EParadoxInteractionOptionState State = EParadoxInteractionOptionState::GridUnresolved;
};

/** Complete deterministic result returned without acquiring Smart Object or GridWorld authority. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxInteractionQueryResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	EParadoxInteractionQueryStatus Status = EParadoxInteractionQueryStatus::NoOptions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	TArray<FParadoxInteractionOption> Options;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FString DiagnosticMessage;

	bool IsSuccess() const
	{
		return Status == EParadoxInteractionQueryStatus::Success
			|| Status == EParadoxInteractionQueryStatus::NoOptions;
	}
};

/** Structured outcome of constructing and submitting one semantic interaction request. */
UENUM(BlueprintType)
enum class EParadoxInteractionRequestStatus : uint8
{
	Accepted,
	InvalidRequester,
	InvalidTarget,
	UnrecordableTarget,
	InvalidInteractionTag,
	MissingGameplayActionComponent,
	QueryFailed,
	NoMatchingInteraction,
	InvalidCurrentPosition,
	SlotUnavailable,
	DefinitionUnavailable,
	InvalidDefinition,
	ParameterSchemaMismatch,
	SubmissionRejected
};

/** UI-facing result of complete effect, path and scheduler preflight. */
UENUM(BlueprintType)
enum class EParadoxInteractionAvailabilityStatus : uint8
{
	AvailableInPlace,
	AvailableAfterMovement,
	InvalidRequester,
	InvalidTarget,
	NoMatchingInteraction,
	EffectUnavailable,
	NoFreeSlot,
	NoReachableSlot,
	DefinitionUnavailable,
	InvalidDefinition,
	SchedulerRejected
};

/** Read-only availability for one exact catalog interaction tag. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxInteractionAvailabilityResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FGameplayTag InteractionTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	EParadoxInteractionAvailabilityStatus Status =
		EParadoxInteractionAvailabilityStatus::InvalidRequester;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	EParadoxInteractionQueryStatus QueryStatus = EParadoxInteractionQueryStatus::NoOptions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FGridCellId DestinationCell;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	double PathCost = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FGameplayActionSubmissionResult SubmissionResult;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FString DiagnosticMessage;

	bool IsAvailable() const
	{
		return Status == EParadoxInteractionAvailabilityStatus::AvailableInPlace
			|| Status == EParadoxInteractionAvailabilityStatus::AvailableAfterMovement;
	}
};

/** Result returned by both player and AI interaction submission paths. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxInteractionRequestResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	EParadoxInteractionRequestStatus Status =
		EParadoxInteractionRequestStatus::InvalidRequester;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	EParadoxInteractionQueryStatus QueryStatus =
		EParadoxInteractionQueryStatus::NoOptions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FGameplayActionSubmissionResult SubmissionResult;

	/** Human-readable diagnostics only. Status and Gameplay Tags remain authoritative. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paradox|Interaction")
	FString DiagnosticMessage;

	bool IsAccepted() const
	{
		return Status == EParadoxInteractionRequestStatus::Accepted
			&& SubmissionResult.IsAccepted();
	}
};
