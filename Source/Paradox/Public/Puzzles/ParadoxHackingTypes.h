#pragma once

#include "CoreMinimal.h"
#include "ParadoxHackingTypes.generated.h"

class AActor;

UENUM(BlueprintType)
enum class EParadoxHackingTerminalState : uint8 { Ready, Hacked };

UENUM(BlueprintType)
enum class EParadoxHackingAttemptState : uint8 { Active, Success, TimedOut, Failed, Cancelled, Superseded };

UENUM(BlueprintType)
enum class EParadoxHackingMode : uint8 { Interactive, Simulated, Replay };

/** Isolated view data; modifying it never changes a terminal's authoritative attempt. */
USTRUCT(BlueprintType)
struct PARADOX_API FParadoxHackingLetterSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Hacking") FString TargetLetter;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") FString CurrentLetter;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") FString Pool;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") bool bFrozen = false;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") double RefreshOffset = 0;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") double NextRefreshTime = 0;
};

USTRUCT(BlueprintType)
struct PARADOX_API FParadoxHackingAttemptSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Hacking") FGuid AttemptId;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") int32 Generation = 0;
	UPROPERTY(BlueprintReadOnly, Category="Hacking|Debug") TWeakObjectPtr<AActor> InstigatorActor;
	UPROPERTY(BlueprintReadOnly, Category="Hacking|Debug") bool bHasPlannedOutcome = false;
	UPROPERTY(BlueprintReadOnly, Category="Hacking|Debug") bool bPlannedSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category="Hacking|Debug") double PlannedDurationSeconds = 0;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") EParadoxHackingMode Mode = EParadoxHackingMode::Interactive;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") EParadoxHackingAttemptState State = EParadoxHackingAttemptState::Cancelled;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") FString Password;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") double DurationSeconds = 0;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") double TimeRemaining = 0;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") double TimeLimit = 0;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") float Progress = 0;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") int32 ErrorCount = 0;
	UPROPERTY(BlueprintReadOnly, Category="Hacking") TArray<FParadoxHackingLetterSnapshot> Letters;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FParadoxHackingAttemptChanged, const FParadoxHackingAttemptSnapshot&);
