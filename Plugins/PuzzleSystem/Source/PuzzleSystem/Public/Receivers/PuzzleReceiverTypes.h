#pragma once

#include "CoreMinimal.h"
#include "PuzzleReceiverTypes.generated.h"

/** Policy used to turn aggregated Controller prerequisites into effective Receiver activation. */
UENUM(BlueprintType)
enum class EPuzzleReceiverActivationMode : uint8
{
	/** Preserve the historical behavior: any active Controller request activates the Receiver. */
	Automatic,

	/** Require both an active Controller prerequisite and an explicit manual activation request. */
	Manual
};

/** Outcome of an explicit manual Receiver activation or deactivation command. */
UENUM(BlueprintType)
enum class EPuzzleReceiverActivationCommandStatus : uint8
{
	Applied,
	AlreadyInRequestedState,
	NotInManualMode,
	PrerequisitesNotSatisfied,
	ReceiverUnavailable,
	SupersededDuringNotification
};

/** Structured snapshot returned after a manual Receiver activation command. */
USTRUCT(BlueprintType)
struct PUZZLESYSTEM_API FPuzzleReceiverActivationCommandResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Receiver")
	EPuzzleReceiverActivationCommandStatus Status =
		EPuzzleReceiverActivationCommandStatus::ReceiverUnavailable;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Receiver")
	bool bPrerequisitesSatisfied = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Receiver")
	bool bManualActivationRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Receiver")
	bool bReceiverActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puzzle|Receiver")
	FString DiagnosticMessage;

	/** C++ convenience for commands that were accepted, including idempotent requests. */
	bool WasAccepted() const
	{
		return Status == EPuzzleReceiverActivationCommandStatus::Applied
			|| Status == EPuzzleReceiverActivationCommandStatus::AlreadyInRequestedState;
	}
};
